/* ======================== powerudp.c ======================== */
#define _POSIX_C_SOURCE 200809L
#include "powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <time.h>

#define MAX_PENDING 32
#define MAX_PAYLOAD 512

typedef struct {
    uint32_t            seq;
    int                 len;
    char                data[sizeof(PUDPHeader) + MAX_PAYLOAD];
    struct timeval      ts;
    uint32_t            to_ms;
    int                 retries;
    struct sockaddr_in  dst;
    int                 in_use;
} Pending;

/* pending entries */
static Pending          pend[MAX_PENDING];
static pthread_mutex_t  pend_mtx          = PTHREAD_MUTEX_INITIALIZER;

/* UDP socket */
int udp_sock = -1;


/* sequencing & timeouts */
static uint32_t         seq_tx            = 1;
static uint32_t         expected_seq      = 1;
static uint32_t         base_timeout_ms   = PUDP_BASE_TO_MS;
static uint8_t          max_retries       = PUDP_MAX_RETRY;
static int              drop_probability  = 0;

/* last event for CLI sync */
static int      last_evt_status = 0;  /* 1=ACK, -1=DROP */
static uint32_t last_evt_seq    = 0;

/* get current time in ms */
static uint32_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* apply multicast config */
static void apply_config(const ConfigMessage *cfg) {
    base_timeout_ms = cfg->base_timeout_ms;
    max_retries     = cfg->max_retries;
    printf("[PUDP] NEW CONFIG  timeout=%u ms  max_rtx=%u\n",
           base_timeout_ms, max_retries);
}

/* retransmission loop */
static void *retrans_loop(void *arg) {
    (void)arg;
    while (1) {
        pthread_mutex_lock(&pend_mtx);
        uint32_t now = now_ms();
        for (int i = 0; i < MAX_PENDING; ++i) {
            if (!pend[i].in_use) continue;
            uint32_t sent_ms = pend[i].ts.tv_sec * 1000 + pend[i].ts.tv_usec / 1000;
            uint32_t age = now - sent_ms;
            if (age < pend[i].to_ms) continue;

            /* exceeded timeout */
            if (pend[i].retries >= max_retries) {
                fprintf(stderr,
                    "[PUDP] seq %u DROPPED after %d tries\n",
                    pend[i].seq, pend[i].retries);
                last_evt_status = -1;
                last_evt_seq    = pend[i].seq;
                pend[i].in_use = 0;
                continue;
            }

            /* retransmit */
            sendto(udp_sock, pend[i].data, pend[i].len, 0,
                   (struct sockaddr*)&pend[i].dst, sizeof pend[i].dst);
            gettimeofday(&pend[i].ts, NULL);
            pend[i].retries++;
            pend[i].to_ms *= 2;
            fprintf(stderr,
                "[PUDP] retrans seq=%u (try %d, to %ums)\n",
                pend[i].seq, pend[i].retries, pend[i].to_ms);
        }
        pthread_mutex_unlock(&pend_mtx);
        /* sleep 100ms */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* add a new pending entry */
static void add_pending(uint32_t seq,
                        const char *frame, int len,
                        const struct sockaddr_in *dst) {
    for (int i = 0; i < MAX_PENDING; ++i) {
        if (!pend[i].in_use) {
            pend[i].seq     = seq;
            pend[i].len     = len;
            memcpy(pend[i].data, frame, len);
            pend[i].dst     = *dst;
            gettimeofday(&pend[i].ts, NULL);
            pend[i].to_ms   = base_timeout_ms;
            pend[i].retries = 0;
            pend[i].in_use  = 1;
            return;
        }
    }
}

/* acknowledge a pending entry */
static void ack_pending(uint32_t seq) {
    for (int i = 0; i < MAX_PENDING; ++i) {
        if (pend[i].in_use && pend[i].seq == seq) {
            pend[i].in_use = 0;
            last_evt_status = 1;
            last_evt_seq    = seq;
            return;
        }
    }
}

/* instant retransmit on NAK */
static int resend_now(uint32_t seq) {
    for (int i = 0; i < MAX_PENDING; ++i) {
        if (pend[i].in_use && pend[i].seq == seq) {
            sendto(udp_sock, pend[i].data, pend[i].len, 0,
                   (struct sockaddr*)&pend[i].dst, sizeof pend[i].dst);
            gettimeofday(&pend[i].ts, NULL);
            fprintf(stderr,
                "[PUDP] NACK → instant retrans seq=%u\n", seq);
            return 0;
        }
    }
    return -1;
}

/* common UDP initialization */
static int common_udp_init(uint16_t port) {
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) return -1;
    struct sockaddr_in a = {
        .sin_family = AF_INET,
        .sin_port   = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };
    if (bind(udp_sock, (struct sockaddr*)&a, sizeof a) < 0) return -1;
    /* start retransmission thread */
    pthread_t th;
    pthread_create(&th, NULL, retrans_loop, NULL);
    pthread_detach(th);
    return 0;
}

/* API implementations */

int init_protocol_client(void) {
    return common_udp_init(0);
}

int init_protocol_server(void) {
    return common_udp_init(PUDP_DATA_PORT);
}

void close_protocol(void) {
    if (udp_sock >= 0) close(udp_sock);
}

int inject_packet_loss(int pct) {
    if (pct < 0 || pct > 100) return -1;
    drop_probability = pct;
    return 0;
}

int send_message(const char *dest_ip, const void *buf, int len) {
    if (len > MAX_PAYLOAD) { errno = EINVAL; return -1; }
    char frame[sizeof(PUDPHeader) + MAX_PAYLOAD];
    PUDPHeader *h = (PUDPHeader*)frame;
    h->seq   = htonl(seq_tx++);
    h->flags = 0;
    memcpy(frame + sizeof(*h), buf, len);
    int flen = sizeof(*h) + len;

    struct sockaddr_in dst = {
        .sin_family = AF_INET,
        .sin_port   = htons(PUDP_DATA_PORT)
    };
    if (inet_pton(AF_INET, dest_ip, &dst.sin_addr) != 1) {
        errno = EINVAL; return -1;
    }

    pthread_mutex_lock(&pend_mtx);
    add_pending(ntohl(h->seq), frame, flen, &dst);
    pthread_mutex_unlock(&pend_mtx);

    /* simulate drop */
    if (drop_probability && (rand() % 100) < drop_probability) {
        fprintf(stderr, "[PUDP]  **SIM DROP %d%%**\n", drop_probability);
        return len;
    }

    return sendto(udp_sock, frame, flen, 0,
                  (struct sockaddr*)&dst, sizeof dst) == flen
         ? len : -1;
}

int receive_message(void *buf, int buflen) {
    char frame[sizeof(PUDPHeader) + MAX_PAYLOAD];
    struct sockaddr_in src; socklen_t sl = sizeof src;
    int n = recvfrom(udp_sock, frame, sizeof frame, 0,
                     (struct sockaddr*)&src, &sl);
    if (n <= 0) return n;

    PUDPHeader *h = (PUDPHeader*)frame;
    h->seq = ntohl(h->seq);

    /* ACK */
    if (h->flags & PUDP_F_ACK) {
        ack_pending(h->seq);
        return 0;
    }
    /* NAK */
    if (h->flags & PUDP_F_NAK) {
        resend_now(h->seq);
        return 0;
    }
    /* CONFIG */
    if (h->flags & PUDP_F_CFG) {
        if ((size_t)n >= sizeof(*h) + sizeof(ConfigMessage))
            apply_config((ConfigMessage*)(frame + sizeof(*h)));
        return 0;
    }
    /* data */
    if (h->seq != expected_seq) {
        /* send NAK for expected */
        PUDPHeader nack = { htonl(expected_seq), PUDP_F_NAK, {0} };
        sendto(udp_sock, &nack, sizeof nack, 0,
               (struct sockaddr*)&src, sizeof src);
        return 0;
    }
    expected_seq++;
    /* send ACK */
    PUDPHeader ack = { htonl(h->seq), PUDP_F_ACK, {0} };
    sendto(udp_sock, &ack, sizeof ack, 0,
           (struct sockaddr*)&src, sizeof src);

    int dlen = n - (int)sizeof(*h);
    if (dlen > buflen) dlen = buflen;
    memcpy(buf, frame + sizeof(*h), dlen);
    return dlen;
}

/* count pending entries */
int powerudp_pending_count(void) {
    int c = 0;
    for (int i = 0; i < MAX_PENDING; ++i)
        if (pend[i].in_use) c++;
    return c;
}

/* retrieve last event (ACK or DROP) */
int powerudp_last_event(uint32_t *seq, int *status) {
    if (!last_evt_status) return 0;
    *seq    = last_evt_seq;
    *status = last_evt_status;
    last_evt_status = 0;
    return 1;
}
