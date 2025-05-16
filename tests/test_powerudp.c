/* ==============================================================
   Teste simples ao PowerUDP v0.2
   Mede latência média e total de mensagens entregues
   sem aceder a variáveis internas da biblioteca.
   --------------------------------------------------------------
   Usage:
     ./test_powerudp <server_ip> <tcp_port> <psk> <loss_pct> <N>
     ex.: ./test_powerudp 127.0.0.1 7010 mypsk 30 100
   ============================================================== */
#include "../src/powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <arpa/inet.h>

static double now_ms(void)
{
    struct timeval tv; gettimeofday(&tv, NULL);
    return tv.tv_sec*1000.0 + tv.tv_usec/1000.0;
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr,
            "Usage: %s <ip> <tcp_port> <psk> <loss_pct> <N>\n", argv[0]);
        return 1;
    }
    const char *ip   = argv[1];
    int         port = atoi(argv[2]);
    const char *psk  = argv[3];
    int         loss = atoi(argv[4]);
    int         N    = atoi(argv[5]);

    /* inicia protocolo UDP + perda */
    if (init_protocol_client() != 0) return 1;
    inject_packet_loss(loss);

    /* registo TCP mínimo */
    int tcp = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in d = {0};
    d.sin_family = AF_INET; d.sin_port = htons(port);
    inet_pton(AF_INET, ip, &d.sin_addr);
    if (connect(tcp, (struct sockaddr*)&d, sizeof d) < 0) {
        perror("TCP connect"); return 1;
    }
    RegisterMessage r = {0}; strncpy(r.psk, psk, sizeof r.psk - 1);
    send(tcp, &r, sizeof r, 0);

    /* preenche payload */
    char payload[100]; memset(payload, 'X', sizeof payload);

    double t0_total = now_ms();
    double sum_lat  = 0;
    int    delivered = 0;

    for (int i = 0; i < N; ++i) {
        double t0 = now_ms();
        send_message(ip, payload, sizeof payload);

        /* espera até ACK desta mensagem
           – assumimos ordem: 1 a 1 (seq em série)           */
        int acked = 0;
        while (!acked) {
            if (receive_message(NULL, 0) == 0) acked = 1;
            usleep(1000);
        }
        double dt = now_ms() - t0;
        sum_lat += dt;
        delivered++;
        printf("msg %03d  %.1f ms\n", i+1, dt);
    }

    double t_total = now_ms() - t0_total;

    printf("\n==== resumo ====\n");
    printf("perda simulada     : %d %%\n", loss);
    printf("mensagens enviadas  : %d\n", delivered);
    printf("latência média      : %.1f ms\n", sum_lat / delivered);
    printf("throughput médio    : %.1f msg/s\n",
           delivered / (t_total/1000.0));

    close(tcp);
    close_protocol();
    return 0;
}
