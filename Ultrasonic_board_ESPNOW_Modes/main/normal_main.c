#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include <freertos/task.h>
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include <ultrasonic.h>
#include <esp_err.h>
#include "esp_system.h"
#include "esp_crc.h"
#include "esp_now.h"
#include "sdkconfig.h"
#include <cJSON.h>

#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_STA
#define MASTER_MAC_FIRST CONFIG_MASTER_MAC_ADDRESS_FIRST
#define MASTER_MAC_LAST CONFIG_MASTER_MAC_ADDRESS_LAST

#define MAX_DISTANCE_CM CONFIG_EXAMPLE_MAX_DISTANCE
#define ESPNOW_PMK CONFIG_ESPNOW_PMK
#define length CONFIG_NO_OF_ULTRASONIC_SENSOR

int32_t SENSOR_CONFIG[8] = {
CONFIG_SENSOR_1,
CONFIG_SENSOR_2,
CONFIG_SENSOR_3,
CONFIG_SENSOR_4,
CONFIG_SENSOR_5,
CONFIG_SENSOR_6,
CONFIG_SENSOR_7,
CONFIG_SENSOR_8
};

//hjahabcas
int32_t SENSOR_CONNECTOR[8] = {4,10,6,5,8,7,9,11};

int32_t USST[] = {1,41,39,37,35,47,13,11};
int32_t USSE[] = {2,42,40,38,36,48,14,12};


bool once_flg = false;
static const char *TAG = "espnow";
uint8_t Broadcast_mac[ESP_NOW_ETH_ALEN];
//uint8_t s_example_broadcast_mac[ESP_NOW_ETH_ALEN] = {0xec, 0x94, 0xcb, 0x78, 0x2f, 0xc8};
uint8_t cb_data[100];

uint8_t* hex_str_to_uint8(const char* string) {
    if (string == NULL)
        return NULL;
    size_t slength = strlen(string);
    if ((slength % 2) != 0) // must be even
        return NULL;
    size_t dlength = slength / 2;
    uint8_t* data = (uint8_t*)malloc(dlength);
    memset(data, 0, dlength);
    size_t index = 0;
    while (index < slength) {
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

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK(esp_wifi_set_mode(ESPNOW_WIFI_MODE) );
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (mac_addr == NULL) 
    {
        ESP_LOGE(TAG,"SEND_CB_ERROR");
        return;
    }
    if(status != ESP_NOW_SEND_SUCCESS)
    {
        //ESP_LOGE(TAG,"ESP_SEND_DATA_FAILED\n");
        once_flg = false;
    }
    else if(status == ESP_NOW_SEND_SUCCESS && once_flg == false)
    {
        ESP_LOGI(TAG,"ESP_SEND_DATA_SUCCESS\n");
        once_flg = true;
    }

}
static void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len)
{
    for (int i = 0; i < 6; i++) 
    {
        printf("%02X", mac_addr[i]);
        if (i < 5)printf(":");
    }
    printf("\n");
    memcpy(&cb_data,data,sizeof(cb_data));
    printf("data : %s\n",cb_data);
}

void ultrasonic_test(void *pvParameters)
{
    cJSON *root;
    cJSON *dataArray;
   
    int numbers[length];
    ultrasonic_sensor_t sensor[length];
    uint32_t distance[length];

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    wifi_init();
    if(esp_now_init()!=ESP_OK)
    {
        ESP_LOGE(TAG,"ESP_INIT_ERROR\n");
    }
    ESP_ERROR_CHECK( esp_now_register_send_cb(espnow_send_cb) );
    ESP_ERROR_CHECK( esp_now_register_recv_cb(espnow_recv_cb) );
    ESP_ERROR_CHECK( esp_now_set_pmk((uint8_t *)ESPNOW_PMK) );
    uint32_t version;
    esp_now_get_version(&version);
    ESP_LOGI(TAG,"ESP_NOW_VERSION : %u\n", version);
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    if (peer == NULL) 
    {
        printf("Malloc peer information fail");
    }
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = 0;
    peer->ifidx = ESPNOW_WIFI_IF;
    peer->encrypt = false;
    memcpy(peer->peer_addr, Broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(peer));
    free(peer);

    for(uint16_t i=0;i<length;i++)
    {
        for(uint16_t j=0;j<8;j++)
        {
            if(SENSOR_CONFIG[i] == SENSOR_CONNECTOR[j])
            {
                sensor[i].trigger_pin = USST[j];
                sensor[i].echo_pin = USSE[j];
                ultrasonic_init(&sensor[i]);
                ESP_LOGI(TAG,"TRG AND ECO : [%d , %d]",USST[j],USSE[j]);
            }
        }
    }
    while (true)
    {
        root = cJSON_CreateObject();
        for(uint16_t j = 0;j<length;j++)
        
        {
            esp_err_t res = ultrasonic_measure_cm(&sensor[j], MAX_DISTANCE_CM, &distance[j]);
            if(res != ESP_OK)
            {
                switch (res)
                {
                    case ESP_ERR_ULTRASONIC_PING:
                        numbers[j] = 0;
                        break;
                    case ESP_ERR_ULTRASONIC_PING_TIMEOUT:
                        numbers[j] = 0;
                        break;
                    case ESP_ERR_ULTRASONIC_ECHO_TIMEOUT:
                        numbers[j] = 0;
                        break;
                    default:
                        printf("%s\n", esp_err_to_name(res));
                }
            }
            else
            {
                numbers[j] = distance[j];
            }  
        }
        dataArray = cJSON_CreateIntArray(numbers, length);
	    cJSON_AddItemToObject(root, "u", dataArray);
        char *my_json_string = cJSON_PrintUnformatted(root);
        uint8_t data_send[strlen(my_json_string)];
        memcpy(data_send, my_json_string, strlen(my_json_string));
        if (esp_now_send(Broadcast_mac,data_send,strlen(my_json_string)) != ESP_OK) 
        {
            //ESP_LOGE(TAG,"SEND_ERROR_OVER_ESPNOW");
        }
        ESP_LOGI(TAG,"DATA :  %s ",my_json_string);
	    cJSON_Delete(root);
        cJSON_free(my_json_string);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
void Config_to_mac()
{
    printf("\n");
    printf("Broadcast Mac : ");
    uint8_t *MAC_data = hex_str_to_uint8(CONFIG_MASTER_MAC_ADDRESS);
    for(int i=0;i<6;i++)
    {
        Broadcast_mac[i] = MAC_data[i];
        printf("%02X",Broadcast_mac[i]);
    }
    printf("\n");
}

void app_main()
{
    Config_to_mac();
    xTaskCreate(ultrasonic_test, "ultrasonic_test", configMINIMAL_STACK_SIZE * 5, NULL, 1, NULL);
}

