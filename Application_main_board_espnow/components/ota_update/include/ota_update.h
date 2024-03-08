#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <sdkconfig.h>

#if !CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
#error App rollback is not enabled menuconfig
#endif

#define HIGH 1
#define LOW 0
#define OTA_BUTTON 0
#define INPUT_BIT_MASK (1ULL << OTA_BUTTON)

typedef enum{
    OTA_UPDATE_START,
    OTA_UPDATE_PROGRESS,
    OTA_UPDATE_FAILED,
    OTA_UPDATE_SUCCESS,
    OTA_VALIDATION_SUCCESS,
    OTA_VALIDATION_FAILED,
    OTA_NOT_FOUND
}ota_state_t;

ota_state_t ota_status_get(void);
void ota_update_init(void);
void ota_trigger_event(void);

#endif