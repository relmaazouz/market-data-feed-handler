#include <cstring>
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

#include <cstdlib>   // strtoull

int main(int argc, char** argv) {

    const uint64_t drop_every = (argc >= 3) ? std::strtoull(argv[2], nullptr, 10) : 0;

    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: %s <itch-file> [drop_every]\n"
                     "  decompress first:  gunzip data/20190130.BX_ITCH_50.gz\n"
                     "  drop_every N: drop every Nth packet to simulate loss (omit = send all)\n",
                     argv[0]);
        return 1;
    }

    int fd = ::open(argv[1], O_RDONLY);
    if (fd < 0) { std::perror("open"); return 1; }

    struct stat st{};
    if (::fstat(fd, &st)) { std::perror("fstat"); ::close(fd); return 1; }
    const std::size_t size = static_cast<std::size_t>(st.st_size);

    void* addr = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) { std::perror("mmap"); ::close(fd); return 1; }
    ::madvise(addr, size, MADV_SEQUENTIAL);
    const auto* data = static_cast<const uint8_t*>(addr);


    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if(sock<0){
        std::perror("socket"); return 1;
    }

    sockaddr_in group{};
    group.sin_family = AF_INET;
    group.sin_port = htons(12345);

    in_addr lo_if{};
    ::inet_pton(AF_INET, "127.0.0.1", &lo_if);
    if (::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &lo_if, sizeof(lo_if)) < 0) {
        std::perror("IP_MULTICAST_IF"); return 1;
    }

    // deliver to receivers on THIS host too
    unsigned char loop = 1;
    if (::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) < 0) {
        std::perror("IP_MULTICAST_LOOP"); return 1;
    }


    std::size_t pos = 0, count = 0;
    uint64_t seq = 1;                                   // 1-based sequence of the first message
    uint8_t  packet[20 + 2 + 65535];                    // MoldUDP header + one framed ITCH message
    const char session[10] = {'B','X','0','0','0','0','0','0','0','0'};   // fixed placeholder for v1

    while(pos + 2 <size){
        
        const uint16_t len = itch::load_be16(data + pos); // reuse for loader

        if (len ==0 || pos+2+len > size){break;}

        std::memcpy(packet,      session,      10);          // [0..9]   session
        itch::store_be64(packet + 10, seq);                  // [10..17] sequence number (be64)
        itch::store_be16(packet + 18, 1);                    // [18..19] message count = 1
        std::memcpy(packet + 20, data + pos, 2 + len);       // [20..]   the [len][body] message, verbatim

        // in the loop, wrap the send:
        if (drop_every == 0 || (seq % drop_every) != 0) {
            const ssize_t sent = ::sendto(sock, packet, 20u + 2u + len, 0,
                                        reinterpret_cast<sockaddr*>(&group), sizeof(group));
            if (sent < 0) { std::perror("sendto"); break; }
            ++count;
        }
        // else: deliberately dropped — simulates a lost packet
        pos += 2 + static_cast<std::size_t>(len);
        
        ++seq;      // increments on BOTH paths — the dropped seq leaves a real hole
        
        if (count >= 5000000) break;   // 5M: builds a big real book, completes without stalling forever

    }

    std::printf("sent %zu packets\n", count);
    ::close(sock);
    ::munmap(addr, size);
    ::close(fd);
    return 0;


}