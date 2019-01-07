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
#include <semaphore.h>
#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>

/* Constants */
#define MAX_MSG_LEN 64      /* maximum length of a message */
#define MAX_PREVIEW_LEN 16  /* maximum length of a message when listing */
#define MAX_THREADS 64      /* thread pool size */

/* Message structure */
struct msg {
    unsigned char *content; /* message text */
    int len;                /* encryption length */
    time_t ts;              /* message timestamp */
    unsigned long id;       /* message id */
    pthread_rwlock_t lock;  /* lock in case of read/edit */
    struct msg *next;       /* pointer to next message */
};

/* Global variables */
int g_listenfd;             /* listen fd of the server */
unsigned long ids = 1;      /* message ID counter */
struct msg *msgHead = NULL; /* head to the list of messages */
pthread_mutex_t listLock;   /* message list mutex */
unsigned char *key;         /* A 256 bit key (used for encryption) */
unsigned char *iv;          /* A 128 bit IV (used for encryption) */
sem_t threads_sem;          /* thread pool semaphore */
int connq[MAX_THREADS];     /* threads will obtain the file descriptors from this queue */
int connq_top = -1;         /* queue top */
pthread_mutex_t connq_lock; /* operations from the queue must be guarded (thread-safe) */

/* push a socket into the connections queue */
int push_conn (int fd) {
    if (connq_top == MAX_THREADS - 1) {
        return -1;
    }
    connq[++connq_top] = fd;
    return 0;
}

/* pop a socket from the connections queue */
int pop_conn () {
    if (connq_top == -1) {
        return -1;
    }
    connq_top--;
    return connq[connq_top + 1];
}

/* cleanup function in case of shutting down the server */
void handleSig (int sig) {
    printf("\nshutting down server...\n");

    /* close the listening socket */
    close(g_listenfd);

    /* kill the thread pool */
    int i;
    for (i = 0; i < MAX_THREADS; i++) {
        push_conn(1);
        sem_post(&threads_sem);
    }

    /* destroy mutexes and sempahores*/
    pthread_mutex_destroy(&listLock);
    pthread_mutex_destroy(&connq_lock);
    sem_destroy(&threads_sem);

    exit(0);
}

/* write n bytes to a given file descriptor */
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

/* read n bytes from a given file descriptor */
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

/* read from a given file descriptor, until 'end' character is reached */
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

/*
 *  The following two functions use aes-256 to encrypt/decrypt given strings.
 *  For more information visit
 *      https://wiki.openssl.org/index.php/EVP_Symmetric_Encryption_and_Decryption
 */
int encrypt(unsigned char *plaintext, int plaintext_len, unsigned char *key,
            unsigned char *iv, unsigned char *ciphertext) {
    EVP_CIPHER_CTX *ctx;
    int len;
    int ciphertext_len;

    if(!(ctx = EVP_CIPHER_CTX_new())) {
        return -1;
    }

    if(1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv))
        return -1;

    if(1 != EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len))
        return -1;
    ciphertext_len = len;

    if(1 != EVP_EncryptFinal_ex(ctx, ciphertext + len, &len))
        return -1;
    ciphertext_len += len;

    EVP_CIPHER_CTX_free(ctx);

    return ciphertext_len;
}

int decrypt(unsigned char *ciphertext, int ciphertext_len, unsigned char *key,
            unsigned char *iv, unsigned char *plaintext) {
    EVP_CIPHER_CTX *ctx;
    int len;
    int plaintext_len;

    if(!(ctx = EVP_CIPHER_CTX_new()))
        return -1;

    if(1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv))
        return -1;

    if(1 != EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, ciphertext_len))
        return -1;

    plaintext_len = len;

    if(1 != EVP_DecryptFinal_ex(ctx, plaintext + len, &len))
        return -1;
    plaintext_len += len;

    EVP_CIPHER_CTX_free(ctx);

    return plaintext_len;
}

int add_msg (unsigned char *buf) {
    /* encrypt message */
    unsigned char ciphertext[128];
    bzero(ciphertext, 128);
    int ciphertext_len = encrypt(buf, strlen(buf), key, iv, ciphertext);

    if (ciphertext_len == -1) {
        printf("encryption fail!\n");
        return -1;
    }

    pthread_mutex_lock(&listLock);
    if (msgHead == NULL) {
        struct msg *newmsg = (struct msg *) malloc(sizeof(struct msg));

        /* fill info for newmsg */
        newmsg -> content = (unsigned char *) malloc(ciphertext_len * sizeof(unsigned char));
        memcpy(newmsg -> content, ciphertext, ciphertext_len);
        newmsg -> ts = time(NULL);
        newmsg -> next = NULL;
        newmsg -> id = ids++;
        newmsg -> len = ciphertext_len;
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
        newmsg -> content = (unsigned char *) malloc(ciphertext_len * sizeof(unsigned char));
        memcpy(newmsg -> content, ciphertext, ciphertext_len);
        newmsg -> ts = time(NULL);
        newmsg -> next = NULL;
        newmsg -> id = ids++;
        newmsg -> len = ciphertext_len;
        pthread_rwlock_init(&newmsg -> lock, NULL);

        /* update link */
        curmsg -> next = newmsg;
    }
    pthread_mutex_unlock(&listLock);

    return 0;
}

int delete_msg (unsigned long msgid) {
    pthread_mutex_lock(&listLock);
    struct msg *cur = msgHead;

    if (cur -> id == msgid) {
        msgHead = cur -> next;
        pthread_rwlock_destroy(&cur -> lock);
        free(cur);
        pthread_mutex_unlock(&listLock);
        return 0;
    }

    while (cur -> next != NULL && cur -> next -> id != msgid) {
        cur = cur -> next;
    }

    if (cur -> next == NULL) {
        pthread_mutex_unlock(&listLock);
        return -1;
    } else {
        struct msg *aux = cur -> next;
        cur -> next = cur -> next -> next;
        pthread_rwlock_destroy(&aux -> lock);
        free(aux);
        pthread_mutex_unlock(&listLock);
        return 0;
    }
}

int list_msg (int fd) {
    pthread_mutex_lock(&listLock);
    struct msg *curmsg = msgHead;

    while (curmsg != NULL) {
        unsigned char decryptedtext[128];
        bzero(decryptedtext, 128);
        int decryptedtext_len = decrypt(curmsg -> content, curmsg -> len, key, iv, decryptedtext);

        if (decryptedtext_len == -1) {
            printf("decryption fail!\n");
            pthread_mutex_unlock(&listLock);
            return -1;
        }
        /* Add a NULL terminator. We are expecting printable text */
        decryptedtext[decryptedtext_len] = '\0';

        writen(fd, decryptedtext, MAX_PREVIEW_LEN > strlen(decryptedtext) ? strlen(decryptedtext) : MAX_PREVIEW_LEN);
        writen(fd, "\n...\n", 5 * sizeof(char));

        char msgtime[40];
        bzero(msgtime, 40);
        ctime_r(&(curmsg -> ts), msgtime);
        msgtime[strlen(msgtime) - 1] = 0;

        char sid[sizeof(int)];
        snprintf(sid, sizeof(unsigned long), "%lu", curmsg -> id);

        writen(fd, "@ ", 2 * sizeof(char));
        writen(fd, msgtime, sizeof(msgtime));
        writen(fd, "\t ID:", 5 * sizeof(char));
        writen(fd, sid, sizeof(int));
        writen(fd, "\n\n", 2 * sizeof(char));

        curmsg = curmsg -> next;
    }

    pthread_mutex_unlock(&listLock);
    return 0;
}

/* given an ID of a message, return its address */
struct msg *find_msg (unsigned long msgid) {
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
int print_msg (int fd, unsigned long msgid) {
    struct msg *m = find_msg(msgid);

    if (m == NULL) {
        return -1;
    }
    pthread_rwlock_rdlock(&m -> lock);

    unsigned char decryptedtext[128];
    bzero(decryptedtext, 128);
    int decryptedtext_len = decrypt(m -> content, m -> len, key, iv, decryptedtext);

    if (decryptedtext_len == -1) {
        printf("decryption fail!\n");
        pthread_rwlock_unlock(&m -> lock);
        return -1;
    }
    /* Add a NULL terminator. We are expecting printable text */
    decryptedtext[decryptedtext_len] = '\0';

    writen(fd, "\n", sizeof(char));
    writen(fd, decryptedtext, decryptedtext_len + 1);
    char msgtime[40];
    bzero(msgtime, 40);
    ctime_r(&(m -> ts), msgtime);
    msgtime[strlen(msgtime) - 1] = 0;

    char sid[sizeof(unsigned long)];
    snprintf(sid, sizeof(unsigned long), "%lu", m -> id);

    writen(fd, "\n", sizeof(char));
    writen(fd, "@ ", 2 * sizeof(char));
    writen(fd, msgtime, sizeof(msgtime));
    writen(fd, "\t ID:", 5 * sizeof(char));
    writen(fd, sid, strlen(sid));
    writen(fd, "\n\n", 2 * sizeof(char));
    pthread_rwlock_unlock(&m -> lock);

    return 0;
}

/* output available options to a given file descriptor */
int print_options (int fd) {
    char list[] = "[1] Compose message. \n"
                  "[2] List messages. \n"
                  "[3] Read message. \n"
                  "[4] Delete message. \n"
                  "[5] EXIT. \n";

    if (writen(fd, list, sizeof(list)) < sizeof(list)) {
        return -1;
    }

    return 0;
}

/* get user's choice */
unsigned long get_option (int fd) {
    char response[8];
    unsigned long ans;
    ssize_t ok;

    do {
        ok = readline(fd, response, 8, '\n');
        ans = strtoul(response, NULL, 10);
    } while (ans == 0 || ok == -1);

    return ans;
}

/* main function for each thread */
void* thread_func (void *arg) {
    while (1) {
        /* wait for a connection */
        sem_wait(&threads_sem);

        /* obtain the socket fd from the connections queue */
        pthread_mutex_lock(&connq_lock);
        int connfd = pop_conn();
        pthread_mutex_unlock(&connq_lock);

        if (connfd == -1) {
            /* pop_conn() returned an error */
            printf("No connection fd available!\n");
            continue;
        } else if (connfd == 1) {
            /* in case of shutting down the server, kill all threads */
            pthread_exit(NULL);
        }

        /* main loop */
        unsigned long option = 0;
        do {
            if (print_options(connfd) != 0) {
                printf("print_options() failure!\n");
                continue;
            }

            option = get_option(connfd);

            if (option == 1) {
                /* compose message */
                unsigned char ans[MAX_MSG_LEN];
                bzero(ans, MAX_MSG_LEN);
                readline(connfd, ans, MAX_MSG_LEN, ']');

                if (add_msg(ans) != 0) {
                    char err[] = "Whoops... something went wrong! Please try again!";
                    writen(connfd, err, sizeof(err));
                }
            } else if (option == 2) {
                if (list_msg(connfd) == -1) {
                    char err[] = "Whoops... something went wrong! Please try again!";
                    writen(connfd, err, sizeof(err));
                }
            } else if (option == 3) {
                unsigned long msgid = get_option(connfd);

                if (print_msg(connfd, msgid) != 0) {
                    char err[] = "Incorrect message ID! Use [2] to list messages.\n";
                    writen(connfd, err, sizeof(err));
                }
            } else if (option == 4) {
                unsigned long msgid = get_option(connfd);

                if (delete_msg(msgid) != 0) {
                    char err[] = "Incorrect message ID! Use [2] to list messages.\n";
                    writen(connfd, err, sizeof(err));
                }
            }
        } while (option != 5);

        /* cleanup */
        close(connfd);
    }
}

void print_usage() {
    printf("Usage:\n\tcmail -p <PORT_NUMBER> -k <KEY> -i <INIT_VECTOR>\n");
}

int main(int argc, char *argv[]) {
    /* parse command line arguments */
    if (argc != 7) {
        print_usage();
        exit(0);
    }

    int SRV_PORT;
    int c = 0;
    while ( (c = getopt(argc, argv, "p:k:i:")) != -1) {
        switch (c) {
            case 'p':
                SRV_PORT = atoi(optarg);
                break;
            case 'k':
                key = (unsigned char *)optarg;
                break;
            case 'i':
                iv = (unsigned char *)optarg;
                break;
            default:
                print_usage();
                exit(0);
        }
    }
    printf("port: %d\nkey: %s\nIV: %s\n\n", SRV_PORT, key, iv);

    /* shut down gracefully */
    signal(SIGTERM, handleSig);
    signal(SIGINT, handleSig);

    /* create the thread pool */
    pthread_t tids[MAX_THREADS];
    sem_init(&threads_sem, 0, 0);
    pthread_mutex_init(&connq_lock, NULL);

    int i;
    for (i = 0; i < MAX_THREADS; i++) {
        if (pthread_create(&tids[i], NULL, &thread_func, NULL) != 0) {
            perror("creating thread failure");
            exit(0);
        }
    }

    /* start the listening socket */
    struct sockaddr_in servaddr, cliaddr;
    socklen_t slen = sizeof(cliaddr);
    pthread_mutex_init(&listLock, NULL);

    g_listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listenfd == -1) {
        perror("socket creation failed");
        exit(0);
    }

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(SRV_PORT);

    if (bind(g_listenfd, (struct sockaddr*) &servaddr, sizeof(servaddr)) == -1) {
        perror("bind failed");
        close(g_listenfd);
        exit(0);
    }

    if (listen(g_listenfd, 128) == -1) {
        perror("listen failed");
        close(g_listenfd);
        exit(0);
    }

    printf("Server is listening...\n\n");

    for ( ; ; ) {
        int newconn = accept(g_listenfd, (struct sockaddr *) &cliaddr, &slen);

        printf("connection from %s, port %d\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));

        if (push_conn(newconn) != 0) {
            printf("No thread available!\n");
        } else {
            sem_post(&threads_sem);
        }
    }
}