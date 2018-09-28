#include <stdio.h>
#include <string.h>

#include "thread.h"
#include "shell.h"
#include "shell_commands.h"
#include "xtimer.h"
//#include "timex.h"

#include "modem.h"

#define INTERVAL (20U * US_PER_SEC)


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

static d7ap_session_config_t session_config = {
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

int main(void)
{
    puts("Welcome to RIOT!");

    modem_callbacks_t modem_callbacks = {
        .command_completed_callback = &on_modem_command_completed_callback,
        .return_file_data_callback = &on_modem_return_file_data_callback,
        .write_file_data_callback = &on_modem_write_file_data_callback,
    };

    modem_init(UART_DEV(1), &modem_callbacks);

    xtimer_ticks32_t last_wakeup = xtimer_now();
    uint8_t counter = 0;
    while(1) {
        printf("Sending msg with counter %i\n", counter);
        uint32_t start = xtimer_now_usec();

        if(modem_send_unsolicited_response(0x40, 0, 1, &counter, &session_config)) {
            uint32_t duration_usec = xtimer_now_usec() - start;
            printf("Command completed successfully in %li ms\n", duration_usec / 1000);
        } else {
            printf("!!! modem communication failed or command timeout, reiniting\n"); // TODO distinguish
            modem_reinit();
        }

        counter++;
        xtimer_periodic_wakeup(&last_wakeup, INTERVAL);
        printf("slept until %" PRIu32 "\n", xtimer_usec_from_ticks(xtimer_now()));
    }

    return 0;
}