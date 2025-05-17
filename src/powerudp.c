/* ==============================================================
   PowerUDP  v0.4
   --------------------------------------------------------------
   – Sequenciação e ACK
   – Retransmissão com back-off exponencial
   – NACK + recuperação de fora-de-ordem
   – Injecção de perda artificial
   – Configuração dinâmica (ConfigMessage via multicast)
   ============================================================ */

#define _POSIX_C_SOURCE 200809L
#include <unistd.h>
#include <time.h>
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
    char                data[sizeof(PUDPHeader)+MAX_PAYLOAD];
    struct timeval      ts;
    uint32_t            to_ms;               /* timeout corrente      */
    int                 retries;
    struct sockaddr_in  dst;
    int                 in_use;
} Pending;

/* ----------------------- estado global ---------------------- */
static int              udp_sock = -1;
static Pending          pend[MAX_PENDING];
static pthread_mutex_t  pend_mtx = PTHREAD_MUTEX_INITIALIZER;

static uint32_t         seq_tx   = 1;        /* próximo seq a enviar */
static uint32_t         expected_seq = 1;    /* próximo seq (Rx)     */

static uint32_t         base_timeout_ms = PUDP_BASE_TO_MS;
static uint8_t          max_retries     = PUDP_MAX_RETRY;
static int              drop_probability = 0;   /* sim-drop 0-100 %  */

/* ----------------------- utilidades ------------------------- */
static uint32_t now_ms(void)
{
    struct timeval tv; gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static void apply_config(const ConfigMessage *cfg)
{
    base_timeout_ms = cfg->base_timeout_ms;
    max_retries     = cfg->max_retries;
    printf("[PUDP] NEW CONFIG  timeout=%u ms  max_rtx=%u\n",
           base_timeout_ms, max_retries);
}

/* -------------------- retransmissão loop -------------------- */
static void *retrans_loop(void *arg)
{
    (void)arg;
    while (1) {
        pthread_mutex_lock(&pend_mtx);
        uint32_t now = now_ms();

        for (int i = 0; i < MAX_PENDING; ++i) if (pend[i].in_use) {
            uint32_t age = now -
                (uint32_t)(pend[i].ts.tv_sec*1000 + pend[i].ts.tv_usec/1000);

            if (age >= pend[i].to_ms) {
                if (pend[i].retries >= max_retries) {
                    fprintf(stderr,
                            "[PUDP] seq %u DROPPED after %d tries\n",
                            pend[i].seq, pend[i].retries);
                    pend[i].in_use = 0;
                    continue;
                }
                sendto(udp_sock, pend[i].data, pend[i].len, 0,
                       (struct sockaddr*)&pend[i].dst,
                       sizeof pend[i].dst);

                gettimeofday(&pend[i].ts, NULL);
                pend[i].retries++;
                pend[i].to_ms *= 2;                    /* back-off ×2 */

                fprintf(stderr,
                        "[PUDP] retrans seq=%u (try %d, to %ums)\n",
                        pend[i].seq, pend[i].retries, pend[i].to_ms);
            }
        }
        pthread_mutex_unlock(&pend_mtx);
        usleep(100 * 1000);                            /* 100 ms */
    }
    return NULL;
}

/* -------------------- tabela pendentes ---------------------- */
static void add_pending(uint32_t seq, const char *frame, int len,
                        const struct sockaddr_in *dst)
{
    for (int i = 0; i < MAX_PENDING; ++i) if (!pend[i].in_use) {
        pend[i].seq     = seq;
        pend[i].len     = len;
        memcpy(pend[i].data, frame, len);
        pend[i].dst     = *dst;
        gettimeofday(&pend[i].ts, NULL);
        pend[i].retries = 0;
        pend[i].to_ms   = base_timeout_ms;
        pend[i].in_use  = 1;
        return;
    }
}

static void ack_pending(uint32_t seq)
{
    for (int i = 0; i < MAX_PENDING; ++i)
        if (pend[i].in_use && pend[i].seq == seq) {
            pend[i].in_use = 0; return;
        }
}

static int resend_now(uint32_t seq)
{
    for (int i = 0; i < MAX_PENDING; ++i)
        if (pend[i].in_use && pend[i].seq == seq) {
            sendto(udp_sock, pend[i].data, pend[i].len, 0,
                   (struct sockaddr*)&pend[i].dst,
                   sizeof pend[i].dst);
            gettimeofday(&pend[i].ts, NULL);
            fprintf(stderr,
                    "[PUDP] NACK → instant retrans seq=%u\n", seq);
            return 0;
        }
    return -1;
}

/* -------------------- socket init / close ------------------- */
static int common_udp_init(uint16_t bind_port)
{
    static int rng = 0; if (!rng){ srand((unsigned)time(NULL)); rng = 1; }

    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) { perror("udp socket"); return -1; }

    struct sockaddr_in a = {0};
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port        = htons(bind_port);
    if (bind(udp_sock, (struct sockaddr*)&a, sizeof a) < 0) {
        perror("udp bind"); return -1;
    }

    int fl = fcntl(udp_sock, F_GETFL, 0);
    fcntl(udp_sock, F_SETFL, fl | O_NONBLOCK);

    pthread_t th; pthread_create(&th, NULL, retrans_loop, NULL);
    pthread_detach(th);
    return 0;
}

int init_protocol_client(void) { return common_udp_init(0); }
int init_protocol_server(void) { return common_udp_init(PUDP_DATA_PORT); }

void close_protocol(void)
{
    if (udp_sock >= 0) close(udp_sock);
}

/* -------------------- perda artificial ---------------------- */
int inject_packet_loss(int pct)
{
    if (pct < 0 || pct > 100) return -1;
    drop_probability = pct;
    return 0;
}

/* -------------------- envio fiável -------------------------- */
int send_message(const char *dest_ip, const void *buf, int len)
{
    if (udp_sock < 0 || len > MAX_PAYLOAD) { errno = EINVAL; return -1; }

    /* frame pronto */
    char frame[sizeof(PUDPHeader) + MAX_PAYLOAD];
    PUDPHeader *h = (PUDPHeader*)frame;
    h->seq   = htonl(seq_tx++);
    h->flags = 0;
    memcpy(frame + sizeof(PUDPHeader), buf, len);
    int flen = sizeof(PUDPHeader) + len;

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(PUDP_DATA_PORT);
    if (inet_pton(AF_INET, dest_ip, &dst.sin_addr) != 1) {
        errno = EINVAL; return -1;
    }

    /* Regista pendente antes de possível drop simulado */
    pthread_mutex_lock(&pend_mtx);
    add_pending(ntohl(h->seq), frame, flen, &dst);
    pthread_mutex_unlock(&pend_mtx);

    /* Perda artificial */
    if (drop_probability && (rand() % 100) < drop_probability) {
        fprintf(stderr, "[PUDP]  **SIM DROP %d%%**\n", drop_probability);
        return len;                 /* temporizador cuidará do retry */
    }

    if (sendto(udp_sock, frame, flen, 0,
               (struct sockaddr*)&dst, sizeof dst) != flen)
        return -1;

    return len;
}

/* -------------------- recepção + NACK ----------------------- */
int receive_message(void *buf, int buflen)
{
    if (udp_sock < 0) { errno = EBADF; return -1; }

    char frame[sizeof(PUDPHeader) + MAX_PAYLOAD];
    struct sockaddr_in src; socklen_t sl = sizeof src;

    int n = recvfrom(udp_sock, frame, sizeof frame, 0,
                     (struct sockaddr*)&src, &sl);
    if (n <= 0) return n;

    PUDPHeader *h = (PUDPHeader*)frame;
    h->seq = ntohl(h->seq);

    /* ---------- ACK */
    if (h->flags & PUDP_F_ACK) {
        pthread_mutex_lock(&pend_mtx);
        ack_pending(h->seq);
        pthread_mutex_unlock(&pend_mtx);
        return 0;
    }

    /* ---------- NACK */
    if (h->flags & PUDP_F_NAK) {
        resend_now(h->seq);
        return 0;
    }

    /* ---------- ConfigMessage */
    if (h->flags & PUDP_F_CFG) {
        if ((size_t)n >= sizeof(PUDPHeader) + sizeof(ConfigMessage)) {
            apply_config((ConfigMessage*)
                         (frame + sizeof(PUDPHeader)));
        }
        return 0;
    }

    /* ---------- Payload normal */
    if (h->seq != expected_seq) {
        /* fora-de-ordem: pede NACK */
        PUDPHeader nack = { htonl(expected_seq), PUDP_F_NAK,{0,0,0} };
        sendto(udp_sock, &nack, sizeof nack, 0,
               (struct sockaddr*)&src, sizeof src);
        return 0;                   /* descarta o fora-de-ordem */
    }
    expected_seq++;                 /* ordem correcta — avança */

    /* envia ACK */
    PUDPHeader ack = { htonl(h->seq), PUDP_F_ACK,{0,0,0} };
    sendto(udp_sock, &ack, sizeof ack, 0,
           (struct sockaddr*)&src, sizeof src);

    int dlen = n - (int)sizeof(PUDPHeader);
    if (dlen > buflen) dlen = buflen;
    memcpy(buf, frame + sizeof(PUDPHeader), dlen);
    return dlen;
}
