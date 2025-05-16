#ifndef POWERUDP_H
#define POWERUDP_H
#include <stdint.h>

/* -------- parâmetros fixos / multicast -------- */
#define PUDP_DATA_PORT     6001          /* dados fiáveis                 */
#define PUDP_CFG_PORT      6000          /* multicast ConfigMessage       */
#define PUDP_CFG_MC_ADDR "239.0.0.100"   /* grupo multicast de config     */

/* bits em PUDPHeader.flags */
#define PUDP_F_ACK 0x01
#define PUDP_F_NAK 0x02
#define PUDP_F_CFG 0x04

/* valores iniciais (podem mudar por ConfigMessage) */
#define PUDP_BASE_TO_MS 500
#define PUDP_MAX_RETRY   3

/* -------- cabeçalhos -------- */
typedef struct {
    uint32_t seq;
    uint8_t  flags;
    uint8_t  _rsv[3];
} __attribute__((packed)) PUDPHeader;

typedef struct {                     /* enviado em multicast */
    uint16_t base_timeout_ms;
    uint8_t  max_retries;
    uint8_t  _pad;
} __attribute__((packed)) ConfigMessage;

typedef struct {                     /* registo TCP */
    char psk[64];
} __attribute__((packed)) RegisterMessage;

/* -------- API -------- */
int  init_protocol_client(void);      /* bind a porto efémero     */
int  init_protocol_server(void);      /* bind fixo 6001           */
void close_protocol(void);

int  send_message(const char *dest_ip, const void *buf, int len);
int  receive_message(void *buf, int buflen);   /* payload, ACK/NACK/CFG internos */

int  inject_packet_loss(int pct);     /* 0-100 % perda Tx         */
#endif
