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

/* Constants */
#define MAX_MSG_LEN 64      /* maximum length of a message */
#define MAX_PREVIEW_LEN 16  /* maximum length of a message when listing */

struct msg {
    char *content;          /* message text */
    time_t ts;              /* message timestamp */
    int id;                 /* message id */
    pthread_rwlock_t lock;  /* lock in case of read/edit */
    struct msg *next;       /* pointer to next message */
};

/* Global variables */
int listenfd, ids = 1;
struct msg *msgHead = NULL;
pthread_mutex_t listLock;

/* for cleanup purposes while this is still work in progress */
void handleSig (int sig) {
    printf(">>> cleanup & exit process started...\n");
    close(listenfd);
    pthread_mutex_destroy(&listLock);
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

/* read from a given file descriptor, until end character */
ssize_t readline (int fd, void *vptr, size_t maxlen, char end) {
    ssize_t n, rc;
    char c, *ptr = vptr;

    for (n = 1; n < maxlen; n++) {
        again:
            if ((rc = read(fd, &c, 1)) == 1) {
                if (c == end) {
                    break;
                } else {
                    *ptr++ = c;
                }
            } else if (rc == 0) {
                /* EOF */
                *ptr = 0;
                return n - 1;
            } else {
                if (errno == EINTR) {
                    goto again;
                }
                return -1;
            }
    }

    *ptr = 0;
    return n;
}

/* add a new message */
void addmsg (char *buf) {
    pthread_mutex_lock(&listLock);
    if (msgHead == NULL) {
        struct msg *newmsg = (struct msg *) malloc(sizeof(struct msg));

        /* fill info for newmsg */
        newmsg -> content = (char *) malloc((strlen(buf) + 1) * sizeof(char));
        memcpy(newmsg -> content, buf, strlen(buf) + 1);
        newmsg -> ts = time(NULL);
        newmsg -> next = NULL;
        newmsg -> id = ids++;
        pthread_rwlock_init(&newmsg -> lock, NULL);

        /* update head */
        msgHead = newmsg;
    } else {
        struct msg *curmsg = msgHead;
        while (curmsg -> next != NULL) {
            curmsg = curmsg -> next;
        }
        struct msg *newmsg = (struct msg *) malloc(sizeof(struct msg));

        /* fill info for newmsg */
        newmsg -> content = (char *) malloc((strlen(buf) + 1) * sizeof(char));
        memcpy(newmsg -> content, buf, strlen(buf) + 1);
        newmsg -> ts = time(NULL);
        newmsg -> next = NULL;
        newmsg -> id = ids++;
        pthread_rwlock_init(&newmsg -> lock, NULL);

        /* update link */
        curmsg -> next = newmsg;
    }
    pthread_mutex_unlock(&listLock);
}

/* list all messages */
void listmsg (int fd) {
    pthread_mutex_lock(&listLock);
    struct msg *curmsg = msgHead;

    while (curmsg != NULL) {
        writen(fd, curmsg -> content, MAX_PREVIEW_LEN);
        writen(fd, "\n...\n", 5 * sizeof(char));

        char msgtime[32];
        ctime_r(&(curmsg -> ts), msgtime);
        msgtime[strlen(msgtime) - 1] = 0;

        char sid[sizeof(int)];
        snprintf(sid, sizeof(int), "%d", curmsg -> id);

        writen(fd, "@ ", 2 * sizeof(char));
        writen(fd, msgtime, sizeof(msgtime));
        writen(fd, "\t ID:", 5 * sizeof(char));
        writen(fd, sid, sizeof(int));
        writen(fd, "\n", sizeof(char));

        curmsg = curmsg -> next;
    }

    pthread_mutex_unlock(&listLock);
}

/* given an ID of a message, return its address */
struct msg * findmsg (int msgid) {
    pthread_mutex_lock(&listLock);
    struct msg *curmsg = msgHead;
    while (curmsg != NULL) {
        if (curmsg -> id == msgid) {
            pthread_mutex_unlock(&listLock);
            return curmsg;
        }
        curmsg = curmsg -> next;
    }
    pthread_mutex_unlock(&listLock);

    return NULL;
}

/* print entire message */
int printmsg (int fd, int msgid) {
    struct msg *m = findmsg(msgid);

    if (m == NULL) {
        return -1;
    }

    pthread_rwlock_rdlock(&m -> lock);
    writen(fd, m -> content, strlen(m -> content));
    char msgtime[32];
    ctime_r(&(m -> ts), msgtime);
    msgtime[strlen(msgtime) - 1] = 0;

    char sid[sizeof(int)];
    snprintf(sid, sizeof(int), "%d", m -> id);

    writen(fd, "\n", sizeof(char));
    writen(fd, "@ ", 2 * sizeof(char));
    writen(fd, msgtime, sizeof(msgtime));
    writen(fd, "\t ID:", 5 * sizeof(char));
    writen(fd, sid, sizeof(int));
    writen(fd, "\n", sizeof(char));
    pthread_rwlock_unlock(&m -> lock);

    return 0;
}

/* output available options to a given file descriptor */
int printOptions (int fd) {
    char list[] = "[1] Compose message. \n"
                  "[2] List messages. \n"
                  "[3] Read message. \n"
                  "[4] Edit message. \n"
                  "[5] Delete message. \n"
                  "[6] EXIT. \n";

    if (writen(fd, list, sizeof(list)) < sizeof(list)) {
        return -1;
    }

    return 0;
}

/* get user's choice */
int getOption (int fd) {
    char response[8];
    int ans;
    ssize_t ok;

    do {
        ok = readline(fd, response, 8, '\n');
        ans = atoi(response);
    } while (ans == 0 || ok == -1);

    return ans;
}

/* main function for each thread */
void* threadMain (void *arg) {
    int connfd = *((int *) arg);
    free(arg);

    int option = 0;
    do {
        if (printOptions(connfd) != 0) {
            printf("printOptions() failure!\n");
            continue;
        }

        option = getOption(connfd);

        if (option == 1) {
            /* compose message */
            char ans[MAX_MSG_LEN];
            bzero(ans, sizeof(ans));
            readline(connfd, ans, MAX_MSG_LEN, ']');

            addmsg(ans);
        } else if (option == 2) {
            listmsg(connfd);
        } else if (option == 3) {
            int msgid = getOption(connfd);

            if (printmsg(connfd, msgid) != 0) {
                char err[] = "Incorrect ID! Use [2] to list messages.\n";
                writen(connfd, err, sizeof(err));
            }
        }
    } while (option != 6);

    /* cleanup and exit */
    pthread_detach(pthread_self());
    close(connfd);
    pthread_exit(NULL);
}

int main() {
    /* close socket when shutting down the server */
    signal(SIGTERM, handleSig);
    signal(SIGINT, handleSig);

    int *connptr;
    struct sockaddr_in servaddr;
    pthread_t tids[16];
    pthread_mutex_init(&listLock, NULL);

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