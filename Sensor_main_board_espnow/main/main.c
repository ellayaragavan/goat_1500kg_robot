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
#include "sdkconfig.h"
#include "espnow_example.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include <ota_update.h>

#define HIGH (1)
#define LOW (0)

#define EMG_READ_INPUT (40)
#define BUMPER (41)
#define AUTO_MANUAL (42)
#define PLAY_PAUSE_SWITCH (35)
#define RF_SWITCH (98)
#define ECHO_UART_PORT_NUM (1)
#define ECHO_UART_TX (21)
#define ECHO_UART_RX (47)
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)
#define BUF_SIZE (1536)

#define ECHO_UART_BAUD_RATE (115200)
#define ECHO_TASK_STACK_SIZE (CONFIG_EXAMPLE_TASK_STACK_SIZE)

#define GPIO_BIT_MASK ((1ULL << EMG_READ_INPUT)) | ((1ULL << PLAY_PAUSE_SWITCH)) | (1ULL << AUTO_MANUAL)
#define BUMP_BIT_MASK ((1ULL << BUMPER))

#define EMG_SAMPLE_LIMIT (5)
#define BUMPER_SAMPLE_LIMIT_FRONT (5)
#define AUTO_MANUAL_LIMIT (5)
#define RESET_SWITCH_LIMIT (5)
//#define RF_SWITCH_LIMIT (5)
#define PLAY_PAUSE_SWITCH_LIMIT (5)
#define APP_SWITCH_LIMIT (5)

#define ESPNOW_WIFI_MODE WIFI_MODE_STA
static const char *TAG = "espnow";
uint8_t cliff_mac[ESP_NOW_ETH_ALEN] = {0xf4, 0x12, 0xfa, 0xc2, 0xaa, 0x38};
uint8_t ultrasonic_mac[ESP_NOW_ETH_ALEN] = {0xf4, 0x12, 0xfa, 0xc4, 0x3b, 0x88};
uint8_t main_board_mac[ESP_NOW_ETH_ALEN] = {0xf4, 0x12, 0xfa, 0xc2, 0x9c, 0x48};
bool soft_emg = false;

#define EMG_BUTTON_TIMER 10000000 // 5sec

static volatile bool switchState = false;

// static xQueueHandle gpio_evt_queue = NULL;

typedef struct read_param
{
    bool input_read;
    uint32_t input_count;
} IO_READ;
IO_READ Emg, bumper, auto_manual_value,play_pause_read;

int64_t emg_timer, emg_start_timer, emg_end_timer = 20000000;

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
}
static void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    char *string_json = (char *)data;
    cJSON *root2 = cJSON_Parse(string_json);
    if (cJSON_HasObjectItem(root2, "u") | cJSON_HasObjectItem(root2, "cliff1"))
    {
        cJSON_AddBoolToObject(root2, "emg", Emg.input_read);
        // cJSON_AddBoolToObject(root2, "reset", Reset.input_read);
        //  cJSON_AddBoolToObject(root2, "rf_switch", RF.input_read);
        cJSON_AddBoolToObject(root2, "bumper", bumper.input_read);
        cJSON_AddBoolToObject(root2, "manual_switch_status", auto_manual_value.input_read);
        cJSON_AddBoolToObject(root2, "execute_play_pause_switch_status", play_pause_read.input_read);
    }
    char *my_json_string = cJSON_PrintUnformatted(root2);
    uart_write_bytes(ECHO_UART_PORT_NUM, my_json_string, strlen(my_json_string));
    uart_write_bytes(ECHO_UART_PORT_NUM, "\n", 1);
    printf("%s\n", my_json_string);
   // ESP_LOGI(TAG,"%s\n", my_json_string);
    cJSON_Delete(root2);
    cJSON_free(my_json_string);
}

bool emergency_read()
{
    if (gpio_get_level(EMG_READ_INPUT) == LOW)
    {
        Emg.input_count = Emg.input_count + 1;
        if (Emg.input_count >= EMG_SAMPLE_LIMIT)
        {
            return true;
        }
    }
    else
    {
        Emg.input_count = 0;
    }
    return false;
}

bool bumper_read_front()
{
    if (gpio_get_level(BUMPER) == HIGH)
    {
        bumper.input_count = bumper.input_count + 1;
        if (bumper.input_count >= BUMPER_SAMPLE_LIMIT_FRONT)
        {
            return true;
        }
    }
    else
    {
        bumper.input_count = 0;
    }
    return false;
}

bool auto_manualpinread()
{
   // ESP_LOGI(TAG,"THE INPUT VALUE IS %d",gpio_get_level(AUTO_MANUAL));
    if (gpio_get_level(AUTO_MANUAL) ==LOW)
    {
        
        auto_manual_value.input_count = auto_manual_value.input_count + 1;
        if (auto_manual_value.input_count >= AUTO_MANUAL_LIMIT)
        {
            return true;
        }
    }
    else
    {
        auto_manual_value.input_count = 0;
    }
    return false;

}

// bool reset_read()
// {
//     if (gpio_get_level(RESET_SWITCH) == HIGH)
//     {
//         Reset.input_count = Reset.input_count + 1;
//         if (Reset.input_count >= RESET_SWITCH_LIMIT)
//         {
//             return true;
//         }
//     }
//     else
//     {
//         Reset.input_count = 0;
//     }
//     return false;
// }

// bool rfswitch_read()
// {
//     if (gpio_get_level(RF_SWITCH) == LOW)
//     {
//         RF.input_count = RF.input_count + 1;
//         if (RF.input_count >= RF_SWITCH_LIMIT)
//         {
//             return true;
//         }
//     }
//     else
//     {
//         RF.input_count = 0;
//     }
//     return false;
// }

bool play_pause()
{
    if (gpio_get_level(PLAY_PAUSE_SWITCH) == LOW)
    {
        play_pause_read.input_count = play_pause_read.input_count + 1;
        if (play_pause_read.input_count >= PLAY_PAUSE_SWITCH_LIMIT)
        {
            return true;
        }
    }
    else
    {
        play_pause_read.input_count = 0;
    }
    return false;
}


// void play_pause()
// {
//     bool flag = false;
//     while (1)
//     {
//         int read = gpio_get_level(PLAY_PAUSE);
//         if (read == 0)
//         {
//             flag = true;
//             while (flag == true)
//             {
//                 int read = gpio_get_level(PLAY_PAUSE);
//                 if (read == 1)
//                 {
//                     flag = false;
//                     switchState = !switchState;
//                     ESP_LOGI(TAG, " SWITCH VALUE IS %d", switchState);
//                 }
//             }
//         }
//         vTaskDelay(10);
//     }
// }

void input_read()
{
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = GPIO_BIT_MASK;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    gpio_config_t io_conf_bumper;
    io_conf_bumper.intr_type = GPIO_INTR_DISABLE;
    io_conf_bumper.mode = GPIO_MODE_INPUT;
    io_conf_bumper.pin_bit_mask = BUMP_BIT_MASK;
    io_conf_bumper.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf_bumper.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf_bumper);

    while (true)
    {
        // if (soft_emg == false)
        // {
        //     bool emg_status = emergency_read();
        //     if (emg_status == 1)
        //     {
        //         soft_emg = true;
        //     }
        // }
        // else if(soft_emg == true)
        // {
        //     bool emg_status = emergency_read();
        //     if(emg_status == 1)
        //     {
        //         emg_timer = esp_timer_get_time();
        //         while (emergency_read())
        //         {
        //             if((esp_timer_get_time() - emg_timer) >= EMG_BUTTON_TIMER)
        //             {
        //                 emg_timer = esp_timer_get_time();
        //                 ESP_LOGI(TAG,"Emergency button released");
        //                 soft_emg = false;
        //                 Emg.input_read = false;
        //             }
        //             vTaskDelay(50 / portTICK_PERIOD_MS);
        //         }
        //     }
        //     else
        //     {
        //         emg_timer = esp_timer_get_time();
        //     }
        // }
        Emg.input_read = emergency_read();
        play_pause_read.input_read = play_pause ();
        bumper.input_read = bumper_read_front();
        auto_manual_value.input_read = auto_manualpinread();
        
        if (auto_manual_value.input_read == true)
        {
            if (switchState == true)
            {
                switchState = false;
            }
        }
        // Reset.input_read = reset_read();
        // RF.input_read = rfswitch_read();
        // ESP_LOGI(TAG,"EMG : [%d] , BUMPER : [%d] , RESET : [%d] , RF : [%d]",Emg.input_read,Bumper.input_read,Reset.input_read,RF.input_read);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void send_ultrasonic(cJSON *data)
{
    char *my_json_string = cJSON_PrintUnformatted(data);
    uint8_t data_send[strlen(my_json_string)];
   // printf("%s\n",my_json_string_rgb);
    memcpy(data_send, my_json_string, strlen(my_json_string));
    if (esp_now_send(ultrasonic_mac, data_send, strlen(my_json_string)) != ESP_OK)
    {
        ESP_LOGE(TAG, "SEND_ERROR\n");
    }
    cJSON_free(my_json_string);
    cJSON_Delete(data);
}

void send_cliff(cJSON *data)
{
    char *my_json_string = cJSON_PrintUnformatted(data);
    uint8_t data_send[strlen(my_json_string)];
    // printf("%s\n",my_json_string_rgb);
    memcpy(data_send, my_json_string, strlen(my_json_string));
    if (esp_now_send(cliff_mac, data_send, strlen(my_json_string)) != ESP_OK)
    {
        ESP_LOGE(TAG, "SEND_ERROR\n");
    }
    cJSON_free(my_json_string);
    cJSON_Delete(data);
}

void json_control()
{
    while (true)
    {
        uint8_t *data = (uint8_t *)malloc(BUF_SIZE);
        int len = uart_read_bytes(ECHO_UART_PORT_NUM, data, BUF_SIZE, 10 / portTICK_RATE_MS);
        // ESP_LOGI(TAG, "heap size : %d", esp_get_free_heap_size());
        if (len > 0)
        {
            ESP_LOGI(TAG, "Inside the UART Buffer");
            cJSON *parse_data = cJSON_Parse((char *)data);
            if (cJSON_HasObjectItem(parse_data, "ota"))
            {
                int ota_data = cJSON_GetObjectItem(parse_data, "ota")->valueint;
                if (ota_data == 1)
                {
                    ota_update_init();
                }
                else if (ota_data == 2)
                {
                    send_ultrasonic(parse_data);
                }
                else if (ota_data == 3)
                {
                    send_cliff(parse_data);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(300));
        free(data);
    }
}

void add_peer_address(uint8_t *peer_address)
{
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL)
    {
        printf("Malloc peer information fail");
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = 0;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, peer_address, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(peer));
    free(peer);
}

void app_main(void)
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
    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, 1024 * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, ECHO_UART_TX, ECHO_UART_RX, ECHO_TEST_RTS, ECHO_TEST_CTS));
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    esp_timer_init();
    //esp_base_mac_addr_set(main_board_mac);
    wifi_init();
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
    add_peer_address(ultrasonic_mac);
    add_peer_address(cliff_mac);

    // esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    // if (peer == NULL)
    // {
    //     printf("Malloc peer information fail\n");
    // }
    // memset(peer, 0, sizeof(esp_now_peer_info_t));
    // peer->channel = 0;
    // peer->ifidx = ESPNOW_WIFI_IF;
    // peer->encrypt = false;
    // memcpy(peer->peer_addr, s_example_broadcast_mac, ESP_NOW_ETH_ALEN);
    // ESP_ERROR_CHECK(esp_now_add_peer(peer));
    // free(peer);
    // vTaskDelay(100 / portTICK_PERIOD_MS);
    // Configure the GPIO pin for the switch

    xTaskCreate(input_read, "Read_input_pins", 4096, NULL, 1, NULL);
    xTaskCreate(json_control, "read json emergency stop", 2048, NULL, 0, NULL);
    //xTaskCreate(play_pause, "function change ",2048, NULL, 0, NULL);
    // xTaskCreate(auto_manual_function,"auto_manual function",1024, NULL, 0, NULL);

 
}
