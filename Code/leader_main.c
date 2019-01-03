/* Infrared IR/UART for election skill
   October 2018 -- Emily Lam
   RMT Pulse          -- pin 26 -- A0
   UART Transmitter   -- pin 25 -- A1
   Receiver           -- pin 34 -- A2
   Hardware interrupt -- pin 4 - A5
   Red LED            -- pin 33
   Green LED          -- pin 32
   Blue LED           -- Pin 14
*/

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt.h"
#include "soc/rmt_reg.h"
#include "driver/uart.h"
#include "driver/periph_ctrl.h"

// RMT definitions
#define RMT_TX_CHANNEL    1     // RMT channel for transmitter
#define RMT_TX_GPIO_NUM   25    // GPIO number for transmitter signal -- A1
#define RMT_CLK_DIV       100   // RMT counter clock divider
#define RMT_TICK_10_US    (80000000/RMT_CLK_DIV/100000)   // RMT counter value for 10 us.(Source clock is APB clock)
#define rmt_item32_tIMEOUT_US   9500     // RMT receiver timeout value(us)

// UART definitions
#define UART_TX_GPIO_NUM 26 // A0
#define UART_RX_GPIO_NUM 34 // A2
#define BUF_SIZE (1024)

// Hardware interrupt definitions
#define GPIO_INPUT_IO_1       4
#define ESP_INTR_FLAG_DEFAULT 0
#define GPIO_INPUT_PIN_SEL    1ULL<<GPIO_INPUT_IO_1

// LED Output pins definitions
#define BLUEPIN   32
#define GREENPIN  15
#define REDPIN    33

// Default ID
#define ID 2

// Variables for my ID, minVal and status plus received ID, minVal, and status
char start = 0x0A;
char myID = (char) ID;
char minVal = (char) ID;
char status = 'U';
char rxID;
char rxMinVal;
char rxStatus;
int len_out = 4;

// Semaphores (for signaling), Mutex (for resources), and Queues (for button)
SemaphoreHandle_t xSemaphore = NULL;
SemaphoreHandle_t mux = NULL;
static xQueueHandle gpio_evt_queue = NULL;

// Button interrupt handler -- add to queue
static void IRAM_ATTR gpio_isr_handler(void* arg){
  uint32_t gpio_num = (uint32_t) arg;
  xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

// RMT tx init
static void rmt_tx_init() {
    rmt_config_t rmt_tx;
    rmt_tx.channel = RMT_TX_CHANNEL;
    rmt_tx.gpio_num = RMT_TX_GPIO_NUM;
    rmt_tx.mem_block_num = 1;
    rmt_tx.clk_div = RMT_CLK_DIV;
    rmt_tx.tx_config.loop_en = false;
    rmt_tx.tx_config.carrier_duty_percent = 50;
    // Carrier Frequency of the IR receiver
    rmt_tx.tx_config.carrier_freq_hz = 38000;
    rmt_tx.tx_config.carrier_level = 1;
    rmt_tx.tx_config.carrier_en = 1;
    // Never idle -> aka ontinuous TX of 38kHz pulses
    rmt_tx.tx_config.idle_level = 1;
    rmt_tx.tx_config.idle_output_en = true;
    rmt_tx.rmt_mode = 0;
    rmt_config(&rmt_tx);
    rmt_driver_install(rmt_tx.channel, 0, 0);
}

// Configure UART
static void uart_init() {
  // Basic configs
  uart_config_t uart_config = {
      .baud_rate = 2400, // Slow BAUD rate
      .data_bits = UART_DATA_8_BITS,
      .parity    = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
  };
  uart_param_config(UART_NUM_1, &uart_config);

  // Set UART pins using UART0 default pins
  uart_set_pin(UART_NUM_1, UART_TX_GPIO_NUM, UART_RX_GPIO_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

  // Reverse receive logic line
  uart_set_line_inverse(UART_NUM_1,UART_INVERSE_RXD);

  // Install UART driver
  uart_driver_install(UART_NUM_1, BUF_SIZE * 2, 0, 0, NULL, 0);
}

// GPIO init for LEDs
static void led_init() {
    gpio_pad_select_gpio(BLUEPIN);
    gpio_pad_select_gpio(GREENPIN);
    gpio_pad_select_gpio(REDPIN);
    gpio_set_direction(BLUEPIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(GREENPIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(REDPIN, GPIO_MODE_OUTPUT);
}

// Button interrupt init
static void hw_int_init() {
    gpio_config_t io_conf;
    //interrupt of rising edge
    io_conf.intr_type = GPIO_PIN_INTR_POSEDGE;
    //bit mask of the pins, use GPIO4 here
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
    gpio_intr_enable(GPIO_INPUT_IO_1 );
    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL3);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_INPUT_IO_1, gpio_isr_handler, (void*) GPIO_INPUT_IO_1);
    //create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    //start gpio task
}

// Button task -- reset minVal and status
void button_task(){
  uint32_t io_num;
  while(1) {
    if(xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
      xSemaphoreTake(mux, portMAX_DELAY);
      minVal = myID;
      status = 'U';
      xSemaphoreGive(mux);
      printf("Button pressed.\n");
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

}

// Send task -- sends payload | Start | myID | minVal | status
void send_task(){
  while(1) {

    char *data_out = (char *) malloc(len_out);
    xSemaphoreTake(mux, portMAX_DELAY);
    data_out[0] = start;
    data_out[1] = myID;
    data_out[2] = minVal;
    data_out[3] = status;
    uart_write_bytes(UART_NUM_1, data_out, len_out+1);
    // printf("Sent: 0x%02X\n",data_out[0]);
    // printf("Sent: %d\n",data_out[1]);
    // printf("Sent: %d\n",data_out[2]);
    // printf("Sent: %c\n",data_out[3]);
    xSemaphoreGive(mux);

    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// Receives task -- looks for Start byte then stores received values
void recv_task(){
  // Buffer for input data
  uint8_t *data_in = (uint8_t *) malloc(BUF_SIZE);
  while (1) {
    int len_in = uart_read_bytes(UART_NUM_1, data_in, BUF_SIZE, 20 / portTICK_RATE_MS);
    if (len_in >0) {
      for (int i=0; i < 24; i++) {
        if (data_in[i] == 0x0A) {

          rxID = data_in[i+1];
          rxMinVal = data_in[i+2];
          rxStatus = data_in[i+3];

          printf("Received comm from ID 0x%02X with min value, 0x%02X, and status, %c\n", rxID, rxMinVal, rxStatus);

          // Signal to election
          xSemaphoreGive( xSemaphore );
          break;
        }
      }
    }
    else{
      printf("Nothing received.\n");
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
  free(data_in);
}

// Election task --> write state machine here
void election_task(){
  while(1) {
    if( xSemaphore != NULL ) {
     // See if we can obtain the semaphore.  If the semaphore is not available
     // wait 10 ticks to see if it becomes free.
     if( xSemaphoreTake( xSemaphore, ( TickType_t ) 10 ) == pdTRUE )
     {
         // We were able to obtain the semaphore and can now access the
         // shared resource.
         // Election code goes here

        switch (status) {
            case 'U' : // You are Unassigned

                if (myID < rxID && rxID != 0) {
                status = 'L';
                } else if (myID > rxID && rxID != 0) {
                    status = 'F';
                    minVal = rxMinVal;
                } else {
                    status = 'U';
                }

                break;

            case 'L' : // You are a Leader

                switch(rxStatus) {

                    case 'U' :
                        if (myID < rxID) {
                        status = 'L';
                        } else if (myID > rxID) {
                            status = 'F';
                        } else {
                            status = 'S'; // equal IDs, signal there is an error going on.
                        }  
                        break;

                    case 'L' :
                        if (minVal > rxMinVal){
                            status = 'F';
                            minVal = rxMinVal;
                        } else {
                            status = 'S';
                        }
                        break;

                    case 'F' :
                        if (minVal > rxMinVal) {
                            status = 'F';
                            minVal = rxMinVal;
                        } else {
                            status = 'S';
                        }
                        break;

                }

                break;

            case 'F' : // You are a Follower

                switch(rxStatus) {

                    case 'U' :

                        if (myID < rxID) {
                        status = 'L';
                        } else if (myID > rxID) {
                            status = 'F';
                        } else {
                            status = 'S'; // equal IDs, signal there is an error going on.
                        }  
                        break;

                    case 'L' :
                        break;

                    case 'F' :
                        if (minVal > rxMinVal) {
                            minVal = rxMinVal;
                        }
                        break;

                }

                break;
        }



         printf("Election!\n");


     }
     else{
         // We could not obtain the semaphore and can therefore not access
         // the shared resource safely.
     }
     vTaskDelay(100 / portTICK_PERIOD_MS);
   }
 }
}

// LED task to light LED based on status
void led_task(){
  while(1) {
    switch(status){
      case 'U' :
        gpio_set_level(GREENPIN, 0);
        gpio_set_level(REDPIN, 1);
        gpio_set_level(BLUEPIN, 0);
        // printf("Current state: %c\n",status);
        break;
      case 'L' :
        gpio_set_level(GREENPIN, 0);
        gpio_set_level(REDPIN, 0);
        gpio_set_level(BLUEPIN, 1);
        // printf("Current state: %c\n",status);
        break;
      case 'F' :
        gpio_set_level(GREENPIN, 1);
        gpio_set_level(REDPIN, 0);
        gpio_set_level(BLUEPIN, 0);
        // printf("Current state: %c\n",status);
        break;
      case 'S' :  // error state
        gpio_set_level(GREENPIN, 1);
        gpio_set_level(REDPIN, 1);
        gpio_set_level(BLUEPIN, 1);
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void app_main() {

    // Create the semaphore to signal
    vSemaphoreCreateBinary( xSemaphore );
    // Mutex for current values when sending and during election
    mux = xSemaphoreCreateMutex();

    // Initialize transmitt and button interrupt
    rmt_tx_init();
    uart_init();
    led_init();
    hw_int_init();

    // Create tasks for receive, send, elect, set gpio, and button
    xTaskCreate(recv_task, "uart_rx_task", 1024*4, NULL, configMAX_PRIORITIES, NULL);
    xTaskCreate(send_task, "uart_tx_task", 1024*2, NULL, configMAX_PRIORITIES, NULL);
    xTaskCreate(election_task, "elect_task", 1024*4, NULL, configMAX_PRIORITIES-2, NULL);
    xTaskCreate(led_task, "set_gpio_task", 1024*2, NULL, configMAX_PRIORITIES-3, NULL);
    xTaskCreate(button_task, "button_task", 1024*2, NULL, configMAX_PRIORITIES-3, NULL);

}
