#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_EVENTS 64
#define BUF_SIZE 4096

typedef struct Conn {
    int fd;
    int id;          // client ID
    char *outbuf;
    size_t out_len;
    size_t out_sent;
} Conn;

Conn *conns[FD_SETSIZE];
char *index_html = NULL;
size_t index_len = 0;
int next_client_id = 1;

// Utility
void die(const char *msg) { perror(msg); exit(1); }
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// Build HTTP response
Conn* conn_new(int fd) {
    Conn *c = calloc(1, sizeof(Conn));
    c->fd = fd;
    c->id = next_client_id++;
    c->out_len = 0;
    c->out_sent = 0;
    return c;
}

void build_response(Conn *c) {
    const char *hdr = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n";
    char header[256];
    int hlen = snprintf(header, sizeof(header), hdr, index_len);

    c->out_len = hlen + index_len;
    c->outbuf = malloc(c->out_len);
    memcpy(c->outbuf, header, hlen);
    memcpy(c->outbuf + hlen, index_html, index_len);
}

// Read request and switch to write
void handle_read(int epfd, Conn *c) {
    char buf[BUF_SIZE];
    ssize_t n = read(c->fd, buf, sizeof(buf));
    if (n <= 0) {
        printf("[Client %d] Connection closed by client\n", c->id);
        close(c->fd);
        free(c->outbuf);
        free(c);
        return;
    }
    printf("[Client %d] Received request (%zd bytes)\n", c->id, n);
    build_response(c);
    struct epoll_event ev = { .events = EPOLLOUT, .data.fd = c->fd };
    epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &ev);
}

// Write response
void handle_write(int epfd, Conn *c) {
    while (c->out_sent < c->out_len) {
        ssize_t n = write(c->fd, c->outbuf + c->out_sent, c->out_len - c->out_sent);
        if (n <= 0) break;
        c->out_sent += n;
    }
    if (c->out_sent >= c->out_len) {
        printf("[Client %d] Response sent, closing connection\n", c->id);
        close(c->fd);
        free(c->outbuf);
        free(c);
    }
}

int main() {
    const char *bind_ip = "0.0.0.0";
    int port = 8080;

    // Load index.html
    FILE *f = fopen("index.html", "rb");
    if (!f) die("open index.html");
    fseek(f, 0, SEEK_END);
    index_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    index_html = malloc(index_len);
    fread(index_html, 1, index_len, f);
    fclose(f);

    signal(SIGPIPE, SIG_IGN);

    // Create listening socket
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) die("socket");
    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    set_nonblocking(lfd);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, bind_ip, &addr.sin_addr);

    if (bind(lfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) die("bind");
    if (listen(lfd, 128) < 0) die("listen");

    // Epoll
    int epfd = epoll_create1(0);
    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);

    printf("Server running on %s:%d\n", bind_ip, port);
    printf("Waiting for connections...\n");

    while (1) {
        int n = epoll_wait(epfd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            uint32_t evt = events[i].events;

            if (fd == lfd) {
                // Accept new connections
                for (;;) {
                    int cfd = accept(lfd, NULL, NULL);
                    if (cfd < 0) break;
                    set_nonblocking(cfd);
                    Conn *c = conn_new(cfd);
                    conns[cfd] = c;
                    ev.events = EPOLLIN | EPOLLET;
                    ev.data.fd = cfd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
                    printf("[Client %d] Connection accepted\n", c->id);
                }
            } else {
                Conn *c = conns[fd];
                if (!c) continue;
                if (evt & EPOLLIN) handle_read(epfd, c);
                if (evt & EPOLLOUT) handle_write(epfd, c);
            }
        }
    }

    return 0;
}

