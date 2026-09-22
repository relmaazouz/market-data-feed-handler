#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <itch/itch.hpp>

int main(int argc, char** argv) {
    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { std::perror("socket"); return 1; }

    int reuse = 1;
    if (::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::perror("SO_REUSEADDR"); return 1;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port   = htons(12345);
    local.sin_addr.s_addr = htonl(INADDR_ANY);          // bind to the port on any interface
    if (::bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
        std::perror("bind"); return 1;
    }

    ip_mreq mreq{};
    ::inet_pton(AF_INET, "239.1.1.1", &mreq.imr_multiaddr);          // the group
    ::inet_pton(AF_INET, "127.0.0.1", &mreq.imr_interface);          // on loopback (matches sender)
    if (::setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        std::perror("IP_ADD_MEMBERSHIP"); return 1;
    }


    uint8_t buf[2048];
    std::size_t count = 0;
    while (true) {
        const ssize_t n = ::recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n < 0) { std::perror("recvfrom"); break; }
        std::printf("recv %zd bytes  (msg type '%c')\n", n, static_cast<char>(buf[2]));
        if (++count >= 20) break;    // cap for the first test
    }
    ::close(sock);


    return 0;

}