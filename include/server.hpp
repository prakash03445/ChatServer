#ifndef SERVER_HPP
#define SERVER_HPP

#include <vector>
#include <deque>
#include <string>
#include <unordered_map>
#include <cstdint>

class Server {
    public:
        explicit Server(int port);
        void start();

    private:
        struct Client {
            int fd;
            bool named = false;
            std::string name;
            std::string inbuf;
            std::deque<std::string> outq;
        };

        int port;
        int server_fd = -1;
        int epfd = -1;

        std::unordered_map<int, Client> clients;

        bool setup_listener();
        static bool set_nonblocking(int fd);
        bool add_epoll(int fd, uint32_t events);
        bool mod_epoll(int fd, uint32_t events);
        void del_epoll(int fd);

        void accept_new();
        void handle_read(int fd);
        void handle_write(int fd);
        void close_client(int fd, const char* reason);

        void on_line(Client& c, const std::string& line);
        void broadcast(const std::string& msg, int except_fd = -1);
        static void trim_newlines(std::string& s);
};

#endif //SERVER_HPP