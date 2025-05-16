#ifndef POWERUDP_H
#define POWERUDP_H
#include <stdint.h>

/* ----- parâmetros do protocolo ----- */
#define PUDP_DATA_PORT 6001          /* porto UDP fixo */

#define PUDP_F_ACK 0x01              /* bit flags */
#define PUDP_F_NAK 0x02

#define PUDP_BASE_TO_MS 500          /* timeout base retransmissão */
#define PUDP_MAX_RETRY   3

/* ----- cabeçalhos de controlo ----- */
typedef struct {
    uint32_t seq;
    uint8_t  flags;
    uint8_t  _rsv[3];
} __attribute__((packed)) PUDPHeader;

typedef struct {                     /* para futura config dinâmica */
    uint8_t  enable_retx;
    uint8_t  enable_backoff;
    uint8_t  enable_seq;
    uint16_t base_timeout;
    uint8_t  max_retries;
} __attribute__((packed)) ConfigMessage;

typedef struct {                     /* registo TCP cliente→servidor */
    char psk[64];
} __attribute__((packed)) RegisterMessage;

/* ----- API mínima ----- */
int  init_protocol_client(void);   /* bind a porto aleatório           */
int  init_protocol_server(void);   /* bind fixo em PUDP_DATA_PORT=6001 */
void close_protocol(void);

int  send_message(const char *dest_ip, const void *buf, int len);
int  receive_message(void *buf, int buflen);

/* ------------- SIMULADOR DE PERDAS ---------------- */
/* Define a percentagem de pacotes a descartar (0-100).  
   Devolve 0 se aceitável, –1 se valor fora dos limites. */
int  inject_packet_loss(int probability_pct);

#endif /* POWERUDP_H */
