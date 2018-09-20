#include <stdio.h>
#include <string.h>

#include "thread.h"
#include "shell.h"
#include "shell_commands.h"

#include "modem.h"


void on_modem_command_completed_callback(bool with_error) 
{
    printf("modem command completed (success = %i)", !with_error);
}

void on_modem_return_file_data_callback(uint8_t file_id, uint32_t offset, uint32_t size, uint8_t* output_buffer)
{
    printf("modem return file data file %i offset %li size %li buffer %p", file_id, offset, size, output_buffer);
}

void on_modem_write_file_data_callback(uint8_t file_id, uint32_t offset, uint32_t size, uint8_t* output_buffer)
{
    printf("modem write file data file %i offset %li size %li buffer %p", file_id, offset, size, output_buffer);
}

int main(void)
{
    puts("Welcome to RIOT!");

    modem_callbacks_t modem_callbacks = {
        .command_completed_callback = &on_modem_command_completed_callback,
        .return_file_data_callback = &on_modem_return_file_data_callback,
        .write_file_data_callback = &on_modem_write_file_data_callback,
    };

    modem_init(UART_DEV(1), &modem_callbacks);
    uint8_t uid[8];
    modem_read_file(0, 0, 8, uid);
    printf("modem UID: %02X%02X%02X%02X%02X%02X%02X%02X\n", 
        uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6], uid[7]);
    
    char line_buf[SHELL_DEFAULT_BUFSIZE];
    shell_run(NULL, line_buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
