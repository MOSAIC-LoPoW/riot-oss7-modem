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

#include "xtimer.h"
#include "thread.h"
#include "mutex.h"
#include "assert.h"
#include "errors.h"

#include "modem.h"
#include "debug.h"
#include "log.h"
#include "fifo.h"

#include "alp.h"
//#include "scheduler.h"
//#include "timer.h"
#include "d7ap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include "platform.h"

#define RX_BUFFER_SIZE 256
#define CMD_BUFFER_SIZE 256
#define OSS7MODEM_BAUDRATE 9600 // TODO
#define CMD_TIMEOUT_MS 1000 * 30
#define DPRINT(...) printf(__VA_ARGS__)
#define DPRINT_DATA(...)
// #if defined(FRAMEWORK_LOG_ENABLED) && defined(FRAMEWORK_MODEM_LOG_ENABLED)
//   #define DPRINT(...) log_print_string(__VA_ARGS__)
//   #define DPRINT_DATA(...) log_print_data(__VA_ARGS__)
// #else
//     #define DPRINT(...)
//     #define DPRINT_DATA(...)
// #endif


typedef struct {
  uint8_t tag_id;
  bool is_active;
  fifo_t fifo;
  bool execute_synchronuous;
  uint8_t* response_buffer; // used for sync responses
  uint8_t buffer[256];
} command_t;

static uart_t uart_handle;
static modem_callbacks_t* callbacks;
static fifo_t rx_fifo;
static uint8_t rx_buffer[RX_BUFFER_SIZE];
static char rx_thread_stack[THREAD_STACKSIZE_MAIN];
static mutex_t rx_mutex = MUTEX_INIT_LOCKED;
static mutex_t cmd_mutex = MUTEX_INIT;

static command_t command; // TODO only one active command supported for now
static uint8_t next_tag_id = 0;
static bool parsed_header = false;
static uint8_t payload_len = 0;

static void process_serial_frame(fifo_t* fifo) {
  bool command_completed = false;
  bool completed_with_error = false;
  while(fifo_get_size(fifo)) {
    alp_action_t action;
    alp_parse_action(fifo, &action);

    switch(action.operation) {
      case ALP_OP_RETURN_TAG:
        if(action.tag_response.tag_id == command.tag_id) {
          command_completed = action.tag_response.completed;
          completed_with_error = action.tag_response.error;
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
        DPRINT("received resp from: \n");
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
        callbacks->command_completed_callback(completed_with_error);
    }

    command.is_active = false;
  }
}

static void process_rx_fifo(void) {
  if(!parsed_header) {
    // <sync byte (0xC0)><version (0x00)><length of ALP command (1 byte)><ALP command> // TODO CRC
    if(fifo_get_size(&rx_fifo) > SERIAL_ALP_FRAME_HEADER_SIZE) {
        uint8_t header[SERIAL_ALP_FRAME_HEADER_SIZE];
        fifo_peek(&rx_fifo, header, 0, SERIAL_ALP_FRAME_HEADER_SIZE);
        DPRINT_DATA(header, 3); // TODO tmp

        if(header[0] != SERIAL_ALP_FRAME_SYNC_BYTE || header[1] != SERIAL_ALP_FRAME_VERSION) {
          fifo_skip(&rx_fifo, 1);
          DPRINT("skip");
          parsed_header = false;
          payload_len = 0;
          if(fifo_get_size(&rx_fifo) < SERIAL_ALP_FRAME_HEADER_SIZE)
            mutex_lock(&rx_mutex); // wait for more data

          return;
        }

        parsed_header = true;
        fifo_skip(&rx_fifo, SERIAL_ALP_FRAME_HEADER_SIZE);
        payload_len = header[2];
        DPRINT("found header, payload size = %i\n", payload_len);
		    mutex_unlock(&rx_mutex); // implicit return, task will re-run to parse payload
    }
  } else {
    if(fifo_get_size(&rx_fifo) < payload_len) {
      //DPRINT("payload not complete yet\n");
      return;
    }

    // payload complete, start parsing
    // rx_fifo can be bigger than the current serial packet, init a subview fifo
    // which is restricted to payload_len so we can't parse past this packet.
    fifo_t payload_fifo;
    fifo_init_subview(&payload_fifo, &rx_fifo, 0, payload_len);
    process_serial_frame(&payload_fifo);

    // pop parsed bytes from original fifo
    fifo_skip(&rx_fifo, payload_len - fifo_get_size(&payload_fifo));
    parsed_header = false;
  }
}

static void rx_cb(void * arg, uint8_t byte) {
  (void) arg; // keep compiler happy
  (void) byte; // keep compiler happy
  fifo_put_byte(&rx_fifo, byte);
  
	mutex_unlock(&rx_mutex); // start processing thread
}

static void send(uint8_t* buffer, uint8_t len) {
#ifdef PLATFORM_USE_MODEM_INTERRUPT_LINES
  platform_modem_wakeup();
#endif

  uint8_t header[] = {'A', 'T', '$', 'D', 0xC0, 0x00, len };
  uart_write(uart_handle, header, sizeof(header));
  uart_write(uart_handle, buffer, len);

#ifdef PLATFORM_USE_MODEM_INTERRUPT_LINES
  platform_modem_release();
#endif

  //DPRINT("> %i bytes @ %i", len, timer_get_counter_value());
  DPRINT("> %i bytes\n", len);
}

void* rx_thread(void* arg) {
	(void) arg; // supress warning
	
	while(true) {
		// thread running forever, wait untill mutex available
		// if unlocked --> there is data to process
		mutex_lock(&rx_mutex);
		
		process_rx_fifo();
	}
	
	return NULL;
}

void modem_init(uart_t uart, modem_callbacks_t* cbs) {
  uart_handle = uart;
  callbacks = cbs;
  fifo_init(&rx_fifo, rx_buffer, RX_BUFFER_SIZE);

	thread_create(rx_thread_stack, sizeof(rx_thread_stack), THREAD_PRIORITY_MAIN -1, 
	 	0 , rx_thread , NULL, "oss7_modem_rx");
	
  int ret = uart_init(uart_handle, OSS7MODEM_BAUDRATE, &rx_cb, NULL);
  assert(ret == UART_OK);
  // TODO for now we keep uart enabled so we can use RX IRQ.
  // can be optimized later if GPIO IRQ lines are implemented.
  // assert(uart_enable(uart_handle));
  // uart_set_rx_interrupt_callback(uart_handle, &rx_cb);
  // assert(uart_rx_interrupt_enable(uart_handle) == SUCCESS);
}

void modem_reinit(void) {
  command.is_active = false;
}

bool modem_execute_raw_alp(uint8_t* alp, uint8_t len) {
  send(alp, len);
  return true; // TODO
}

bool alloc_command(void) {
  if(command.is_active) {
    //DPRINT("prev command still active @ %i", timer_get_counter_value());
    DPRINT("prev command still active\n");
    return false;
  }

  command.is_active = true;
  command.execute_synchronuous = false;
  fifo_init(&command.fifo, command.buffer, CMD_BUFFER_SIZE);
  command.tag_id = next_tag_id;
  next_tag_id++;

  alp_append_tag_request_action(&command.fifo, command.tag_id, true);
  return true;
}

static int block_until_cmd_completed(uint32_t timeout_ms) {
  // lock first and try to lock again with timeout, should block until ready, or timeout
  mutex_lock(&cmd_mutex);
  int timeout = xtimer_mutex_lock_timeout(&cmd_mutex, timeout_ms * 1000);
	mutex_unlock(&cmd_mutex);
  if(!timeout) {
		command.is_active = false;
    return 0;
	} else {
    DPRINT("!!! timeout, unlocking\n");
    return -ETIMEDOUT;
  }
}

static void send_read_file(uint8_t file_id, uint32_t offset, uint32_t size) {
	alp_append_read_file_data_action(&command.fifo, file_id, offset, size, true, false);
	send(command.buffer, fifo_get_size(&command.fifo));
}

// TODO can be removed later?
int modem_read_file_async(uint8_t file_id, uint32_t offset, uint32_t size) {
  if(!alloc_command())
    return -EBUSY;
  
  send_read_file(file_id, offset, size);
  return 0;
}

int modem_read_file(uint8_t file_id, uint32_t offset, uint32_t size, uint8_t* response_buffer) {
  if(!alloc_command())
    return -EBUSY;
    
  command.execute_synchronuous = true;  
  command.response_buffer = response_buffer;

	send_read_file(file_id, offset, size);
  return block_until_cmd_completed(CMD_TIMEOUT_MS);
}


// TODO can be removed later?
int modem_write_file_async(uint8_t file_id, uint32_t offset, uint32_t size, uint8_t* data) {
  if(!alloc_command())
    return -EBUSY;

  alp_append_write_file_data_action(&command.fifo, file_id, offset, size, data, true, false);

  send(command.buffer, fifo_get_size(&command.fifo));

  return 0;
}

static void send_unsolicited_response(uint8_t file_id, uint32_t offset, uint32_t length, uint8_t* data,
                                     alp_itf_id_t itf, void* interface_config) {
  switch(itf) {
    case ALP_ITF_ID_D7ASP:
      alp_append_forward_action(&command.fifo, ALP_ITF_ID_D7ASP, (uint8_t *)interface_config, sizeof(d7ap_session_config_t));
      break;
    case ALP_ITF_ID_LORAWAN:
      alp_append_forward_action(&command.fifo, ALP_ITF_ID_LORAWAN, (uint8_t *)interface_config, sizeof(lorawan_session_config_t));
      break;
    case ALP_ITF_ID_HOST:
      break;
    default:
      assert(false);
  }
  
  
  alp_append_return_file_data_action(&command.fifo, file_id, offset, length, data);

  send(command.buffer, fifo_get_size(&command.fifo));
}

int modem_send_unsolicited_response(uint8_t file_id, uint32_t offset, uint32_t length, uint8_t* data,
                                     alp_itf_id_t itf, void* interface_config) {
  if(!alloc_command())
    return -EBUSY;
  
  command.execute_synchronuous = true;
  send_unsolicited_response(file_id, offset, length, data, itf, interface_config);
  return block_until_cmd_completed(CMD_TIMEOUT_MS); // TODO take timeout as param
}

int modem_send_unsolicited_response_async(uint8_t file_id, uint32_t offset, uint32_t length, uint8_t* data,
                                     alp_itf_id_t itf, void* interface_config) {
  if(!alloc_command())
    return -EBUSY;

  send_unsolicited_response(file_id, offset, length, data, itf, interface_config);
  return 0;
}

bool modem_send_raw_unsolicited_response(uint8_t* alp_command, uint32_t length,
                                         alp_itf_id_t itf, void* interface_config) {
  if(!alloc_command())
    return -EBUSY;
  
  switch(itf) {
    case ALP_ITF_ID_D7ASP:
      alp_append_forward_action(&command.fifo, ALP_ITF_ID_D7ASP, (uint8_t *)interface_config, sizeof(d7ap_session_config_t));
      break;
    case ALP_ITF_ID_HOST:
      break;
    default:
      assert(false);
  }

  fifo_put(&command.fifo, alp_command, length);

  send(command.buffer, fifo_get_size(&command.fifo));
  return 0;
}