#include <stdio.h>
#include "ota_update.h"
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <esp_http_server.h>
#include <freertos/task.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <esp_wifi.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#define SAMPLE_DEVICE_NAME "ESP_OTA_"
#define SSID_PASSWORD "12345678"

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

static const char *TAG = "[OTA-APP]";
bool trigger_update = true;
bool once_init_flag = true;
wifi_mode_t wifi_mode_get;
static httpd_handle_t http_server = NULL;
ota_state_t ota_state_internal = OTA_NOT_FOUND;

void deinitialise_resources(void){
	esp_err_t wf_err = esp_wifi_get_mode(&wifi_mode_get);
	if(wf_err != ESP_OK){
		ESP_LOGW(TAG,"Error in getting wifi mode");}
	else{
		if(wifi_mode_get != WIFI_MODE_NULL){
			ESP_LOGW(TAG,"WiFi already initialized - Uninitiation resources: %d",wifi_mode_get);
			ESP_ERROR_CHECK(esp_wifi_stop());
			ESP_ERROR_CHECK(esp_wifi_deinit());
		}
	}
}
void deinitialise_internal_resources(void){
	ESP_ERROR_CHECK(esp_wifi_stop());
	ESP_ERROR_CHECK(esp_wifi_deinit());
	ESP_LOGI(TAG,"Deinitialize Resource Done");
}
esp_err_t index_get_handler(httpd_req_t *req)
{
	httpd_resp_send(req, (const char *) index_html_start, index_html_end - index_html_start);
	return ESP_OK;
}
esp_err_t update_post_handler(httpd_req_t *req)
{
	char buf[1000];
	esp_ota_handle_t ota_handle;
	int remaining = req->content_len;

	const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
	ESP_ERROR_CHECK(esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle));

	while (remaining > 0) {
		int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));

		// Timeout Error: Just retry
		if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
			continue;

		// Serious Error: Abort OTA
		} else if (recv_len <= 0) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
			trigger_update = true;
			//deinitialise_internal_resources();
			ota_state_internal = OTA_UPDATE_FAILED;
			return ESP_FAIL;
		}

		ota_state_internal = OTA_UPDATE_PROGRESS;
		// Successful Upload: Flash firmware chunk
		if (esp_ota_write(ota_handle, (const void *)buf, recv_len) != ESP_OK) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash Error");
			trigger_update = true;
			//deinitialise_internal_resources();
			ota_state_internal = OTA_UPDATE_FAILED;
			return ESP_FAIL;
		}

		remaining -= recv_len;
	}

	// Validate and switch to new OTA image and reboot
	if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
			ESP_LOGE(TAG,"ERROR: Failed to update");
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,"FIRMWARE UPDATE FAILED");
			trigger_update = true;
			vTaskDelay(500 / portTICK_PERIOD_MS);
			//deinitialise_internal_resources();
			ota_state_internal = OTA_UPDATE_FAILED;
			return ESP_FAIL;
	}

	httpd_resp_sendstr(req, "Firmware update completed, rebooting now! Timetaken");
	ota_state_internal = OTA_UPDATE_SUCCESS;
	vTaskDelay(500 / portTICK_PERIOD_MS);
	esp_restart();

	return ESP_OK;
}
esp_err_t macaddress_handler(httpd_req_t *req) {
    uint8_t staMac[6];
    esp_wifi_get_mac(WIFI_IF_STA, staMac);

    uint8_t apMac[6];
    esp_wifi_get_mac(WIFI_IF_AP, apMac);

    char macAddress[2][18];
    sprintf(macAddress[0], "%02X:%02X:%02X:%02X:%02X:%02X", staMac[0], staMac[1], staMac[2], staMac[3], staMac[4], staMac[5]);
    sprintf(macAddress[1], "%02X:%02X:%02X:%02X:%02X:%02X", apMac[0], apMac[1], apMac[2], apMac[3], apMac[4], apMac[5]);

    char response[100];
    snprintf(response, sizeof(response), "%s\n%s", macAddress[0], macAddress[1]);

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, response, strlen(response));

    return ESP_OK;
}
esp_err_t version_handler(httpd_req_t *req) {
	const esp_app_desc_t *app_desc = esp_ota_get_app_description();
    const char *response = app_desc->version;
	httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}
static esp_err_t esp_idf_version_handler(httpd_req_t *req) {
	const esp_app_desc_t *app_desc = esp_ota_get_app_description();
    const char *response = app_desc->idf_ver;
    //ESP_LOGI(TAG, "ESP-IDF Version: %s", response);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

static esp_err_t project_name_handler(httpd_req_t *req) {
    const esp_app_desc_t *app_desc = esp_ota_get_app_description();
    const char *response = app_desc->project_name;
    //ESP_LOGI(TAG, "Project Name: %s", response);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

static esp_err_t compile_time_handler(httpd_req_t *req) {
     const esp_app_desc_t *app_desc = esp_ota_get_app_description();
    const char *response = app_desc->date;
    //ESP_LOGI(TAG, "Compile Time: %s", response);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}
esp_err_t restart_handler(httpd_req_t *req) {
    // Respond with 200 OK status
    httpd_resp_send(req, NULL, 0);
    // Delay to allow the response to be sent
    vTaskDelay(pdMS_TO_TICKS(100));
    // Restart the ESP32
    esp_restart();

    return ESP_OK;
}
/*
 * HTTP Server
 */
httpd_uri_t index_get = {
	.uri	  = "/",
	.method   = HTTP_GET,
	.handler  = index_get_handler,
	.user_ctx = NULL
};

httpd_uri_t update_post = {
	.uri	  = "/update",
	.method   = HTTP_POST,
	.handler  = update_post_handler,
	.user_ctx = NULL
};

httpd_uri_t macaddress_uri = {
	.uri = "/macaddress",
	.method = HTTP_GET,
	.handler = macaddress_handler,
	.user_ctx = NULL
};
httpd_uri_t version_uri = {
	.uri      = "/version",
	.method   = HTTP_GET,
	.handler  = version_handler,
	.user_ctx = NULL
};
static const httpd_uri_t esp_idf_version_uri = {
    .uri      = "/esp_idf_version",
    .method   = HTTP_GET,
    .handler  = esp_idf_version_handler,
    .user_ctx = NULL
};

static const httpd_uri_t project_name_uri = {
    .uri      = "/project_name",
    .method   = HTTP_GET,
    .handler  = project_name_handler,
    .user_ctx = NULL
};

static const httpd_uri_t compile_time_uri = {
    .uri      = "/compile_time",
    .method   = HTTP_GET,
    .handler  = compile_time_handler,
    .user_ctx = NULL
};
httpd_uri_t restart_uri = {
	.uri = "/restart",
	.method = HTTP_GET,
	.handler = restart_handler,
};

static esp_err_t http_server_init(void)
{

	httpd_config_t config = HTTPD_DEFAULT_CONFIG();

	if (httpd_start(&http_server, &config) == ESP_OK) {
		httpd_register_uri_handler(http_server, &index_get);
		httpd_register_uri_handler(http_server, &update_post);
		httpd_register_uri_handler(http_server, &macaddress_uri);
		httpd_register_uri_handler(http_server, &version_uri);
		httpd_register_uri_handler(http_server, &esp_idf_version_uri);
        httpd_register_uri_handler(http_server, &project_name_uri);
        httpd_register_uri_handler(http_server, &compile_time_uri);
		httpd_register_uri_handler(http_server, &restart_uri);
	}

	return http_server == NULL ? ESP_FAIL : ESP_OK;
}

/*
 * WiFi configuration
 */
static void get_device_mac_name(char *service_name)
{
    uint8_t eth_mac[6];
    esp_read_mac(eth_mac,ESP_MAC_BT);
    sprintf(service_name,"%02X%02X%02X",eth_mac[3],eth_mac[4],eth_mac[5]);
}

static esp_err_t softap_init(void)
{
	esp_err_t res = ESP_OK;

	if(once_init_flag){
		res |= esp_netif_init();
		res |= esp_event_loop_create_default();
		esp_netif_create_default_wifi_ap();
		once_init_flag = false;}

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	res |= esp_wifi_init(&cfg);

	char dev_name_mac[12];
	get_device_mac_name(dev_name_mac);
	char *_mac_name = (char *)malloc(strlen(SAMPLE_DEVICE_NAME)+strlen(dev_name_mac));
	strcpy(_mac_name,SAMPLE_DEVICE_NAME);
	strcat(_mac_name,dev_name_mac);

	wifi_config_t wifi_config = {
		.ap = {
			.ssid_len = 0,
			.password = SSID_PASSWORD,
			.channel = 6,
			.authmode = WIFI_AUTH_WPA2_PSK,
			.max_connection = 3
		},
	};
	memcpy(wifi_config.ap.ssid,_mac_name,strlen(_mac_name));

	res |= esp_wifi_set_mode(WIFI_MODE_AP);
	res |= esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config);
	res |= esp_wifi_start();

	return res;
}

void ota_trigger_event(void)
{
	if(trigger_update){
		trigger_update = false;
		ESP_LOGI(TAG,"OTA Update initialized");
		deinitialise_resources();
		softap_init();
		http_server_init();
		ota_state_internal = OTA_UPDATE_START;
	}else{
		ESP_LOGW(TAG,"Already Update is in progress");
	}
}
void ota_handler(void *pvParameters)
{
	bool button_once = false;
	uint64_t button_timer = 0;
    gpio_config_t io_conf_input;
	io_conf_input.intr_type = GPIO_INTR_DISABLE;
	io_conf_input.mode = GPIO_MODE_INPUT;
	io_conf_input.pin_bit_mask = INPUT_BIT_MASK;
	io_conf_input.pull_down_en = GPIO_PULLDOWN_ENABLE;
	io_conf_input.pull_up_en = GPIO_PULLUP_DISABLE;
	gpio_config(&io_conf_input);
    while(true)
    {
		if(gpio_get_level(OTA_BUTTON) == LOW){
			if(esp_timer_get_time() - button_timer >= 5000000 && button_once == false){
				ESP_LOGI(TAG,"Button Pressed for OTA Update");
				ota_trigger_event();
				button_once = true;
			}
		}else{
			button_once = false;
			button_timer = esp_timer_get_time();
		}
		vTaskDelay(pdMS_TO_TICKS(20));
    }
}
ota_state_t ota_status_get(void){
	return ota_state_internal;
}
bool run_diagnostics() 
{
  return true;
}
void ota_update_init(void)
{
    esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);
    const esp_partition_t *partition = esp_ota_get_running_partition();
	ESP_LOGI(TAG,"Currently running partition: %s",partition->label);
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(partition, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "An OTA update has been detected.");
        if (run_diagnostics()) {
            ESP_LOGI(TAG,
                    "Diagnostics completed successfully! Continuing execution.");
			ota_state_internal = OTA_VALIDATION_SUCCESS;
            esp_ota_mark_app_valid_cancel_rollback();
        } else {
            ESP_LOGE(TAG,
                    "Diagnostics failed! Start rollback to the previous version.");
			ota_state_internal = OTA_VALIDATION_FAILED;
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
        }
    }
    xTaskCreate(ota_handler,"handle_ota_task",3072,NULL,tskIDLE_PRIORITY,NULL);
}
