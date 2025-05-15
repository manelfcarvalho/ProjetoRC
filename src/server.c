/* newline added */

/* =========================== src/server.c ============================ */
#include "powerudp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

int main(int argc,char **argv){
 if(argc!=2){fprintf(stderr,"Usage: %s <tcp-port>\n",argv[0]);return 1;}
 printf("[SRV] Listening on 0.0.0.0:%s\n",argv[1]);
 pause();
 return 0;
}


