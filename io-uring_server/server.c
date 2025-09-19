// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <liburing.h>

#define PORT 8080
#define QUEUE_DEPTH 256
#define MAX_MESSAGE_LEN 2048

static int client_counter = 0;  // track clients

// Load index.html into memory
char* load_index(size_t *len) {
    FILE *f = fopen("index.html", "r");
    if (!f) {
        perror("index.html not found");
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    rewind(f);

    char *buf = malloc(*len + 1);
    fread(buf, 1, *len, f);
    buf[*len] = '\0';
    fclose(f);
    return buf;
}

// Setup listening socket
int setup_listening_socket() {
    int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(sockfd, SOMAXCONN) < 0) {
        perror("listen");
        exit(1);
    }

    return sockfd;
}

int main() {
    struct io_uring ring;
    int ret;

    // load HTML file
    size_t index_len;
    char *index_html = load_index(&index_len);

    // setup socket
    int sock_listen_fd = setup_listening_socket();
    printf("Server running on port %d\n", PORT);

    // init io_uring
    if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        exit(1);
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (1) {
        // accept connection
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_accept(sqe, sock_listen_fd,
                             (struct sockaddr *)&client_addr,
                             &client_len, 0);

        io_uring_submit(&ring);

        struct io_uring_cqe *cqe;
        io_uring_wait_cqe(&ring, &cqe);

        int client_fd = cqe->res;
        io_uring_cqe_seen(&ring, cqe);

        if (client_fd < 0) {
            perror("accept failed");
            continue;
        }

        // client connected
        client_counter++;
        int this_client = client_counter;
        printf("[Client %d] connected\n", this_client);

        // read request
        char buf[MAX_MESSAGE_LEN];
        sqe = io_uring_get_sqe(&ring);
        io_uring_prep_read(sqe, client_fd, buf, sizeof(buf)-1, 0);
        io_uring_submit(&ring);

        io_uring_wait_cqe(&ring, &cqe);
        ret = cqe->res;
        io_uring_cqe_seen(&ring, cqe);

        if (ret <= 0) {
            printf("[Client %d] disconnected\n", this_client);
            close(client_fd);
            continue;
        }
        buf[ret] = 0; // null terminate request

        // prepare response
        char header[256];
        snprintf(header, sizeof(header),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Length: %zu\r\n"
                 "Content-Type: text/html\r\n"
                 "Connection: close\r\n\r\n",
                 index_len);

        struct iovec iov[2];
        iov[0].iov_base = header;
        iov[0].iov_len = strlen(header);
        iov[1].iov_base = index_html;
        iov[1].iov_len = index_len;

        sqe = io_uring_get_sqe(&ring);
        io_uring_prep_writev(sqe, client_fd, iov, 2, 0);
        io_uring_submit(&ring);

        io_uring_wait_cqe(&ring, &cqe);
        io_uring_cqe_seen(&ring, cqe);

        // close connection
        close(client_fd);
        printf("[Client %d] disconnected\n", this_client);
    }

    free(index_html);
    io_uring_queue_exit(&ring);
    close(sock_listen_fd);
    return 0;
}

