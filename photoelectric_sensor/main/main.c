#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include <esp_err.h>
#include "esp_now.h"
#include <cJSON.h>

#define LOW 0
#define photoelectric_sensor 27
#define GPIO_BIT_MASK (1ULL << photoelectric_sensor);

#define PHOTO_LIMIT_SWITCH 5

#define ECHO_UART_PORT_NUM 0
#define UART_TX_PIN (UART_PIN_NO_CHANGE)
#define UART_RX_PIN (UART_PIN_NO_CHANGE)
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)

#define ECHO_UART_BAUD_RATE (115200)
#define ECHO_TASK_STACK_SIZE (CONFIG_EXAMPLE_TASK_STACK_SIZE)

#define BUF_SIZE (4096)

uint8_t SENDER_MAC[ESP_NOW_ETH_ALEN] = {0xf4, 0x12, 0xfa, 0xc3, 0x4c, 0x98};
char mac_addr[30];

static const char *TAG = "ARFF";

typedef struct read_param
{
    bool input_read;
    uint32_t input_count;
} IO_READ;
IO_READ photoelectric;

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
    uint8_t *MAC_ADD_1 = hex_str_to_uint8("C049EF69C72C");
    for (int i = 0; i < 6; i++)
    {
        SENDER_MAC[i] = MAC_ADD_1[i];
        printf("%02X", SENDER_MAC[i]);
    }
    printf("\n");
}

void print_mac(const unsigned char *mac)
{
    sprintf(mac_addr, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGE(TAG, "The mac address of the ESP is %s", mac_addr);
}

void add_peer_address(uint8_t *peer_address)
{
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL)
    {
        printf("Malloc peer information fail");
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = 1;
    peer->ifidx = ESP_IF_WIFI_STA;
    peer->encrypt = false;
    memcpy(peer->peer_addr, peer_address, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(peer));
    free(peer);
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_LR));
}

static void espnow_send_cb(const uint8_t *SENDER_MAC, esp_now_send_status_t status)
{
    ESP_LOGI(TAG, "Mesg sent");
}

static void espnow_recv_cb(const uint8_t *mac_addr, uint8_t *data, int len)
{
}

void espnow_init()
{

    if (esp_now_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "ESP_INIT_ERROR\n");
    }
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));
    add_peer_address(SENDER_MAC);
    esp_now_peer_num_t *num = malloc(sizeof(esp_now_peer_num_t));
    esp_now_get_peer_num(num);
    ESP_LOGI(TAG, "NUMBER OF PEER : %d", num->total_num);
    free(num);
}

bool photoelectric_pinread()
{
    if (gpio_get_level(photoelectric_sensor) == LOW)
    {
        photoelectric.input_count = photoelectric.input_count + 1;
        if (photoelectric.input_count >= PHOTO_LIMIT_SWITCH)
        {
            return true;
        }
    }
    else
    {
        photoelectric.input_count = 0;
    }
    return false;
}

void uart_send(cJSON *data)
{
    char *my_json_string = cJSON_PrintUnformatted(data);
    uart_write_bytes(ECHO_UART_PORT_NUM, my_json_string, strlen(my_json_string));
    uart_write_bytes(ECHO_UART_PORT_NUM, "\r\n", 2);
    // printf("successfully transmit the data");
    cJSON_Delete(data);
    free(my_json_string);
}

void sensor_read()
{
    uart_config_t uart_config = {
        .baud_rate = ECHO_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
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
    while (1)
    {
        // ESP_LOGI(TAG, "The input from the photo electric sensor is %d",photoelectric_pinread());
        cJSON *root;
        int read_status = photoelectric_pinread();
        root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "photoelectric_sensor", read_status);
        uart_send(root);
        vTaskDelay(1);
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

    unsigned char mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    print_mac(mac);
    printf("MAC: { 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x } \n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Config_to_mac();
    wifi_init();
    espnow_init();

    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = GPIO_BIT_MASK;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    xTaskCreate(sensor_read, "photoelectricsensor_read", 1024 * 2, NULL, 1, NULL);
}