/* ============================ src/client.c =========================== */
#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

/* extern UDP socket defined in powerudp.c (remove static qualifier there) */
extern int udp_sock;

#define BUFSZ 512

/* faz join no grupo multicast para ConfigMessage */
static void join_cfg_multicast(void) {
    struct ip_mreq m;
    inet_pton(AF_INET, PUDP_CFG_MC_ADDR, &m.imr_multiaddr);
    m.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(udp_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof m);
}

/* listener de pacotes UDP (ACK/NACK/CFG + payload de outros peers) */
static void *udp_listener(void *arg) {
    (void)arg;
    char buf[BUFSZ];
    while (1) {
        int n = receive_message(buf, sizeof buf - 1);
        if (n <= 0) continue;  /* ACK/NACK/CFG ou nada */
        buf[n] = '\0';
        printf("\n[CLI] RX «%s»\n> ", buf);
        fflush(stdout);
    }
    return NULL;
}

/* registo TCP + PSK */
static int tcp_register(const char *srv_ip, int port, const char *psk) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in d = {0};
    d.sin_family = AF_INET;
    d.sin_port   = htons(port);
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

    /* 1) inicia PowerUDP (socket UDP 6001) */
    if (init_protocol_server() != 0) return 1;

    /* 2) regista no grupo multicast para configuração dinâmica */
    join_cfg_multicast();

    /* 3) thread UDP listener */
    pthread_t th_udp;
    pthread_create(&th_udp, NULL, udp_listener, NULL);
    pthread_detach(th_udp);

    /* 4) registo TCP no servidor */
    int tcp = tcp_register(ip, port, psk);
    if (tcp < 0) return 1;

    /* 5) CLI: suporte a pedidos de config e P2P */
    char line[BUFSZ];
    printf("> "); fflush(stdout);
    while (fgets(line, sizeof line, stdin)) {
        size_t len = strlen(line);
        if (len && line[len-1]=='\n') line[--len]='\0';
        if (!len) { printf("> "); continue; }

        /* comandos de configuração via TCP */
        if (!strncmp(line, ":setcfg", 7)) {
            send(tcp, line, (int)len, 0);
            printf("[CLI] pedido de nova config enviado\n> ");
            continue;
        }
        /* P2P UDP unicast: sintaxe "<dest_ip> <mensagem>" */
        char *space = strchr(line, ' ');
        if (space) {
            *space = '\0';
            const char *dest_ip = line;
            const char *msg     = space + 1;
            if (send_message(dest_ip, msg, (int)strlen(msg)) < 0)
                perror("send_message");
            else
                printf("[CLI] sent seq to %s\n> ", dest_ip);
            continue;
        }
        /* sem espaço: envia ao servidor */
        if (send_message(ip, line, (int)strlen(line)) < 0)
            perror("send_message");
        else
            printf("[CLI] sent seq to server %s\n> ", ip);
    }

    close_protocol();
    close(tcp);
    return 0;
}
