#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <itch/itch.hpp>
#include <vector>
#include <cstring>

#include <cstdlib>   // strtoull


static bool send_record(int sock, const sockaddr_in& group, const char* session, uint64_t seq, const uint8_t* rec){
    const uint16_t len = itch::load_be16(rec);
    uint8_t packet[20 + 2 + 65535];                    // MoldUDP header + one framed ITCH message
    std::memcpy(packet,      session,      10);          // [0...9]   session
    itch::store_be64(packet + 10, seq);                  // [10...17] sequence number (be64)
    itch::store_be16(packet + 18, 1);                    // [18...19] message count = 1
    std::memcpy(packet + 20, rec, 2 + len);              // [20...]   the [len][body] message, verbatim
    if(::sendto(sock, packet, 20u + 2u + len, 0,
                 reinterpret_cast<const sockaddr*>(&group), sizeof(group)) < 0) {
        std::perror("sendto");
        return false;
    }
    return true;
}


static bool service_request(int req_sock, int sock, const sockaddr_in& group, const char* session, const std::vector<size_t>& offsets, const uint8_t* data, int flags, uint64_t& retransmitted){
    // independent of the transmit path, check for retransmission requests from the receiver
    uint8_t req[20];
    const ssize_t r = recvfrom(req_sock, req, sizeof(req), flags, nullptr, nullptr);
    if(r!=20){return false;}
    
    const uint64_t first_missing = itch::load_be64(req + 10);
    const uint16_t missing_count = itch::load_be16(req + 18);

    std::printf("retransmission %llu..%llu\n", (unsigned long long)first_missing, (unsigned long long)(first_missing + missing_count -1));

    for (uint64_t s = first_missing; s < first_missing + missing_count; ++s) {
        if (s == 0 || s > offsets.size()) {
            std::fprintf(stderr, "invalid retransmission request: %llu\n", (unsigned long long)s);
            continue;
        }
        const size_t offset = offsets[s - 1];
        
        if (!send_record(sock, group, session, s, data + offset)){break;}
        ++retransmitted;
    }

    return true;
}

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

    if (::inet_pton(AF_INET, "239.1.1.1", &group.sin_addr) != 1) {
        std::fprintf(stderr, "invalid multicast address\n");
        return 1;
    }

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

    //creating a second for retransmission of dropped packets
    int req_sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (req_sock < 0) { std::perror("socket req"); return 1; }

    sockaddr_in req_addr{};
    req_addr.sin_family      = AF_INET;
    req_addr.sin_port        = htons(12346);
    ::inet_pton(AF_INET, "127.0.0.1", &req_addr.sin_addr);   // loopback, matches receiver's destination
    if (::bind(req_sock, reinterpret_cast<sockaddr*>(&req_addr), sizeof(req_addr)) < 0) {
        std::perror("bind req"); return 1;
    }


    std::size_t pos = 0, count = 0;
    uint64_t seq = 1;                                   // 1-based sequence of the first message
    uint64_t retransmitted = 0;                           // count of retransmitted messages

    const char session[10] = {'B','X','0','0','0','0','0','0','0','0'};   // fixed placeholder for v1

    std::vector<size_t> offsets;   // offsets[s-1] = file offset of message with sequence s

    while(pos + 2 <size){
        offsets.push_back(pos);  // record the file offset of this message for later random access

        const uint16_t len = itch::load_be16(data + pos); // reuse for loader

        if (len ==0 || pos+2+len > size){break;}

        // in the loop, wrap the send:
        if (drop_every == 0 || (seq % drop_every) != 0 || seq == 5000000) {
            if(!send_record(sock, group, session, seq, data + pos)){
                break;
            }
            ++count;
        } 

        service_request(req_sock, sock, group, session, offsets, data, MSG_DONTWAIT,retransmitted);

        pos += 2 + static_cast<std::size_t>(len);
        
        ++seq;      // increments on BOTH paths — the dropped seq leaves a real hole
        
        if (seq > 5000000) break;   // 5M: builds a big real book, completes without stalling forever

    }

    timeval tv{2, 0};
    ::setsockopt(req_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    while (service_request(req_sock, sock, group, session, offsets, data, 0, retransmitted)) {}

    std::printf("sent %zu first-pass + %llu retransmitted = %llu total\n",
            count, (unsigned long long)retransmitted,
            (unsigned long long)(count + retransmitted));
    ::close(sock);
    ::munmap(addr, size);
    ::close(fd);
    return 0;
}