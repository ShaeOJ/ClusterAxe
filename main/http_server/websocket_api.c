#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "global_state.h"
#include "http_server.h"
#include "websocket.h"
#include "websocket_api.h"

// How often to push a telemetry frame while clients are connected.
#define WS_API_INTERVAL_MS 1000

static const char * TAG = "websocket_api";

static int api_clients[MAX_WEBSOCKET_CLIENTS];
static int api_active_clients = 0;
static SemaphoreHandle_t api_clients_mutex = NULL;
static httpd_handle_t api_handle = NULL;
static int api_prebuffer_len = 512;

void websocket_api_set_handle(httpd_handle_t handle)
{
    api_handle = handle;
}

static esp_err_t api_add_client(int fd)
{
    if (api_clients_mutex == NULL || xSemaphoreTake(api_clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex for adding client");
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_FAIL;
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (api_clients[i] == -1) {
            api_clients[i] = fd;
            api_active_clients++;
            ESP_LOGI(TAG, "Added live client, fd: %d, slot: %d", fd, i);
            ret = ESP_OK;
            break;
        }
    }

    xSemaphoreGive(api_clients_mutex);
    return ret;
}

void websocket_api_remove_client(int fd)
{
    if (api_clients_mutex == NULL || xSemaphoreTake(api_clients_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
        if (api_clients[i] == fd) {
            api_clients[i] = -1;
            api_active_clients--;
            ESP_LOGI(TAG, "Removed live client, fd: %d, slot: %d", fd, i);
            break;
        }
    }

    xSemaphoreGive(api_clients_mutex);
}

esp_err_t websocket_api_handler(httpd_req_t * req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    if (req->method == HTTP_GET) {
        if (api_active_clients >= MAX_WEBSOCKET_CLIENTS) {
            ESP_LOGE(TAG, "Max live clients reached, rejecting new connection");
            httpd_resp_send_custom_err(req, "429 Too Many Requests", "Max WebSocket clients reached");
            int fd = httpd_req_to_sockfd(req);
            if (fd >= 0) {
                httpd_sess_trigger_close(req->handle, fd);
            }
            return ESP_FAIL;
        }

        int fd = httpd_req_to_sockfd(req);
        if (api_add_client(fd) != ESP_OK) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unexpected failure adding client");
            httpd_sess_trigger_close(req->handle, fd);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Live WebSocket handshake successful, fd: %d", fd);
        return ESP_OK;
    }

    // Incoming frames from a live client (mostly ping/close). Drain and handle.
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK || ws_pkt.len == 0) {
        return ret;
    }

    uint8_t * buf = (uint8_t *) calloc(ws_pkt.len + 1, sizeof(uint8_t));
    if (buf == NULL) {
        return ESP_FAIL;
    }
    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        free(buf);
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        websocket_api_remove_client(httpd_req_to_sockfd(req));
    } else if (ws_pkt.type == HTTPD_WS_TYPE_PING) {
        ws_pkt.type = HTTPD_WS_TYPE_PONG;
        httpd_ws_send_frame(req, &ws_pkt);
    }

    free(buf);
    return ESP_OK;
}

void websocket_api_task(void * pvParameters)
{
    GlobalState * GLOBAL_STATE = (GlobalState *) pvParameters;
    ESP_LOGI(TAG, "websocket_api_task starting");

    memset(api_clients, -1, sizeof(api_clients));
    api_clients_mutex = xSemaphoreCreateMutex();
    if (api_clients_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create live clients mutex");
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(WS_API_INTERVAL_MS));

        // Nothing to do if no one is listening.
        if (api_active_clients == 0 || api_handle == NULL) {
            continue;
        }

        cJSON * root = build_live_info_json(GLOBAL_STATE);
        if (root == NULL) {
            continue;
        }

        const char * json_str = cJSON_PrintBuffered(root, api_prebuffer_len, false);
        cJSON_Delete(root);
        if (json_str == NULL) {
            continue;
        }
        int len = strlen(json_str);
        if (len > api_prebuffer_len) {
            api_prebuffer_len = (int) (len * 1.2);
        }

        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        ws_pkt.payload = (uint8_t *) json_str;
        ws_pkt.len = len;
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;

        for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++) {
            int client_fd = api_clients[i];
            if (client_fd != -1) {
                if (httpd_ws_send_frame_async(api_handle, client_fd, &ws_pkt) != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to send live frame to fd: %d", client_fd);
                    websocket_api_remove_client(client_fd);
                }
            }
        }

        free((void *) json_str);
    }
}
