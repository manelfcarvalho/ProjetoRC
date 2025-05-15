#ifndef POWERUDP_H
#define POWERUDP_H
#include <stdint.h>

/* porto UDP usado para os dados PowerUDP */
#define PUDP_DATA_PORT 6001

/*-------------------------------------------------- cabeçalhos */
typedef struct {
    uint32_t seq;   /* número de sequência               */
    uint8_t  flags; /* bit0=ACK bit1=NAK bit2=CFG        */
    uint8_t  _rsv[3];
} __attribute__((packed)) PUDPHeader;

typedef struct {
    uint8_t  enable_retx;
    uint8_t  enable_backoff;
    uint8_t  enable_seq;
    uint16_t base_timeout;   /* ms */
    uint8_t  max_retries;
} __attribute__((packed)) ConfigMessage;

typedef struct {
    char psk[64];
} __attribute__((packed)) RegisterMessage;

/*-------------------------------------------------- API pública */
int  init_protocol(const char *server_ip, int tcp_port, const char *psk);
void close_protocol(void);

int  send_message(const char *dest_ip, const void *buf, int len);
int  receive_message(void *buf, int buflen);   /* bloqueia  (TODO: opc) */

#endif /* POWERUDP_H */
