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
#include <signal.h>

/* Global variables */
int listenfd;

/* for cleanup purposes while this is still work in progress */
void handleSig (int sig) {
    printf(">>> cleanup & exit process started...\n");
    close(listenfd);
    exit(0);
}

/* write n bytes to a file descriptor */
ssize_t writen (int fd, const void *vptr, size_t n) {
    size_t nleft = n;
    ssize_t nwritten;
    const char *ptr = vptr;

    while (nleft > 0) {
        if ((nwritten = write(fd, ptr, nleft)) <= 0) {
            if (nwritten < 0 && errno == EINTR) {
                nwritten = 0;
            } else {
                return -1;
            }
        }
        nleft -= nwritten;
        ptr += nwritten;
    }

    return n;
}

/* read n bytes from a file descriptor */
ssize_t readn (int fd, void *vptr, size_t n) {
    size_t nleft = n;
    ssize_t nread;
    char *ptr = vptr;

    while (nleft > 0) {
        if ((nread = read(fd, ptr, nleft)) < 0) {
            if (errno == EINTR) {
                nread = 0;
            } else {
                return -1;
            }
        } else if (nread == 0) {
            /* EOF */
            break;
        }

        nleft -= nread;
        ptr += nread;
    }

    return n - nleft;
}

/* output available options to a given file descriptor */
int printOptions (int fd) {
    char list[] = "[1] Compose message. \n"
                  "[2] Read message. \n"
                  "[3] Delete message. \n";

    if (writen(fd, list, sizeof(list)) < sizeof(list)) {
        return -1;
    }

    return 0;
}

/* get user's choice */
int getOption (int fd) {
    char response[2];

    if (readn(fd, response, sizeof(response)) != sizeof(response)) {
        return -1;
    }

    if (response[0] >= '0' && response[0] <= '9') {
        return response[0] - '0';
    } else {
        return -2;
    }
}

/* main function for each thread */
void* threadMain (void *arg) {
    int connfd = *((int *) arg);
    free(arg);

    int ok = 0, option;
    do {
        if (printOptions(connfd) != 0) {
            printf("printOptions() failure!\n");
            continue;
        }

        option = getOption(connfd);
        if (option >= 0 && option <= 9) {
            ok = 1;
        }
    } while (!ok);

    char ans[32];
    bzero(ans, sizeof(ans));
    snprintf(ans, sizeof(ans), "You chose option #%d\n", option);
    writen(connfd, ans, sizeof(ans));

    /* cleanup and exit */
    pthread_detach(pthread_self());
    close(connfd);
    pthread_exit(NULL);
}

int main() {
    signal(SIGTERM, handleSig);

    int *connptr;
    struct sockaddr_in servaddr;
    pthread_t tids[16];

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
    int i = 0;

    for ( ; ; ) {
        connptr = (int *) malloc (sizeof(int));
        *connptr = accept(listenfd, NULL, NULL);

        if (pthread_create(&tids[i++], NULL, &threadMain, connptr) != 0) {
            perror("creating thread failure");
            close(*connptr);
        }
    }
}