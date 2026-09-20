#ifndef WEBSOCKET_API_H_
#define WEBSOCKET_API_H_

#include "esp_err.h"
#include "esp_http_server.h"

// Live-telemetry WebSocket (/api/ws/live). Self-contained: keeps its own client
// list separate from the log WebSocket in websocket.c.

esp_err_t websocket_api_handler(httpd_req_t * req);
void websocket_api_task(void * pvParameters);       // pvParameters = GlobalState *
void websocket_api_set_handle(httpd_handle_t handle);
void websocket_api_remove_client(int fd);           // called from websocket_close_fn

#endif /* WEBSOCKET_API_H_ */
