/* ============================== src/client.c ============================ */
#include "powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFSZ 512

/* ------------------------------------------------------------------ */
/* Thread que fica a ouvir o socket UDP para:                         */
/*   • processar automaticamente os ACKs (receive_message)            */
/*   • imprimir qualquer payload que venha do servidor (eco, etc.)    */
static void *udp_listener(void *arg)
{
    (void)arg;
    char buf[BUFSZ];

    while (1) {
        int n = receive_message(buf, sizeof buf - 1);
        if (n <= 0)                 /* 0 = ACK processado; <0 = nada novo */
            continue;

        buf[n] = '\0';
        printf("\n[CLI] RX «%s»\n> ", buf);   /* mostra payload recebido */
        fflush(stdout);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Envia registo TCP com a PSK                                         */
static int tcp_register(const char *srv_ip, int port, const char *psk)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return -1; }

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(port);
    if (inet_pton(AF_INET, srv_ip, &dst.sin_addr) != 1) {
        fprintf(stderr, "invalid IP\n"); close(sock); return -1;
    }
    if (connect(sock, (struct sockaddr *)&dst, sizeof dst) < 0) {
        perror("connect"); close(sock); return -1;
    }

    RegisterMessage reg = {0};
    strncpy(reg.psk, psk, sizeof reg.psk - 1);

    if (send(sock, &reg, sizeof reg, 0) != sizeof reg) {
        perror("send reg"); close(sock); return -1;
    }
    printf("[CLI] Registered at %s:%d (PSK OK)\n", srv_ip, port);
    return sock;                              /* guardamos – futuro uso */
}

/* ------------------------------------------------------------------ */
int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <tcp_port> <psk>\n", argv[0]);
        return 1;
    }
    const char *ip   = argv[1];
    int         port = atoi(argv[2]);
    const char *psk  = argv[3];

    /* 1) Iniciar protocolo (bind UDP a porto efémero) */
    if (init_protocol_client() != 0)            /* ← nova função */
        return 1;
    
    inject_packet_loss(50);
    
    /* 2) Lançar thread que consome ACKs / payloads recebidos */
    pthread_t udp_th;
    pthread_create(&udp_th, NULL, udp_listener, NULL);
    pthread_detach(udp_th);

    /* 3) Registo TCP com o servidor */
    int tcp_sock = tcp_register(ip, port, psk);
    if (tcp_sock < 0) return 1;

    /* 4) Interface de linha-de-comando simples                       */
    char line[BUFSZ];
    printf("> "); fflush(stdout);
    while (fgets(line, sizeof line, stdin)) {
        size_t len = strlen(line);
        if (len && line[len - 1] == '\n') line[--len] = '\0';
        if (!len) { printf("> "); continue; }

        if (send_message(ip, line, (int)len) < 0)
            perror("[CLI] send_message");
        else
            printf("[CLI] sent seq\n> ");
        fflush(stdout);
    }

    close_protocol();
    close(tcp_sock);
    return 0;
}
/* ======================================================================= */
