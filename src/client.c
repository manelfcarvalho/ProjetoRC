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
#include <net/if.h>
#include <pthread.h>
#include <sys/ioctl.h>

/* extern UDP socket defined in powerudp.c */
extern int udp_sock;

#define BUFSZ 512

/* Join no grupo multicast para ConfigMessage */
static void join_cfg_multicast(void) {
    // Permite múltiplos sockets no mesmo endereço
    int yes = 1;
    if (setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        perror("SO_REUSEADDR");
        exit(1);
    }

    // Configura o socket para receber multicast
    struct ip_mreq mreq;
    inet_pton(AF_INET, PUDP_CFG_MC_ADDR, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    // Adiciona opção para especificar interface
    char *iface = getenv("PUDP_IFACE");
    if (iface != NULL) {
        struct ifreq ifr;
        strncpy(ifr.ifr_name, iface, IFNAMSIZ-1);
        if (ioctl(udp_sock, SIOCGIFADDR, &ifr) >= 0) {
            mreq.imr_interface = ((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr;
            printf("[CLI] Using interface %s for multicast\n", iface);
        }
    }

    if (setsockopt(udp_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   &mreq, sizeof mreq) < 0) {
        perror("IP_ADD_MEMBERSHIP");
        exit(1);
    }

    // Configura o socket para permitir broadcast/multicast
    int multicast_ttl = 5;
    if (setsockopt(udp_sock, IPPROTO_IP, IP_MULTICAST_TTL, 
                   &multicast_ttl, sizeof(multicast_ttl)) < 0) {
        perror("IP_MULTICAST_TTL");
        exit(1);
    }

    printf("[CLI] Joined multicast group %s\n", PUDP_CFG_MC_ADDR);
}

/* Listener for UDP messages (ACK/NACK/CFG and peer payload) */
static void *udp_listener(void *arg) {
    (void)arg;
    char buf[BUFSZ];
    while (1) {
        int n = receive_message(buf, sizeof buf - 1);
        if (n <= 0) continue;

        // Se for uma mensagem de configuração, será processada dentro do receive_message
        // e não chegará aqui

        buf[n] = '\0';
        printf("\n[CLI] RX «%s»\n> ", buf);
        fflush(stdout);
    }
    return NULL;
}

/* Register via TCP with PSK */
static int tcp_register(const char *srv_ip, int port, const char *psk) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in d = { .sin_family = AF_INET, .sin_port = htons(port) };
    if (inet_pton(AF_INET, srv_ip, &d.sin_addr) != 1) {
        perror("inet_pton srv_ip"); return -1;
    }
    if (connect(s, (struct sockaddr *)&d, sizeof d) < 0) {
        perror("connect"); return -1;
    }
    RegisterMessage r = {0};
    strncpy(r.psk, psk, sizeof r.psk - 1);
    if (send(s, &r, sizeof r, 0) != sizeof r) {
        perror("send PSK"); close(s); return -1;
    }
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

    /* 1) initialize PowerUDP (UDP socket on port 6001) */
    if (init_protocol_server() != 0) {
        fprintf(stderr, "Failed to init PowerUDP\n");
        return 1;
    }

    /* opcional: simular perda de 30% nos pacotes enviados */
    inject_packet_loss(30);

    /* 2) join multicast group for dynamic config */
    join_cfg_multicast();

    /* 3) start UDP listener thread */
    pthread_t th_udp;
    if (pthread_create(&th_udp, NULL, udp_listener, NULL) != 0) {
        perror("pthread_create udp_listener");
        return 1;
    }
    pthread_detach(th_udp);

    /* 4) register via TCP to server */
    int tcp = tcp_register(ip, port, psk);
    if (tcp < 0) return 1;

    /* 5) CLI loop: only peer-to-peer and :setcfg commands */
    char line[BUFSZ];
    printf("> "); fflush(stdout);
    while (fgets(line, sizeof line, stdin)) {
        size_t len = strlen(line);
        if (len && line[len-1] == '\n') line[--len] = '\0';
        if (!len) { printf("> "); continue; }

        if (!strncmp(line, ":setcfg", 7)) {
            send(tcp, line, (int)len, 0);
            printf("[CLI] Config request sent\n> ");
            continue;
        }

        char *space = strchr(line, ' ');
        if (!space) {
            fprintf(stderr, "Invalid. Use '<peer_ip> <msg>' or ':setcfg'\n> ");
            continue;
        }
        *space = '\0';
        const char *dest = line;
        const char *msg  = space + 1;
        if (send_message(dest, msg, (int)strlen(msg)) < 0)
            perror("send_message");
        else
            printf("[CLI] Message sent to %s\n> ", dest);
    }

    close_protocol();
    close(tcp);
    return 0;
}
