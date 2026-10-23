#include <string.h>
#include "app_web.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"

#define TAG "app.web"

// The page is sent in three chunks: head + status text + tail.
// (Kept free of '%' characters so it is safe to edit without printf concerns.)
static const char *PAGE_HEAD =
    "<!DOCTYPE html><html lang='en'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Smart home</title><style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{min-height:100vh;background:#2b2b2b;color:#fff;display:flex;justify-content:center;"
    "font-family:ui-sans-serif,system-ui,'Segoe UI',Helvetica,Arial,sans-serif}"
    "main{width:100%;max-width:440px;padding:48px 24px;display:flex;flex-direction:column}"
    ".name{font-size:15px;font-weight:600}"
    "h1{font-size:64px;line-height:1;font-weight:800;letter-spacing:-.04em;margin:96px 0 32px}"
    "form{display:flex;flex-direction:column;gap:12px}"
    "button{width:100%;padding:22px 0;border:3px solid #fff;font:inherit;font-size:18px;"
    "font-weight:700;cursor:pointer;background:transparent;color:#fff}"
    ".on{background:#fff;color:#2b2b2b}"
    ".on:hover{background:#2b2b2b;color:#fff}"
    ".off:hover{background:#fff;color:#2b2b2b}"
    "button:active{transform:translateY(2px)}"
    "button:focus-visible{outline:3px solid #fff;outline-offset:4px}"
    ".status{margin-top:32px;font-size:15px}"
    "</style></head><body><main>"
    "<p class='name'>Smart home</p>"
    "<h1>Light</h1>"
    "<form method='post' action='/'>"
    "<button class='on' name='switch_state' value='ON'>Turn on</button>"
    "<button class='off' name='switch_state' value='OFF'>Turn off</button>"
    "</form>"
    "<p class='status'>";

static const char *PAGE_TAIL = "</p></main></body></html>";

static set_switch_f set_sw = NULL;

void appweb_init(set_switch_f f)
{
    set_sw = f;
}

static esp_err_t send_page(httpd_req_t *req, const char *status)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send_chunk(req, PAGE_HEAD, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, status, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, PAGE_TAIL, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(req, NULL, 0); // end of response
}

static esp_err_t handle_http_get(httpd_req_t *req)
{
    return send_page(req, "No command sent yet");
}

static esp_err_t handle_http_post(httpd_req_t *req)
{
    char content[100];
    size_t len = req->content_len;

    // Reject empty or oversized bodies (prevents a buffer overflow)
    if (len == 0 || len >= sizeof(content))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }

    int ret = httpd_req_recv(req, content, len);
    if (ret <= 0)
    {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    content[ret] = 0;
    ESP_LOGI(TAG, "%s", content);

    // Body looks like: switch_state=ON
    const char *val = strchr(content, '=');
    if (val == NULL)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }
    val++;

    bool on = (strcmp(val, "ON") == 0);
    if (!on && strcmp(val, "OFF") != 0)
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad request");
        return ESP_FAIL;
    }

    if (set_sw)
    {
        set_sw(on);
    }

    return send_page(req, on ? "Turned on" : "Turned off");
}

void appweb_start_server(void)
{
    httpd_uri_t uri_get = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handle_http_get,
        .user_ctx = NULL};

    httpd_uri_t uri_post = {
        .uri = "/",
        .method = HTTP_POST,
        .handler = handle_http_post,
        .user_ctx = NULL};

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_post);
        ESP_LOGI(TAG, "web server started");
    }
    else
    {
        ESP_LOGE(TAG, "failed to start web server");
    }
}