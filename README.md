# cmail
A multithreaded mail server with OpenSSL encryption, written from scratch in C99.

## Dependencies
* [gcc](https://www.gnu.org/software/gcc/)
* [OpenSSL](https://github.com/openssl/openssl)
* any client program, such as telnet

## Running
To start the server, just run:
```
$ gcc main.c -o cmail -pthread -lcrypto
$ ./cmail -p <PORT_NUMBER> -k <KEY> -i <INIT_VECTOR>
```
where
* `<PORT_NUMBER>` is the desired port on which the server will listen for incoming connections (it's best to choose an ephemeral one, e.g 60000). Starting the server on a port which is already used by the OS will result in a `bind failed: Address already in use` error
* `<KEY>` and `<INIT_VECTOR>` are the cryptographic key and [initialization vector](https://en.wikipedia.org/wiki/Initialization_vector) used by the AES-256 algorithm

A successful attempt to start the server should look like this:
```
$ gcc main.c -o cmail -pthread -lcrypto
$ ./cmail -p 60000 -k smartpassword -i anothersmartpassword
port: 60000
key: smartpassword
IV: anothersmartpassword

Server is listening...
```

The server now created a thread pool (number of threads is set by the `MAX_THREADS` constant, set to 64 by default) and each incoming connection will be handled by one of them.

A new connection can be made from any IP, on the server IP and port. Use a client program to connect, such as telnet:
```
$ telnet 192.x.x.x 60000
```
If you don't have another device, you can connect to the server on localhost. Fire up another terminal and run:
```
$ telnet 127.0.0.1 60000
```

The server logs all connections in the console, so you should see something like
```
connection from 192.x.x.x, port 58932
connection from 127.0.0.1, port 52862
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
* __[1]__ will allow you to type a message (followed by a `]` and ENTER), which will be encrypted and stored on the server, along with a timestamp and a not-so-random ID. Example:
```
1
Wow!

This server is awesome!]

```

* __[2]__ outputs a preview of all messages (preview message length set by the `MAX_PREVIEW_LEN` constant on the server), with their corresponding timestamp and ID. Example (only one message, the one added at the previous step):
```
2
Wow!

This ser
...
@ Mon Jan 7 18:33:22 2019	ID: 1
```

* __[3]__ allows you to enter a message ID and then printing the entire message (if one with the given ID was found). Example:
```
3
1

Wow!

This server is awesome!
@ Mon Jan 7 18:33:22 2019	ID: 1
```

* __[4]__ will delete the message with the specified ID. Example (after which the server deletes the previous message with ID 1):
```
4
1
```

## Encryption remarks
Encryption was done with the help of the [OpenSSL](https://github.com/openssl/openssl) library, with the [AES-256](https://en.wikipedia.org/wiki/Advanced_Encryption_Standard) algorithm. By default, all messages stored on the server are encrypted, with the given cryptographic key and initialization vector in the command line arguments. They are decrypted only when listing messages or when reading one of them.

In a perfect world, the key should be 256 bit and the IV 128 bit and they probably should be safely generated and exchanged with each client when connecting, definitely not stored as plaintext on the server, due to obvious security reasons. Nevertheless, the current approach works fine for educational purposes, but the aforementioned improvement should be implemented in the future (if time permits).

## TODOs
- [X] Thread pool
- [ ] IPv6 support
- [X] Custom server port and key/IV through command line arguments
- [ ] Client program to support safe exchange of cryptographic keys
- [ ] Message sorting by IPs