# cmail
A multithreaded mail server written from scratch in C99.

## Dependencies
* [gcc](https://www.gnu.org/software/gcc/)
* [OpenSSL](https://github.com/openssl/openssl)
* any client program, such as telnet

## Running
```
$ gcc main.c -o cmail -pthread -lcrypto
$ ./cmail
```

## TODOs
- [ ] IPv6 support
- [ ] Custom server port and key/IV through command line arguments
- [ ] Client program to support safe exchange of cryptographic keys
- [ ] Message sorting by IPs