/*
 *Connects to a VNC server over TCP.
 *Performs the RFB handshake.
 *Sends a framebuffer update request.
 *Prints raw pixel data received (limited).
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT 5900

void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void send_all(int sock, const void *buf, size_t len) {
    size_t total_sent = 0;
    const char *ptr = (const char *)buf;
    while (total_sent < len) {
        ssize_t sent = send(sock, ptr + total_sent, len - total_sent, 0);
        if (sent <= 0) die("send");
        total_sent += sent;
    }
}

void recv_all(int sock, void *buf, size_t len) {
    size_t total_recv = 0;
    char *ptr = (char *)buf;
    while (total_recv < len) {
        ssize_t recvd = recv(sock, ptr + total_recv, len - total_recv, 0);
        if (recvd <= 0) die("recv");
        total_recv += recvd;
    }
}

int main() {
    int sock;
    struct sockaddr_in server;
    char buffer[1024];

    // 1. Connect to server
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) die("socket");

    server.sin_family = AF_INET;
    server.sin_port = htons(SERVER_PORT);
    server.sin_addr.s_addr = inet_addr(SERVER_HOST);

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0)
        die("connect");

    // 2. Receive RFB version
    char version[13] = {0};
    recv_all(sock, version, 12);
    printf("Server version: %s\n", version);

    // 3. Send back same version (3.3)
    const char *client_version = "RFB 003.003\n";
    send_all(sock, client_version, 12);

    // 4. Receive security type (1 byte for 3.3)
    uint32_t sec_type;
    recv_all(sock, &sec_type, 4);
    sec_type = ntohl(sec_type);
    if (sec_type == 0) {
        uint32_t reason_len;
        recv_all(sock, &reason_len, 4);
        reason_len = ntohl(reason_len);
        char *reason = malloc(reason_len + 1);
        recv_all(sock, reason, reason_len);
        reason[reason_len] = 0;
        printf("Connection failed: %s\n", reason);
        free(reason);
        close(sock);
        return 1;
    }
    printf("Security type: %d\n", sec_type);

    // 5. No authentication for type 1, continue

    // 6. Send ClientInit message (1 byte)
    char shared_flag = 1; // share the desktop
    send_all(sock, &shared_flag, 1);

    // 7. Receive ServerInit message
    uint16_t width, height;
    recv_all(sock, &width, 2);
    recv_all(sock, &height, 2);
    width = ntohs(width);
    height = ntohs(height);
    printf("Framebuffer: %dx%d\n", width, height);

    // Skip full pixel format (16 bytes), name length (4 bytes), name
    recv_all(sock, buffer, 16); // pixel format
    uint32_t name_len;
    recv_all(sock, &name_len, 4);
    name_len = ntohl(name_len);
    char *name = malloc(name_len + 1);
    recv_all(sock, name, name_len);
    name[name_len] = '\0';
    printf("Desktop name: %s\n", name);
    free(name);

    // 8. Send FramebufferUpdateRequest
    uint8_t msg_type = 3; // FramebufferUpdateRequest
    uint8_t incremental = 0;
    uint16_t x = 0, y = 0, w = width, h = height;
    char fbuf_req[10];
    fbuf_req[0] = msg_type;
    fbuf_req[1] = incremental;
    *(uint16_t *)(fbuf_req + 2) = htons(x);
    *(uint16_t *)(fbuf_req + 4) = htons(y);
    *(uint16_t *)(fbuf_req + 6) = htons(w);
    *(uint16_t *)(fbuf_req + 8) = htons(h);
    send_all(sock, fbuf_req, 10);

    // 9. Read FramebufferUpdate
    uint8_t update_msg_type;
    recv_all(sock, &update_msg_type, 1);
    if (update_msg_type != 0) {
        printf("Unexpected message type: %d\n", update_msg_type);
        close(sock);
        return 1;
    }

    recv_all(sock, buffer, 1); // padding
    uint16_t num_rects;
    recv_all(sock, &num_rects, 2);
    num_rects = ntohs(num_rects);
    printf("Number of rectangles: %d\n", num_rects);

    for (int i = 0; i < num_rects; i++) {
        uint16_t rx, ry, rw, rh;
        uint32_t encoding;
        recv_all(sock, &rx, 2);
        recv_all(sock, &ry, 2);
        recv_all(sock, &rw, 2);
        recv_all(sock, &rh, 2);
        recv_all(sock, &encoding, 4);
        rx = ntohs(rx); ry = ntohs(ry);
        rw = ntohs(rw); rh = ntohs(rh);
        encoding = ntohl(encoding);

        printf("Rectangle %d: %dx%d at (%d,%d), encoding %d\n", i+1, rw, rh, rx, ry, encoding);

        size_t data_len = rw * rh * 4; // assuming 32bpp (RGBA)
        char *pixel_data = malloc(data_len);
        recv_all(sock, pixel_data, data_len);

        // You could render this data with a GUI toolkit like SDL
        // For now, just show first pixel
        uint8_t r = pixel_data[0];
        uint8_t g = pixel_data[1];
        uint8_t b = pixel_data[2];
        printf("First pixel RGB: %d %d %d\n", r, g, b);

        free(pixel_data);
    }

    close(sock);
    return 0;
}

