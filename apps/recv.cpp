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
#include <cerrno>

int main() {
    std::printf("Started listening");
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


    timeval tv{2, 0};   // stop after 2s of silence (end of feed)
    if (::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        std::perror("SO_RCVTIMEO"); return 1;
    }

    std::printf("listening on 239.1.1.1:12345\n");
    std::fflush(stdout);




    uint8_t buf[2048];
    itch::OrderBook book;
    std::size_t count = 0;
    
    while (true) {
        const ssize_t n = ::recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n < 0) { 
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            std::perror("recvfrom"); 
            break;
            
        }
        
        const uint16_t len = itch::load_be16(buf);
        itch::dispatch(book,buf + 2, len);
        
        //if (++count >= 10000) break;    // cap for the first test
        ++count;
    }

    std::printf("received %zu messages | peak %zu  final orders %zu\n",
                count, book.peak, book.orders.size());
    ::close(sock);


    return 0;

}