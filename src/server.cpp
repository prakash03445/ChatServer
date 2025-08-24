#include "server.hpp"

#include <iostream>
#include <cstring>
#include <algorithm>
#include <cerrno>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>

Server::Server(int port) : port(port) {}

bool Server::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

bool Server::setup_listener() {
    server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return false; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return false; }
    if (!set_nonblocking(server_fd)) { perror("fcntl O_NONBLOCK (listener)"); return false; }
    if (listen(server_fd, 512) < 0) { perror("listen"); return false; }
    return true;
}

bool Server::add_epoll(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == 0;
}

bool Server::mod_epoll(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    return epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) == 0;
}

void Server::del_epoll(int fd) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
}

void Server::start() {
    if (!setup_listener()) return;

    epfd = epoll_create1(0);
    if (epfd < 0) { perror("epoll_create1"); return; }

    if (!add_epoll(server_fd, EPOLLIN)) { perror("epoll_ctl ADD listen"); return; }

    std::cout << "Server (epoll) listening on port " << port << "\n";

    std::vector<epoll_event> events(1024);

    while (true) {
        int n = epoll_wait(epfd, events.data(), static_cast<int>(events.size()), -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == server_fd) {
                accept_new();
                continue;
            }

            if (ev & (EPOLLERR | EPOLLHUP)) {
                close_client(fd, "EPOLLERR/HUP");
                continue;
            }

            if (ev & EPOLLIN) handle_read(fd);
            if (ev & EPOLLOUT) handle_write(fd);
        }
    }
}

void Server::accept_new() {
    while (true) {
        sockaddr_in caddr{};
        socklen_t clen = sizeof(caddr);
        int cfd = accept(server_fd, (sockaddr*)&caddr, &clen);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("accept");
            break;
        }
        if (!set_nonblocking(cfd)) {
            perror("fcntl O_NONBLOCK (client)");
            ::close(cfd);
            continue;
        }
        if (!add_epoll(cfd, EPOLLIN | EPOLLET)) {
            perror("epoll_ctl ADD client");
            ::close(cfd);
            continue;
        }

        Client c{};
        c.fd = cfd;
        clients[cfd] = std::move(c);

        std::string prompt = "Enter your name: ";
        clients[cfd].outq.push_back(prompt);
        mod_epoll(cfd, EPOLLIN | EPOLLOUT | EPOLLET);
    }
}

void Server::handle_read(int fd) {
    auto it = clients.find(fd);
    if (it == clients.end()) return;
    Client& c = it->second;

    char buf[4096];
    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            c.inbuf.append(buf, static_cast<size_t>(n));
        } else if (n == 0) {
            close_client(fd, "peer closed");
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            perror("recv");
            close_client(fd, "recv error");
            return;
        }
    }

    size_t pos = 0;
    while (true) {
        size_t nl = c.inbuf.find('\n', pos);
        if (nl == std::string::npos) {
            if (pos > 0) c.inbuf.erase(0, pos);
            break;
        }
        std::string line = c.inbuf.substr(pos, nl - pos);
        pos = nl + 1;
        trim_newlines(line);
        on_line(c, line);
    }
}

void Server::handle_write(int fd) {
    auto it = clients.find(fd);
    if (it == clients.end()) return;
    Client& c = it->second;

    while (!c.outq.empty()) {
        const std::string& front = c.outq.front();
        ssize_t sent = ::send(fd, front.data(), front.size(), 0);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            perror("send");
            close_client(fd, "send error");
            return;
        }
        if (static_cast<size_t>(sent) < front.size()) {
            c.outq.front().erase(0, static_cast<size_t>(sent));
            return;
        }
        c.outq.pop_front();
    }

    mod_epoll(fd, EPOLLIN | EPOLLET);
}

void Server::close_client(int fd, const char* reason) {
    auto it = clients.find(fd);
    if (it != clients.end()) {
        Client c = std::move(it->second);
        clients.erase(it);
        del_epoll(fd);
        ::close(fd);

        if (c.named) {
            std::string leave = c.name + " has left the chat.\n";
            broadcast(leave, fd);
        }

        std::cerr << "Closed client fd=" << fd << " (" << (reason ? reason : "") << ")\n";
    } else {
        del_epoll(fd);
        ::close(fd);
    }
}

void Server::on_line(Client& c, const std::string& line) {
    if (!c.named) {
        std::string uname = line;
        trim_newlines(uname);
        if (uname.empty()) {
            c.outq.push_back("Name cannot be empty. Enter your name: ");
            mod_epoll(c.fd, EPOLLIN | EPOLLOUT | EPOLLET);
            return;
        }
        c.named = true;
        c.name = uname;

        std::string joined = c.name + " has joined the chat.\n";
        broadcast(joined, c.fd);
        return;
    }

    std::string msg = c.name + ": " + line + "\n";
    broadcast(msg, c.fd);
}

void Server::broadcast(const std::string& msg, int except_fd) {
    std::vector<int> to_close;

    for (auto& [fd, cl] : clients) {
        if (fd == except_fd) continue;

        if (cl.outq.empty()) {
            ssize_t sent = ::send(fd, msg.data(), msg.size(), 0);
            if (sent == static_cast<ssize_t>(msg.size())) continue;
            if (sent >= 0 && static_cast<size_t>(sent) < msg.size()) {
                cl.outq.emplace_back(msg.substr(static_cast<size_t>(sent)));
            } else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                cl.outq.push_back(msg);
            } else if (sent < 0) {
                if (!(errno == ECONNRESET || errno == EPIPE)) {
                    perror("send (broadcast)");
                }
                to_close.push_back(fd);
                continue;
            }
            mod_epoll(fd, EPOLLIN | EPOLLOUT | EPOLLET);
        } else {
            cl.outq.push_back(msg);
            mod_epoll(fd, EPOLLIN | EPOLLOUT | EPOLLET);
        }
    }

    for (int fd : to_close) {
        close_client(fd, "broadcast send error");
    }
}


void Server::trim_newlines(std::string& s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
}
