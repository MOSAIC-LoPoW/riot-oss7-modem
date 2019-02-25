/* * OSS-7 - An opensource implementation of the DASH7 Alliance Protocol for ultra
 * lowpower wireless sensor communication
 *
 * Copyright 2015 University of Antwerp
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "modem.h"
#include "debug.h"
#include "log.h"
#include "errors.h"
#include "fifo.h"
#include "alp.h"
#include "d7ap.h"
#include <stdio.h>
#include <stdlib.h>
#include "modem_interface.h"
#include "mutex.h"
#include "xtimer.h"
#include "string.h"

#define RX_BUFFER_SIZE 256
#define CMD_BUFFER_SIZE 256

#define CMD_TIMEOUT_MS 1000 * 30

#define DPRINT(...) printf(__VA_ARGS__)
#define DPRINT_DATA(...)


typedef struct {
  uint8_t tag_id;
  bool is_active;
  bool completed_with_error;
  fifo_t fifo;
  bool execute_synchronuous;
  uint8_t* response_buffer; // used for sync responses
  uint8_t buffer[256];
} command_t;

static modem_callbacks_t* callbacks;
static mutex_t cmd_mutex = MUTEX_INIT;
static command_t command; // TODO only one active command supported for now
static uint8_t next_tag_id = 0;

static void process_serial_frame(fifo_t* fifo) {
  bool command_completed = false;
  while(fifo_get_size(fifo)) {
    alp_action_t action;
    alp_parse_action(fifo, &action);

    switch(action.operation) {
      case ALP_OP_RETURN_TAG:
        if(action.tag_response.tag_id == command.tag_id) {
          command_completed = action.tag_response.completed;
          command.completed_with_error = action.tag_response.error;
        } else {
          DPRINT("received resp with unexpected tag_id (%i vs %i)\n", action.tag_response.tag_id, command.tag_id);
          // TODO unsolicited responses
        }
        break;
      case ALP_OP_WRITE_FILE_DATA:
        if(callbacks->write_file_data_callback)
          callbacks->write_file_data_callback(action.file_data_operand.file_offset.file_id,
                                               action.file_data_operand.file_offset.offset,
                                               action.file_data_operand.provided_data_length,
                                               action.file_data_operand.data);
        break;
      case ALP_OP_RETURN_FILE_DATA:
        if(command.execute_synchronuous) {
          memcpy(command.response_buffer, action.file_data_operand.data, action.file_data_operand.provided_data_length);
        } else if(callbacks->return_file_data_callback) {
          callbacks->return_file_data_callback(action.file_data_operand.file_offset.file_id,
                                               action.file_data_operand.file_offset.offset,
                                               action.file_data_operand.provided_data_length,
                                               action.file_data_operand.data);
        }
        break;
      case ALP_OP_RETURN_STATUS: ;
        d7ap_session_result_t interface_status = *((d7ap_session_result_t*)action.status.data);
        //uint8_t addressee_len =
        d7ap_addressee_id_length(interface_status.addressee.ctrl.id_type);
        DPRINT("received resp\n");
        // TODO DPRINT_DATA(interface_status.addressee.id, addressee_len);
        // TODO callback?
        break;
      default:
        assert(false);
    }
  }


  if(command_completed) {
    //DPRINT("command with tag %i completed @ %i", command.tag_id, timer_get_counter_value());
    DPRINT("command with tag %i completed\n", command.tag_id);
    if(command.execute_synchronuous) {
      mutex_unlock(&cmd_mutex);
    } else {
      if(callbacks->command_completed_callback)
        callbacks->command_completed_callback(command.completed_with_error);
    }

    command.is_active = false;
  }
}

void modem_cb_init(modem_callbacks_t* cbs)
{
    callbacks = cbs;
}

void modem_init(uint8_t uart_idx, uint32_t baudrate)
{
  modem_interface_init(uart_idx, baudrate, 0, 0); // TODO pins
  modem_interface_register_handler(&process_serial_frame, SERIAL_MESSAGE_TYPE_ALP_DATA);
}

void modem_reinit(void) {
  command.is_active = false;
}

void modem_send_ping(void) {
  uint8_t ping_request[1]={0x01};
  modem_interface_transfer_bytes((uint8_t*) &ping_request, 1, SERIAL_MESSAGE_TYPE_PING_REQUEST);
}

void modem_execute_raw_alp(uint8_t* alp, uint8_t len) {
  modem_interface_transfer_bytes(alp, len, SERIAL_MESSAGE_TYPE_ALP_DATA);
}

bool alloc_command(void) {
  if(command.is_active) {
    //DPRINT("prev command still active @ %i", timer_get_counter_value());
    DPRINT("prev command still active\n");
    return false;
  }

  command.is_active = true;
  command.execute_synchronuous = false;
  command.completed_with_error = false;
  fifo_init(&command.fifo, command.buffer, CMD_BUFFER_SIZE);
  command.tag_id = next_tag_id;
  next_tag_id++;

  alp_append_tag_request_action(&command.fifo, command.tag_id, true);
  return true;
}

static modem_status_t block_until_cmd_completed(uint32_t timeout_ms) {
  // lock first and try to lock again with timeout, should block until ready, or timeout
  mutex_lock(&cmd_mutex);
  int timeout = xtimer_mutex_lock_timeout(&cmd_mutex, timeout_ms * 1000);
	mutex_unlock(&cmd_mutex);
  if(!timeout) {
		command.is_active = false;
    if(command.completed_with_error)
      return MODEM_STATUS_COMMAND_COMPLETED_ERROR;
    else
      return MODEM_STATUS_COMMAND_COMPLETED_SUCCESS;
	} else {
    DPRINT("!!! timeout, unlocking\n");
    return MODEM_STATUS_COMMAND_TIMEOUT;
  }
}

static void send_read_file(uint8_t file_id, uint32_t offset, uint32_t size) {
	alp_append_read_file_data_action(&command.fifo, file_id, offset, size, true, false);
  modem_interface_transfer_bytes(command.buffer, fifo_get_size(&command.fifo), SERIAL_MESSAGE_TYPE_ALP_DATA);
}

// TODO can be removed later?
modem_status_t modem_read_file_async(uint8_t file_id, uint32_t offset, uint32_t size) {
  if(!alloc_command())
    return MODEM_STATUS_BUSY;

  send_read_file(file_id, offset, size);
  return MODEM_STATUS_COMMAND_PROCESSING;
}

modem_status_t modem_read_file(uint8_t file_id, uint32_t offset, uint32_t size, uint8_t* response_buffer) {
  if(!alloc_command())
    return MODEM_STATUS_BUSY;

  command.execute_synchronuous = true;
  command.response_buffer = response_buffer;

	send_read_file(file_id, offset, size);
  return block_until_cmd_completed(CMD_TIMEOUT_MS);
}

// TODO can be removed later?
modem_status_t modem_write_file_async(uint8_t file_id, uint32_t offset, uint32_t size, uint8_t* data) {
  if(!alloc_command())
    return MODEM_STATUS_BUSY;

  alp_append_write_file_data_action(&command.fifo, file_id, offset, size, data, true, false);

  modem_interface_transfer_bytes(command.buffer, fifo_get_size(&command.fifo), SERIAL_MESSAGE_TYPE_ALP_DATA);

  return MODEM_STATUS_COMMAND_PROCESSING;
}

modem_status_t modem_send_unsolicited_response(uint8_t file_id, uint32_t offset, uint32_t length, uint8_t* data,
                                     session_config_t* session_config) {
  if(!alloc_command())
    return MODEM_STATUS_BUSY;

  if(session_config->interface_type==DASH7)
    alp_append_forward_action(&command.fifo, ALP_ITF_ID_D7ASP, (uint8_t *) &session_config->d7ap_session_config, sizeof(d7ap_session_config_t));
  else if(session_config->interface_type==LORAWAN_OTAA)
    alp_append_forward_action(&command.fifo, ALP_ITF_ID_LORAWAN_OTAA, (uint8_t *) &session_config->lorawan_session_config_otaa, sizeof(lorawan_session_config_otaa_t));
  else if(session_config->interface_type==lorawan_ABP)
    alp_append_forward_action(&command.fifo, ALP_ITF_ID_LORAWAN_ABP, (uint8_t *) &session_config->lorawan_session_config_abp, sizeof(lorawan_session_config_abp_t));

  alp_append_return_file_data_action(&command.fifo, file_id, offset, length, data);

  command.execute_synchronuous = true;
  modem_interface_transfer_bytes(command.buffer, fifo_get_size(&command.fifo), SERIAL_MESSAGE_TYPE_ALP_DATA);
  return block_until_cmd_completed(CMD_TIMEOUT_MS); // TODO take timeout as param
}
