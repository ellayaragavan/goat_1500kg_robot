#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_crc.h"
#include "esp_now.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "sdkconfig.h"
#include "led_strip.h"
#include "espnow_example.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "driver/rmt.h"
#include "sdkconfig.h"
#include "esp_timer.h"
#include "ota_update.h"

#define RMT_TX_CHANNEL RMT_CHANNEL_0
#define CHARGER_VOLTAGE_ADC 500
#define DEFAULT_VREF 1100
#define NO_OF_SAMPLES 60
#define LIMIT_SWITCH_1 37
#define LIMIT_SWITCH_2 41
#define RELAY_PIN 9
#define ACTUATOR_RELAY1 38
#define ACTUATOR_RELAY2 39

#define ACTUATOR_RELAY_BIT_MASK ((1ULL << ACTUATOR_RELAY1) | (1ULL << ACTUATOR_RELAY2))
#define GPIO_BIT_MASK ((1ULL << LIMIT_SWITCH_1) | (1ULL << LIMIT_SWITCH_2))
#define RELAY_BIT_MAK (1ULL << RELAY_PIN)

#define LED_TYPE LED_STRIP_WS2812
#define LED_GPIO 36
#define LED_STRIP_LENGTH 150
#define ESPNOW_MAXDELAY 10
#define NUM_LED 60

led_strip_t *strip;

static const char *TAG = "Docking";
const uint8_t MAIN_BOARD_MAC[ESP_NOW_ETH_ALEN] = {0xf4, 0x12, 0xfa, 0xc2, 0x91, 0x54}; // EC:94:CB:78:2F:89//ac:67:b2:3c:b3:75-new//ac:67:b2:3c:98:95
const uint8_t NEW_MAC[ESP_NOW_ETH_ALEN] = {0x68, 0xb6, 0xb3, 0x47, 0xd6, 0x5c};

static QueueHandle_t s_example_espnow_queue;

static esp_adc_cal_characteristics_t *adc_chars;
static const adc_channel_t voltage_channel = ADC_CHANNEL_9;
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;

char str[20];
char string_json[100];
char *dock_state = NULL;
bool pin_read = false;
bool charger_flg = false;
bool limit_switch = false;
bool status_recieved = false;
uint32_t voltage_reading = 0;
int32_t count = 0;
int32_t limit_1 = 0;
int32_t limit_2 = 0;
int64_t time_set = 0;

static int colors[7][3] = {
    {255, 255, 255},
    {255, 255, 0},
    {255, 0, 255},
    {0, 255, 0},
    {0, 0, 255},
    {0, 0, 0},
    {0, 0, 255}};

// static const rgb_t colors[] = {
//     {.r = 0x77, .g = 0x77, .b = 0x77},
//     {.r = 0xff, .g = 0xff, .b = 0x00},
//     {.r = 0xff, .g = 0x00, .b = 0x00},
//     {.r = 0x00, .g = 0xff, .b = 0x00},
//     {.r = 0x00, .g = 0x00, .b = 0xff},
//     {.r = 0x00, .g = 0x00, .b = 0x00},
//     {.r = 0x00, .g = 0x00, .b = 0xff},
// }; // yellow,red,green,blue

enum key_value
{
    ideal,
    Alive,
    dockmode_on,
    dockmode_off,
    dock,
    pre_dock,
    success,
    undock,
    restart,
    pin_status,
    Relay_switching,
    fail,
    actuator_on,
    actuator_off,
    docker_relayon,
    docker_relayoff
} key_string;

enum sensor
{
    ideal_sensor,
    read_pin,
    charger_voltage
} key_sensor;

enum led_display
{
    ideal_led,
    boot,
    charging,
    error,
    undocking,
    communication,
    ideal_hold,
    actuator_on_led
} led_control;

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
    ESP_ERROR_CHECK(esp_wifi_start());
}
static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (mac_addr == NULL)
    {
        ESP_LOGE(TAG, "SEND_CB_ERROR");
        return;
    }
    if (status != ESP_NOW_SEND_SUCCESS)
    {
        ESP_LOGE(TAG, "ESP_SEND_DATA_FAILED\n");
    }
    if (status == ESP_NOW_SEND_SUCCESS)
    {
        ESP_LOGI(TAG, "ESP_SEND_DATA_SUCCESS\n");
    }
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
        // printf("%02X:",recv_mac[i]);
    }
    // printf("\n%d\n",cnt);
    if (cnt == 6)
    {
        return true;
    }
    else
    {
        return false;
    }
}
uint32_t key_identify(const char *rev_string)
{
    if (strcmp(rev_string, "Alive?") == 0)
    {
        return 1;
    }
    else if (strcmp(rev_string, "dockmode_on") == 0)
    {
        return 2;
    }
    else if (strcmp(rev_string, "dockmode_off") == 0)
    {
        return 3;
    }
    else if (strcmp(rev_string, "dock") == 0)
    {
        return 4;
    }
    else if (strcmp(rev_string, "predocking") == 0)
    {
        return 5;
    }
    else if (strcmp(rev_string, "success") == 0)
    {
        return 6;
    }
    else if (strcmp(rev_string, "undock") == 0)
    {
        return 7;
    }
    else if (strcmp(rev_string, "restart") == 0)
    {
        return 8;
    }
    else if (strcmp(rev_string, "fail") == 0)
    {
        return 11;
    }
    else if (strcmp(rev_string, "actuator_on") == 0)
    {
        return 12;
    }
    else if (strcmp(rev_string, "actuator_off") == 0)
    {
        return 13;
    }
    else if (strcmp(rev_string, "docker_relayon") == 0)
    {
        return 14;
    }
    else if (strcmp(rev_string, "docker_relayoff") == 0)
    {
        return 15;
    }
    else
    {
        return 0;
    }
}

void send_main(cJSON *data)
{
    char *my_json_string = cJSON_PrintUnformatted(data);
    uint8_t data_send[strlen(my_json_string)];
    ESP_LOGI(TAG, "the send string is : %s", my_json_string);
    memcpy(data_send, my_json_string, strlen(my_json_string));
    if (esp_now_send(MAIN_BOARD_MAC, data_send, strlen(my_json_string)) != ESP_OK)
    {
        ESP_LOGE(TAG, "SEND_ERROR\n");
    }
    cJSON_free(my_json_string);
    cJSON_Delete(data);
}

static void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    memcpy(&string_json, data, sizeof(string_json));
    printf(string_json);
    cJSON *root2 = cJSON_Parse(string_json);
    if (compare_mac(mac_addr, MAIN_BOARD_MAC) == true)
    {
        ESP_LOGI(TAG, "RECIVED_FROM_MASTER");
        if (cJSON_HasObjectItem(root2, "ota"))
        {
            cJSON *send_data = cJSON_CreateObject();
            cJSON_AddBoolToObject(send_data, "ota", true);
            ESP_LOGI(TAG,"OTA");
            send_main(send_data);
            ota_update_init();
        }
        else if (cJSON_HasObjectItem(root2, "docking"))
        {
            dock_state = cJSON_GetObjectItem(root2, "docking")->valuestring;
            ESP_LOGI(TAG, "The Key string is %s", dock_state);
            key_string = key_identify(dock_state);
            ESP_LOGI(TAG, "The key string is %d", key_string);
        }
    }
    if (mac_addr == NULL || data == NULL || len <= 0)
    {
        ESP_LOGE(TAG, "Receive cb arg error");
        return;
    }
    cJSON_Delete(root2);
}

void read_sensor()
{
    adc1_config_channel_atten((adc1_channel_t)voltage_channel, atten);
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);
    key_sensor = ideal_sensor;
    while (true)
    {
        switch (key_sensor)
        {
        case read_pin:
            limit_1 = gpio_get_level(LIMIT_SWITCH_1);
            limit_2 = gpio_get_level(LIMIT_SWITCH_2);
            if (limit_1 == 1 || limit_2 == 1)
            {
                ESP_LOGE(TAG, "SWITCH_1 : [%d],SWITCH_2 : [%d]", limit_1, limit_2);
                count = count + 1;
                if (count >= 10)
                {
                    limit_switch = false;
                }
            }
            else
            {
                count = 0;
                limit_switch = true;
            }
            if (limit_switch == false)
            {
                cJSON *root2 = cJSON_CreateObject();
                ESP_LOGE(TAG, "SWITCH_1 : [%d],SWITCH_2 : [%d]", gpio_get_level(LIMIT_SWITCH_1), gpio_get_level(LIMIT_SWITCH_2));
                cJSON_AddStringToObject(root2, "docking", "fail");
                cJSON_AddStringToObject(root2, "reason", "robot");
                gpio_set_level(RELAY_PIN, 0);
                send_main(root2);
                vTaskDelay(2000 / portTICK_PERIOD_MS);
                // cJSON *root_data_send = cJSON_CreateObject();
                // cJSON_AddStringToObject(root_data_send, "docker", "relay_off");
                // cJSON_AddBoolToObject(root_data_send, "relay_off", true);
                // send_main(root_data_send);
                key_sensor = ideal_sensor;
                led_control = error;
                key_string = ideal;
                limit_switch = true;
                cJSON *root_fail = cJSON_CreateObject();
                charger_flg = false;
                cJSON_AddStringToObject(root_fail, "docker", "fail");
                cJSON_AddStringToObject(root_fail, "fail", "fail");
                send_main(root_fail);
                break;
            }
            key_sensor = charger_voltage;
            break;
        case charger_voltage:
            if ((voltage_reading <= CHARGER_VOLTAGE_ADC) && (charger_flg == true))
            {
                cJSON *root2 = cJSON_CreateObject();
                ESP_LOGE(TAG, "CHARGER_VOLTAGE_DOWN : [%d]", voltage_reading);
                cJSON_AddStringToObject(root2, "docking", "fail");
                cJSON_AddStringToObject(root2, "reason", "docker");
                gpio_set_level(RELAY_PIN, 0);
                send_main(root2);
                key_sensor = ideal_sensor;
                led_control = error;
                key_string = ideal;
                charger_flg = false;
                cJSON *root_fail = cJSON_CreateObject();
                cJSON_AddStringToObject(root_fail, "docker", "fail");
                send_main(root_fail);
                vTaskDelay(2000 / portTICK_PERIOD_MS);
                led_control = ideal_led;
                break;
            }
            key_sensor = read_pin;
            break;
        case ideal_sensor:
            break;
        }
        for (int i = 0; i < NO_OF_SAMPLES; i++)
        {
            voltage_reading += adc1_get_raw((adc1_channel_t)voltage_channel);
        }
        voltage_reading /= NO_OF_SAMPLES;
        vTaskDelay(pdMS_TO_TICKS(100));
        // printf("time-s: %lld\n",esp_timer_get_time());
    }
}

void actuatorOn()
{
    gpio_set_level(ACTUATOR_RELAY1, 0);
    gpio_set_level(ACTUATOR_RELAY2, 1);
    vTaskDelay(6000 / portTICK_PERIOD_MS);
    gpio_set_level(ACTUATOR_RELAY1, 1);
    gpio_set_level(ACTUATOR_RELAY2, 1);
}

void actuatorOff()
{
    gpio_set_level(ACTUATOR_RELAY1, 1);
    gpio_set_level(ACTUATOR_RELAY2, 0);
    vTaskDelay(6000 / portTICK_PERIOD_MS);
    gpio_set_level(ACTUATOR_RELAY1, 1);
    gpio_set_level(ACTUATOR_RELAY2, 1);
}

void main_response()
{
    // esp_base_mac_addr_set(NEW_MAC);

    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = GPIO_BIT_MASK;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    io_conf.pin_bit_mask = RELAY_BIT_MAK;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    gpio_config_t actuator_config;
    actuator_config.pin_bit_mask = ACTUATOR_RELAY_BIT_MASK;
    actuator_config.mode = GPIO_MODE_OUTPUT;
    actuator_config.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&actuator_config);
    gpio_set_level(ACTUATOR_RELAY1, 1);
    gpio_set_level(ACTUATOR_RELAY2, 1);
    gpio_set_level(RELAY_PIN, 0);
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    wifi_init();
    // ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    s_example_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(example_espnow_event_t));
    if (s_example_espnow_queue == NULL)
    {
        ESP_LOGE(TAG, "Create mutex fail");
    }

    if (esp_now_init() != ESP_OK)
    {
        printf("ESP_INIT_ERROR\n");
    }
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
    ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK));
    uint32_t version;
    esp_now_get_version(&version);
    ESP_LOGI(TAG, "ESP_NOW_VERSION : %u\n", version);
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL)
    {
        printf("Malloc peer information fail\n");
        vSemaphoreDelete(s_example_espnow_queue);
        esp_now_deinit();
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = 1;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, MAIN_BOARD_MAC, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(peer));
    free(peer);

    esp_now_peer_num_t *num = malloc(sizeof(esp_now_peer_num_t));
    esp_now_get_peer_num(num);
    ESP_LOGI(TAG, "NUMBER OF PEER : %d", num->total_num);
    free(num);
    key_string = 0;

    while (true)
    {
        // ESP_LOGI(TAG, "SWITCH_1 : [%d],SWITCH_2 : [%d]", gpio_get_level(LIMIT_SWITCH_1), gpio_get_level(LIMIT_SWITCH_2));
        cJSON *root = cJSON_CreateObject();
        switch (key_string)
        {
        case Alive:
            cJSON_AddBoolToObject(root, "docking", true);
            send_main(root);
            key_string = ideal;
            break;
        case dockmode_on:
            cJSON_AddBoolToObject(root, "dockmode_on", true);
            send_main(root);
            led_control = communication;
            key_string = pin_status;
            break;
        case pin_status:
            if ((gpio_get_level(LIMIT_SWITCH_1) == 0) && gpio_get_level(LIMIT_SWITCH_2) == 0)
            {
                ESP_LOGI(TAG, "SWITCH_PRESSED");
                cJSON_AddBoolToObject(root, "dockpin", true);
                send_main(root);
                led_control = ideal_led;
                key_string = ideal;
                break;
            }
            cJSON_Delete(root);
            break;
        case dockmode_off:
            cJSON_AddBoolToObject(root, "dockmode_off", true);
            send_main(root);
            key_string = ideal;
            led_control = ideal_led;
            key_sensor = ideal_sensor;
            break;
        case dock:
            cJSON_AddStringToObject(root, "docker", "response");
            send_main(root);
            key_string = pre_dock;
            break;
        case pre_dock:
            ESP_LOGV(TAG, "CHARGER_VOLT : [%d]", voltage_reading);
            if (voltage_reading >= CHARGER_VOLTAGE_ADC)
            {
                cJSON_AddStringToObject(root, "docking", "predocking");
                send_main(root);
                led_control = actuator_on_led;
                actuatorOn();
                led_control = ideal;
                key_string = Relay_switching;
                charger_flg = true;
                break;
            }
            else
            {
                ESP_LOGE(TAG, "CHARGER_VOLT : [%d]", voltage_reading);
                cJSON_AddStringToObject(root, "docking", "fail");
                cJSON_AddStringToObject(root, "reason", "docker");
                key_sensor = ideal_sensor;
                led_control = error;
                send_main(root);
                key_string = ideal;
                break;
            }
            break;
        case Relay_switching:
            gpio_set_level(RELAY_PIN, 1);
            cJSON_AddStringToObject(root, "docker", "relay_on");
            cJSON_AddBoolToObject(root, "relay_on", true);
            send_main(root);
            vTaskDelay(1300 / portTICK_PERIOD_MS);
            cJSON *root_data_send = cJSON_CreateObject();
            cJSON_AddStringToObject(root_data_send, "docker", "relay_off");
            cJSON_AddBoolToObject(root_data_send, "relay_off", true);
            send_main(root_data_send);
            gpio_set_level(RELAY_PIN, 0);
            break;
        case success:
            ESP_LOGI(TAG, "DOCKING_SUCCESS");
            cJSON_AddStringToObject(root, "docker", "success");
            cJSON_AddBoolToObject(root, "success", true);
            send_main(root);
            gpio_set_level(RELAY_PIN, 1);
            key_sensor = read_pin;
            led_control = charging;
            key_string = ideal;
            break;
        case undock:
            ESP_LOGI(TAG, "UNDOCKING_SUCCESS");
            gpio_set_level(RELAY_PIN, 0);
            cJSON_AddStringToObject(root, "undocking", "success");
            led_control = undocking;
            actuatorOff();
            send_main(root);
            charger_flg = false;
            key_string = ideal;
            led_control = ideal_led;
            key_sensor = ideal_sensor;
            break;
        case restart:
            ESP_LOGW(TAG, "MAIN_BOARD_RESTARTED");
            gpio_set_level(RELAY_PIN, 0);
            charger_flg = false;
            key_string = ideal;
            led_control = ideal_led;
            key_sensor = ideal_sensor;
            cJSON_Delete(root);
            break;
        case fail:
            led_control = error;
            actuatorOff();
            led_control = ideal;
            key_string = ideal;
            break;
        case actuator_on:
            actuatorOn();
            key_string = ideal;
            break;
        case actuator_off:
            led_control = undocking;
            ESP_LOGI(TAG, "Inside actuator off");
            actuatorOff();
            key_string = ideal;
            led_control = ideal_led;
            break;
        case docker_relayon:
            gpio_set_level(RELAY_PIN, 1);
            key_string = ideal;
            break;
        case docker_relayoff:
            gpio_set_level(RELAY_PIN, 0);
            key_string = ideal;
            break;
        case ideal:
            cJSON_Delete(root);
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(400));
        // printf("time-m: %lld\n",esp_timer_get_time());
    }
}
void rgb_display(void *pvParameters)
{
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(LED_GPIO, RMT_TX_CHANNEL);
    config.clk_div = 2;
    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(RMT_TX_CHANNEL, 0, 0));
    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(NUM_LED, (led_strip_dev_t)config.channel);
    strip = led_strip_new_rmt_ws2812(&strip_config);
    if (!strip)
    {
        ESP_LOGE(TAG, "LED driver install failed");
    }
    ESP_LOGI(TAG, "LED driver installed");
    ESP_ERROR_CHECK(strip->clear(strip, NUM_LED));
    vTaskDelay(pdMS_TO_TICKS(10));

    // led_strip_t strip = {
    //     .type = LED_TYPE,
    //     .length = LED_STRIP_LENGTH,
    //     .gpio = LED_GPIO,
    //     .buf = NULL,
    //     .brightness = 250,
    // };
    // ESP_ERROR_CHECK(led_strip_init(&strip));
    led_control = boot;
    uint32_t num = 150;
    while (true)
    {
        switch (led_control)
        {
        case ideal_led:
            for (int i = 0; i < NUM_LED; i++)
            {
                ESP_ERROR_CHECK(strip->set_pixel(strip, i, colors[0][0], colors[0][1], colors[0][2]));
            }
            ESP_ERROR_CHECK(strip->refresh(strip, NUM_LED));
            // ESP_ERROR_CHECK(led_strip_flush(&strip));
            // ESP_ERROR_CHECK(led_strip_fill(&strip, 0, strip.length, colors[0]));
            // ESP_ERROR_CHECK(led_strip_flush(&strip));
            led_control = ideal_hold;
            // led_control = charging;
            break;
        case boot:
            for (int i = 0; i < NUM_LED; i++)
            {
                ESP_ERROR_CHECK(strip->set_pixel(strip, i, colors[5][0], colors[5][1], colors[5][2]));
            }
            ESP_ERROR_CHECK(strip->refresh(strip, NUM_LED));
            // ESP_ERROR_CHECK(led_strip_set_pixel(&strip, num, colors[5]));
            // ESP_ERROR_CHECK(led_strip_flush(&strip));
            for (int i = 0; i < NUM_LED; i++)
            {
                ESP_ERROR_CHECK(strip->set_pixel(strip, i, colors[0][0], colors[0][1], colors[0][2]));
            }
            ESP_ERROR_CHECK(strip->refresh(strip, NUM_LED));
            // ESP_ERROR_CHECK(led_strip_set_pixel(&strip, num, colors[0]));
            // ESP_ERROR_CHECK(led_strip_flush(&strip));
            if (num == LED_STRIP_LENGTH)
            {
                led_control = ideal_led;
                break;
            }
            num++;
            // led_strip_wait(&strip,pdMS_TO_TICKS(10));
            break;
        case charging:
            for (int i = 0; i < NUM_LED; i++)
            {
                ESP_ERROR_CHECK(strip->set_pixel(strip, i, colors[1][0], colors[1][1], colors[1][2]));
            }
            ESP_ERROR_CHECK(strip->refresh(strip, NUM_LED));
            // ESP_ERROR_CHECK(led_strip_flush(&strip));
            // ESP_ERROR_CHECK(led_strip_fill(&strip, 0, strip.length, colors[1]));
            // ESP_ERROR_CHECK(led_strip_flush(&strip));
            led_control = ideal_hold;
            break;
        case error:
            for (int i = 0; i < NUM_LED; i++)
            {
                ESP_ERROR_CHECK(strip->set_pixel(strip, i, colors[2][0], colors[2][1], colors[2][2]));
            }
            ESP_ERROR_CHECK(strip->refresh(strip, NUM_LED));
            // ESP_ERROR_CHECK(led_strip_flush(&strip));
            // ESP_ERROR_CHECK(led_strip_fill(&strip, 0, strip.length, colors[2]));
            // ESP_ERROR_CHECK(led_strip_flush(&strip));
            vTaskDelay(pdMS_TO_TICKS(5000));
            led_control = ideal_led;
            break;
        case undocking:
            for (int i = 0; i < NUM_LED; i++)
            {
                ESP_ERROR_CHECK(strip->set_pixel(strip, i, colors[3][0], colors[3][1], colors[3][2]));
            }
            ESP_ERROR_CHECK(strip->refresh(strip, NUM_LED));
            // ESP_ERROR_CHECK(led_strip_flush(&strip));
            // ESP_ERROR_CHECK(led_strip_fill(&strip, 0, strip.length, colors[3]));
            // ESP_ERROR_CHECK(led_strip_flush(&strip));
            vTaskDelay(pdMS_TO_TICKS(5000));
            led_control = ideal_led;
            break;
        case communication:
            for (int i = 0; i < NUM_LED; i++)
            {
                ESP_ERROR_CHECK(strip->set_pixel(strip, i, colors[4][0], colors[4][1], colors[4][2]));
            }
            ESP_ERROR_CHECK(strip->refresh(strip, NUM_LED));
            // ESP_ERROR_CHECK(led_strip_flush(&strip));
            // ESP_ERROR_CHECK(led_strip_fill(&strip, 0, strip.length, colors[4]));
            // ESP_ERROR_CHECK(led_strip_flush(&strip));
            led_control = ideal_hold;
            break;
        case actuator_on_led:
            for (int i = 0; i < NUM_LED; i++)
            {
                ESP_ERROR_CHECK(strip->set_pixel(strip, i, colors[6][0], colors[6][1], colors[6][2]));
            }
            ESP_ERROR_CHECK(strip->refresh(strip, NUM_LED));
            ESP_LOGI(TAG, "Inside the actuator on LED");
            // ESP_ERROR_CHECK(led_strip_flush(&strip));
            // ESP_ERROR_CHECK(led_strip_fill(&strip, 0, strip.length, colors[6]));
            // ESP_ERROR_CHECK(led_strip_flush(&strip));
            led_control = ideal_hold;
            break;
        case ideal_hold:
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        // printf("time-r: %lld\n",esp_timer_get_time());
    }
}
void app_main(void)
{
    // led_strip_install();
    xTaskCreate(main_response, "Response to main board", 2048 * 2, NULL, 1, NULL);
    xTaskCreate(read_sensor, "Read Input parameters", 2048 * 2, NULL, 2, NULL);
    xTaskCreate(rgb_display, "led_display", configMINIMAL_STACK_SIZE * 5, NULL, 3, NULL);
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    actuatorOff();
}
