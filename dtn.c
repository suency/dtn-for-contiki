/**
 * \addtogroup dtn
 * @{
 * \file
 * \brief
 *     Source file for the \ref dtn module
 * \author
 *     Yiwei Chen <yiwei.chen.13@ucl.ac.uk>
 */
#include "dtn.h"

#include <string.h>
#include <stddef.h>
#include "net/rime.h"

#define _DEBUG 1

#if _DEBUG
#define PRINTF(fmt,...) printf(fmt,##__VA_ARGS__)
#define PRINTADDR(addr) printf("%02x:%02x", (addr)->u8[1], (addr)->u8[0])
#else
#define PRINTF(fmt,...)
#define PRINTADDR(addr)
#endif

#define DTN_PENDING (void*)(0)
#define DTN_READY (void*)(1)

struct dtn_hdr * dtn_buf_ptr(void);

/*-MESSAGE QUEUE-------------------------------------------------------------*/
PACKETQUEUE(dtn_packetqueue, DTN_QUEUE_MAX);
/*-DEBUG FUNCTIONS-----------------------------------------------------------*/
void
print_packetbuf(char * func)
{
  PRINTF("%s: total length %d\n", func, packetbuf_totlen());
  struct dtn_hdr *hdrptr = dtn_buf_ptr();
  PRINTF("    packetbuf_hdr: {l: %d", hdrptr->num_copies);
  PRINTF(", es: ");
  PRINTADDR(&(hdrptr->esender));
  PRINTF(", er: ");
  PRINTADDR(&(hdrptr->ereceiver));
  PRINTF(", ep: %d}\n", hdrptr->epacketid);
  PRINTF("    packetbuf_data: {len: %d, s: %s}\n",
         packetbuf_totlen() - sizeof(struct dtn_hdr),
         hdrptr + 1);
}
/*-LOCAL FUNCTIONS-----------------------------------------------------------*/
struct dtn_hdr *
dtn_buf_ptr(void)
{
  struct dtn_hdr *hdrptr;
  if (packetbuf_hdrlen() > 0) hdrptr = (struct dtn_hdr *)packetbuf_hdrptr();
  else (struct dtn_hdr *)packetbuf_dataptr();
  return hdrptr;
}
/*---------------------------------------------------------------------------*/
void
dtn_queue_spray(void *ptr)
{
  struct dtn_conn *c = (struct dtn_conn *)ptr;
  PRINTF("dtn_queue_spray: Spraying, queue length: %d\n", packetqueue_len(c->q));
  if (packetqueue_len(c->q) <= 0) {
    PRINTF("dtn_queue_spray: Empty packetqueue, nothing to spray.\n");
    PRINTF("dtn_queue_spray: Stopped spraying.\n");
    return;
  }
  struct packetqueue_item *q_item = packetqueue_first(c->q);
  while (q_item) {
    if (q_item->ptr != DTN_READY) {
      PRINTF("dtn_queue_spray: packet still pending, skip.");
      q_item = q_item->next;
      continue;
    }
    struct dtn_hdr *qbufdata = (struct dtn_hdr *)
                               queuebuf_dataptr(packetqueue_queuebuf(q_item));
    if (qbufdata == NULL) {
      q_item = q_item->next;
      continue;
    }
    if (qbufdata->num_copies == 0) {
      q_item = q_item->next;
      continue;
    }
    queuebuf_to_packetbuf(packetqueue_queuebuf(q_item));
    print_packetbuf("dtn_queue_spray");
    broadcast_send(&c->spray_c);
    PRINTF("dtn_queue_spray: broadcast Spray sent.\n");
    q_item = q_item->next;
  }
  PRINTF("dtn_queue_spray: Paused spraying.\n");
  if (ctimer_expired(&c->spray_ct)){
    ctimer_set(&c->spray_ct,
               DTN_SPRAY_DELAY * CLOCK_SECOND,
               dtn_queue_spray, (void *)c);
    PRINTF("dtn_queue_spray: Timer started.\n");
  }
}
/*---------------------------------------------------------------------------*/
struct packetqueue_item *
dtn_queue_find(struct dtn_conn *c)
{
  struct dtn_hdr *bufdata = dtn_buf_ptr();
  struct packetqueue_item *q_item = packetqueue_first(c->q);
  while (q_item) {
    struct dtn_hdr *qbufdata = (struct dtn_hdr *)
                               queuebuf_dataptr(packetqueue_queuebuf(q_item));
    if (qbufdata == NULL) return NULL;
    if (rimeaddr_cmp(&(bufdata->esender), &(qbufdata->esender))
        && bufdata->epacketid == qbufdata->epacketid) {
      return q_item;
    }
    q_item = q_item->next;
  }
  return NULL;
}
/*---------------------------------------------------------------------------*/
int
dtn_valid_hdr()
{
  struct dtn_hdr *bufdata = dtn_buf_ptr();
  if (bufdata->version == DTN_VERSION
          && bufdata->magic[0] == 'S'
          && bufdata->magic[1] == 'W') {
    return 1;
  } else {
    print_packetbuf("dtn_valid_hdr");
    PRINTF("dtn_valid_hdr: packet invalid.\n");
    return 0;
  }
}
/*-SPRAY---------------------------------------------------------------------*/
void
dtn_spray_recv(struct broadcast_conn *b_c, const rimeaddr_t *from)
{
  PRINTF("dtn_spray_recv: broadcast received from %02x:%02x.\n",
         from->u8[1], from->u8[0]);
  if (!dtn_valid_hdr()) return;
  struct dtn_conn *c = (struct dtn_conn *)
                       ((void *)b_c - offsetof(struct dtn_conn, spray_c));
  print_packetbuf("dtn_spray_recv");
  struct packetqueue_item * item;
  if (item = dtn_queue_find(c)) {
    if (item->ptr == DTN_READY) {
      PRINTF("dtn_spray_recv: Spray in the queue and ready, do nothing.\n");
    } else {
      unicast_send(&c->request_c, from);
      PRINTF("dtn_spray_recv: Spray in the queue but pending, unicast Request sent.\n");
    }
    return;
  }
  struct dtn_hdr *bufdata = dtn_buf_ptr();
  if (rimeaddr_cmp(&(bufdata->ereceiver), &rimeaddr_node_addr)) { //for me
    PRINTF("dtn_spray_recv: Spray message is to me, invoking callback.\n");
    unicast_send(&c->request_c, from);
    PRINTF("dtn_spray_recv: unicast Request confirmation sent.\n");
    static struct dtn_hdr recv_hdr;
    memcpy(&recv_hdr, bufdata, sizeof (struct dtn_hdr));
    packetbuf_hdrreduce(sizeof(struct dtn_hdr));
    c->cb->recv(c, &(recv_hdr.esender), recv_hdr.epacketid);
    return;
  }
  if (bufdata->num_copies == 1) return; //not for me and only one copy
  bufdata->num_copies = 0;
  if (packetqueue_enqueue_packetbuf(c->q,
                                    DTN_MAX_LIFETIME * CLOCK_SECOND,
                                    DTN_PENDING)) {
    PRINTF("dtn_spray_recv: Enqueued (pending) successfully.\n");
    unicast_send(&c->request_c, from);
    PRINTF("dtn_spray_recv: unicast Request sent.\n");
  } else {
    PRINTF("dtn_spray_recv: Failed to enqueue.\n");
  }
}
/*---------------------------------------------------------------------------*/
const struct broadcast_callbacks dtn_spray_call = {dtn_spray_recv};
/*-REQUEST-------------------------------------------------------------------*/
void
dtn_request_recv(struct unicast_conn *u_c, const rimeaddr_t *from)
{
  PRINTF("dtn_request_recv: unicast received from %02x:%02x\n",
         from->u8[1], from->u8[0]);
  if (!dtn_valid_hdr()) return;
  struct dtn_conn *c = (struct dtn_conn *)
                       ((void *)u_c - offsetof(struct dtn_conn, request_c));
  print_packetbuf("dtn_request_recv");
  struct packetqueue_item * q_item;
  if (q_item = dtn_queue_find(c)) {
    if (q_item->ptr != DTN_READY) {
      PRINTF("dtn_request_recv: Request in the queue, but still pending, do nothing.");
      return;
    }
    PRINTF("dtn_request_recv: Request found in the queue.\n");
    struct dtn_hdr *qbufdata = queuebuf_dataptr(packetqueue_queuebuf(q_item));
    if (rimeaddr_cmp(&(qbufdata->ereceiver), from)) {
      qbufdata->num_copies = 0;
      PRINTF("dtn_request_recv: receiver got message, set L to 0.\n");
      return;
    }
    if ((qbufdata->num_copies == 1)
        && !rimeaddr_cmp(from, &(qbufdata->ereceiver))) {
      PRINTF("dtn_request_recv: L == 1, and from != ereceiver, do nothing.\n");
      return;
    }
    if (qbufdata->num_copies == 0) {
      PRINTF("dtn_request_recv: L == 0, do nothing.\n");
      return;
    }
    queuebuf_to_packetbuf(packetqueue_queuebuf(q_item));
    struct dtn_hdr *bufdata = dtn_buf_ptr();
    bufdata->num_copies /= 2;
    if (c->handoff_qb != NULL) {
      PRINTF("dtn_request_recv: Another HandOff in progress, failed.");
      return;
    }
    c->handoff_qb = (struct dtn_hdr *)
                    queuebuf_dataptr(packetqueue_queuebuf(q_item));
    memcpy(&(c->handoff_hdr), c->handoff_qb, sizeof(struct dtn_hdr));
    runicast_send(&c->handoff_c, from, DTN_RTX);
    PRINTF("dtn_request_recv: runicast HandOff(L=%d) sent.\n", bufdata->num_copies);
  } else {
    PRINTF("dtn_request_recv: Request not in the queue, do nothing.\n");
  }
}
/*---------------------------------------------------------------------------*/
const struct unicast_callbacks dtn_request_call = {dtn_request_recv};
/*-HANDOFF-------------------------------------------------------------------*/
void
dtn_handoff_recv(struct runicast_conn *r_c, const rimeaddr_t *from, uint8_t seqno)
{
  PRINTF("dtn_handoff_recv: runicast received from %02x:%02x, seqno %d\n",
         from->u8[1], from->u8[0], seqno);
  if (!dtn_valid_hdr()) return;
  struct dtn_conn *c = (struct dtn_conn *)
                       ((void *)r_c - offsetof(struct dtn_conn, handoff_c));
  struct packetqueue_item * q_item;
  if (q_item = dtn_queue_find(c)) {
    if (q_item->ptr != DTN_READY) {
      PRINTF("dtn_handoff_recv: HandOff in the queue, but still pending, do nothing.");
      return;
    }
    PRINTF("dtn_handoff_recv: HandOff found in the queue.\n");
    struct dtn_hdr *qbufdata = (struct dtn_hdr *)
                               queuebuf_dataptr(packetqueue_queuebuf(q_item));
    struct dtn_hdr *bufdata = dtn_buf_ptr();
    qbufdata->num_copies += bufdata->num_copies;
    if (qbufdata->num_copies > DTN_L_COPIES) {
      qbufdata->num_copies = DTN_L_COPIES;
    }
    PRINTF("dtn_handoff_recv: HandOff(L=%d) received and processed.\n", bufdata->num_copies);
    dtn_queue_spray((void *)c);
  } else {
    PRINTF("dtn_handoff_recv: HandOff not in the queue, do nothing.\n");
  }
}
/*---------------------------------------------------------------------------*/
void
dtn_handoff_sent(struct runicast_conn *r_c, const rimeaddr_t *to,
                 uint8_t retransmissions)
{
  PRINTF("dtn_handoff_sent: runicast sent to %02x:%02x, retried %d\n",
         to->u8[1], to->u8[0], retransmissions);
  struct dtn_conn *c = (struct dtn_conn *)
                       ((void *)r_c - offsetof(struct dtn_conn, handoff_c));
  if (rimeaddr_cmp(&(c->handoff_qb->esender), &(c->handoff_hdr.esender))
      && c->handoff_qb->epacketid == c->handoff_hdr.epacketid) {
    c->handoff_qb->num_copies = c->handoff_qb->num_copies
                              - c->handoff_qb->num_copies / 2;
    PRINTF("dtn_handoff_sent: HandOff processed.\n");
  } else {
    PRINTF("dtn_handoff_sent: queuebuf not matched (expired), HandOff not processed.\n");
  }
  c->handoff_qb = NULL;
}
/*---------------------------------------------------------------------------*/
void
dtn_handoff_timedout(struct runicast_conn *r_c, const rimeaddr_t *to,
                     uint8_t retransmissions)
{
  PRINTF("dtn_handoff_timedout: runicast timed out, to %02x:%02x, retried %d\n",
         to->u8[1], to->u8[0], retransmissions);
  struct dtn_conn *c = (struct dtn_conn *)
                       ((void *)r_c - offsetof(struct dtn_conn, handoff_c));
  c->handoff_qb = NULL;
  PRINTF("dtn_handoff_sent: HandOff failed.\n");
}
/*---------------------------------------------------------------------------*/
const struct runicast_callbacks dtn_handoff_call = {dtn_handoff_recv,
                                                    dtn_handoff_sent,
                                                    dtn_handoff_timedout};
/*-DTN CALLS-----------------------------------------------------------------*/
void
dtn_open(struct dtn_conn *c, uint16_t dtn_channel,
         const struct dtn_callbacks *cb)
{
  packetqueue_init(&dtn_packetqueue);
  c->q = &dtn_packetqueue;
  c->seqno = 0;
  c->cb = cb;
  c->handoff_qb = NULL;
  broadcast_open(&c->spray_c, dtn_channel, &dtn_spray_call);
  unicast_open(&c->request_c, dtn_channel + 1, &dtn_request_call);
  runicast_open(&c->handoff_c, dtn_channel + 2, &dtn_handoff_call);
  PRINTF("dtn_open: DTN connection opened at channel (%d, %d, %d).\n",
         dtn_channel, dtn_channel + 1, dtn_channel + 2);
}
/*---------------------------------------------------------------------------*/
void
dtn_close(struct dtn_conn *c)
{
  broadcast_close(&c->spray_c);
  unicast_close(&c->request_c);
  runicast_close(&c->handoff_c);
  PRINTF("dtn_close: DTN closed.");
}
/*---------------------------------------------------------------------------*/
void
dtn_send(struct dtn_conn *c, const rimeaddr_t *to)
{
  struct dtn_hdr hdr;
  hdr.version = DTN_VERSION;
  hdr.magic[0] = 'S';
  hdr.magic[1] = 'W';
  hdr.num_copies = DTN_L_COPIES;
  hdr.epacketid = c->seqno;
  c->seqno++;
  rimeaddr_copy(&hdr.ereceiver, to);
  rimeaddr_copy(&hdr.esender, &rimeaddr_node_addr);
  packetbuf_hdralloc(sizeof(struct dtn_hdr));
  memcpy(packetbuf_hdrptr(), &hdr, sizeof(struct dtn_hdr));
  print_packetbuf("dtn_send");
  if (packetqueue_enqueue_packetbuf(c->q,
                                    DTN_MAX_LIFETIME * CLOCK_SECOND,
                                    DTN_READY)) { //Hand-offed
    PRINTF("dtn_send: Enqueued successfully.\n");
    dtn_queue_spray((void *)c);
  } else {
    PRINTF("dtn_send: Failed to enqueue.\n");
  }
}
/*---------------------------------------------------------------------------*/
void
dtn_set_power(uint8_t power)
{
  set_power(power);
  PRINTF("dtn_set_power: Power set to 0x%02x.\n", power);
}
/*---------------------------------------------------------------------------*/
void
dtn_set_addr(rimeaddr_t *addr)
{
  rimeaddr_set_node_addr(addr);
  PRINTF("dtn_set_addr: Local address set to ");
  PRINTADDR(addr);
  PRINTF(".\n");
}
/*---------------------------------------------------------------------------*/
/** }@ */
