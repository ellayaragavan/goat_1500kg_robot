
/**
 * ----------------------------APPLICATION-MAIN-BOARD-FILE-INFO-----------------------------
 * @file    main.c
 * @brief   This main code is developed for goat 350 kg robot main application board . It utilizes the FreeRTOS Kernel for task handling
 *          and scheduling functionality. It is a request and response system where ROS (Robot Operating System) will request and give response.The RF Remote Switch Emergency and handshake
 *          will handled in the code
 * @author  <ellayaragavan@katomaran.com>
 * @date    2024
 * @version 1.0.1
 * @section Dependencies
 *          - ESP-IDF (Espressif IOT Development Framework)
 * @note    - Developed with ESP-IDF v4.4.6 version
 *
 * @Legal Notice:
 *          - This header file is intended solely for use in indoor air quality devices developed by Organization. Unauthorized distribution,
 *            modification, or use of this file outside the scope of Organization is strictly prohibited.
 *
 * @Usage Restrictions:
 *          - This main file is proprietary to Organization and may only be used within the organization.
 *          - Any external use or distribution of this file requires prior legal approval from  Organization.
 */

/*------------NewLib--------------*/
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
/*------------FreeRTOS-------------*/
#include <freertos/FreeRTOS.h>
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include <freertos/task.h>
/*------------Drivers--------------*/
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/adc.h"
/*---------------EspLib------------*/
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include <esp_err.h>
#include "esp_system.h"
#include "esp_crc.h"
#include "esp_now.h"
#include "espnow_example.h"
/*----sdk config variables--------*/
#include "sdkconfig.h"
/*------Cjson library-------------*/
#include <cJSON.h>
/*--------OTA Library-------------*/
#include <ota_update.h>

/*-------PMK Key definition--------*/
#define CONFIG_ESPNOW_PMK "pmk1234567890123"

/*-------General Definition--------*/
#define DEFAULT_VREF 1100
#define NO_OF_SAMPLES 60
#define IN_MAX 2267
#define IN_MIN 2241
#define OUT_MIN 0
#define OUT_MAX 0.328
#define CURRENT_AMPS 1.0
#define BUF_SIZE (4096)
#define DOCK_READ 500

/*-------UART Definition-------------*/
#define ECHO_UART_PORT_NUM 1
#define UART_TX_PIN (21)
#define UART_RX_PIN (47)
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)
#define ECHO_UART_BAUD_RATE (115200)
#define ECHO_TASK_STACK_SIZE (CONFIG_EXAMPLE_TASK_STACK_SIZE)

/*--------GPIO-Pin-Declaration-------*/
#define EMG_RELAY_OUT 36
#define DOCKING_RELAY 41
// #define RF_INPUT 37
#define BUZZER_PIN 40
#define MOTOR_RELAY 8
#define SWITCH_INPUT 38
#define EXECUTE_SWITCH_INPUT 39
#define MAIN_SWITCH_RELAY 18
#define SAFETY_SIGNAL_RELAY 42
#define POWER_BUTTON_TIMER 3000000 // 5sec

/*-----------GPIO-Bit-Masking---------*/

#define GPIO_BIT_MASK ((1ULL << EMG_RELAY_OUT) | (1ULL << DOCKING_RELAY) | (1ULL << BUZZER_PIN) | (1ULL << MOTOR_RELAY) | (1ULL << MAIN_SWITCH_RELAY)) 
#define RF_BIT_MASK (1ULL << SWITCH_INPUT) | (1ULL << EXECUTE_SWITCH_INPUT) |(1ULL << SAFETY_SIGNAL_RELAY )

/*-----------RGB Board Number Definition---------*/
#define UPPER_RGB 2
#define LOWER_RGB 1
#define INDICATOR 5
#define EXECUTE_SWITCH_LIMIT (5)
#define SAFETY_SWITCH_LIMIT (5)

/*-----------ADC characterstics and ADC Channel definition---------*/
static esp_adc_cal_characteristics_t *adc_chars;
static const adc_channel_t current_channel = ADC_CHANNEL_3;
static const adc_channel_t voltage_channel = ADC_CHANNEL_1;
static const adc_channel_t docking_channel = ADC_CHANNEL_4;
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
static const adc_atten_t atten = ADC_ATTEN_DB_11;
static const adc_unit_t unit = ADC_UNIT_1;

/*----------Gloabal variables--------------------*/
uint32_t current_reading = 0;
uint32_t voltage_reading = 0;
uint32_t docking_reading = 0;
uint32_t get_dockvoltage = 0;
int32_t ota_data;

// bool charging_status = false;
uint8_t charging_status;
bool emg_state = false, handshake_state, power_state = false, buzzer_value, motor_value;
char *dock_state = NULL;
char *recv_state = NULL;
uint32_t board_no = 0;
bool excute_status = false, power_bnt_ctrl = false;
bool write_key = false;
bool execute_state = false;

bool soft_emg_status = false;
bool power_switch_status = false;
bool safety_switch_status = false;

bool once_flg = false;
bool docking_connection = false;
bool now_flg = false;
static const char *TAG = "ESP_MAIN";

bool handshake_relay_status = false, rf_relay_status = false;
int64_t health_check_start_timer = 0, health_check_end_timer = 5000000;
bool handshake_check_status = true;

int64_t power_timer, power_start_timer, power_end_timer = 20000000;
int64_t nuc_power_timer, nuc_power_start_timer, nuc_power_end_timer = 20000000;
int64_t handshake_timer, handshake_end_timer = 5000000;
bool nuc_timer_key = false;

/* ESP Peer to Peer Mac address*/
uint8_t APPLICATION_MAC[ESP_NOW_ETH_ALEN] = {0xf4, 0x12, 0xfa, 0xc2, 0x9b, 0x21};
uint8_t DOCKING_MAC[ESP_NOW_ETH_ALEN] = {0x68, 0xb6, 0xb3, 0x47, 0xdc, 0xe8};
uint8_t RGB_MAC_DOWN[ESP_NOW_ETH_ALEN] = {0xf4, 0x12, 0xfa, 0xc3, 0x50, 0x90};
uint8_t RGB_MAC_UP[ESP_NOW_ETH_ALEN] = {0xf4, 0x12, 0xfa, 0xc2, 0x9b, 0x1c};
//uint8_t main_board_mac[ESP_NOW_ETH_ALEN] = {0xf4, 0x12, 0xfa, 0xc2, 0x91, 0x54};
/* structure contains rf input read and input count*/
typedef struct read_param
{
    bool input_read;
    uint32_t input_count;
} IO_READ;

IO_READ app,safety_signal,excute_switch_status;

/* Enum contains a values of the UART and ESPrecv */
enum key_value
{
    ideal,
    status,
    data_fetch,
    soft_emg,
    docking,
    relay_on,
    relay_off,
    success,
    fail,
    undock,
    RGB_Board,
    pre_dock,
    ota,
    handshake,
    buzzer,
    motor_shutdown,
    power_fullshutdown,
   // execute_reset_switch
} key_string;

/**
 * @brief   This function is used to initialize the wifi and setting a wifi mode.
 */

static void wifi_init(void)
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

/**
 * @brief   This function is used to identify the key and return the code.
 *
 * @return  It will return an code in a enum.
 */

uint32_t key_identify(cJSON *rev_string)
{
    if (cJSON_HasObjectItem(rev_string, "status"))
    {
        return 1;
    }
    else if (cJSON_HasObjectItem(rev_string, "data_fetch"))
    {
        return 2;
    }
    else if (cJSON_HasObjectItem(rev_string, "soft_emg"))
    {
        return 3;
    }
    else if (cJSON_HasObjectItem(rev_string, "docking"))
    {
        return 4;
    }
    else if (cJSON_HasObjectItem(rev_string, "relay_on"))
    {
        return 5;
    }
    else if (cJSON_HasObjectItem(rev_string, "relay_off"))
    {
        return 6;
    }
    else if (cJSON_HasObjectItem(rev_string, "success"))
    {
        return 7;
    }
    else if (cJSON_HasObjectItem(rev_string, "fail"))
    {
        return 8;
    }
    else if (cJSON_HasObjectItem(rev_string, "undock"))
    {
        return 9;
    }
    else if (cJSON_HasObjectItem(rev_string, "board_no"))
    {
        return 10;
    }
    else if (cJSON_HasObjectItem(rev_string, "ota"))
    {
        return 12;
    }
    else if (cJSON_HasObjectItem(rev_string, "handshake"))
    {
        return 13;
    }
    else if (cJSON_HasObjectItem(rev_string, "buzzer"))
    {
        return 14;
    }
    else if (cJSON_HasObjectItem(rev_string, "motor_shutdown"))
    {
        return 15;
    }
    else if (cJSON_HasObjectItem(rev_string, "power_fullshutdown"))
    {
        return 16;
    }
     else if (cJSON_HasObjectItem(rev_string, "execute_reset_switch"))
    {
        return 17;
    }
    else
    {
        return 0;
    }
}

/**
 * @brief   This function is used to compare the mac. If two mac are return true or else return False
 *
 * @return  It will return an bool (True or False).
 */

bool compare_mac(const uint8_t *recv_mac, const uint8_t *assigned_mac)
{
    uint32_t cnt = 0;
    for (int i = 0; i < 6; i++)
    {
        if (recv_mac[i] == assigned_mac[i])
        {
            cnt++;
        }
        printf("%02X:", recv_mac[i]);
    }
    printf("\n%d\n", cnt);
    if (cnt == 6)
    {
        return true;
    }
    else
    {
        return false;
    }
}

/**
 * @brief     This function will send a JSON data through a UART
 *
 * @param[in]  data pass a CJSON Data that to be transfered via UART.
 */

void uart_send(cJSON *data)
{
    write_key = true;
    char *my_json_string = cJSON_PrintUnformatted(data);
    ESP_LOGI(TAG, "The UART send data is %s", my_json_string);
    uart_write_bytes(ECHO_UART_PORT_NUM, my_json_string, strlen(my_json_string));
    uart_write_bytes(ECHO_UART_PORT_NUM, "\r\n", 2);
    printf("successfully transmit the data");
    cJSON_Delete(data);
    free(my_json_string);
    write_key = false;
}

/**
 * @brief     This function will recieve a callback when a send a message via esp_now_send
 *
 * @param[in]  mac_addr Mac address of the send message
 * @param[in]  status status of the message (Message will give response whether the message is send to slave or not)
 */

static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (mac_addr == NULL)
    {
        ESP_LOGE(TAG, "SEND_CB_ERROR");
        return;
    }
    if (status != ESP_NOW_SEND_SUCCESS)
    {
        once_flg = false;
        ESP_LOGE(TAG, "ESP_SEND_DATA_FAILED\n");
        cJSON *root3 = cJSON_CreateObject();
        if (compare_mac(mac_addr, DOCKING_MAC) == true)
        {
            cJSON_AddStringToObject(root3, "docking", "false");
            cJSON_AddStringToObject(root3, "reason", "docker_dead");
            key_string = ideal;
            uart_send(root3);
        }
        if (compare_mac(mac_addr, RGB_MAC_DOWN) == true)
        {
            cJSON_AddStringToObject(root3, "Data_send", "false");
            cJSON_AddStringToObject(root3, "Board", "rgb_down");
            key_string = ideal;
            uart_send(root3);
        }
        if (compare_mac(mac_addr, RGB_MAC_UP) == true)
        {
            cJSON_AddStringToObject(root3, "Data_send", "false");
            cJSON_AddStringToObject(root3, "Board", "rgb_up");
            key_string = ideal;
            uart_send(root3);
        }
    }
    else if (status == ESP_NOW_SEND_SUCCESS && once_flg == false)
    {
        ESP_LOGI(TAG, "ESP_SEND_DATA_SUCCESS\n");
        once_flg = true;
    }
}

/**
 * @brief     This function will recieve a callback when a recieve a message from the slave
 *
 * @param[in]  mac_addr Mac address of the send message
 * @param[in]  data message from the slave
 * @param[in]  len length of the data recieved
 */

static void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    ESP_LOGI(TAG, "The message from the docking board %s\n", data);
    cJSON *root_cb = cJSON_Parse((char *)data);
    cJSON *current_element = NULL;
    const char *string_json = NULL;
    now_flg = true;
    cJSON_ArrayForEach(current_element, root_cb)
    {
        if (current_element->string)
        {
            string_json = current_element->string;
            break;
        }
    }
    if (compare_mac(mac_addr, DOCKING_MAC) == true)
    {
        if (strcmp(string_json, "docker") == 0)
        {
            recv_state = cJSON_GetObjectItem(root_cb, "docker")->valuestring;
            ESP_LOGI(TAG, "The docker string is %s", recv_state);
            key_string = key_identify(root_cb);
            ESP_LOGI(TAG, "The return Key string for relay_on is %d", key_string);
        }
        else
        {
            uart_write_bytes(ECHO_UART_PORT_NUM, data, len);
            uart_write_bytes(ECHO_UART_PORT_NUM, "\n", 1);
        }
    }
    if (compare_mac(mac_addr, RGB_MAC_DOWN) == true)
    {
        uart_write_bytes(ECHO_UART_PORT_NUM, data, len);
        uart_write_bytes(ECHO_UART_PORT_NUM, "\n", 1);
    }
    cJSON_Delete(root_cb);
}

/**
 * @brief     This function will map the in min and in max and out min and out max.
 * @param[in]  x data to be mapped
 * @param[in]  in_min min value
 * @param[in]  in_max max value
 * @param[in]  out_min min value
 * @param[in]  out_max max value
 */
float map(long x, float in_min, float in_max, float out_min, float out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/**
 * @brief     This function will read ADC Channel for current sensor, voltage channel and docking channel.
 */

void adc_read_parameters()
{
    adc1_config_channel_atten((adc1_channel_t)current_channel, atten);
    adc1_config_channel_atten((adc1_channel_t)voltage_channel, atten);
    adc1_config_channel_atten((adc1_channel_t)docking_channel, atten);
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(unit, atten, width, DEFAULT_VREF, adc_chars);
    while (true)
    {
        uint32_t current_sample = 0;
        uint32_t voltage_sample = 0;
        uint32_t docking_sample = 0;
        for (int i = 0; i < NO_OF_SAMPLES; i++)
        {
            current_sample += adc1_get_raw((adc1_channel_t)current_channel);
            voltage_sample += adc1_get_raw((adc1_channel_t)voltage_channel);
            docking_sample += adc1_get_raw((adc1_channel_t)docking_channel);
        }
        current_reading = current_sample / NO_OF_SAMPLES;
        voltage_reading = voltage_sample / NO_OF_SAMPLES;
        docking_reading = docking_sample / NO_OF_SAMPLES;
       // printf("ADC VOLTAGE : %d\n", voltage_reading);
        //printf("ADC CURRENT : %d\n", current_reading);
        // printf("current : %f\n",map(current_reading,IN_MIN,IN_MAX,OUT_MIN,OUT_MAX));
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

/**
 * @brief     This function will add a peer address for a mac address..
 * @param[in]  peer_address address added for esp-now communication.
 */

void add_peer_address(uint8_t *peer_address)
{
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL)
    {
        printf("Malloc peer information fail");
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = 1;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, peer_address, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(peer));
    free(peer);
}

/**
 * @brief     This function will send a message to the docking board.
 * @param[in]  data cJSON Data to be send to the docking board .
 */

void send_docking(cJSON *data)
{
    printf("Inside the send docking function");
    char *my_json_string = cJSON_PrintUnformatted(data);
    uint8_t data_send[strlen(my_json_string)];
    printf("%s\n", my_json_string);
    memcpy(data_send, my_json_string, strlen(my_json_string));
    if (esp_now_send(DOCKING_MAC, data_send, strlen(my_json_string)) != ESP_OK)
    {
        ESP_LOGE(TAG, "SEND_ERROR\n");
    }
    cJSON_free(my_json_string);
    cJSON_Delete(data);
}

/**
 * @brief     This function will send a message to the RGB board Down.
 * @param[in]  data_rgb cJSON Data to be send to the RGB board Down.
 */

void send_RGB_DOWN(cJSON *data_rgb)
{
    char *my_json_string_rgb = cJSON_PrintUnformatted(data_rgb);
    uint8_t data_send_rgb[strlen(my_json_string_rgb)];
    printf("%s\n", my_json_string_rgb);
    memcpy(data_send_rgb, my_json_string_rgb, strlen(my_json_string_rgb));
    if (esp_now_send(RGB_MAC_DOWN, data_send_rgb, strlen(my_json_string_rgb)) != ESP_OK)
    {
        ESP_LOGE(TAG, "SEND_ERROR\n");
    }
    cJSON_free(my_json_string_rgb);
    cJSON_Delete(data_rgb);
}

/**
 * @brief     This function will send a message to the RGB board Up.
 * @param[in]  data_RGB_PIR cJSON Data to be send to the RGB board Up.
 */

void send_RGB_UP(cJSON *data_RGB_PIR)
{
    char *my_json_string_rgb = cJSON_PrintUnformatted(data_RGB_PIR);
    uint8_t data_send_rgb[strlen(my_json_string_rgb)];
    // printf("%s\n",my_json_string_rgb);
    memcpy(data_send_rgb, my_json_string_rgb, strlen(my_json_string_rgb));
    if (esp_now_send(RGB_MAC_UP, data_send_rgb, strlen(my_json_string_rgb)) != ESP_OK)
    {
        ESP_LOGE(TAG, "SEND_ERROR\n");
    }
    cJSON_free(my_json_string_rgb);
    cJSON_Delete(data_RGB_PIR);
}

/**
 * @brief     This function will Intialize UART and Request Response from the ROS.
 */

void json_control()
{
    uart_config_t uart_config = {
        .baud_rate = ECHO_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = GPIO_BIT_MASK;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = RF_BIT_MASK;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    gpio_set_level(DOCKING_RELAY, 0);
    gpio_set_level(MOTOR_RELAY,0);
    gpio_set_level(BUZZER_PIN,0);
    int intr_alloc_flags = 0;
    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, ECHO_TEST_RTS, ECHO_TEST_CTS));
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    //esp_base_mac_addr_set(main_board_mac);
    wifi_init();
    if (esp_now_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "ESP_INIT_ERROR\n");
    }
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
    ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK));
    uint16_t timeout = 3000;
    ESP_ERROR_CHECK(esp_now_set_wake_window(&timeout));
    uint32_t version;
    esp_now_get_version(&version);
    ESP_LOGI(TAG, "ESP_NOW_VERSION : %u", version);
    add_peer_address(DOCKING_MAC);
    add_peer_address(RGB_MAC_UP);
    add_peer_address(RGB_MAC_DOWN);
    esp_now_peer_num_t *num = malloc(sizeof(esp_now_peer_num_t));
    esp_now_get_peer_num(num);
    ESP_LOGI(TAG, "NUMBER OF PEER : %d", num->total_num);
    free(num);
    key_string = 0;
    cJSON *init_dock = cJSON_CreateObject();
    cJSON_AddStringToObject(init_dock, "docking", "restart");
    send_docking(init_dock);
    // gpio_set_level(MOTOR_RELAY, 1);
    while (true)
    {
        int length = 0;
        if (write_key == false)
        {
            ESP_ERROR_CHECK(uart_get_buffered_data_len(ECHO_UART_PORT_NUM, (size_t *)&length));
        }
        uint8_t *data = (uint8_t *)malloc(BUF_SIZE);
        // ESP_LOGI(TAG, "data size /: %d", length);
        if (length > 0 || now_flg == true)
        {
            int len = uart_read_bytes(ECHO_UART_PORT_NUM, data, BUF_SIZE, 10 / portTICK_RATE_MS);
            cJSON *root = cJSON_CreateObject();
            cJSON *root2 = cJSON_CreateObject();
            if (len > 0)
            {
                root2 = cJSON_Parse((char *)data);
                key_string = key_identify(root2);
                ESP_LOGI(TAG, "The inside data is %d", key_string);
                health_check_start_timer = esp_timer_get_time();
                handshake_timer = esp_timer_get_time();
            }
            switch(key_string)
            {
            case status:
                cJSON_AddBoolToObject(root, "status", true);
                ESP_LOGI(TAG, "UART Send data");
                uart_send(root);
                cJSON_Delete(root2);
                key_string = ideal;
                break;
            case data_fetch:
                cJSON_AddNumberToObject(root,"battery",voltage_reading);
                cJSON_AddNumberToObject(root,"charging",current_reading);
                cJSON_AddBoolToObject(root,"power_switch_status",power_switch_status);
                cJSON_AddBoolToObject(root,"safety_relay_status",safety_switch_status);
                cJSON_AddBoolToObject(root,"app_switch2",excute_status);
                uart_send(root);
                cJSON_Delete(root2);
                key_string = ideal;
                break;
            case soft_emg:
                emg_state = cJSON_GetObjectItem(root2, "soft_emg")->valueint;
                if (emg_state == 1)
                {
                    ESP_LOGI(TAG, "relay on");
                    soft_emg_status = true;
                    cJSON_AddBoolToObject(root, "soft_emg", true);
                    uart_send(root);
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                    gpio_set_level(EMG_RELAY_OUT, 1);
                }
                if (emg_state == 0)
                {
                    gpio_set_level(EMG_RELAY_OUT, 0);
                    soft_emg_status = false;
                    cJSON_AddBoolToObject(root, "soft_emg", true);
                    uart_send(root);
                }
                key_string = ideal;
                cJSON_Delete(root2);
                break;
            case docking:
                dock_state = cJSON_GetObjectItem(root2, "docking")->valuestring;
                if (strcmp(dock_state, "Alive?") == 0)
                {
                    cJSON_AddStringToObject(root, "docking", "Alive?");
                    send_docking(root);
                    key_string = ideal;
                    break;
                }
                else if (strcmp(dock_state, "dockmode_on") == 0)
                {
                    cJSON_AddStringToObject(root, "docking", "dockmode_on");
                    send_docking(root);
                    key_string = ideal;
                    break;
                }
                else if (strcmp(dock_state, "dockmode_off") == 0)
                {
                    cJSON_AddStringToObject(root, "docking", "dockmode_off");
                    send_docking(root);
                    key_string = ideal;
                    break;
                }
                else if (strcmp(dock_state, "dock") == 0)
                {
                    cJSON_AddBoolToObject(root, "docking", true);
                    uart_send(root);
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                    // cJSON_AddBoolToObject(root,"docking",true);
                    // uart_send(root);
                    send_docking(root2);
                    key_string = pre_dock;
                    break;
                }
                else if (strcmp(dock_state, "undock") == 0)
                {
                    cJSON *root_dock = cJSON_CreateObject();
                    cJSON_AddBoolToObject(root_dock, "undocking", true);
                    uart_send(root_dock);
                    cJSON_AddStringToObject(root, "docking", "undock");
                    send_docking(root);
                    gpio_set_level(DOCKING_RELAY, 0);
                    key_string = ideal;
                    break;
                }
                else if (strcmp(dock_state, "f_undock") == 0)
                {
                    cJSON_AddBoolToObject(root, "undocking", true);
                    uart_send(root);
                    gpio_set_level(DOCKING_RELAY, 0);
                    key_string = ideal;
                    break;
                }
                else if (strcmp(dock_state, "actuator_on") == 0)
                {
                    ESP_LOGE(TAG, "Error in hello");
                    cJSON_AddBoolToObject(root, "actuator_on", true);
                    uart_send(root);
                    printf("After UART tcvhucavhjhgcsag");
                    send_docking(root2);
                    key_string = ideal;
                    break;
                }
                else if (strcmp(dock_state, "actuator_off") == 0)
                {
                    ESP_LOGE(TAG, "Error in hello");
                    cJSON_AddBoolToObject(root, "actuator_off", true);
                    uart_send(root);
                    printf("After UART tcvhucavhjhgcsag");
                    send_docking(root2);
                    key_string = ideal;
                    break;
                }
                else if (strcmp(dock_state, "docker_relayon") == 0)
                {
                    ESP_LOGE(TAG, "docking relay on");
                    cJSON_AddBoolToObject(root, "docker_relayon", true);
                    gpio_set_level(DOCKING_RELAY, 1);
                    uart_send(root);
                    send_docking(root2);
                    key_string = ideal;
                    break;
                }
                else if (strcmp(dock_state, "docker_relayoff") == 0)
                {
                    ESP_LOGE(TAG, "docking relay off");
                    cJSON_AddBoolToObject(root, "docker_relayoff", true);
                    gpio_set_level(DOCKING_RELAY, 0);
                    uart_send(root);
                    send_docking(root2);
                    key_string = ideal;
                    break;
                }
                else
                {
                    key_string = ideal;
                    break;
                }
                break;
            case pre_dock:
                ESP_LOGE(TAG, "Inside the predocking state");
                cJSON_AddStringToObject(root, "docking", "predocking");
                send_docking(root);
                // cJSON_Delete(root2);
                key_string = ideal;
                break;
            case relay_on:
                ESP_LOGI(TAG, "Inside the Realy on function");
                vTaskDelay(pdMS_TO_TICKS(100));
                get_dockvoltage = docking_reading;
                ESP_LOGI(TAG, "DOCK_VOLTAGE : [%d]", get_dockvoltage);
                // cJSON_Delete(root2);
                key_string = ideal;
                break;
            case relay_off:
                if (get_dockvoltage >= DOCK_READ)
                {

                    // ESP_LOGI(TAG,"DOCKING_VOLTAGE : [%d]",docking_reading);
                    cJSON_AddStringToObject(root, "docking", "success");
                    send_docking(root);
                    key_string = ideal;
                    break;
                }
                else
                {
                    // ESP_LOGE(TAG,"ERROR_DOCKING_VOLTAGE : [%d]",docking_reading);
                    cJSON_AddStringToObject(root, "docking", "fail");
                    cJSON_AddStringToObject(root, "reason", "robot");
                    key_string = ideal;
                    uart_send(root);
                    cJSON *root_dock_send = cJSON_CreateObject();
                    cJSON_AddStringToObject(root_dock_send, "docking", "fail");
                    cJSON_AddStringToObject(root_dock_send, "reason", "robot");
                    send_docking(root_dock_send);
                    break;
                }
                cJSON_Delete(root2);
                break;
            case success:
                gpio_set_level(DOCKING_RELAY, 1);
                cJSON_AddStringToObject(root, "docking", "success");
                uart_send(root);
                cJSON_Delete(root2);
                key_string = ideal;
                break;
            case fail:
                gpio_set_level(DOCKING_RELAY, 0);
                cJSON_Delete(root2);
                key_string = ideal;
                break;
            case undock:
                cJSON_AddStringToObject(root, "undocking", "success");
                uart_send(root);
                cJSON_Delete(root2);
                key_string = ideal;
                break;
            case RGB_Board:
                board_no = cJSON_GetObjectItem(root2, "board_no")->valueint;
                if (board_no == LOWER_RGB)
                {
                    ESP_LOGI(TAG, "RECIVED_DOWN_RGB_DATA_OVER_SERIAL");
                    send_RGB_DOWN(root2);
                    key_string = ideal;
                    break;
                }
                if (board_no == UPPER_RGB)
                {
                    // ESP_LOGI(TAG,"RECIVED_UPPER_RGB_DATA_OVER_SERIAL");
                    send_RGB_UP(root2);
                    key_string = ideal;
                    break;
                }
                if (board_no == INDICATOR)
                {

                    ESP_LOGI(TAG, "The Indicator string");
                    send_RGB_DOWN(root2);
                    key_string = ideal;
                    break;
                }
                break;
            case ota:
                ota_data = cJSON_GetObjectItem(root2, "ota")->valueint;
                if (ota_data == 4)
                {
                    cJSON *ota_response = cJSON_CreateObject();
                    cJSON_AddBoolToObject(ota_response, "ota", true);
                    uart_send(ota_response);
                    ota_update_init();
                }
                else if (ota_data == 5)
                {
                    send_docking(root2);
                }
                else if (ota_data == 6)
                {
                    //   send_Relay_board(root2);
                }
                else if (ota_data == 7)
                {
                    send_RGB_DOWN(root2);
                }
                else if (ota_data == 8)
                {
                    send_RGB_UP(root2);
                }
                key_string = ideal;
                break;

            case handshake:
                handshake_state = cJSON_GetObjectItem(root2, "handshake")->valueint;
                if (handshake_state == 1)
                {
                    handshake_check_status = true;
                }
                else if (handshake_state == 0)
                {
                    gpio_set_level(EMG_RELAY_OUT, 0);
                    handshake_check_status = false;
                }
                cJSON *handshake_response = cJSON_CreateObject();
                cJSON_AddBoolToObject(handshake_response, "handshake", true);
                uart_send(handshake_response);
                key_string = ideal;
                break;

            case buzzer:
                buzzer_value = cJSON_GetObjectItem(root2, "buzzer")->valueint;
                if (buzzer_value == 1)
                {
                    gpio_set_level(BUZZER_PIN, 1);
                    cJSON *buzzer_status = cJSON_CreateObject();
                    cJSON_AddBoolToObject(buzzer_status, "buzzer", true);
                    uart_send(buzzer_status);
                    key_string = ideal;
                }
                else
                {
                    if (buzzer_value == 0)
                        gpio_set_level(BUZZER_PIN, 0);
                    cJSON *buzzer_status = cJSON_CreateObject();
                    cJSON_AddBoolToObject(buzzer_status, "buzzer", true);
                    uart_send(buzzer_status);
                    key_string = ideal;
                    
                }
                break;
            case motor_shutdown:
                motor_value = cJSON_GetObjectItem(root2, "motor_shutdown")->valueint;
                if (motor_value == 1)
                {
                    gpio_set_level(MOTOR_RELAY, 0);
                    cJSON *motor_status = cJSON_CreateObject();
                    cJSON_AddBoolToObject(motor_status, "motor_shutdown", true);
                    uart_send(motor_status);
                    key_string = ideal;
                }
                else
                {
                    if (motor_value == 0)
                        gpio_set_level(MOTOR_RELAY, 1);
                    cJSON *motor_status = cJSON_CreateObject();
                    cJSON_AddBoolToObject(motor_status, "motor_shutdown", true);
                    uart_send(motor_status);
                    key_string = ideal;
                }
                break;
            case power_fullshutdown:
                power_state = cJSON_GetObjectItem(root2, "power_fullshutdown")->valueint;
                if (power_state == 1)
                {
                    ESP_LOGI(TAG, "relay on");
                    soft_emg_status = true;
                    cJSON_AddBoolToObject(root, "power_fullshutdown", true);
                    uart_send(root);
                    vTaskDelay(50 / portTICK_PERIOD_MS);
                    nuc_timer_key = true;
                    nuc_power_start_timer = esp_timer_get_time();
                    gpio_set_level(EMG_RELAY_OUT, 1);
                }
                if (power_state == 0)
                {
                    soft_emg_status = false;
                    nuc_timer_key = false;
                    cJSON_AddBoolToObject(root, "power_fullshutdown", true);
                    uart_send(root);
                }
                key_string = ideal;
                cJSON_Delete(root2);
                break;

            // case execute_reset_switch:
            //      execute_state = cJSON_GetObjectItem(root2, "execute_reset_switch")->valueint;
            //      if(execute_state == false)
            //      {
            //         excute_status = false;
            //         cJSON *execute_value_status = cJSON_CreateObject();
            //         cJSON_AddBoolToObject(execute_value_status, "execute_reset_switch", true);
            //         uart_send(execute_value_status);
            //         key_string = ideal;
            //      }
            //      break;
            case ideal:
                cJSON_Delete(root2);
                cJSON_Delete(root);
                break;
            }
        }
        now_flg = false;
        vTaskDelay(pdMS_TO_TICKS(50));
        free(data);
    }
}

void read_power_switch()
{
    while (true)
    {
        if (gpio_get_level(SWITCH_INPUT) == 0)
        {
            if (esp_timer_get_time() - power_timer >= POWER_BUTTON_TIMER && power_bnt_ctrl)
            {
                power_timer = esp_timer_get_time();
                power_bnt_ctrl = false;
                ESP_LOGI(TAG, "Power button pressed:");
                power_switch_status = true;
                power_start_timer = esp_timer_get_time();
                cJSON *RGB_board_send = cJSON_CreateObject();
                cJSON_AddNumberToObject(RGB_board_send, "board_no", 1);
                cJSON_AddNumberToObject(RGB_board_send, "port_no", 3);
                int rgb_colour1[3] = {255, 0, 0};
                cJSON *intArray = cJSON_CreateIntArray(rgb_colour1, 3);
                cJSON_AddItemToObject(RGB_board_send, "colour1", intArray);
                int rgb_colour2[3] = {0, 0, 0};
                cJSON *intArray1 = cJSON_CreateIntArray(rgb_colour2, 3);
                cJSON_AddItemToObject(RGB_board_send, "colour2", intArray1);
                cJSON_AddNumberToObject(RGB_board_send, "no_of_led", 100);
                cJSON_AddNumberToObject(RGB_board_send, "mode_no", 1);
                cJSON_AddNumberToObject(RGB_board_send, "speed", 300);
                send_RGB_DOWN(RGB_board_send);
                //  cJSON_Delete(RGB_board_send);
            }
        }
        else
        {
            power_timer = esp_timer_get_time();
            power_bnt_ctrl = true;
        }

        if (power_switch_status == true)
        {
            if (esp_timer_get_time() - power_start_timer >= power_end_timer)
            {
                ESP_LOGI(TAG, "time ended after 20 seconds");
                gpio_set_level(MAIN_SWITCH_RELAY, 1);
            }
        }

        if (nuc_timer_key == true)
        {
            if ((esp_timer_get_time() - nuc_power_start_timer >= nuc_power_end_timer) | (esp_timer_get_time() - handshake_timer >= handshake_end_timer))
            {
                ESP_LOGI(TAG, "NUC Timer ended before");
                gpio_set_level(MAIN_SWITCH_RELAY, 1);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * @brief     This function will read a RF Switch.
 * @return    Return true when the switch pressed or else false
 */

bool execute_read()
{
        if (gpio_get_level(EXECUTE_SWITCH_INPUT) == LOW)
    {
        excute_switch_status.input_count = safety_signal.input_count + 1;
        if (excute_switch_status.input_count >= EXECUTE_SWITCH_LIMIT)
        {
            return true;
        }
    }
    else
    {
        excute_switch_status.input_count = 0;
    }
    return false;
}

bool safety_signal_read ()
{
     if (gpio_get_level(SAFETY_SIGNAL_RELAY) == HIGH)
    {
        safety_signal.input_count = safety_signal.input_count + 1;
        if (safety_signal.input_count >= SAFETY_SWITCH_LIMIT)
        {
            return true;
        }
    }
    else
    {
        safety_signal.input_count = 0;
    }
    return false;
}

/**
 * @brief     This function will read a RF switch and switch on a relay.
 */

void read_switch()
{
    while (true)
    {
        excute_status   = execute_read();
        safety_switch_status = safety_signal_read();

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

/**
 * @brief     This function will switch on a relay when a NUC stops publishing a data for 5 seconds.
 */

void health_check_timer_task()
{
    while (true)
    {
        if (esp_timer_get_time() - health_check_start_timer > health_check_end_timer)
        {
            if (soft_emg_status == false && handshake_check_status)
            {
                gpio_set_level(EMG_RELAY_OUT, 1);
                vTaskDelay(30 / portTICK_PERIOD_MS);
                handshake_relay_status = true;
            }
        }
        else
        {
            if (soft_emg_status == false && handshake_check_status)
            {
                gpio_set_level(EMG_RELAY_OUT, 0);
                vTaskDelay(30 / portTICK_PERIOD_MS);
                handshake_relay_status = false;
            }
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

void app_main(void)
{
    /**
     * @brief     This function will read ADC Channel for current sensor, voltage channel and docking channel.
     */
    xTaskCreate(adc_read_parameters, "Read_current_adc", 2048, NULL, 2, NULL);
    /**
     * @brief     This function will Intialize UART and Request Response from the ROS.
     */
    xTaskCreate(json_control, "Request_response_json", 4096 * 2, NULL, 1, NULL);
    /**
     * @brief     This function will read a RF switch and switch on a relay.
     */
    xTaskCreate(read_power_switch, "read the power switch", 4096, NULL, 0, NULL);

    xTaskCreate(read_switch, "app  switch", 2048, NULL, 0, NULL);

    /**
     * @brief     This function will switch on a relay when a NUC stops publishing a data for 5 seconds.
     */

    xTaskCreate(health_check_timer_task, "Task for NUC shutdown and NUC Health check", 2048, NULL, 0, NULL);
    uint8_t derived_mac_addr[6] = {0};
    // Get MAC address for SoftAp interface
    ESP_ERROR_CHECK(esp_read_mac(derived_mac_addr, ESP_MAC_WIFI_SOFTAP));
    // Soft MAC address print for logging
    ESP_LOGI("SoftAP MAC", "0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x",
             derived_mac_addr[0], derived_mac_addr[1], derived_mac_addr[2],
             derived_mac_addr[3], derived_mac_addr[4], derived_mac_addr[5]);
}
