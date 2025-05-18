/* ======================== powerudp.h ======================== */
#ifndef POWERUDP_H
#define POWERUDP_H

#include <stdint.h>
#include <netinet/in.h>

#define PUDP_DATA_PORT    6001
#define PUDP_CFG_PORT     6000
#define PUDP_CFG_MC_ADDR "239.0.0.100"
#define PUDP_BASE_TO_MS   500
#define PUDP_MAX_RETRY    5

/* flags */
#define PUDP_F_ACK 0x1
#define PUDP_F_NAK 0x2
#define PUDP_F_CFG 0x4

/* expõe o socket UDP interno para join_multicast */
extern int udp_sock;


/* header */
typedef struct {
    uint32_t seq;
    uint8_t  flags;
    uint8_t  _pad[3];
} PUDPHeader;

/* dynamic config message */
typedef struct {
    uint32_t base_timeout_ms;
    uint8_t  max_retries;
    uint8_t  _pad[3];
} ConfigMessage;

/* register message */
typedef struct {
    char psk[32];
} RegisterMessage;

/* API */
int init_protocol_client(void);
int init_protocol_server(void);
void close_protocol(void);

int send_message(const char *dest_ip, const void *buf, int len);
int receive_message(void *buf, int buflen);
int inject_packet_loss(int pct);

/* extras for CLI synchronization */
int powerudp_pending_count(void);
int powerudp_last_event(uint32_t *seq, int *status);

#endif /* POWERUDP_H */
