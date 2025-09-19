#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8080
#define BUFFER_SIZE 1024

void serve_html(int client_socket) {
    // Read the HTML file content
    FILE *html_file = fopen("index.html", "r");
    if (html_file == NULL) {
        perror("Failed to open index.html");
        // Send a 404 Not Found response if the file isn't found
        const char *response_404 = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nFile Not Found";
        send(client_socket, response_404, strlen(response_404), 0);
        return;
    }

    // Read the entire file content into a buffer
    fseek(html_file, 0, SEEK_END);
    long file_size = ftell(html_file);
    fseek(html_file, 0, SEEK_SET);

    char *html_content = malloc(file_size + 1);
    fread(html_content, 1, file_size, html_file);
    html_content[file_size] = '\0';
    fclose(html_file);

    // Construct the HTTP response headers
    char headers[BUFFER_SIZE];
    snprintf(headers, sizeof(headers),
             "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n",
             file_size);

    // Send the headers and the HTML content
    send(client_socket, headers, strlen(headers), 0);
    send(client_socket, html_content, file_size, 0);

    free(html_content);
}

int main() {
    int server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Create a socket: IPv4 (AF_INET) and TCP (SOCK_STREAM)
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Bind the socket to the specified port and address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on any available network interface
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 1) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    while (1) {
        // Accept a new connection (this is the blocking call)
        if ((client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        printf("Connection accepted.\n");

        // Read the client's request (optional for this simple server)
        char buffer[BUFFER_SIZE] = {0};
        read(client_socket, buffer, BUFFER_SIZE);
        // printf("Request:\n%s\n", buffer); // Uncomment to see the request

        // Serve the HTML content to the client
        serve_html(client_socket);

        // Close the client socket
        close(client_socket);
        printf("Connection closed.\n");
    }

    close(server_fd); // This part is technically unreachable, but good practice.
    return 0;
}
