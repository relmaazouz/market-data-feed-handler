#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <itch/itch.hpp>

int main() {
    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { std::perror("socket"); return 1; }

    int reuse = 1;
    if (::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::perror("SO_REUSEADDR"); return 1;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port   = htons(12345);
    local.sin_addr.s_addr = htonl(INADDR_ANY);          // bind the port on any interface
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

    uint64_t expected_seq = 1;
    uint64_t gaps = 0;

    while (true) {
        const ssize_t n = ::recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;   // timeout: feed is over
            std::perror("recvfrom"); break;                       // real error
        }

        // MoldUDP64 header: sequence at offset 10, message count at offset 18.
        const uint64_t seq       = itch::load_be64(buf + 10);
        const uint16_t msg_count = itch::load_be16(buf + 18);

        if (seq > expected_seq) {                 // gap: sequence jumped — messages were lost
            const uint64_t missing = seq - expected_seq;
            gaps += missing;
            std::printf("GAP at seq %llu: expected %llu, lost %llu\n",
                        (unsigned long long)seq,
                        (unsigned long long)expected_seq,
                        (unsigned long long)missing);
        }

        if (seq >= expected_seq) {                // process in-order / first-seen packets, skip duplicates
            const uint16_t len = itch::load_be16(buf + 20);   // message starts after the 20-byte header
            itch::dispatch(book, buf + 22, len);
            ++count;
            expected_seq = seq + msg_count;       // advance to the next expected sequence
        }
    }

    std::printf("received %zu messages | gaps %llu | peak %zu  final orders %zu\n",
                count, (unsigned long long)gaps, book.peak, book.orders.size());
    ::close(sock);
    return 0;
}
