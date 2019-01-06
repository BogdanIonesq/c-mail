# cmail
A multithreaded mail server with OpenSSL encryption, written from scratch in C99.

## Dependencies
* [gcc](https://www.gnu.org/software/gcc/)
* [OpenSSL](https://github.com/openssl/openssl)
* any client program, such as telnet

## Running
To start the server:
```
$ gcc main.c -o cmail -pthread -lcrypto
$ ./cmail -p <PORT_NUMBER> -k <KEY> -i <INIT_VECTOR>
```
The server is now running and listening for connections from any IP on the given port. Use a client program to connect, such as telnet:
```
$ telnet 192.x.x.x 60000
```
The server logs all connections in the console, so you should see something like
```
Listening...
connection from 192.x.x.x, port 58932
```
The client should see the following menu:
```
[1] Compose message.
[2] List messages.
[3] Read message.
[4] Delete message.
[5] EXIT.
```
Each option is explained below:
* __[1]__ will allow you to type a message (followed by a `]` and ENTER), which will be encrypted and stored on the server, along with a timestamp and a not-so-random ID
* __[2]__ outputs a preview of all messages (preview message length set by the `MAX_PREVIEW_LEN` constant on the server), with their corresponding timestamp and ID
* __[3]__ allows you to enter a message ID and then printing the entire message (if one with the given ID was found)
* __[4]__ will delete the message with the specified ID

## Encryption remarks
Encryption was done with the help of the [OpenSSL](https://github.com/openssl/openssl) library, with the [AES-256](https://en.wikipedia.org/wiki/Advanced_Encryption_Standard) algorithm. By default, all messages stored on the server are encrypted, with the given cryptographic key and initialization vector in the command line arguments. They are decrypted only when listing messages or when reading one of them.

In a perfect world, the key should be 256 bit and the IV 128 bit and they probably should be safely generated and exchanged with each client when connecting, definitely not stored as plaintext on the server, due to obvious security reasons. Nevertheless, the current approach works fine for educational purposes, but the aforementioned improvement should be implemented in the future (if time permits).

## TODOs
- [ ] IPv6 support
- [X] Custom server port and key/IV through command line arguments
- [ ] Client program to support safe exchange of cryptographic keys
- [ ] Message sorting by IPs