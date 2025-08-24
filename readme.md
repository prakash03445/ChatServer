# ChatServer

A simple **C++ chat server** using **epoll** for efficient handling of multiple clients.  
This project demonstrates a high-performance, event-driven server using **non-blocking sockets**.

### Building

```bash
mkdir build
cd build
cmake ..
make
```

### Running the server

```bash
./chat-server <port>
```
* If no port is specified, the server will run  on the default port 8080.

### Using the server

Connect via telnet:
```bash
telnet 127.0.0.1 <port>
```
* Enter your username.
* Type a message and press Enter to broadcast to all connected clients.
* To exit ***telnet***, press `ctrl + ]` and then type `quit`.

#### Connecting multiple clients:
* Open multiple terminals
* In each terminal run the same telnet command:

```bash
telnet 127.0.0.1 <port>
```
