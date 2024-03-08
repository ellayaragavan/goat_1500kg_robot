#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_now.h"
#include "esp_system.h"
#include <cJSON.h>
#include "soc/rtc.h"
#include "esp_timer.h"
#include "driver/mcpwm.h"
#include "nvs_flash.h"
#include "driver/rmt.h"
#include <ultrasonic.h>
#include "ota_update.h"

#define RETURN_ZERO 0
#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_PMK CONFIG_ESPNOW_PMK

#define NO_OF_SENSORS CONFIG_NUMBER_OF_SENSORS
#define TRIGGER_THREAD_STACK_SIZE 512 * 4
#define READ_TASK_STACK_SIZE 1024 * 4
#define TRIGGER_SAMPLE_PERIOD_MS 100

#define length 2
#define MAX_DISTANCE_CM 200

_Static_assert(TRIGGER_SAMPLE_PERIOD_MS > 60, "Sample period too short!");
_Static_assert(NO_OF_SENSORS <= 8, "Only six sensor will be supported");

bool ota_flag = false;

int32_t USST[6] = {
    1,
    1,
    1,
    1,
    1,
    1,
};
int32_t USSE[6] = {};

uint32_t pulse_count[6] = {-1, -1, -1, -1, -1, -1};
static uint8_t broadcast_mac[6];

int32_t freq_count = 0;
bool print_once = false;

static uint32_t cap_val_begin_of_sample = 0;
static uint32_t cap_val_end_of_sample = 0;

static const char *TAG = "[ULTRA-M]";

cJSON *root;
cJSON *dataArray;
float distance[NO_OF_SENSORS];
int numbers[NO_OF_SENSORS];

int num9_11[length]; // sensor j9 and j 11 data
uint32_t distance9_11[length];

int32_t SENSOR_CONFIG[2] = {9, 11};
// CONFIG_SENSOR_1,
// CONFIG_SENSOR_2,
// CONFIG_SENSOR_3,
// CONFIG_SENSOR_4,
// CONFIG_SENSOR_5,
// CONFIG_SENSOR_6,
// CONFIG_SENSOR_9,
// CONFIG_SENSOR_11};

int32_t SENSOR_CONNECTOR[2] = {9, 11};

int32_t USST1[2] = {13, 11};
int32_t USSE1[2] = {14, 12};

float ultrasonic_read(uint32_t _pulsecount)
{
    uint32_t pulse_width_us = _pulsecount * (1000000.0 / rtc_clk_apb_freq_get());
    if (pulse_width_us > 35000)
    {
        return RETURN_ZERO;
    }
    else
    {
        freq_count++;
        return (float)pulse_width_us / 58.0;
    }
}

static bool mcpwm_isr_handler1(mcpwm_unit_t mcpwm, mcpwm_capture_channel_id_t cap_sig, const cap_event_data_t *edata, void *arg)
{
    BaseType_t high_task_wakeup = pdFALSE;
    if (edata->cap_edge == MCPWM_POS_EDGE)
    {
        cap_val_begin_of_sample = edata->cap_value;
        cap_val_end_of_sample = cap_val_begin_of_sample;
    }
    else
    {
        cap_val_end_of_sample = edata->cap_value;
        pulse_count[0] = cap_val_end_of_sample - cap_val_begin_of_sample;
    }
    return high_task_wakeup == pdTRUE;
}

static bool mcpwm_isr_handler2(mcpwm_unit_t mcpwm, mcpwm_capture_channel_id_t cap_sig, const cap_event_data_t *edata, void *arg)
{
    BaseType_t high_task_wakeup = pdFALSE;
    if (edata->cap_edge == MCPWM_POS_EDGE)
    {
        cap_val_begin_of_sample = edata->cap_value;
        cap_val_end_of_sample = cap_val_begin_of_sample;
    }
    else
    {
        cap_val_end_of_sample = edata->cap_value;
        pulse_count[1] = cap_val_end_of_sample - cap_val_begin_of_sample;
    }
    return high_task_wakeup == pdTRUE;
}

static bool mcpwm_isr_handler3(mcpwm_unit_t mcpwm, mcpwm_capture_channel_id_t cap_sig, const cap_event_data_t *edata, void *arg)
{
    BaseType_t high_task_wakeup = pdFALSE;
    if (edata->cap_edge == MCPWM_POS_EDGE)
    {
        cap_val_begin_of_sample = edata->cap_value;
        cap_val_end_of_sample = cap_val_begin_of_sample;
    }
    else
    {
        cap_val_end_of_sample = edata->cap_value;
        pulse_count[2] = cap_val_end_of_sample - cap_val_begin_of_sample;
    }
    return high_task_wakeup == pdTRUE;
}
static bool mcpwm_isr_handler4(mcpwm_unit_t mcpwm, mcpwm_capture_channel_id_t cap_sig, const cap_event_data_t *edata, void *arg)
{
    BaseType_t high_task_wakeup = pdFALSE;
    if (edata->cap_edge == MCPWM_POS_EDGE)
    {
        cap_val_begin_of_sample = edata->cap_value;
        cap_val_end_of_sample = cap_val_begin_of_sample;
    }
    else
    {
        cap_val_end_of_sample = edata->cap_value;
        pulse_count[3] = cap_val_end_of_sample - cap_val_begin_of_sample;
    }
    return high_task_wakeup == pdTRUE;
}
static bool mcpwm_isr_handler5(mcpwm_unit_t mcpwm, mcpwm_capture_channel_id_t cap_sig, const cap_event_data_t *edata, void *arg)
{
    BaseType_t high_task_wakeup = pdFALSE;
    if (edata->cap_edge == MCPWM_POS_EDGE)
    {
        cap_val_begin_of_sample = edata->cap_value;
        cap_val_end_of_sample = cap_val_begin_of_sample;
    }
    else
    {
        cap_val_end_of_sample = edata->cap_value;
        pulse_count[4] = cap_val_end_of_sample - cap_val_begin_of_sample;
    }
    return high_task_wakeup == pdTRUE;
}
static bool mcpwm_isr_handler6(mcpwm_unit_t mcpwm, mcpwm_capture_channel_id_t cap_sig, const cap_event_data_t *edata, void *arg)
{
    BaseType_t high_task_wakeup = pdFALSE;
    if (edata->cap_edge == MCPWM_POS_EDGE)
    {
        cap_val_begin_of_sample = edata->cap_value;
        cap_val_end_of_sample = cap_val_begin_of_sample;
    }
    else
    {
        cap_val_end_of_sample = edata->cap_value;
        pulse_count[5] = cap_val_end_of_sample - cap_val_begin_of_sample;
    }
    return high_task_wakeup == pdTRUE;
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
        // ESP_LOGE(TAG, "ESP_SEND_DATA_FAILED\n");
        print_once = false;
    }
    else if (status == ESP_NOW_SEND_SUCCESS && print_once == false)
    {
        ESP_LOGI(TAG, "ESP_SEND_DATA_SUCCESS\n");
        print_once = true;
    }
}

bool compare_mac(const uint8_t *recv_mac, const uint8_t *assigned_mac)
{
    uint32_t cnt = 0;
    for (int i = 0; i < 6; i++)
    {
        ESP_LOGI(TAG, "The mac is assigned %d", recv_mac[i]);
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

static void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    // for (int i = 0; i < 6; i++)
    // {
    //     printf("%02X", mac_addr[i]);
    //     if (i < 5)
    //         printf(":");
    // }
    if (compare_mac(mac_addr, broadcast_mac))
    {
        ESP_LOGI(TAG, "Incoming data:%s", data);
        cJSON *parse_data = cJSON_Parse((char *)data);
        if (cJSON_HasObjectItem(parse_data, "ota"))
        {
            ota_flag = true;
            ESP_LOGI(TAG,"...1");
            cJSON *send_data = cJSON_CreateObject();
            cJSON_AddBoolToObject(send_data, "ota", true);
            char *my_json_string = cJSON_PrintUnformatted(send_data);
            uint8_t data_send[strlen(my_json_string)];
            memcpy(data_send, my_json_string, strlen(my_json_string));
            vTaskDelay(1000/portTICK_PERIOD_MS);
            if (esp_now_send(broadcast_mac, data_send, strlen(my_json_string)) != ESP_OK)
            {
                ESP_LOGE(TAG, "SEND_ERROR_OVER_ESPNOW");
            }
            ota_update_init();
        }
        free(parse_data);
    }
    
}

static void trigger_task_handler(void *pvParameters)
{
    ESP_LOGI(TAG, "Trigger task Started");
    TickType_t xLastWakeTime = xTaskGetTickCount();
    while (true)
    {
        for (int16_t i = 0; i < NO_OF_SENSORS; i++)
        {
            vTaskDelayUntil(&xLastWakeTime, TRIGGER_SAMPLE_PERIOD_MS / portTICK_PERIOD_MS);
            gpio_set_level(USST[i], 1); // set high
            esp_rom_delay_us(10);
            gpio_set_level(USST[i], 0); // set low
        }
    }
}

static void read_task_handler(void *pvParameters)
{

    if (esp_now_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "ESP_INIT_ERROR");
    }
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
    ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)ESPNOW_PMK));
    uint32_t version;
    esp_now_get_version(&version);
    ESP_LOGI(TAG, "ESP_NOW_VERSION : %u", version);
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL)
    {
        printf("Malloc peer information fail");
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = 0;
    peer->ifidx = WIFI_IF_STA;
    peer->encrypt = false;
    memcpy(peer->peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(peer));
    free(peer);

    while (1)
    {
        for (int i = 0; i < NO_OF_SENSORS; i++)
        {
            distance[i] = ultrasonic_read(pulse_count[i]);
            numbers[i] = (int)distance[i];
        }
        numbers[6] = num9_11[0];
        numbers[7] = num9_11[1];
        // ESP_LOGW(TAG,"NUMBER6 %d AND NUMBER7 %d",numbers[6],numbers[7]);
        root = cJSON_CreateObject();
        dataArray = cJSON_CreateIntArray(numbers, NO_OF_SENSORS);
        cJSON_AddItemToObject(root, "u", dataArray);
        char *my_json_string = cJSON_PrintUnformatted(root);
        uint8_t data_send[strlen(my_json_string)];
        memcpy(data_send, my_json_string, strlen(my_json_string));
        if(ota_flag == false )
        {
        if (esp_now_send(broadcast_mac, data_send, strlen(my_json_string)) != ESP_OK)
        {
            // ESP_LOGE(TAG, "SEND_ERROR_OVER_ESPNOW");
        }
        }
        ESP_LOGI(TAG, "DATA :  %s ", my_json_string);
        cJSON_Delete(root);
        cJSON_free(my_json_string);
        vTaskDelay(1);
    }
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
static void wifi_initialization(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(ESPNOW_WIFI_MODE));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
    ESP_ERROR_CHECK(esp_wifi_start());
}
void Config_to_mac(void)
{
    uint8_t *MAC_data = hex_str_to_uint8(CONFIG_MASTER_MAC_ADDRESS);
    for (int i = 0; i < 6; i++)
    {
        broadcast_mac[i] = MAC_data[i];
    }
    ESP_LOGI(TAG, "MASTER_MAC_ADDRESS : %02X-%02X-%02X-%02X-%02X-%02X",
             broadcast_mac[0], broadcast_mac[1], broadcast_mac[2], broadcast_mac[3], broadcast_mac[4], broadcast_mac[5]);
}

void ultrasonic_test()
{
    ultrasonic_sensor_t sensor[length];

    for (uint16_t i = 0; i < length; i++)
    {
        for (uint16_t j = 0; j < 2; j++)
        {
            if (SENSOR_CONFIG[i] == SENSOR_CONNECTOR[j])
            {
                sensor[i].trigger_pin = USST1[j];
                sensor[i].echo_pin = USSE1[j];
                ultrasonic_init(&sensor[i]);
                ESP_LOGI(TAG, "TRG AND ECO : [%d , %d]", USST1[j], USSE1[j]);
            }
        }
    }
    while (true)
    {
       // root = cJSON_CreateObject();
        for (uint16_t j = 0; j < length; j++)
        {
            esp_err_t res = ultrasonic_measure_cm(&sensor[j], MAX_DISTANCE_CM, &distance9_11[j]);
            if (res != ESP_OK)
            {
                switch (res)
                {
                case ESP_ERR_ULTRASONIC_PING:
                    num9_11[j] = 0;
                    break;
                case ESP_ERR_ULTRASONIC_PING_TIMEOUT:
                    num9_11[j] = 0;
                    break;
                case ESP_ERR_ULTRASONIC_ECHO_TIMEOUT:
                    num9_11[j] = 0;
                    break;
                default:
                    printf("%s\n", esp_err_to_name(res));
                }
            }
            else
            {
                num9_11[j] = distance9_11[j];
            }
        }
        // printf("u6 %d and u7 %d \n ",num9_11[0],num9_11[1]);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
#ifdef CONFIG_J4_CONNECTOR_ENABLE
    USSE[CONFIG_J4_CONNECTOR_SENSOR_NO - 1] = 2;
#endif
#ifdef CONFIG_J10_CONNECTOR_ENABLE
    USSE[CONFIG_J10_CONNECTOR_SENSOR_NO - 1] = 40;
#endif
#ifdef CONFIG_J6_CONNECTOR_ENABLE
    USSE[CONFIG_J6_CONNECTOR_SENSOR_NO - 1] = 36;
#endif
#ifdef CONFIG_J5_CONNECTOR_ENABLE
    USSE[CONFIG_J5_CONNECTOR_SENSOR_NO - 1] = 42;
#endif
#ifdef CONFIG_J8_CONNECTOR_ENABLE
    USSE[CONFIG_J8_CONNECTOR_SENSOR_NO - 1] = 38;
#endif
#ifdef CONFIG_J7_CONNECTOR_ENABLE
    USSE[CONFIG_J7_CONNECTOR_SENSOR_NO - 1] = 48;
#endif
#ifdef CONFIG_J9_CONNECTOR_ENABLE
    USSE[CONFIG_J9_CONNECTOR_SENSOR_NO - 1] = 14;
#endif
#ifdef CONFIG_J11_CONNECTOR_ENABLE
    USSE[CONFIG_J11_CONNECTOR_SENSOR_NO - 1] = 12;
#endif

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    Config_to_mac();
    wifi_initialization();

    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << USST[0]);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    uint64_t bit_mask_echo = 0;
    for (int16_t i = 0; i < NO_OF_SENSORS; i++)
    {
        bit_mask_echo |= (1ULL << USSE[i]);
    }
    gpio_config_t out_conf;
    out_conf.intr_type = GPIO_INTR_POSEDGE;
    out_conf.mode = GPIO_MODE_INPUT;
    out_conf.pin_bit_mask = bit_mask_echo;
    out_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&out_conf);

    mcpwm_capture_config_t mcpwm_config0 = {
        .cap_edge = MCPWM_BOTH_EDGE,
        .cap_prescale = 1,
        .capture_cb = mcpwm_isr_handler1,
        .user_data = NULL};
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM_CAP_0, USSE[0]);
    mcpwm_capture_enable_channel(MCPWM_UNIT_0, MCPWM_SELECT_CAP0, &mcpwm_config0);

    mcpwm_capture_config_t mcpwm_config1 = {
        .cap_edge = MCPWM_BOTH_EDGE,
        .cap_prescale = 1,
        .capture_cb = mcpwm_isr_handler2,
        .user_data = NULL};
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM_CAP_1, USSE[1]);
    mcpwm_capture_enable_channel(MCPWM_UNIT_0, MCPWM_SELECT_CAP1, &mcpwm_config1);

    mcpwm_capture_config_t mcpwm_config2 = {
        .cap_edge = MCPWM_BOTH_EDGE,
        .cap_prescale = 1,
        .capture_cb = mcpwm_isr_handler3,
        .user_data = NULL};
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM_CAP_2, USSE[2]);
    mcpwm_capture_enable_channel(MCPWM_UNIT_0, MCPWM_SELECT_CAP2, &mcpwm_config2);

    mcpwm_capture_config_t mcpwm_config3 = {
        .cap_edge = MCPWM_BOTH_EDGE,
        .cap_prescale = 1,
        .capture_cb = mcpwm_isr_handler4,
        .user_data = NULL};
    mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM_CAP_0, USSE[3]);
    mcpwm_capture_enable_channel(MCPWM_UNIT_1, MCPWM_SELECT_CAP0, &mcpwm_config3);

    mcpwm_capture_config_t mcpwm_config4 = {
        .cap_edge = MCPWM_BOTH_EDGE,
        .cap_prescale = 1,
        .capture_cb = mcpwm_isr_handler5,
        .user_data = NULL};
    mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM_CAP_1, USSE[4]);
    mcpwm_capture_enable_channel(MCPWM_UNIT_1, MCPWM_SELECT_CAP1, &mcpwm_config4);

    mcpwm_capture_config_t mcpwm_config5 = {
        .cap_edge = MCPWM_BOTH_EDGE,
        .cap_prescale = 1,
        .capture_cb = mcpwm_isr_handler6,
        .user_data = NULL};
    mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM_CAP_2, USSE[5]);
    mcpwm_capture_enable_channel(MCPWM_UNIT_1, MCPWM_SELECT_CAP2, &mcpwm_config5);

    xTaskCreate(trigger_task_handler, "trigger ultrasonic sensor", TRIGGER_THREAD_STACK_SIZE, NULL, 2, NULL);
    xTaskCreate(read_task_handler, "read ultrasonic sensor", READ_TASK_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(ultrasonic_test, "ultrasonic_test", READ_TASK_STACK_SIZE , NULL, 1, NULL);
}