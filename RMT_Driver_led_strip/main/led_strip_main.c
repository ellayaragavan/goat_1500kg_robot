/* RMT example -- RGB LED Strip

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "driver/rmt.h"
#include "led_strip.h"
#include "nvs_flash.h"
#include "esp_now.h"
#include "esp_system.h"
#include <cJSON.h>
#include "ota_update.h"

#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF ESP_IF_WIFI_STA
#define CONFIG_ESPNOW_PMK "pmk1234567890123"
#define CONFIG_ESPNOW_CHANNEL 1

#ifdef CONFIG_RGB3_CONNECTOR_ENABLE
#define RMT_GROUPA1_CHANNEL RMT_CHANNEL_0
#define PORTA1_PIN CONFIG_RGB3_GPIO_PIN
#endif
#ifdef CONFIG_RGB2_CONNECTOR_ENABLE
#define RMT_GROUPA2_CHANNEL RMT_CHANNEL_1
#define PORTA2_PIN CONFIG_RGB2_GPIO_PIN
#endif
#ifdef CONFIG_RGB1_CONNECTOR_ENABLE
#define RMT_GROUPB1_CHANNEL RMT_CHANNEL_2
#define PORTB1_PIN CONFIG_RGB1_GPIO_PIN
#endif
#ifdef CONFIG_RGB0_CONNECTOR_ENABLE
#define RMT_GROUPB2_CHANNEL RMT_CHANNEL_3
#define PORTB2_PIN CONFIG_RGB0_GPIO_PIN
#endif

#define NUM_LED 30
#define RED_LED_CONTRAST 255

#define RGB_PIN_1 CONFIG_RGB1_GPIO_PIN
#define RGB_PIN_2 CONFIG_RGB0_GPIO_PIN

#define PORTA1 1
#define PORTA2 2
#define PORTB1 3
#define PORTB2 4

const char *RGB_TAG = "RGB";
int i = 0;
int size = 0;
uint8_t board_no = 0;
uint8_t mode_no = 0;
int rgm_mode = 0;

int colour_array_1[3] = {100, 100, 150};
int colour_array_2[3] = {0, 0, 0};

led_strip_t *strip_1;
led_strip_t *strip_2;

struct rgb_master_data
{
    uint32_t mode;
    uint32_t RGB_CODE1[3];
    uint32_t RGB_CODE2[3];
    uint32_t No_Led;
    uint32_t speed;
    uint32_t prev_led_no;
};
struct rgb_master_data port1;
struct rgb_master_data port2;
struct rgb_master_data port3;
struct rgb_master_data port4;

enum rgb_state
{
    left_blink = 9,
    right_blink = 10,
    left_right_blink = 11,
    left_right_on = 12,
    left_right_off = 13,
    init_on = 15
} rgb_mode;

enum mode_control_A1
{
    IDEAL_MODE,
    BLINK_ON,
    BLINK_OFF,
    FADE_IN,
    FADE_OUT,
    FADE_LOOP,
    ROUND_LOOP,
    SPLIT_LIGHT,
    INIT_LIGHT
} mode_selection_A1,
    mode_selection_A2, mode_selection_B1, mode_selection_B2;

bool once_send_cb = false;
bool SEND_RESP_FLG = false;
bool FADE_FLG = true;
static uint8_t broadcast_mac[ESP_NOW_ETH_ALEN];

static const char *TAG = "RGB-APP";

void set_led_pixel(led_strip_t *strip_recv, uint32_t index_led, uint32_t red, uint32_t green, uint32_t blue)
{
    ESP_ERROR_CHECK(strip_recv->set_pixel(strip_recv, index_led, red, green, blue));
}
bool compare_mac(const uint8_t *recv_mac, const uint8_t *assigned_mac)
{
    uint32_t cnt = 0;
    for (int i = 0; i < 6; i++)
    {
        if (recv_mac[i] == assigned_mac[i])
        {
            cnt++;
        }
    }
    if (cnt == 6)
    {
        return true;
    }
    else
    {
        return false;
    }
}

static void wifi_initialization()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(ESPNOW_WIFI_MODE));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
    ESP_ERROR_CHECK(esp_wifi_start());
}
static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (mac_addr == NULL)
    {
        ESP_LOGE(TAG, "Send cb arg error");
        return;
    }
    if (status != ESP_NOW_SEND_SUCCESS)
    {
        ESP_LOGE(TAG, "ERROR_IN_SENDING_MSG_PEER");
    }
    else if (status == ESP_NOW_SEND_SUCCESS && once_send_cb == false)
    {
        ESP_LOGI(TAG, "SUCCESS_SENDING_MSG_PEER");
    }
}
void Send_response(cJSON *send_resp, bool ERR_RESP)
{
    cJSON_AddBoolToObject(send_resp, "response", ERR_RESP);
    char *json_string_rgb = cJSON_PrintUnformatted(send_resp);
    uint8_t data_send_rgb[strlen(json_string_rgb)];
    memcpy(data_send_rgb, json_string_rgb, strlen(json_string_rgb));
    if (esp_now_send(broadcast_mac, data_send_rgb, strlen(json_string_rgb)) != ESP_OK)
    {
        ESP_LOGE(TAG, "SEND_ERROR_RGB\n");
    }
    cJSON_free(json_string_rgb);
    cJSON_Delete(send_resp);
}
static void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    if (compare_mac(mac_addr, broadcast_mac))
    {
        cJSON *rgb_json = cJSON_Parse((char *)data);
        char *rgb_json_string = cJSON_PrintUnformatted(rgb_json);
        ESP_LOGI(TAG, "RECEIVED_DATA : %s", rgb_json_string);
        if ((cJSON_HasObjectItem(rgb_json, "ota")))
        {
            cJSON *send_data = cJSON_CreateObject();
            cJSON_AddBoolToObject(send_data, "ota", true);
            Send_response(send_data, true);
            SEND_RESP_FLG = true;
            ota_update_init();
        }

        if (cJSON_HasObjectItem(rgb_json, "board_no"))
        {
            board_no = cJSON_GetObjectItem(rgb_json, "board_no")->valueint;
            if (board_no == 5)
            {
                SEND_RESP_FLG = true;
                rgb_mode = cJSON_GetObjectItem(rgb_json, "mode_no")->valueint;
                ESP_LOGW(TAG, "REG MODE %d", SEND_RESP_FLG);
                cJSON *colour1 = cJSON_GetObjectItem(rgb_json, "colour1");
                size = cJSON_GetArraySize(colour1);
                for (int i = 0; i < size; i++)
                {

                    cJSON *colour_value_1 = cJSON_GetArrayItem(colour1, i);
                    colour_array_1[i] = colour_value_1->valueint;
                }
                cJSON *colour2 = cJSON_GetObjectItem(rgb_json, "colour2");
                size = cJSON_GetArraySize(colour2);
                for (int i = 0; i < size; i++)
                {
                    cJSON *colour_value_2 = cJSON_GetArrayItem(colour2, i);
                    colour_array_2[i] = colour_value_2->valueint;
                }
            }
        }
      
        if (cJSON_HasObjectItem(rgb_json, "port_no"))
        {
            SEND_RESP_FLG = true;
            uint16_t port_switch = cJSON_GetObjectItem(rgb_json, "port_no")->valueint;
            if (port_switch < 1 || port_switch > 4)
            {

                SEND_RESP_FLG = false;
                ESP_LOGE(TAG, "ERROR_IN_PORT_NUMBER");
            }
            if (cJSON_HasObjectItem(rgb_json, "colour1"))
            {
                cJSON *colour_array = cJSON_GetObjectItem(rgb_json, "colour1");
                for (int i = 0; i < 3; i++)
                {
                    cJSON *array = cJSON_GetArrayItem(colour_array, i);
                    if (port_switch == PORTA1)
                    {
                        port1.RGB_CODE1[i] = array->valueint;
                        ESP_LOGI(TAG, "RGB_PORT1_COLOUR1[%d]=%d", i, port1.RGB_CODE1[i]);
                    }
                    else if (port_switch == PORTA2)
                    {
                        port2.RGB_CODE1[i] = array->valueint;
                        ESP_LOGI(TAG, "RGB_PORT2_COLOUR1[%d]=%d", i, port2.RGB_CODE1[i]);
                    }
                    else if (port_switch == PORTB1)
                    {
                        port3.RGB_CODE1[i] = array->valueint;
                        ESP_LOGI(TAG, "RGB_PORT3_COLOUR1[%d]=%d", i, port3.RGB_CODE1[i]);
                    }
                    else if (port_switch == PORTB2)
                    {
                        port4.RGB_CODE1[i] = array->valueint;
                        ESP_LOGI(TAG, "RGB_PORT4_COLOUR1[%d]=%d", i, port4.RGB_CODE1[i]);
                    }
                    else
                    {
                        ESP_LOGW(TAG, "...1");
                        SEND_RESP_FLG = false;
                    }
                }
            }
            if (cJSON_HasObjectItem(rgb_json, "colour2"))
            {
                cJSON *colour_array = cJSON_GetObjectItem(rgb_json, "colour2");
                for (int i = 0; i < 3; i++)
                {
                    cJSON *array = cJSON_GetArrayItem(colour_array, i);
                    if (port_switch == PORTA1)
                    {
                        port1.RGB_CODE2[i] = array->valueint;
                        ESP_LOGI(TAG, "RGB_PORT1_COLOUR2[%d]=%d", i, port1.RGB_CODE2[i]);
                    }
                    else if (port_switch == PORTA2)
                    {
                        port2.RGB_CODE2[i] = array->valueint;
                        ESP_LOGI(TAG, "RGB_PORT2_COLOUR2[%d]=%d", i, port2.RGB_CODE2[i]);
                    }
                    else if (port_switch == PORTB1)
                    {
                        port3.RGB_CODE2[i] = array->valueint;
                        ESP_LOGI(TAG, "RGB_PORT3_COLOUR2[%d]=%d", i, port3.RGB_CODE2[i]);
                    }
                    else if (port_switch == PORTB2)
                    {
                        port4.RGB_CODE2[i] = array->valueint;
                        ESP_LOGI(TAG, "RGB_PORT4_COLOUR2[%d]=%d", i, port4.RGB_CODE2[i]);
                    }
                    else
                    {
                        SEND_RESP_FLG = false;
                    }
                }
            }
            if (cJSON_HasObjectItem(rgb_json, "no_of_led"))
            {
                if (port_switch == PORTA1)
                {
                    port1.No_Led = cJSON_GetObjectItem(rgb_json, "no_of_led")->valueint;
                    ESP_LOGI(TAG, "NO_OF_LED_PORT1 : %d", port1.No_Led);
                }
                else if (port_switch == PORTA2)
                {
                    port2.No_Led = cJSON_GetObjectItem(rgb_json, "no_of_led")->valueint;
                    ESP_LOGI(TAG, "NO_OF_LED_PORT2 : %d", port2.No_Led);
                }
                else if (port_switch == PORTB1)
                {
                    port3.No_Led = cJSON_GetObjectItem(rgb_json, "no_of_led")->valueint;
                    ESP_LOGI(TAG, "NO_OF_LED_PORT3 : %d", port3.No_Led);
                }
                else if (port_switch == PORTB2)
                {
                    port4.No_Led = cJSON_GetObjectItem(rgb_json, "no_of_led")->valueint;
                    ESP_LOGI(TAG, "NO_OF_LED_PORT4 : %d", port3.No_Led);
                }
                else
                {
                    SEND_RESP_FLG = false;
                }
            }
            if (cJSON_HasObjectItem(rgb_json, "mode_no"))
            {
                uint32_t port_mode = cJSON_GetObjectItem(rgb_json, "mode_no")->valueint;
                if (port_mode >= 1 && port_mode <= 15)
                {
                    if (port_switch == PORTA1)
                    {
                        port1.mode = port_mode;
                        mode_selection_A1 = port1.mode;
                        ESP_LOGI(TAG, "PORT1_MODE : %d", port1.mode);
                    }
                    else if (port_switch == PORTA2)
                    {
                        port2.mode = port_mode;
                        mode_selection_A2 = port2.mode;
                        ESP_LOGI(TAG, "PORT2_MODE : %d", port2.mode);
                    }
                    else if (port_switch == PORTB1)
                    {
                        port3.mode = port_mode;
                        mode_selection_B1 = port3.mode;
                        ESP_LOGI(TAG, "PORT3_MODE : %d", port3.mode);
                    }
                    else if (port_switch == PORTB2)
                    {
                        port4.mode = port_mode;
                        mode_selection_B2 = port4.mode;
                        ESP_LOGI(TAG, "PORT4_MODE : %d", port4.mode);
                    }
                    else
                    {
                        SEND_RESP_FLG = false;
                    }
                }
                else
                {
                    ESP_LOGW(TAG, "...3");
                    SEND_RESP_FLG = false;
                }
            }
            if (cJSON_HasObjectItem(rgb_json, "speed"))
            {
                int speed_get = cJSON_GetObjectItem(rgb_json, "speed")->valueint;
                if (speed_get >= 5 && speed_get <= 3000)
                {
                    if (port_switch == PORTA1)
                    {
                        port1.speed = speed_get;
                        ESP_LOGI(TAG, "PORT1_SPEED : %d", port1.speed);
                    }
                    else if (port_switch == PORTA2)
                    {
                        port2.speed = speed_get;
                        ESP_LOGI(TAG, "PORT2_SPEED : %d", port2.speed);
                    }
                    else if (port_switch == PORTB1)
                    {
                        port3.speed = speed_get;
                        ESP_LOGI(TAG, "PORT3_SPEED : %d", port3.speed);
                    }
                    else if (port_switch == PORTB2)
                    {
                        port4.speed = speed_get;
                        ESP_LOGI(TAG, "PORT4_SPEED : %d", port4.speed);
                    }
                    else
                    {
                        SEND_RESP_FLG = false;
                    }
                }
                else
                {
                    SEND_RESP_FLG = false;
                }
            }
        }
        ESP_LOGW(TAG, "FLAG VALUE %d", SEND_RESP_FLG);
        Send_response(rgb_json, SEND_RESP_FLG);
    }
}
static void esp_now_initialization()
{
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
    ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK));
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL)
    {
        ESP_LOGE(TAG, "Malloc peer information fail");
        esp_now_deinit();
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = CONFIG_ESPNOW_CHANNEL;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(peer));
    free(peer);
    uint32_t version;
    esp_now_get_version(&version);
    ESP_LOGI(TAG, "ESP_NOW_VERSION : %u", version);
}
uint32_t percentage_decrease(uint32_t value, uint32_t percentage)
{
    uint32_t out = value - ((float)percentage / 100) * value;
    return out;
}
float percentage_increase(float value, uint32_t percentage)
{
    float out = value + ((float)percentage / 100) * value;
    return out;
}
void RMT_GROUPA1(void *pvParameters)
{
#ifdef CONFIG_RGB3_CONNECTOR_ENABLE
    rmt_config_t configA1 = RMT_DEFAULT_CONFIG_TX(PORTA1_PIN, RMT_GROUPA1_CHANNEL);
    configA1.clk_div = 2;
    port1.No_Led = 500;
    ESP_ERROR_CHECK(rmt_config(&configA1));
    ESP_ERROR_CHECK(rmt_driver_install(RMT_GROUPA1_CHANNEL, 0, 0));
    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(port1.No_Led + 5, (led_strip_dev_t)configA1.channel);
    led_strip_t *stripA1 = led_strip_new_rmt_ws2812(&strip_config);
    if (!stripA1)
    {
        ESP_LOGE(TAG, "Install WS2812 driver failed");
    }
    ESP_ERROR_CHECK(stripA1->clear(stripA1, 100));
    mode_selection_A1 = INIT_LIGHT;
    while (true)
    {
        uint32_t _NO_LED_A1 = port1.No_Led;
        if (_NO_LED_A1 != port1.prev_led_no)
        {
            ESP_LOGW(TAG, "LED_CHANGED_PORT1");
            for (uint32_t i = 0; i <= port1.prev_led_no; i++)
            {
                ESP_ERROR_CHECK(stripA1->set_pixel(stripA1, i, 0, 0, 0));
            }
            ESP_ERROR_CHECK(stripA1->refresh(stripA1, port1.prev_led_no));
            port1.prev_led_no = port1.No_Led;
        }
        switch (mode_selection_A1)
        {
        case BLINK_ON:
        {
            uint32_t f_red = port1.RGB_CODE1[0], f_green = port1.RGB_CODE1[1], f_blue = port1.RGB_CODE1[2];
            for (int j = 0; j <= _NO_LED_A1; j++)
            {
                ESP_ERROR_CHECK(stripA1->set_pixel(stripA1, j, f_red, f_green, f_blue));
            }
            ESP_ERROR_CHECK(stripA1->refresh(stripA1, 100));
            vTaskDelay(pdMS_TO_TICKS(port1.speed));
            uint32_t s_red = port1.RGB_CODE2[0], s_green = port1.RGB_CODE2[1], s_blue = port1.RGB_CODE2[2];
            for (int j = 0; j <= _NO_LED_A1; j++)
            {
                ESP_ERROR_CHECK(stripA1->set_pixel(stripA1, j, s_red, s_green, s_blue));
            }
            ESP_ERROR_CHECK(stripA1->refresh(stripA1, 100));
            vTaskDelay(pdMS_TO_TICKS(port1.speed));
        }
        break;
        case BLINK_OFF:
        {
            uint32_t f_red = port1.RGB_CODE1[0], f_green = port1.RGB_CODE1[1], f_blue = port1.RGB_CODE1[2];
            for (int j = 0; j <= _NO_LED_A1; j++)
            {
                ESP_ERROR_CHECK(stripA1->set_pixel(stripA1, j, f_red, f_green, f_blue));
            }
            ESP_ERROR_CHECK(stripA1->refresh(stripA1, 100));
            vTaskDelay(pdMS_TO_TICKS(port1.speed));
        }
        break;
        case FADE_IN:
        {
            uint32_t _red = port1.RGB_CODE1[0], _green = port1.RGB_CODE1[1], _blue = port1.RGB_CODE1[2];
            for (int i = 0; i < 23; i++)
            {
                for (int j = 0; j <= _NO_LED_A1; j++)
                {
                    ESP_ERROR_CHECK(stripA1->set_pixel(stripA1, j, _red, _green, _blue));
                }
                ESP_ERROR_CHECK(stripA1->refresh(stripA1, 100));
                vTaskDelay(pdMS_TO_TICKS(port1.speed));
                _red = percentage_decrease(_red, 8);
                _green = percentage_decrease(_green, 8);
                _blue = percentage_decrease(_blue, 8);
                // ESP_LOGI(TAG,"OUT : %d, %d, %d",_red,_green,_blue);
            }
            mode_selection_A1 = IDEAL_MODE;
            break;
        }
        case FADE_OUT:
        {
            uint32_t _red = port1.RGB_CODE1[0], _green = port1.RGB_CODE1[1], _blue = port1.RGB_CODE1[2];
            float f_red = percentage_decrease(_red, 80);
            float f_green = percentage_decrease(_green, 80);
            float f_blue = percentage_decrease(_blue, 80);
            for (int i = 0; i < 40; i++)
            {
                f_red = percentage_increase(f_red, 15);
                f_green = percentage_increase(f_green, 15);
                f_blue = percentage_increase(f_blue, 15);
                if (f_red >= port1.RGB_CODE1[0])
                {
                    f_red = port1.RGB_CODE1[0];
                }
                if (f_green >= port1.RGB_CODE1[1])
                {
                    f_green = port1.RGB_CODE1[1];
                }
                if (f_blue >= port1.RGB_CODE1[2])
                {
                    f_blue = port1.RGB_CODE1[2];
                }
                for (int j = 0; j <= _NO_LED_A1; j++)
                {
                    ESP_ERROR_CHECK(stripA1->set_pixel(stripA1, j, round(f_red), round(f_green), round(f_blue)));
                }
                ESP_ERROR_CHECK(stripA1->refresh(stripA1, 100));
                vTaskDelay(pdMS_TO_TICKS(port1.speed));
                // ESP_LOGI(TAG,"OUT : %f, %f, %f",f_red,f_green,f_blue);
            }
            mode_selection_A1 = IDEAL_MODE;
            break;
        }
        break;
        case FADE_LOOP:
        {
            uint32_t _red = port1.RGB_CODE1[0], _green = port1.RGB_CODE1[1], _blue = port1.RGB_CODE1[2];
            for (int i = 0; i < 23; i++)
            {
                for (int j = 0; j <= _NO_LED_A1; j++)
                {
                    ESP_ERROR_CHECK(stripA1->set_pixel(stripA1, j, _red, _green, _blue));
                }
                ESP_ERROR_CHECK(stripA1->refresh(stripA1, 100));
                vTaskDelay(pdMS_TO_TICKS(port1.speed));
                _red = percentage_decrease(_red, 8);
                _green = percentage_decrease(_green, 8);
                _blue = percentage_decrease(_blue, 8);
                // ESP_LOGI(TAG,"OUT : %d, %d, %d",_red,_green,_blue);
            }
            vTaskDelay(pdMS_TO_TICKS(port1.speed));
            float f_red = percentage_decrease(port1.RGB_CODE2[0], 80);
            float f_green = percentage_decrease(port1.RGB_CODE2[1], 80);
            float f_blue = percentage_decrease(port1.RGB_CODE2[2], 80);
            for (int i = 0; i < 40; i++)
            {
                f_red = percentage_increase(f_red, 15);
                f_green = percentage_increase(f_green, 15);
                f_blue = percentage_increase(f_blue, 15);
                if (f_red >= port1.RGB_CODE2[0])
                {
                    f_red = port1.RGB_CODE2[0];
                }
                if (f_green >= port1.RGB_CODE2[1])
                {
                    f_green = port1.RGB_CODE2[1];
                }
                if (f_blue >= port1.RGB_CODE2[2])
                {
                    f_blue = port1.RGB_CODE2[2];
                }
                for (int j = 0; j <= _NO_LED_A1; j++)
                {
                    ESP_ERROR_CHECK(stripA1->set_pixel(stripA1, j, round(f_red), round(f_green), round(f_blue)));
                }
                ESP_ERROR_CHECK(stripA1->refresh(stripA1, 100));
                vTaskDelay(pdMS_TO_TICKS(port1.speed));
                // ESP_LOGI(TAG,"OUT : %f, %f, %f",f_red,f_green,f_blue);
            }
        }
        break;
        case ROUND_LOOP:
        {
            for (int j = 0; j < _NO_LED_A1; j++)
            {
                int _red = port1.RGB_CODE1[0], _green = port1.RGB_CODE1[1], _blue = port1.RGB_CODE1[2];
                for (int i = 5; i >= 0; i--)
                {
                    ESP_ERROR_CHECK(stripA1->set_pixel(stripA1, j + i, _red, _green, _blue));
                    _red = round(_red / 1.6);
                    _green = round(_green / 1.6);
                    _blue = round(_blue / 1.6);
                }
                ESP_ERROR_CHECK(stripA1->refresh(stripA1, 100));
                vTaskDelay(pdMS_TO_TICKS(port1.speed));
            }
            ESP_ERROR_CHECK(stripA1->refresh(stripA1, 100));
            break;
        }
        case SPLIT_LIGHT:
        {
            int k = round(_NO_LED_A1 / 2);
            for (int j = round(_NO_LED_A1 / 2); j < _NO_LED_A1; j++)
            {
                int _red = port1.RGB_CODE1[0], _green = port1.RGB_CODE1[1], _blue = port1.RGB_CODE1[2];
                int _red2 = port1.RGB_CODE2[0], _green2 = port1.RGB_CODE2[1], _blue2 = port1.RGB_CODE2[2];
                for (int i = 5; i >= 0; i--)
                {
                    ESP_ERROR_CHECK(stripA1->set_pixel(stripA1, j + i, _red, _green, _blue));
                    if (k > i) // difference of number without negative sign to set pixel
                        ESP_ERROR_CHECK(stripA1->set_pixel(stripA1, k - i, _red2, _green2, _blue2));
                    else
                        ESP_ERROR_CHECK(stripA1->set_pixel(stripA1, i - k, _red2, _green2, _blue2));
                    vTaskDelay(pdMS_TO_TICKS(port1.speed));
                    _red = round(_red / 1.6);
                    _green = round(_green / 1.6);
                    _blue = round(_blue / 1.6);
                    _red2 = round(_red2 / 1.6);
                    _green2 = round(_green2 / 1.6);
                    _blue2 = round(_blue2 / 1.6);
                }
                ESP_ERROR_CHECK(stripA1->refresh(stripA1, 100));
                k = k - 1;
                if (k <= 0)
                {
                    break;
                }
            }
            ESP_ERROR_CHECK(stripA1->refresh(stripA1, 100));
        }
        break;
        case INIT_LIGHT:
        {
            uint32_t init_red = 14, init_green = 140, init_blue = 255;
            if (FADE_FLG)
            {
                for (int j = 0; j <= 350; j++)
                {
                    ESP_ERROR_CHECK(stripA1->set_pixel(stripA1, j, percentage_decrease(init_red, 95), percentage_decrease(init_green, 95), percentage_decrease(init_blue, 95)));
                }
                ESP_ERROR_CHECK(stripA1->refresh(stripA1, 100));
                FADE_FLG = false;
            }
            for (int j = 0; j <= 350; j++)
            {
                set_led_pixel(stripA1, j + 1, percentage_decrease(init_red, 95), percentage_decrease(init_green, 95), percentage_decrease(init_blue, 95));
                set_led_pixel(stripA1, j + 2, percentage_decrease(init_red, 70), percentage_decrease(init_green, 70), percentage_decrease(init_blue, 70));
                set_led_pixel(stripA1, j + 3, percentage_decrease(init_red, 40), percentage_decrease(init_green, 40), percentage_decrease(init_blue, 40));
                set_led_pixel(stripA1, j + 4, init_red, init_green, init_blue);
                set_led_pixel(stripA1, j + 5, percentage_decrease(init_red, 40), percentage_decrease(init_green, 40), percentage_decrease(init_blue, 40));
                set_led_pixel(stripA1, j + 6, percentage_decrease(init_red, 70), percentage_decrease(init_green, 70), percentage_decrease(init_blue, 70));
                set_led_pixel(stripA1, j + 7, percentage_decrease(init_red, 95), percentage_decrease(init_green, 95), percentage_decrease(init_blue, 95));
                ESP_ERROR_CHECK(stripA1->refresh(stripA1, 100));
                vTaskDelay(pdMS_TO_TICKS(15));
            }
        }
        break;
        case IDEAL_MODE:
            vTaskDelay(pdMS_TO_TICKS(30));
            break;
        default:
            vTaskDelay(pdMS_TO_TICKS(10));
            break;
        }
    }
#endif
}
void RMT_GROUPA2(void *pvParameters)
{
#ifdef CONFIG_RGB2_CONNECTOR_ENABLE
    rmt_config_t configA2 = RMT_DEFAULT_CONFIG_TX(PORTA2_PIN, RMT_GROUPA2_CHANNEL);
    configA2.clk_div = 2;
    port2.No_Led = 500;
    ESP_ERROR_CHECK(rmt_config(&configA2));
    ESP_ERROR_CHECK(rmt_driver_install(RMT_GROUPA2_CHANNEL, 0, 0));
    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(port2.No_Led + 5, (led_strip_dev_t)configA2.channel);
    led_strip_t *stripA2 = led_strip_new_rmt_ws2812(&strip_config);
    if (!stripA2)
    {
        ESP_LOGE(TAG, "Install WS2812 driver failed");
    }
    ESP_ERROR_CHECK(stripA2->clear(stripA2, 100));
    mode_selection_A2 = INIT_LIGHT;
    while (true)
    {
        uint32_t _NO_LED_A2 = port2.No_Led;
        if (_NO_LED_A2 != port2.prev_led_no)
        {
            ESP_LOGW(TAG, "LED_CHANGED_port2");
            for (uint32_t i = 0; i <= port2.prev_led_no; i++)
            {
                ESP_ERROR_CHECK(stripA2->set_pixel(stripA2, i, 0, 0, 0));
            }
            ESP_ERROR_CHECK(stripA2->refresh(stripA2, port2.prev_led_no));
            port2.prev_led_no = port2.No_Led;
        }
        switch (mode_selection_A2)
        {
        case BLINK_ON:
        {
            uint32_t f_red = port2.RGB_CODE1[0], f_green = port2.RGB_CODE1[1], f_blue = port2.RGB_CODE1[2];
            for (int j = 0; j <= _NO_LED_A2; j++)
            {
                ESP_ERROR_CHECK(stripA2->set_pixel(stripA2, j, f_red, f_green, f_blue));
            }
            ESP_ERROR_CHECK(stripA2->refresh(stripA2, 100));
            vTaskDelay(pdMS_TO_TICKS(port2.speed));
            uint32_t s_red = port2.RGB_CODE2[0], s_green = port2.RGB_CODE2[1], s_blue = port2.RGB_CODE2[2];
            for (int j = 0; j <= _NO_LED_A2; j++)
            {
                ESP_ERROR_CHECK(stripA2->set_pixel(stripA2, j, s_red, s_green, s_blue));
            }
            ESP_ERROR_CHECK(stripA2->refresh(stripA2, 100));
            vTaskDelay(pdMS_TO_TICKS(port2.speed));
        }
        break;
        case BLINK_OFF:
        {
            uint32_t f_red = port2.RGB_CODE1[0], f_green = port2.RGB_CODE1[1], f_blue = port2.RGB_CODE1[2];
            for (int j = 0; j <= _NO_LED_A2; j++)
            {
                ESP_ERROR_CHECK(stripA2->set_pixel(stripA2, j, f_red, f_green, f_blue));
            }
            ESP_ERROR_CHECK(stripA2->refresh(stripA2, 100));
            vTaskDelay(pdMS_TO_TICKS(port2.speed));
        }
        break;
        case FADE_IN:
        {
            uint32_t _red = port2.RGB_CODE1[0], _green = port2.RGB_CODE1[1], _blue = port2.RGB_CODE1[2];
            for (int i = 0; i < 23; i++)
            {
                for (int j = 0; j <= _NO_LED_A2; j++)
                {
                    ESP_ERROR_CHECK(stripA2->set_pixel(stripA2, j, _red, _green, _blue));
                }
                ESP_ERROR_CHECK(stripA2->refresh(stripA2, 100));
                vTaskDelay(pdMS_TO_TICKS(port2.speed));
                _red = percentage_decrease(_red, 8);
                _green = percentage_decrease(_green, 8);
                _blue = percentage_decrease(_blue, 8);
                // ESP_LOGI(TAG,"OUT : %d, %d, %d",_red,_green,_blue);
            }
            mode_selection_A2 = IDEAL_MODE;
            break;
        }
        case FADE_OUT:
        {
            uint32_t _red = port2.RGB_CODE1[0], _green = port2.RGB_CODE1[1], _blue = port2.RGB_CODE1[2];
            float f_red = percentage_decrease(_red, 80);
            float f_green = percentage_decrease(_green, 80);
            float f_blue = percentage_decrease(_blue, 80);
            for (int i = 0; i < 40; i++)
            {
                f_red = percentage_increase(f_red, 15);
                f_green = percentage_increase(f_green, 15);
                f_blue = percentage_increase(f_blue, 15);
                if (f_red >= port2.RGB_CODE1[0])
                {
                    f_red = port2.RGB_CODE1[0];
                }
                if (f_green >= port2.RGB_CODE1[1])
                {
                    f_green = port2.RGB_CODE1[1];
                }
                if (f_blue >= port2.RGB_CODE1[2])
                {
                    f_blue = port2.RGB_CODE1[2];
                }
                for (int j = 0; j <= _NO_LED_A2; j++)
                {
                    ESP_ERROR_CHECK(stripA2->set_pixel(stripA2, j, round(f_red), round(f_green), round(f_blue)));
                }
                ESP_ERROR_CHECK(stripA2->refresh(stripA2, 100));
                vTaskDelay(pdMS_TO_TICKS(port2.speed));
                // ESP_LOGI(TAG,"OUT : %f, %f, %f",f_red,f_green,f_blue);
            }
            mode_selection_A2 = IDEAL_MODE;
            break;
        }
        break;
        case FADE_LOOP:
        {
            uint32_t _red = port2.RGB_CODE1[0], _green = port2.RGB_CODE1[1], _blue = port2.RGB_CODE1[2];
            for (int i = 0; i < 23; i++)
            {
                for (int j = 0; j <= _NO_LED_A2; j++)
                {
                    ESP_ERROR_CHECK(stripA2->set_pixel(stripA2, j, _red, _green, _blue));
                }
                ESP_ERROR_CHECK(stripA2->refresh(stripA2, 100));
                vTaskDelay(pdMS_TO_TICKS(port2.speed));
                _red = percentage_decrease(_red, 8);
                _green = percentage_decrease(_green, 8);
                _blue = percentage_decrease(_blue, 8);
                // ESP_LOGI(TAG,"OUT : %d, %d, %d",_red,_green,_blue);
            }
            vTaskDelay(pdMS_TO_TICKS(port2.speed));
            float f_red = percentage_decrease(port2.RGB_CODE2[0], 80);
            float f_green = percentage_decrease(port2.RGB_CODE2[1], 80);
            float f_blue = percentage_decrease(port2.RGB_CODE2[2], 80);
            for (int i = 0; i < 40; i++)
            {
                f_red = percentage_increase(f_red, 15);
                f_green = percentage_increase(f_green, 15);
                f_blue = percentage_increase(f_blue, 15);
                if (f_red >= port2.RGB_CODE2[0])
                {
                    f_red = port2.RGB_CODE2[0];
                }
                if (f_green >= port2.RGB_CODE2[1])
                {
                    f_green = port2.RGB_CODE2[1];
                }
                if (f_blue >= port2.RGB_CODE2[2])
                {
                    f_blue = port2.RGB_CODE2[2];
                }
                for (int j = 0; j <= _NO_LED_A2; j++)
                {
                    ESP_ERROR_CHECK(stripA2->set_pixel(stripA2, j, round(f_red), round(f_green), round(f_blue)));
                }
                ESP_ERROR_CHECK(stripA2->refresh(stripA2, 100));
                vTaskDelay(pdMS_TO_TICKS(port2.speed));
                // ESP_LOGI(TAG,"OUT : %f, %f, %f",f_red,f_green,f_blue);
            }
        }
        break;
        case ROUND_LOOP:
        {
            for (int j = 0; j < _NO_LED_A2; j++)
            {
                int _red = port2.RGB_CODE1[0], _green = port2.RGB_CODE1[1], _blue = port2.RGB_CODE1[2];
                for (int i = 5; i >= 0; i--)
                {
                    ESP_ERROR_CHECK(stripA2->set_pixel(stripA2, j + i, _red, _green, _blue));
                    _red = round(_red / 1.6);
                    _green = round(_green / 1.6);
                    _blue = round(_blue / 1.6);
                }
                ESP_ERROR_CHECK(stripA2->refresh(stripA2, 100));
                vTaskDelay(pdMS_TO_TICKS(port2.speed));
            }
            ESP_ERROR_CHECK(stripA2->refresh(stripA2, 100));
            break;
        }
        case SPLIT_LIGHT:
        {
            int k = round(_NO_LED_A2 / 2);
            for (int j = round(_NO_LED_A2 / 2); j < _NO_LED_A2; j++)
            {
                int _red = port2.RGB_CODE1[0], _green = port2.RGB_CODE1[1], _blue = port2.RGB_CODE1[2];
                int _red2 = port2.RGB_CODE2[0], _green2 = port2.RGB_CODE2[1], _blue2 = port2.RGB_CODE2[2];
                for (int i = 5; i >= 0; i--)
                {
                    ESP_ERROR_CHECK(stripA2->set_pixel(stripA2, j + i, _red, _green, _blue));
                    if (k > i) // difference of number without negative sign to set pixel
                        ESP_ERROR_CHECK(stripA2->set_pixel(stripA2, k - i, _red2, _green2, _blue2));
                    else
                        ESP_ERROR_CHECK(stripA2->set_pixel(stripA2, i - k, _red2, _green2, _blue2));
                    vTaskDelay(pdMS_TO_TICKS(port2.speed));
                    _red = round(_red / 1.6);
                    _green = round(_green / 1.6);
                    _blue = round(_blue / 1.6);
                    _red2 = round(_red2 / 1.6);
                    _green2 = round(_green2 / 1.6);
                    _blue2 = round(_blue2 / 1.6);
                }
                ESP_ERROR_CHECK(stripA2->refresh(stripA2, 100));
                k = k - 1;
                if (k <= 0)
                {
                    break;
                }
            }
            ESP_ERROR_CHECK(stripA2->refresh(stripA2, 100));
        }
        break;
        case INIT_LIGHT:
        {
            uint32_t init_red = 14, init_green = 140, init_blue = 255;
            if (FADE_FLG)
            {
                for (int j = 0; j <= 350; j++)
                {
                    ESP_ERROR_CHECK(stripA2->set_pixel(stripA2, j, percentage_decrease(init_red, 95), percentage_decrease(init_green, 95), percentage_decrease(init_blue, 95)));
                }
                ESP_ERROR_CHECK(stripA2->refresh(stripA2, 100));
                FADE_FLG = false;
            }
            for (int j = 0; j <= 350; j++)
            {
                set_led_pixel(stripA2, j + 1, percentage_decrease(init_red, 95), percentage_decrease(init_green, 95), percentage_decrease(init_blue, 95));
                set_led_pixel(stripA2, j + 2, percentage_decrease(init_red, 70), percentage_decrease(init_green, 70), percentage_decrease(init_blue, 70));
                set_led_pixel(stripA2, j + 3, percentage_decrease(init_red, 40), percentage_decrease(init_green, 40), percentage_decrease(init_blue, 40));
                set_led_pixel(stripA2, j + 4, init_red, init_green, init_blue);
                set_led_pixel(stripA2, j + 5, percentage_decrease(init_red, 40), percentage_decrease(init_green, 40), percentage_decrease(init_blue, 40));
                set_led_pixel(stripA2, j + 6, percentage_decrease(init_red, 70), percentage_decrease(init_green, 70), percentage_decrease(init_blue, 70));
                set_led_pixel(stripA2, j + 7, percentage_decrease(init_red, 95), percentage_decrease(init_green, 95), percentage_decrease(init_blue, 95));
                ESP_ERROR_CHECK(stripA2->refresh(stripA2, 100));
                vTaskDelay(pdMS_TO_TICKS(15));
            }
        }
        break;
        case IDEAL_MODE:
            vTaskDelay(pdMS_TO_TICKS(30));
            break;
        default:
            vTaskDelay(pdMS_TO_TICKS(10));
            break;
        }
    }
#endif
}
void RMT_GROUPB1(void *pvParameters)
{
#ifdef CONFIG_RGB1_CONNECTOR_ENABLE
    rmt_config_t configB1 = RMT_DEFAULT_CONFIG_TX(PORTB1_PIN, RMT_GROUPB1_CHANNEL);
    configB1.clk_div = 2;
    port3.No_Led = 500;
    ESP_ERROR_CHECK(rmt_config(&configB1));
    ESP_ERROR_CHECK(rmt_driver_install(RMT_GROUPB1_CHANNEL, 0, 0));
    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(port3.No_Led + 5, (led_strip_dev_t)configB1.channel);
    led_strip_t *stripB1 = led_strip_new_rmt_ws2812(&strip_config);
    if (!stripB1)
    {
        ESP_LOGE(TAG, "Install WS2812 driver failed");
    }
    ESP_ERROR_CHECK(stripB1->clear(stripB1, 100));
    mode_selection_B1 = INIT_LIGHT;
    while (true)
    {
        uint32_t _NO_LED_B1 = port3.No_Led;
        if (_NO_LED_B1 != port3.prev_led_no)
        {
            ESP_LOGW(TAG, "LED_CHANGED_port3");
            for (uint32_t i = 0; i <= port3.prev_led_no; i++)
            {
                ESP_ERROR_CHECK(stripB1->set_pixel(stripB1, i, 0, 0, 0));
            }
            ESP_ERROR_CHECK(stripB1->refresh(stripB1, port3.prev_led_no));
            port3.prev_led_no = port3.No_Led;
        }
        switch (mode_selection_B1)
        {
        case BLINK_ON:
        {
            uint32_t f_red = port3.RGB_CODE1[0], f_green = port3.RGB_CODE1[1], f_blue = port3.RGB_CODE1[2];
            for (int j = 0; j <= _NO_LED_B1; j++)
            {
                ESP_ERROR_CHECK(stripB1->set_pixel(stripB1, j, f_red, f_green, f_blue));
            }
            ESP_ERROR_CHECK(stripB1->refresh(stripB1, 100));
            vTaskDelay(pdMS_TO_TICKS(port3.speed));
            uint32_t s_red = port3.RGB_CODE2[0], s_green = port3.RGB_CODE2[1], s_blue = port3.RGB_CODE2[2];
            for (int j = 0; j <= _NO_LED_B1; j++)
            {
                ESP_ERROR_CHECK(stripB1->set_pixel(stripB1, j, s_red, s_green, s_blue));
            }
            ESP_ERROR_CHECK(stripB1->refresh(stripB1, 100));
            vTaskDelay(pdMS_TO_TICKS(port3.speed));
        }
        break;
        case BLINK_OFF:
        {
            uint32_t f_red = port3.RGB_CODE1[0], f_green = port3.RGB_CODE1[1], f_blue = port3.RGB_CODE1[2];
            for (int j = 0; j <= _NO_LED_B1; j++)
            {
                ESP_ERROR_CHECK(stripB1->set_pixel(stripB1, j, f_red, f_green, f_blue));
            }
            ESP_ERROR_CHECK(stripB1->refresh(stripB1, 100));
            vTaskDelay(pdMS_TO_TICKS(port3.speed));
        }
        break;
        case FADE_IN:
        {
            uint32_t _red = port3.RGB_CODE1[0], _green = port3.RGB_CODE1[1], _blue = port3.RGB_CODE1[2];
            for (int i = 0; i < 23; i++)
            {
                for (int j = 0; j <= _NO_LED_B1; j++)
                {
                    ESP_ERROR_CHECK(stripB1->set_pixel(stripB1, j, _red, _green, _blue));
                }
                ESP_ERROR_CHECK(stripB1->refresh(stripB1, 100));
                vTaskDelay(pdMS_TO_TICKS(port3.speed));
                _red = percentage_decrease(_red, 8);
                _green = percentage_decrease(_green, 8);
                _blue = percentage_decrease(_blue, 8);
                ESP_LOGI(TAG, "OUT : %d, %d, %d", _red, _green, _blue);
            }
            mode_selection_B1 = IDEAL_MODE;
            break;
        }
        case FADE_OUT:
        {
            uint32_t _red = port3.RGB_CODE1[0], _green = port3.RGB_CODE1[1], _blue = port3.RGB_CODE1[2];
            float f_red = percentage_decrease(_red, 80);
            float f_green = percentage_decrease(_green, 80);
            float f_blue = percentage_decrease(_blue, 80);
            for (int i = 0; i < 40; i++)
            {
                f_red = percentage_increase(f_red, 15);
                f_green = percentage_increase(f_green, 15);
                f_blue = percentage_increase(f_blue, 15);
                if (f_red >= port3.RGB_CODE1[0])
                {
                    f_red = port3.RGB_CODE1[0];
                }
                if (f_green >= port3.RGB_CODE1[1])
                {
                    f_green = port3.RGB_CODE1[1];
                }
                if (f_blue >= port3.RGB_CODE1[2])
                {
                    f_blue = port3.RGB_CODE1[2];
                }
                for (int j = 0; j <= _NO_LED_B1; j++)
                {
                    ESP_ERROR_CHECK(stripB1->set_pixel(stripB1, j, round(f_red), round(f_green), round(f_blue)));
                }
                ESP_ERROR_CHECK(stripB1->refresh(stripB1, 100));
                vTaskDelay(pdMS_TO_TICKS(port3.speed));
                // ESP_LOGI(TAG,"OUT : %f, %f, %f",f_red,f_green,f_blue);
            }
            mode_selection_B1 = IDEAL_MODE;
            break;
        }
        break;
        case FADE_LOOP:
        {
            uint32_t _red = port3.RGB_CODE1[0], _green = port3.RGB_CODE1[1], _blue = port3.RGB_CODE1[2];
            for (int i = 0; i < 23; i++)
            {
                for (int j = 0; j <= _NO_LED_B1; j++)
                {
                    ESP_ERROR_CHECK(stripB1->set_pixel(stripB1, j, _red, _green, _blue));
                }
                ESP_ERROR_CHECK(stripB1->refresh(stripB1, 100));
                vTaskDelay(pdMS_TO_TICKS(port3.speed));
                _red = percentage_decrease(_red, 8);
                _green = percentage_decrease(_green, 8);
                _blue = percentage_decrease(_blue, 8);
                // ESP_LOGI(TAG,"OUT : %d, %d, %d",_red,_green,_blue);
            }
            vTaskDelay(pdMS_TO_TICKS(port3.speed));
            float f_red = percentage_decrease(port3.RGB_CODE2[0], 80);
            float f_green = percentage_decrease(port3.RGB_CODE2[1], 80);
            float f_blue = percentage_decrease(port3.RGB_CODE2[2], 80);
            for (int i = 0; i < 40; i++)
            {
                f_red = percentage_increase(f_red, 15);
                f_green = percentage_increase(f_green, 15);
                f_blue = percentage_increase(f_blue, 15);
                if (f_red >= port3.RGB_CODE2[0])
                {
                    f_red = port3.RGB_CODE2[0];
                }
                if (f_green >= port3.RGB_CODE2[1])
                {
                    f_green = port3.RGB_CODE2[1];
                }
                if (f_blue >= port3.RGB_CODE2[2])
                {
                    f_blue = port3.RGB_CODE2[2];
                }
                for (int j = 0; j <= _NO_LED_B1; j++)
                {
                    ESP_ERROR_CHECK(stripB1->set_pixel(stripB1, j, round(f_red), round(f_green), round(f_blue)));
                }
                ESP_ERROR_CHECK(stripB1->refresh(stripB1, 100));
                vTaskDelay(pdMS_TO_TICKS(port3.speed));
                // ESP_LOGI(TAG,"OUT : %f, %f, %f",f_red,f_green,f_blue);
            }
        }
        break;
        case ROUND_LOOP:
        {
            for (int j = 0; j < _NO_LED_B1; j++)
            {
                int _red = port3.RGB_CODE1[0], _green = port3.RGB_CODE1[1], _blue = port3.RGB_CODE1[2];
                for (int i = 5; i >= 0; i--)
                {
                    ESP_ERROR_CHECK(stripB1->set_pixel(stripB1, j + i, _red, _green, _blue));
                    _red = round(_red / 1.6);
                    _green = round(_green / 1.6);
                    _blue = round(_blue / 1.6);
                }
                ESP_ERROR_CHECK(stripB1->refresh(stripB1, 100));
                vTaskDelay(pdMS_TO_TICKS(port3.speed));
            }
            ESP_ERROR_CHECK(stripB1->refresh(stripB1, 100));
            break;
        }
        case SPLIT_LIGHT:
        {
            int k = round(_NO_LED_B1 / 2);
            for (int j = round(_NO_LED_B1 / 2); j < _NO_LED_B1; j++)
            {
                int _red = port3.RGB_CODE1[0], _green = port3.RGB_CODE1[1], _blue = port3.RGB_CODE1[2];
                int _red2 = port3.RGB_CODE2[0], _green2 = port3.RGB_CODE2[1], _blue2 = port3.RGB_CODE2[2];
                for (int i = 5; i >= 0; i--)
                {
                    ESP_ERROR_CHECK(stripB1->set_pixel(stripB1, j + i, _red, _green, _blue));
                    if (k > i) // difference of number without negative sign to set pixel
                        ESP_ERROR_CHECK(stripB1->set_pixel(stripB1, k - i, _red2, _green2, _blue2));
                    else
                        ESP_ERROR_CHECK(stripB1->set_pixel(stripB1, i - k, _red2, _green2, _blue2));
                    vTaskDelay(pdMS_TO_TICKS(port3.speed));
                    _red = round(_red / 1.6);
                    _green = round(_green / 1.6);
                    _blue = round(_blue / 1.6);
                    _red2 = round(_red2 / 1.6);
                    _green2 = round(_green2 / 1.6);
                    _blue2 = round(_blue2 / 1.6);
                }
                ESP_ERROR_CHECK(stripB1->refresh(stripB1, 100));
                k = k - 1;
                if (k <= 0)
                {
                    break;
                }
            }
            ESP_ERROR_CHECK(stripB1->refresh(stripB1, 100));
        }
        break;
        case INIT_LIGHT:
        {
            uint32_t init_red = 255, init_green = 255, init_blue = 255;
            if (FADE_FLG)
            {
                for (int j = 0; j <= 350; j++)
                {
                    ESP_ERROR_CHECK(stripB1->set_pixel(stripB1, j, percentage_decrease(init_red, 95), percentage_decrease(init_green, 95), percentage_decrease(init_blue, 95)));
                }
                ESP_ERROR_CHECK(stripB1->refresh(stripB1, 100));
                FADE_FLG = false;
            }
            for (int j = 0; j <= 350; j++)
            {
                set_led_pixel(stripB1, j + 1, percentage_decrease(init_red, 95), percentage_decrease(init_green, 95), percentage_decrease(init_blue, 95));
                set_led_pixel(stripB1, j + 2, percentage_decrease(init_red, 70), percentage_decrease(init_green, 70), percentage_decrease(init_blue, 70));
                set_led_pixel(stripB1, j + 3, percentage_decrease(init_red, 40), percentage_decrease(init_green, 40), percentage_decrease(init_blue, 40));
                set_led_pixel(stripB1, j + 4, init_red, init_green, init_blue);
                set_led_pixel(stripB1, j + 5, percentage_decrease(init_red, 40), percentage_decrease(init_green, 40), percentage_decrease(init_blue, 40));
                set_led_pixel(stripB1, j + 6, percentage_decrease(init_red, 70), percentage_decrease(init_green, 70), percentage_decrease(init_blue, 70));
                set_led_pixel(stripB1, j + 7, percentage_decrease(init_red, 95), percentage_decrease(init_green, 95), percentage_decrease(init_blue, 95));
                ESP_ERROR_CHECK(stripB1->refresh(stripB1, 100));
                vTaskDelay(pdMS_TO_TICKS(15));
            }
        }
        break;
        case IDEAL_MODE:
            vTaskDelay(pdMS_TO_TICKS(30));
            break;
        default:
            vTaskDelay(pdMS_TO_TICKS(10));
            break;
        }
    }
#endif
}
void RMT_GROUPB2(void *pvParameters)
{
#ifdef CONFIG_RGB0_CONNECTOR_ENABLE
    rmt_config_t configB2 = RMT_DEFAULT_CONFIG_TX(PORTB2_PIN, RMT_GROUPB2_CHANNEL);
    configB2.clk_div = 2;
    port4.No_Led = 500;
    ESP_ERROR_CHECK(rmt_config(&configB2));
    ESP_ERROR_CHECK(rmt_driver_install(RMT_GROUPB2_CHANNEL, 0, 0));
    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(port4.No_Led + 5, (led_strip_dev_t)configB2.channel);
    led_strip_t *stripB2 = led_strip_new_rmt_ws2812(&strip_config);
    if (!stripB2)
    {
        ESP_LOGE(TAG, "Install WS2812 driver failed");
    }
    ESP_ERROR_CHECK(stripB2->clear(stripB2, 100));
    mode_selection_B2 = INIT_LIGHT;
    while (true)
    {
        uint32_t _NO_LED_B2 = port4.No_Led;
        if (_NO_LED_B2 != port4.prev_led_no)
        {
            ESP_LOGW(TAG, "LED_CHANGED_port4");
            for (uint32_t i = 0; i <= port4.prev_led_no; i++)
            {
                ESP_ERROR_CHECK(stripB2->set_pixel(stripB2, i, 0, 0, 0));
            }
            ESP_ERROR_CHECK(stripB2->refresh(stripB2, port4.prev_led_no));
            port4.prev_led_no = port4.No_Led;
        }
        switch (mode_selection_B2)
        {
        case BLINK_ON:
        {
            uint32_t f_red = port4.RGB_CODE1[0], f_green = port4.RGB_CODE1[1], f_blue = port4.RGB_CODE1[2];
            for (int j = 0; j <= _NO_LED_B2; j++)
            {
                ESP_ERROR_CHECK(stripB2->set_pixel(stripB2, j, f_red, f_green, f_blue));
            }
            ESP_ERROR_CHECK(stripB2->refresh(stripB2, 100));
            vTaskDelay(pdMS_TO_TICKS(port4.speed));
            uint32_t s_red = port4.RGB_CODE2[0], s_green = port4.RGB_CODE2[1], s_blue = port4.RGB_CODE2[2];
            for (int j = 0; j <= _NO_LED_B2; j++)
            {
                ESP_ERROR_CHECK(stripB2->set_pixel(stripB2, j, s_red, s_green, s_blue));
            }
            ESP_ERROR_CHECK(stripB2->refresh(stripB2, 100));
            vTaskDelay(pdMS_TO_TICKS(port4.speed));
        }
        break;
        case BLINK_OFF:
        {
            uint32_t f_red = port4.RGB_CODE1[0], f_green = port4.RGB_CODE1[1], f_blue = port4.RGB_CODE1[2];
            for (int j = 0; j <= _NO_LED_B2; j++)
            {
                ESP_ERROR_CHECK(stripB2->set_pixel(stripB2, j, f_red, f_green, f_blue));
            }
            ESP_ERROR_CHECK(stripB2->refresh(stripB2, 100));
            vTaskDelay(pdMS_TO_TICKS(port4.speed));
        }
        break;
        case FADE_IN:
        {
            uint32_t _red = port4.RGB_CODE1[0], _green = port4.RGB_CODE1[1], _blue = port4.RGB_CODE1[2];
            for (int i = 0; i < 23; i++)
            {
                for (int j = 0; j <= _NO_LED_B2; j++)
                {
                    ESP_ERROR_CHECK(stripB2->set_pixel(stripB2, j, _red, _green, _blue));
                }
                ESP_ERROR_CHECK(stripB2->refresh(stripB2, 100));
                vTaskDelay(pdMS_TO_TICKS(port4.speed));
                _red = percentage_decrease(_red, 8);
                _green = percentage_decrease(_green, 8);
                _blue = percentage_decrease(_blue, 8);
                // ESP_LOGI(TAG,"OUT : %d, %d, %d",_red,_green,_blue);
            }
            mode_selection_B2 = IDEAL_MODE;
            break;
        }
        case FADE_OUT:
        {
            uint32_t _red = port4.RGB_CODE1[0], _green = port4.RGB_CODE1[1], _blue = port4.RGB_CODE1[2];
            float f_red = percentage_decrease(_red, 80);
            float f_green = percentage_decrease(_green, 80);
            float f_blue = percentage_decrease(_blue, 80);
            for (int i = 0; i < 40; i++)
            {
                f_red = percentage_increase(f_red, 15);
                f_green = percentage_increase(f_green, 15);
                f_blue = percentage_increase(f_blue, 15);
                if (f_red >= port4.RGB_CODE1[0])
                {
                    f_red = port4.RGB_CODE1[0];
                }
                if (f_green >= port4.RGB_CODE1[1])
                {
                    f_green = port4.RGB_CODE1[1];
                }
                if (f_blue >= port4.RGB_CODE1[2])
                {
                    f_blue = port4.RGB_CODE1[2];
                }
                for (int j = 0; j <= _NO_LED_B2; j++)
                {
                    ESP_ERROR_CHECK(stripB2->set_pixel(stripB2, j, round(f_red), round(f_green), round(f_blue)));
                }
                ESP_ERROR_CHECK(stripB2->refresh(stripB2, 100));
                vTaskDelay(pdMS_TO_TICKS(port4.speed));
                // ESP_LOGI(TAG,"OUT : %f, %f, %f",f_red,f_green,f_blue);
            }
            mode_selection_B2 = IDEAL_MODE;
            break;
        }
        break;
        case FADE_LOOP:
        {
            uint32_t _red = port4.RGB_CODE1[0], _green = port4.RGB_CODE1[1], _blue = port4.RGB_CODE1[2];
            for (int i = 0; i < 23; i++)
            {
                for (int j = 0; j <= _NO_LED_B2; j++)
                {
                    ESP_ERROR_CHECK(stripB2->set_pixel(stripB2, j, _red, _green, _blue));
                }
                ESP_ERROR_CHECK(stripB2->refresh(stripB2, 100));
                vTaskDelay(pdMS_TO_TICKS(port4.speed));
                _red = percentage_decrease(_red, 8);
                _green = percentage_decrease(_green, 8);
                _blue = percentage_decrease(_blue, 8);
                // ESP_LOGI(TAG,"OUT : %d, %d, %d",_red,_green,_blue);
            }
            vTaskDelay(pdMS_TO_TICKS(port4.speed));
            float f_red = percentage_decrease(port4.RGB_CODE2[0], 80);
            float f_green = percentage_decrease(port4.RGB_CODE2[1], 80);
            float f_blue = percentage_decrease(port4.RGB_CODE2[2], 80);
            for (int i = 0; i < 40; i++)
            {
                f_red = percentage_increase(f_red, 15);
                f_green = percentage_increase(f_green, 15);
                f_blue = percentage_increase(f_blue, 15);
                if (f_red >= port4.RGB_CODE2[0])
                {
                    f_red = port4.RGB_CODE2[0];
                }
                if (f_green >= port4.RGB_CODE2[1])
                {
                    f_green = port4.RGB_CODE2[1];
                }
                if (f_blue >= port4.RGB_CODE2[2])
                {
                    f_blue = port4.RGB_CODE2[2];
                }
                for (int j = 0; j <= _NO_LED_B2; j++)
                {
                    ESP_ERROR_CHECK(stripB2->set_pixel(stripB2, j, round(f_red), round(f_green), round(f_blue)));
                }
                ESP_ERROR_CHECK(stripB2->refresh(stripB2, 100));
                vTaskDelay(pdMS_TO_TICKS(port4.speed));
                // ESP_LOGI(TAG,"OUT : %f, %f, %f",f_red,f_green,f_blue);
            }
        }
        break;
        case ROUND_LOOP:
        {
            for (int j = 0; j < _NO_LED_B2; j++)
            {
                int _red = port4.RGB_CODE1[0], _green = port4.RGB_CODE1[1], _blue = port4.RGB_CODE1[2];
                for (int i = 5; i >= 0; i--)
                {
                    ESP_ERROR_CHECK(stripB2->set_pixel(stripB2, j + i, _red, _green, _blue));
                    _red = round(_red / 1.6);
                    _green = round(_green / 1.6);
                    _blue = round(_blue / 1.6);
                }
                ESP_ERROR_CHECK(stripB2->refresh(stripB2, 100));
                vTaskDelay(pdMS_TO_TICKS(port4.speed));
            }
            ESP_ERROR_CHECK(stripB2->refresh(stripB2, 100));
            break;
        }
        case SPLIT_LIGHT:
        {
            int k = round(_NO_LED_B2 / 2);
            for (int j = round(_NO_LED_B2 / 2); j < _NO_LED_B2; j++)
            {
                int _red = port4.RGB_CODE1[0], _green = port4.RGB_CODE1[1], _blue = port4.RGB_CODE1[2];
                int _red2 = port4.RGB_CODE2[0], _green2 = port4.RGB_CODE2[1], _blue2 = port4.RGB_CODE2[2];
                for (int i = 5; i >= 0; i--)
                {
                    ESP_ERROR_CHECK(stripB2->set_pixel(stripB2, j + i, _red, _green, _blue));
                    if (k > i) // difference of number without negative sign to set pixel
                        ESP_ERROR_CHECK(stripB2->set_pixel(stripB2, k - i, _red2, _green2, _blue2));
                    else
                        ESP_ERROR_CHECK(stripB2->set_pixel(stripB2, i - k, _red2, _green2, _blue2));
                    vTaskDelay(pdMS_TO_TICKS(port4.speed));
                    _red = round(_red / 1.6);
                    _green = round(_green / 1.6);
                    _blue = round(_blue / 1.6);
                    _red2 = round(_red2 / 1.6);
                    _green2 = round(_green2 / 1.6);
                    _blue2 = round(_blue2 / 1.6);
                }
                ESP_ERROR_CHECK(stripB2->refresh(stripB2, 100));
                k = k - 1;
                if (k <= 0)
                {
                    break;
                }
            }
            ESP_ERROR_CHECK(stripB2->refresh(stripB2, 100));
        }
        break;
        case INIT_LIGHT:
        {
            uint32_t init_red = 255, init_green = 255, init_blue = 255;
            if (FADE_FLG)
            {
                for (int j = 0; j <= 350; j++)
                {
                    ESP_ERROR_CHECK(stripB2->set_pixel(stripB2, j, percentage_decrease(init_red, 95), percentage_decrease(init_green, 95), percentage_decrease(init_blue, 95)));
                }
                ESP_ERROR_CHECK(stripB2->refresh(stripB2, 100));
                FADE_FLG = false;
            }
            for (int j = 0; j <= 150; j++)
            {
                set_led_pixel(stripB2, j + 1, percentage_decrease(init_red, 95), percentage_decrease(init_green, 95), percentage_decrease(init_blue, 95));
                set_led_pixel(stripB2, j + 2, percentage_decrease(init_red, 70), percentage_decrease(init_green, 70), percentage_decrease(init_blue, 70));
                set_led_pixel(stripB2, j + 3, percentage_decrease(init_red, 40), percentage_decrease(init_green, 40), percentage_decrease(init_blue, 40));
                set_led_pixel(stripB2, j + 4, init_red, init_green, init_blue);
                set_led_pixel(stripB2, j + 5, percentage_decrease(init_red, 40), percentage_decrease(init_green, 40), percentage_decrease(init_blue, 40));
                set_led_pixel(stripB2, j + 6, percentage_decrease(init_red, 70), percentage_decrease(init_green, 70), percentage_decrease(init_blue, 70));
                set_led_pixel(stripB2, j + 7, percentage_decrease(init_red, 95), percentage_decrease(init_green, 95), percentage_decrease(init_blue, 95));
                ESP_ERROR_CHECK(stripB2->refresh(stripB2, 100));
                vTaskDelay(pdMS_TO_TICKS(15));
            }
        }
        break;
        case IDEAL_MODE:
            vTaskDelay(pdMS_TO_TICKS(30));
            break;
        default:
            vTaskDelay(pdMS_TO_TICKS(10));
            break;
        }
    }
#endif
}
uint8_t *hex_str_to_uint8(const char *string)
{
    if (string == NULL)
        return NULL;
    size_t slength = strlen(string);
    if ((slength % 2) != 0) // must be even
        return NULL;
    size_t dlength = slength / 2;
    uint8_t *data = (uint8_t *)malloc(dlength);
    memset(data, 0, dlength);
    size_t index = 0;
    while (index < slength)
    {
        char c = string[index];
        int value = 0;
        if (c >= '0' && c <= '9')
            value = (c - '0');
        else if (c >= 'A' && c <= 'F')
            value = (10 + (c - 'A'));
        else if (c >= 'a' && c <= 'f')
            value = (10 + (c - 'a'));
        else
            return NULL;
        data[(index / 2)] += value << (((index + 1) % 2) * 4);
        index++;
    }
    return data;
}

void Config_to_mac()
{
    printf("\n");
    printf("Broadcast Mac : ");
    uint8_t *MAC_data = hex_str_to_uint8(CONFIG_MASTER_MAC_ADDRESS);
    for (int i = 0; i < 6; i++)
    {
        broadcast_mac[i] = MAC_data[i];
        printf("%02X", broadcast_mac[i]);
    }
    printf("\n");
}
void driver_install()
{
    rmt_config_t config_1 = RMT_DEFAULT_CONFIG_TX(RGB_PIN_1, RMT_GROUPB1_CHANNEL);
    config_1.clk_div = 2;
    ESP_ERROR_CHECK(rmt_config(&config_1));
    ESP_ERROR_CHECK(rmt_driver_install(RMT_GROUPB1_CHANNEL, 0, 0));
    led_strip_config_t strip_config_1 = LED_STRIP_DEFAULT_CONFIG(NUM_LED + 1, (led_strip_dev_t)config_1.channel);
    strip_1 = led_strip_new_rmt_ws2812(&strip_config_1);

    rmt_config_t config_2 = RMT_DEFAULT_CONFIG_TX(RGB_PIN_2, RMT_GROUPB2_CHANNEL);
    config_2.clk_div = 2;
    ESP_ERROR_CHECK(rmt_config(&config_2));
    ESP_ERROR_CHECK(rmt_driver_install(RMT_GROUPB2_CHANNEL, 0, 0));
    led_strip_config_t strip_config_2 = LED_STRIP_DEFAULT_CONFIG(NUM_LED + 1, (led_strip_dev_t)config_2.channel);
    strip_2 = led_strip_new_rmt_ws2812(&strip_config_2);

    if ((!strip_1) && (!strip_2))
    {
        ESP_LOGE(RGB_TAG, "LED driver install failed");
    }
    ESP_LOGI(RGB_TAG, "LED driver installed");
    ESP_ERROR_CHECK(strip_1->clear(strip_1, NUM_LED));
    ESP_ERROR_CHECK(strip_2->clear(strip_2, NUM_LED));
    vTaskDelay(pdMS_TO_TICKS(10));
}
// void init_on()
// {

//     for (i = 0; i <= NUM_LED; i++)
//     {
//         ESP_ERROR_CHECK(strip_1->set_pixel(strip_1, i, colour_array_1[1], colour_array_1[0], colour_array_1[2]));
//         ESP_ERROR_CHECK(strip_2->set_pixel(strip_2, i, colour_array_1[1], colour_array_1[0], colour_array_1[2]));
//     }
//     ESP_ERROR_CHECK(strip_1->refresh(strip_1, NUM_LED));
//     ESP_ERROR_CHECK(strip_2->refresh(strip_2, NUM_LED));

//     vTaskDelay(pdMS_TO_TICKS(500));
// }

void rbg_control()
{
    rgb_mode=init_on;
    while (true)
    {
        switch (rgb_mode)
        {
        case left_blink:
            for (i = 0; i <= NUM_LED; i++)
            {
                ESP_ERROR_CHECK(strip_1->set_pixel(strip_1, i, colour_array_1[1], colour_array_1[0], colour_array_1[2]));
                // ESP_LOGE(RGB_TAG, "count   %d:", i);
            }
            ESP_ERROR_CHECK(strip_1->refresh(strip_1, NUM_LED));
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP_ERROR_CHECK(strip_1->clear(strip_1, NUM_LED));
            vTaskDelay(pdMS_TO_TICKS(250));
            break;
        case right_blink:
            for (i = 0; i <= NUM_LED; i++)
            {
                ESP_ERROR_CHECK(strip_2->set_pixel(strip_2, i, colour_array_1[1], colour_array_1[0], colour_array_1[2]));
            }
            ESP_ERROR_CHECK(strip_2->refresh(strip_2, NUM_LED));
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP_ERROR_CHECK(strip_2->clear(strip_2, NUM_LED));
            vTaskDelay(pdMS_TO_TICKS(250));
            break;
        case left_right_blink:
            for (i = 0; i <= NUM_LED; i++)
            {
                ESP_ERROR_CHECK(strip_1->set_pixel(strip_1, i, colour_array_1[1], colour_array_1[0], colour_array_1[2]));
                ESP_ERROR_CHECK(strip_2->set_pixel(strip_2, i, colour_array_1[1], colour_array_1[0], colour_array_1[2]));
                vTaskDelay(1);
            }
            ESP_ERROR_CHECK(strip_1->refresh(strip_1, NUM_LED));
            ESP_ERROR_CHECK(strip_2->refresh(strip_2, NUM_LED));
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP_ERROR_CHECK(strip_1->clear(strip_1, NUM_LED));
            ESP_ERROR_CHECK(strip_2->clear(strip_2, NUM_LED));
            vTaskDelay(pdMS_TO_TICKS(250));
            break;
        case left_right_on:
            for (i = 0; i <= NUM_LED; i++)
            {
                ESP_ERROR_CHECK(strip_1->set_pixel(strip_1, i, colour_array_1[1], colour_array_1[0], colour_array_1[2]));
                ESP_ERROR_CHECK(strip_2->set_pixel(strip_2, i, colour_array_1[1], colour_array_1[0], colour_array_1[2]));
                vTaskDelay(1);
            }
            ESP_ERROR_CHECK(strip_1->refresh(strip_1, NUM_LED));
            ESP_ERROR_CHECK(strip_2->refresh(strip_2, NUM_LED));
            vTaskDelay(pdMS_TO_TICKS(10));
            break;
        case left_right_off:
            ESP_ERROR_CHECK(strip_1->clear(strip_1, NUM_LED));
            ESP_ERROR_CHECK(strip_2->clear(strip_2, NUM_LED));
            vTaskDelay(pdMS_TO_TICKS(10));
            break;
        case init_on:
            for (i = 0; i <= NUM_LED; i++)
            {
                ESP_ERROR_CHECK(strip_1->set_pixel(strip_1, i, colour_array_1[1], colour_array_1[0], colour_array_1[2]));
                ESP_ERROR_CHECK(strip_2->set_pixel(strip_2, i, colour_array_1[1], colour_array_1[0], colour_array_1[2]));
            }
            ESP_ERROR_CHECK(strip_1->refresh(strip_1, NUM_LED));
            ESP_ERROR_CHECK(strip_2->refresh(strip_2, NUM_LED));

            vTaskDelay(pdMS_TO_TICKS(500));
        default:
            break;
        }
        vTaskDelay(1);
    }
}

void indicator_control()
{
    ESP_LOGW(TAG, "LED_INDICATOR_CONTROL");
    driver_install();
   // init_on();
    while (1)
    {
        rbg_control();

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    Config_to_mac();
    wifi_initialization();
    esp_now_initialization();
#ifdef CONFIG_RGB3_CONNECTOR_ENABLE
    xTaskCreate(RMT_GROUPA1, "Control_led_strip_1", 3072, NULL, 1, NULL);
#endif
#ifdef CONFIG_RGB2_CONNECTOR_ENABLE
    xTaskCreate(RMT_GROUPA2, "Control_led_strip_2", 3072, NULL, 1, NULL);
#endif
#ifdef CONFIG_INDICATOR_ENABLE
    xTaskCreate(indicator_control, "indicator strip", 4096, NULL, 1, NULL);
#else
#ifdef CONFIG_RGB1_CONNECTOR_ENABLE
    xTaskCreate(RMT_GROUPB1, "Control_led_strip_3", 3072, NULL, 1, NULL);
#endif
#ifdef CONFIG_RGB0_CONNECTOR_ENABLE
    xTaskCreate(RMT_GROUPB2, "Control_led_strip_4", 3072, NULL, 1, NULL);
#endif
#endif
}
