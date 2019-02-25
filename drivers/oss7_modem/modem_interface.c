#include <string.h>


#include "fifo.h"
#include "modem_interface.h"
#include "debug.h"
#include "errors.h"
#include "periph/uart.h"
#include "mutex.h"

#include "crc.h"

#include "log.h"


#define RX_BUFFER_SIZE 256

#define TX_FIFO_FLUSH_CHUNK_SIZE 10 // at a baudrate of 115200 this ensures completion within 1 ms
                                    // TODO baudrate dependent

static uart_t uart_handle;

static uint8_t rx_buffer[RX_BUFFER_SIZE];
static fifo_t rx_fifo;

#define DPRINT(...) printf(__VA_ARGS__)
#define DPRINT_DATA(...) // log_print_data(__VA_ARGS__)


#define SERIAL_FRAME_SYNC_BYTE 0xC0
#define SERIAL_FRAME_VERSION   0x00
#define SERIAL_FRAME_HEADER_SIZE 7
#define SERIAL_FRAME_SIZE 4
#define SERIAL_FRAME_COUNTER 2
#define SERIAL_FRAME_TYPE 3
#define SERIAL_FRAME_CRC1   5
#define SERIAL_FRAME_CRC2   6

#define MODEM_INTERFACE_TX_FIFO_SIZE 255
static uint8_t modem_interface_tx_buffer[MODEM_INTERFACE_TX_FIFO_SIZE];
static fifo_t modem_interface_tx_fifo;
static bool request_pending = false;

uint8_t header[SERIAL_FRAME_HEADER_SIZE];
static uint8_t payload_len = 0;
static uint8_t packet_up_counter = 0;
static uint8_t packet_down_counter = 0;
static uint32_t uart_state_pin; // TODO pin
static uint32_t target_uart_state_pin;

static bool modem_listen_uart_inited = false;
static bool parsed_header = false;

cmd_handler_t alp_handler;
cmd_handler_t ping_response_handler;
cmd_handler_t logging_handler;

static mutex_t rx_mutex = MUTEX_INIT_LOCKED;
// static uint8_t rx_buffer[RX_BUFFER_SIZE];
static char rx_thread_stack[THREAD_STACKSIZE_MAIN];


typedef enum {
  STATE_IDLE,
  STATE_REQ_START,
  STATE_REQ_WAIT,
  STATE_REQ_BUSY,
  STATE_RESP,
  STATE_RESP_PENDING_REQ
} state_t;

static state_t state = STATE_IDLE;

#define SWITCH_STATE(s) do { \
  state = s; \
  DPRINT("switch to %s\n", #s); \
} while(0)

static void process_rx_fifo(void);


/** @Brief Enable UART interface and UART interrupt
 *  @return void
 */
static void modem_interface_enable(void)
{
  DPRINT("uart enabled\n");
  uart_poweron(uart_handle);
  //uart_rx_interrupt_enable(uart);
  modem_listen_uart_inited = true;
}

/** @Brief disables UART interface
 *  @return void
 */
// static void modem_interface_disable(void)
// {
//   modem_listen_uart_inited = false;
//   uart_poweroff(uart_handle);
//   DPRINT("uart disabled @ %i",timer_get_counter_value());
// }

/** @brief Lets receiver know that
 *  all the data has been transfered
 *  @return void
 */
// static void release_receiver(void)
// {
// #ifdef PLATFORM_USE_MODEM_INTERRUPT_LINES
//   DPRINT("release receiver\n");
//   modem_interface_disable();
//   hw_gpio_clr(uart_state_pin);
// #endif
// }

/** @brief transmit data in fifo to UART
 *  @return void
 */
static void flush_modem_interface_tx_fifo(void *arg)
{
  (void)arg; // suppress unuesed warning
  uint8_t len = fifo_get_size(&modem_interface_tx_fifo);
  uint8_t buffer[MODEM_INTERFACE_TX_FIFO_SIZE];
  fifo_pop(&modem_interface_tx_fifo, buffer, len);
  uart_write(uart_handle, buffer, len);
  DPRINT("flush %i\n", len);
}

/** @Brief Keeps µC awake while receiving UART data
 *  @return void
 */
// static void modem_listen(void* arg)
// {
//   if(!modem_listen_uart_inited)
//   {
//     modem_interface_enable();
//     hw_gpio_set(uart_state_pin); //set interrupt gpio to indicate ready for data
//   }
//
//   // prevent the MCU to go back to stop mode by scheduling ourself again until pin goes low,
//   // to keep UART RX enabled
//   sched_post_task_prio(&modem_listen, MIN_PRIORITY, NULL);
// }


/** @Brief Schedules flush tx fifo when receiver is ready
 *  @return void
 */
// static void execute_state_machine(void)
// {
// #ifdef PLATFORM_USE_MODEM_INTERRUPT_LINES
//   switch(state) {
//     case STATE_RESP:
//       // response period completed, process the request
//       assert(!hw_gpio_get_in(target_uart_state_pin));
//       sched_post_task(&process_rx_fifo);
//       if(request_pending) {
//         SWITCH_STATE(STATE_RESP_PENDING_REQ);
//       } else {
//         SWITCH_STATE(STATE_IDLE);
//         hw_gpio_clr(uart_state_pin);
//         sched_cancel_task(&modem_listen);
//         modem_interface_disable();
//       }
//       break;
//     case STATE_IDLE:
//       if(hw_gpio_get_in(target_uart_state_pin)) {
//         // wake-up requested
//         SWITCH_STATE(STATE_RESP);
//         modem_listen(NULL);
//         break;
//       } else {
//         SWITCH_STATE(STATE_REQ_START);
//         // fall-through to STATE_REQ_START!
//       }
//     case STATE_REQ_START:
//       // TODO timeout
//       SWITCH_STATE(STATE_REQ_WAIT);
//       hw_gpio_set(uart_state_pin); // wake-up receiver
//       DPRINT("wake-up receiver\n");
//       sched_post_task(&execute_state_machine); // reschedule again to prevent sleeoping
//       // in principle we could go to sleep but this will cause pin to float, this can be improved later
//       break;
//     case STATE_REQ_WAIT:
//       if(hw_gpio_get_in(target_uart_state_pin)) {
//         // receiver active
//         SWITCH_STATE(STATE_REQ_BUSY);
//         // fall-through to STATE_REQ_BUSY!
//       } else {
//         // TODO timeout
//         sched_post_task(&execute_state_machine); // reschedule again to prevent sleeoping
//         // in principle we could go to sleep but this will cause pin to float, this can be improved later
//         break;
//       }
//     case STATE_REQ_BUSY:
//       if(request_pending) {
//         modem_interface_enable();
//         sched_post_task_prio(&flush_modem_interface_tx_fifo, MIN_PRIORITY, NULL);
//       } else {
//         SWITCH_STATE(STATE_IDLE);
//       }
//       break;
//     case STATE_RESP_PENDING_REQ:
//       assert(request_pending);
//       // response period completed, initiate pending request by switching to REQ_START
//       assert(!hw_gpio_get_in(target_uart_state_pin));
//       SWITCH_STATE(STATE_REQ_START);
//       sched_post_task(&execute_state_machine);
//       break;
//     default:
//       DPRINT("unexpected state %i\n", state);
//       assert(false);
//   }
// #endif
// }

/** @Brief Check package counter and crc
 *  @return void
 */
static bool verify_payload(fifo_t* bytes, uint8_t* header)
{
  uint8_t payload[header[SERIAL_FRAME_SIZE]];
  fifo_peek(bytes, (uint8_t*) &payload, 0, header[SERIAL_FRAME_SIZE]);

  //check for missing packages
  packet_down_counter++;
  if(header[SERIAL_FRAME_COUNTER]!=packet_down_counter)
  {
    //TODO consequence? (save total missing packages?)
    DPRINT("!!! missed packages: %i",(header[SERIAL_FRAME_COUNTER]-packet_down_counter));
    packet_down_counter=header[SERIAL_FRAME_COUNTER]; //reset package counter
  }

  DPRINT("RX HEADER: ");
  DPRINT_DATA(header, SERIAL_FRAME_HEADER_SIZE);
  DPRINT("RX PAYLOAD: ");
  DPRINT_DATA(payload, header[SERIAL_FRAME_SIZE]);

  uint16_t calculated_crc = crc_calculate(payload, header[SERIAL_FRAME_SIZE]);

  if(header[SERIAL_FRAME_CRC1]!=((calculated_crc >> 8) & 0x00FF) || header[SERIAL_FRAME_CRC2]!=(calculated_crc & 0x00FF))
  {
    //TODO consequence? (request repeat?)
    DPRINT("CRC incorrect!");
    return false;
  }
  else
    return true;
}

/** @Brief Processes received uart data
 * 1) Search for sync bytes (always)
 * 2) get header size and parse header
 * 3) Wait for correct # of bytes (length present in header)
 * 4) Execute crc check and check message counter
 * 5) send to corresponding service (alp, ping service, log service)
 *  @return void
 */
static void process_rx_fifo(void)
{
  if(!parsed_header)
  {
    if(fifo_get_size(&rx_fifo) > SERIAL_FRAME_HEADER_SIZE)
    {
        fifo_peek(&rx_fifo, header, 0, SERIAL_FRAME_HEADER_SIZE);

        if(header[0] != SERIAL_FRAME_SYNC_BYTE || header[1] != SERIAL_FRAME_VERSION)
        {
          fifo_skip(&rx_fifo, 1);
          DPRINT("skip\n");
          parsed_header = false;
          payload_len = 0;
          if(fifo_get_size(&rx_fifo) > SERIAL_FRAME_HEADER_SIZE)
            mutex_lock(&rx_mutex); // wait for more data

          return;
        }
        parsed_header = true;
        fifo_skip(&rx_fifo, SERIAL_FRAME_HEADER_SIZE);
        payload_len = header[SERIAL_FRAME_SIZE];
        DPRINT("UART RX, payload size = %i\n", payload_len);
        mutex_unlock(&rx_mutex); // implicit return, task will re-run to parse payload
    }
  }
  else
  {
    if(fifo_get_size(&rx_fifo) < payload_len) {
      return;
    }
    // payload complete, start parsing
    // rx_fifo can be bigger than the current serial packet, init a subview fifo
    // which is restricted to payload_len so we can't parse past this packet.
    fifo_t payload_fifo;
    fifo_init_subview(&payload_fifo, &rx_fifo, 0, payload_len);

    if(verify_payload(&payload_fifo,(uint8_t *)&header))
    {
      if(header[SERIAL_FRAME_TYPE]==SERIAL_MESSAGE_TYPE_ALP_DATA && alp_handler != NULL)
        alp_handler(&payload_fifo);
      else if (header[SERIAL_FRAME_TYPE]==SERIAL_MESSAGE_TYPE_PING_RESPONSE  && ping_response_handler != NULL)
        ping_response_handler(&payload_fifo);
      else if (header[SERIAL_FRAME_TYPE]==SERIAL_MESSAGE_TYPE_LOGGING && logging_handler != NULL)
        logging_handler(&payload_fifo);
      else if (header[SERIAL_FRAME_TYPE]==SERIAL_MESSAGE_TYPE_PING_REQUEST)
      {
        uint8_t ping_reply[1]={0x02};
        fifo_skip(&payload_fifo,1);
        modem_interface_transfer_bytes((uint8_t*) &ping_reply,1,SERIAL_MESSAGE_TYPE_PING_RESPONSE);
      }
      else
      {
        fifo_skip(&payload_fifo, payload_len);
        DPRINT("!!!FRAME TYPE NOT IMPLEMENTED: %i\n", header[SERIAL_FRAME_TYPE]);
      }
      fifo_skip(&rx_fifo, payload_len - fifo_get_size(&payload_fifo)); // pop parsed bytes from original fifo
    }
    else
    {
      DPRINT("!!!PAYLOAD DATA INCORRECT\n");
    }

    payload_len = 0;
    parsed_header = false;
    if(fifo_get_size(&rx_fifo) > SERIAL_FRAME_HEADER_SIZE)
      mutex_unlock(&rx_mutex); // implicit return, task will re-run
  }
}
/** @Brief put received UART data in fifo
 *  @return void
 */
static void uart_rx_cb(void * arg, uint8_t data)
{
    (void)arg; // suppress warning
    assert(fifo_put(&rx_fifo, &data, 1) == 0);

#ifndef PLATFORM_USE_MODEM_INTERRUPT_LINES
    mutex_unlock(&rx_mutex); // start processing thread
#endif
}

/** @Brief Processes events on UART interrupt line
 *  @return void
 */
// static void uart_int_cb(pin_id_t pin_id, uint8_t event_mask)
// {
//   // do not read GPIO level here in interrupt context (GPIO clock might not be enabled yet), execute state machine instead
//   sched_post_task(&execute_state_machine);
// }

// static void modem_interface_set_rx_interrupt_callback(uart_rx_inthandler_t uart_rx_cb) {
// #ifdef PLATFORM_USE_USB_CDC
// 	cdc_set_rx_interrupt_callback(uart_rx_cb);
// #else
//   uart_set_rx_interrupt_callback(uart, uart_rx_cb);
// #endif
// }

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


void modem_interface_init(uint8_t idx, uint32_t baudrate, uint32_t uart_state_int_pin, uint32_t target_uart_state_int_pin) // TODO pins
{
  fifo_init(&modem_interface_tx_fifo, modem_interface_tx_buffer, MODEM_INTERFACE_TX_FIFO_SIZE);
  // sched_register_task(&flush_modem_interface_tx_fifo);
  // sched_register_task(&execute_state_machine);
  // sched_register_task(&process_rx_fifo);
  state = STATE_IDLE;
  uart_state_pin = uart_state_int_pin;
  target_uart_state_pin = target_uart_state_int_pin;

  thread_create(rx_thread_stack, sizeof(rx_thread_stack), THREAD_PRIORITY_MAIN -1,
	 	0 , rx_thread , NULL, "oss7_modem_rx");


  uart_handle = UART_DEV(idx);
  uart_init(uart_handle, baudrate, &uart_rx_cb, NULL);

  fifo_init(&rx_fifo, rx_buffer, sizeof(rx_buffer));
  //modem_interface_set_rx_interrupt_callback(&uart_rx_cb);

#ifdef PLATFORM_USE_MODEM_INTERRUPT_LINES
  assert(sched_register_task(&modem_listen) == SUCCESS);
  DPRINT("using interrupt lines");
  assert(hw_gpio_configure_interrupt(target_uart_state_pin, &uart_int_cb, GPIO_RISING_EDGE | GPIO_FALLING_EDGE) == SUCCESS);
  assert(hw_gpio_enable_interrupt(target_uart_state_pin) == SUCCESS);
  if(hw_gpio_get_in(target_uart_state_pin))
  {
    sched_post_task(&modem_listen);
    DPRINT("Ready to receive (boot) @ %i",timer_get_counter_value());
  }
#endif

// When not using interrupt lines we keep uart enabled so we can use RX IRQ.
// If the platform has interrupt lines the UART should be re-enabled when handling the modem interrupt
#ifndef PLATFORM_USE_MODEM_INTERRUPT_LINES
  modem_interface_enable();
#endif
}

void modem_interface_transfer_bytes(uint8_t* bytes, uint8_t length, serial_message_type_t type)
{
  uint8_t header[SERIAL_FRAME_HEADER_SIZE];
  uint16_t crc=crc_calculate(bytes,length);

  packet_up_counter++;
  header[0] = SERIAL_FRAME_SYNC_BYTE;
  header[1] = SERIAL_FRAME_VERSION;

  header[SERIAL_FRAME_COUNTER] = packet_up_counter;
  header[SERIAL_FRAME_TYPE] = type;
  header[SERIAL_FRAME_SIZE] = length;
  header[SERIAL_FRAME_CRC1] = (crc >> 8) & 0x00FF;
  header[SERIAL_FRAME_CRC2] = crc & 0x00FF;

  DPRINT("TX HEADER:\n");
  DPRINT_DATA(header, SERIAL_FRAME_HEADER_SIZE);
  DPRINT("TX PAYLOAD: %i bytes\n", length);
  DPRINT_DATA(bytes, length);

  // start_atomic();
  request_pending = true;
  fifo_put(&modem_interface_tx_fifo, (uint8_t*) &header, SERIAL_FRAME_HEADER_SIZE);
  fifo_put(&modem_interface_tx_fifo, bytes, length);
  // end_atomic();

  flush_modem_interface_tx_fifo(NULL);
// #ifdef PLATFORM_USE_MODEM_INTERRUPT_LINES
//   sched_post_task_prio(&execute_state_machine, MIN_PRIORITY, NULL);
// #else
//   sched_post_task_prio(&flush_modem_interface_tx_fifo, MIN_PRIORITY, NULL); // state machine is not used when not using interrupt lines
// #endif
}

void modem_interface_transfer(char* string) {
  modem_interface_transfer_bytes((uint8_t*) string, strlen(string), SERIAL_MESSAGE_TYPE_LOGGING);
}


void modem_interface_register_handler(cmd_handler_t cmd_handler, serial_message_type_t type)
{
  if(type == SERIAL_MESSAGE_TYPE_ALP_DATA)
    alp_handler=cmd_handler;
  else if(type == SERIAL_MESSAGE_TYPE_PING_RESPONSE)
    ping_response_handler=cmd_handler;
  else if(type == SERIAL_MESSAGE_TYPE_LOGGING)
    logging_handler=cmd_handler;
  else
  {
    DPRINT("Modem interface callback not implemented\n");
  }
}
