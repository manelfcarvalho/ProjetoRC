/* =========================== src/client.c =========================== */
#include "powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFSZ 512

/* ---------- join ao grupo multicast para ConfigMessage ---------- */
static void join_cfg_multicast(int udp_fd)
{
    struct ip_mreq m;
    inet_pton(AF_INET, PUDP_CFG_MC_ADDR, &m.imr_multiaddr);
    m.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(udp_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof m);
}

/* ---------- UDP listener (ACK/NACK/CFG + payload) -------------- */
static void *udp_listener(void *arg)
{
    (void)arg;
    char buf[BUFSZ];

    while (1) {
        int n = receive_message(buf, sizeof buf - 1);
        if (n <= 0) continue;                 /* ACK/NACK/CFG ou nada */

        buf[n] = '\0';
        printf("\n[CLI] RX «%s»\n> ", buf);
        fflush(stdout);
    }
    return NULL;
}

/* ---------- registo TCP + PSK ---------------------------------- */
static int tcp_register(const char *srv_ip, int port, const char *psk)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in d = {0};
    d.sin_family = AF_INET;
    d.sin_port   = htons(port);
    inet_pton(AF_INET, srv_ip, &d.sin_addr);

    if (connect(s, (struct sockaddr *)&d, sizeof d) < 0) { perror("connect"); return -1; }

    RegisterMessage r = {0};
    strncpy(r.psk, psk, sizeof r.psk - 1);
    send(s, &r, sizeof r, 0);

    printf("[CLI] Registered at %s:%d (PSK OK)\n", srv_ip, port);
    return s;
}

/* ------------------------------ main ------------------------------ */
int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <tcp_port> <psk>\n", argv[0]);
        return 1;
    }
    const char *ip   = argv[1];
    int         port = atoi(argv[2]);
    const char *psk  = argv[3];

    /* 1) inicia protocolo (UDP) */
    if (init_protocol_client() != 0) return 1;

    /* 2) join multicast ConfigMessage */
    join_cfg_multicast(0);             /* udp_sock é global na lib */

    /* 3) thread UDP listener */
    pthread_t th_udp;
    pthread_create(&th_udp, NULL, udp_listener, NULL);
    pthread_detach(th_udp);

    /* 4) injeta perda, se quiseres */
    //inject_packet_loss(30);

    /* 5) TCP registo */
    int tcp_sock = tcp_register(ip, port, psk);
    if (tcp_sock < 0) return 1;

    /* 6) CLI simples */
    char line[BUFSZ];
    printf("> "); fflush(stdout);
    while (fgets(line, sizeof line, stdin)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n') line[--len] = '\0';
        if (!len) { printf("> "); continue; }

        if (!strncmp(line, ":setcfg", 7)) {          /* pedido de nova cfg */
            send(tcp_sock, line, (int)len, 0);
            printf("[CLI] pedido de nova config enviado\n> ");
            continue;
        }

        if (send_message(ip, line, (int)len) < 0)
            perror("send_message");
        else
            printf("[CLI] sent seq\n> ");
    }

    close_protocol();
    close(tcp_sock);
    return 0;
}
