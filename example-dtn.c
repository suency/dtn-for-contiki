/**
 * \file
 * \brief
 *     An example of using the Delay-Tolerant Networking module
 * \author
 *     Yiwei Chen <yiwei.chen.13@ucl.ac.uk>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contiki.h"
#include "dev/leds.h"
#include "button-sensors.h"
#include "net/rime.h"
#include "dtn.h"

#define FLASH_LED(l) {leds_on(l); clock_delay_msec(50); leds_off(l); clock_delay_msec(50);}

#define DTN_CHANNEL 128

void
dtn_recv(struct dtn_conn *c, const rimeaddr_t *from, uint16_t packetid)
{
  printf("[DTN] Message(id: %d) received from %02x:%02x: '%s'\n",
         packetid, from->u8[1], from->u8[0],
         (char *)packetbuf_dataptr());
}

const struct dtn_callbacks dtn_call = {dtn_recv};

PROCESS(main_process, "DTN Example Process");
AUTOSTART_PROCESSES(&main_process);
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(main_process, ev, data)
{
  static struct dtn_conn conn;
  static char msg[100];
  static int count = 0;
  static rimeaddr_t my_addr, dest_addr;
  static uint8_t power = DTN_POWER_MIN;
  
  PROCESS_EXITHANDLER(dtn_close(&conn););
  PROCESS_BEGIN();

  rimeaddr_copy(&my_addr, &rimeaddr_null);
  my_addr.u8[0] = 15;
  dtn_set_addr(&my_addr);
  
  rimeaddr_copy(&dest_addr, &rimeaddr_null);
  dest_addr.u8[0] = 9;
  
  dtn_set_power(power);
  dtn_open(&conn, DTN_CHANNEL, &dtn_call);
  
  while(1) {
    PROCESS_WAIT_EVENT();
    if (ev == sensors_event && data == &button_sensor) {
      count++;
      sprintf(msg, "YiweiChen-%d", count);
      printf("[DTN] Sending DTN message: %s\n", msg);
      packetbuf_copyfrom(msg, strlen(msg) + 1);
      dtn_send(&conn, &dest_addr);
      FLASH_LED(LEDS_GREEN);
    } else if (ev == sensors_event && data == &button2_sensor) {
      dest_addr.u8[0]++;
      if (dest_addr.u8[0] > 20) dest_addr.u8[0] = 0;
      printf("[DTN] Destination set to %02x:%02x.\n",
             dest_addr.u8[1], dest_addr.u8[0]);
      FLASH_LED(LEDS_BLUE);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/

