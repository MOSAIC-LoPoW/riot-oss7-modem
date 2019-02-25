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

#define LORAWAN_NETW_SESSION_KEY  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define LORAWAN_APP_SESSION_KEY  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define LORAWAN_DEV_ADDR 0x00000000
#define LORAWAN_NETW_ID 0x000000

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

static session_config_t session_config = {
  .interface_type = DASH7,
  .d7ap_session_config = {
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
  }
};

int main(void)
{
    puts("Welcome to RIOT!");

    modem_callbacks_t modem_callbacks = {
        .command_completed_callback = &on_modem_command_completed_callback,
        .return_file_data_callback = &on_modem_return_file_data_callback,
        .write_file_data_callback = &on_modem_write_file_data_callback,
    };

    modem_init(1, 9600);
    modem_cb_init(&modem_callbacks);

    uint8_t uid[D7A_FILE_UID_SIZE];
    modem_read_file(D7A_FILE_UID_FILE_ID, 0, D7A_FILE_UID_SIZE, uid);
    printf("modem UID: %02X%02X%02X%02X%02X%02X%02X%02X\n", uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6], uid[7]);

    xtimer_ticks32_t last_wakeup = xtimer_now();
    uint8_t counter = 0;
    while(1) {
        printf("Sending msg with counter %i\n", counter);
        uint32_t start = xtimer_now_usec();
        modem_status_t status = modem_send_unsolicited_response(0x40, 0, 1, &counter, &session_config);
        uint32_t duration_usec = xtimer_now_usec() - start;
        printf("Command completed in %li ms\n", duration_usec / 1000);
        if(status == MODEM_STATUS_COMMAND_COMPLETED_SUCCESS) {
            printf("Command completed successfully\n");
        } else if(status == MODEM_STATUS_COMMAND_COMPLETED_ERROR) {
            printf("Command completed with error\n");
        } else if(status == MODEM_STATUS_COMMAND_TIMEOUT) {
            printf("Command timed out\n");
        }

        counter++;
        xtimer_periodic_wakeup(&last_wakeup, INTERVAL);
        printf("slept until %" PRIu32 "\n", xtimer_usec_from_ticks(xtimer_now()));
    }

    return 0;
}
