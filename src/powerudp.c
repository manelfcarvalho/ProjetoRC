/* ================== PowerUDP – implementação básica ================= */
#include "powerudp.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>

static int udp_sock = -1;

/* ------------------------------------------------ init / close */
int init_protocol(const char *server_ip, int tcp_port, const char *psk)
{
    (void)server_ip; (void)tcp_port; (void)psk; /* ainda não usados aqui */

    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) { perror("UDP socket"); return -1; }

    /* bind a porto efémero – deixa OS escolher */
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = 0;
    if (bind(udp_sock, (struct sockaddr*)&addr, sizeof addr) < 0) {
        perror("UDP bind"); close(udp_sock); return -1;
    }

    /* non-blocking para poder usar recv poll */
    int flags = fcntl(udp_sock, F_GETFL, 0);
    fcntl(udp_sock, F_SETFL, flags | O_NONBLOCK);

    return 0;
}

void close_protocol(void)
{
    if (udp_sock >= 0) close(udp_sock);
}

/* ------------------------------------------------ envio simples */
int send_message(const char *dest_ip, const void *buf, int len)
{
    if (udp_sock < 0) { errno = EBADF; return -1; }

    struct sockaddr_in dst = {0};
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(PUDP_DATA_PORT);
    if (inet_pton(AF_INET, dest_ip, &dst.sin_addr) != 1) {
        errno = EINVAL; return -1;
    }
    return sendto(udp_sock, buf, len, 0,
                  (struct sockaddr*)&dst, sizeof dst);
}

/* ------------------------------------------------ recepção simples */
int receive_message(void *buf, int buflen)
{
    if (udp_sock < 0) { errno = EBADF; return -1; }
    return recv(udp_sock, buf, buflen, 0);
}

/* -------- TODO: retransmissão, back-off, ACK/NACK, tabela de envios */
