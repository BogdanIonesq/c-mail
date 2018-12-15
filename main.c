#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main() {
    int listenfd, connfd;
    struct sockaddr_in servaddr;
    char buff[16];

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        perror("socket creation failed");
        exit(0);
    }

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    if (inet_aton("127.0.0.1", &servaddr.sin_addr) == 0) {
        perror("IP address conversion failed");
        exit(0);
    }
    servaddr.sin_port = htons(60000);

    if (bind(listenfd, (struct sockaddr*) &servaddr, sizeof(servaddr)) == -1) {
        perror("bind failed");
        close(listenfd);
        exit(0);
    }

    if (listen(listenfd, 128) == -1) {
        perror("listen failed");
        close(listenfd);
        exit(0);
    }

    printf("Listening...\n");
    for ( ; ; ) {
        connfd = accept(listenfd, NULL, NULL);
        snprintf(buff, sizeof(buff), "hello!\n");
        write(connfd, buff, strlen(buff));

        close(listenfd);
        break;
    }

    return 0;
}