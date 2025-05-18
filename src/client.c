/* ============================ src/client.c =========================== */
#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFSZ 512

/* Regista no grupo multicast no mesmo socket DATA_PORT */
static void join_cfg_multicast(void) {
    struct ip_mreq m;
    inet_pton(AF_INET, PUDP_CFG_MC_ADDR, &m.imr_multiaddr);
    m.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(udp_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   &m, sizeof m) < 0) {
        perror("multicast join");
        exit(1);
    }
}

/* Thread que apenas processa ACK/NACK/CFG e printa payload de peers */
static void *udp_listener(void *arg) {
    (void)arg;
    char buf[BUFSZ];
    while (1) {
        int n = receive_message(buf, sizeof buf - 1);
        if (n <= 0) continue;
        buf[n] = '\0';
        printf("\n[CLI] RX «%s»\n> ", buf);
        fflush(stdout);
    }
    return NULL;
}

/* Registo TCP + PSK */
static int tcp_register(const char *srv_ip, int port, const char *psk) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in d = { .sin_family = AF_INET,
                             .sin_port   = htons(port) };
    inet_pton(AF_INET, srv_ip, &d.sin_addr);
    if (connect(s, (struct sockaddr *)&d, sizeof d) < 0) {
        perror("connect"); return -1;
    }
    RegisterMessage r = {0};
    strncpy(r.psk, psk, sizeof r.psk - 1);
    send(s, &r, sizeof r, 0);
    printf("[CLI] Registered at %s:%d (PSK OK)\n", srv_ip, port);
    return s;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <tcp_port> <psk>\n", argv[0]);
        return 1;
    }
    const char *ip   = argv[1];
    int         port = atoi(argv[2]);
    const char *psk  = argv[3];

    /* 1) Inicia o socket DATA_PORT (=6001) */
    if (init_protocol_server() != 0) return 1;

    /* 2) Faz join no grupo multicast para configs */
    join_cfg_multicast();

    /* 3) Thread UDP listener para peers e configs */
    pthread_t th;
    pthread_create(&th, NULL, udp_listener, NULL);
    pthread_detach(th);

    /* 4) Registo TCP ao servidor */
    int tcp = tcp_register(ip, port, psk);
    if (tcp < 0) return 1;

    /* 5) CLI apenas peer-to-peer e :setcfg */
    char line[BUFSZ];
    printf("> "); fflush(stdout);
    while (fgets(line, sizeof line, stdin)) {
        size_t len = strlen(line);
        if (len && line[len-1]=='\n') line[--len]='\0';
        if (!len) { printf("> "); continue; }

        if (!strncmp(line, ":setcfg", 7)) {
            send(tcp, line, (int)len, 0);
            printf("[CLI] pedido de nova config enviado\n> ");
            continue;
        }
        char *space = strchr(line, ' ');
        if (!space) {
            fprintf(stderr,
                "Invalid. Use '<peer_ip> <msg>' or ':setcfg'\n> ");
            continue;
        }
        *space = '\0';
        const char *dest = line;
        const char *msg  = space + 1;
        if (send_message(dest, msg, (int)strlen(msg)) < 0)
            perror("send_message");
        else
            printf("[CLI] sent seq to %s\n> ", dest);
    }

    close_protocol();
    close(tcp);
    return 0;
}
