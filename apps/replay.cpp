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

    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: %s <itch-file>\n"
                     "  decompress first:  gunzip data/20190130.BX_ITCH_50.gz\n",
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

    while(pos + 2 <size){
        
        const uint16_t len = itch::load_be16(data + pos); // reuse for loader

        if (len ==0 || pos+2+len > size){break;}

        const ssize_t sent = ::sendto(sock, data+ pos, 2 + len,0,reinterpret_cast<sockaddr*>(&group), sizeof(group));

        if(sent < 0){
            std::perror("sendto");
            break;
        }

        pos += 2+static_cast<std::size_t>(len);

        if (++count >= 1000) break;   // cap for the first tcpdump test; remove later

    }

    std::printf("sent %zu packets\n", count);
    ::close(sock);
    ::munmap(addr, size);
    ::close(fd);
    return 0;


}