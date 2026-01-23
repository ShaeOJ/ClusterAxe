#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

#include <esp_http_server.h>
#include <esp_http_client.h>

#include "cJSON.h"
#include "cluster_config.h"

esp_err_t is_network_allowed(httpd_req_t * req);
esp_err_t start_rest_server(void *pvParameters);
esp_err_t HTTP_send_json(httpd_req_t * req, const cJSON * item, int * prebuffer_len);

#if CLUSTER_IS_MASTER
/**
 * @brief Proxy an HTTP request to a slave device
 * @param slave_ip IP address of the slave
 * @param path API path (e.g., "/api/system")
 * @param method HTTP method (HTTP_METHOD_GET, HTTP_METHOD_POST, HTTP_METHOD_PATCH)
 * @param post_data POST/PATCH data (NULL for GET requests)
 * @param response Output buffer for response (caller must free, can be NULL)
 * @return ESP_OK on success
 */
esp_err_t http_proxy_to_slave(const char *slave_ip, const char *path,
                               esp_http_client_method_t method,
                               const char *post_data, char **response);
#endif

#endif /* HTTP_SERVER_H_ */
