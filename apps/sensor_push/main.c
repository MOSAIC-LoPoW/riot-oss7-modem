/*
This example shows how to use the modem API to interface with a serial OSS-7 modem.
An unsolicited message will be transmitted periodically using the DASH7 interface or the LoRaWAN interface (alternating)
*/

#include <stdio.h>
#include <string.h>

#include "thread.h"
#include "shell.h"
#include "shell_commands.h"
#include "xtimer.h"
#include "errors.h"

#include "modem.h"

#define INTERVAL (20U * US_PER_SEC)

#define LORAWAN_NETW_SESSION_KEY  { 0x73, 0x30, 0xCE, 0x41, 0x6D, 0x6A, 0x30, 0x86, 0xFA, 0x40, 0x82, 0xBF, 0xAD, 0x62, 0x99, 0x10 }
#define LORAWAN_APP_SESSION_KEY  { 0xC8, 0xF1, 0x95, 0xBA, 0x7A, 0xA5, 0x0F, 0x29, 0xC0, 0x76, 0x3A, 0x58, 0x40, 0xD5, 0xB3, 0x57 }
#define LORAWAN_DEV_ADDR 0x26000850
#define LORAWAN_NETW_ID 0x000017

void on_modem_command_completed_callback(bool with_error) 
{
    printf("modem command completed (success = %i)\n", !with_error);
}

void on_modem_return_file_data_callback(uint8_t file_id, uint32_t offset, uint32_t size, uint8_t* output_buffer)
{
    printf("modem return file data file %i offset %li size %li buffer %p\n", file_id, offset, size, output_buffer);
}

void on_modem_write_file_data_callback(uint8_t file_id, uint32_t offset, uint32_t size, uint8_t* output_buffer)
{
    printf("modem write file data file %i offset %li size %li buffer %p\n", file_id, offset, size, output_buffer);
}

static d7ap_session_config_t d7_session_config = {
    .qos = {
        .qos_resp_mode = SESSION_RESP_MODE_PREFERRED,
        .qos_retry_mode = SESSION_RETRY_MODE_NO
    },
    .dormant_timeout = 0,
    .addressee = {
        .ctrl = {
            .nls_method = AES_NONE,
            .id_type = ID_TYPE_NOID
        },
        .access_class = 0x01,
        .id = {0},
    },
};
  
static lorawan_session_config_t lorawan_session_config = {
    .activationMethod = ABP,
    .appSKey = LORAWAN_APP_SESSION_KEY,
    .nwkSKey = LORAWAN_NETW_SESSION_KEY,
    .devAddr = LORAWAN_DEV_ADDR,
    .request_ack = false
};

int main(void)
{
    puts("Welcome to RIOT!");
    
    modem_callbacks_t modem_callbacks = {
        .command_completed_callback = &on_modem_command_completed_callback,
        .return_file_data_callback = &on_modem_return_file_data_callback,
        .write_file_data_callback = &on_modem_write_file_data_callback,
    };

    modem_init(UART_DEV(1), &modem_callbacks);

    uint8_t uid[D7A_FILE_UID_SIZE];
    modem_read_file(D7A_FILE_UID_FILE_ID, 0, D7A_FILE_UID_SIZE, uid);
    printf("modem UID: %02X%02X%02X%02X%02X%02X%02X%02X\n", uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6], uid[7]);
    
    xtimer_ticks32_t last_wakeup = xtimer_now();
    alp_itf_id_t current_interface_id = ALP_ITF_ID_D7ASP;
    void* current_interface_config = (void*)&d7_session_config;
    uint8_t counter = 0;
    while(1) {
        printf("Sending msg with counter %i\n", counter);
        uint32_t start = xtimer_now_usec();
        
        if(counter % 5 == 0) {
            if(current_interface_id == ALP_ITF_ID_D7ASP) {
                printf("Switching to LoRaWAN\n");
                current_interface_id = ALP_ITF_ID_LORAWAN;
                current_interface_config = &lorawan_session_config;
            } else {
                printf("Switching to D7AP\n");
                current_interface_id = ALP_ITF_ID_D7ASP;
                current_interface_config = &d7_session_config;
            }
        }

        int rc = modem_send_unsolicited_response(0x40, 0, 1, &counter, current_interface_id, current_interface_config);
        if(rc == 0) {
            uint32_t duration_usec = xtimer_now_usec() - start;
            printf("Command completed successfully in %li ms\n", duration_usec / 1000);
        } else if(rc == -EBUSY) {
            printf("Previous command still active, ignoring\n");
        } else if(rc == -ETIMEDOUT) {
            printf("Command timed out\n");
        }

        counter++;
        xtimer_periodic_wakeup(&last_wakeup, INTERVAL);
        printf("slept until %" PRIu32 "\n", xtimer_usec_from_ticks(xtimer_now()));
    }

    return 0;
}