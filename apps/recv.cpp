#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <itch/itch.hpp>
#include <vector>
#include <map>

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
    uint64_t requested_seq = 0;

    std::map<uint64_t, std::vector<uint8_t>> buffer;   // store missing messages for retransmission
    auto apply = [&](const uint8_t* msg, uint16_t len) {
        itch::dispatch(book, msg, len);
        ++count;
        ++expected_seq;          // one message per packet in this version
    };

    while (true) {
        const ssize_t n = ::recvfrom(sock, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;   // timeout: feed is over
            std::perror("recvfrom"); break;                       // real error
        }

        // MoldUDP64 header: sequence at offset 10, message count at offset 18.
        const uint64_t seq       = itch::load_be64(buf + 10);
        const uint16_t len = itch::load_be16(buf + 20);   // this packet's ITCH message length

        if(seq < expected_seq){   // ignore out-of-order messages
            continue;
        }else if(seq == expected_seq){
            apply(buf + 22, len);
            //drain apply buffer messages that are now contiguous with the expected sequence
            while(!buffer.empty() && buffer.begin()->first == expected_seq) {
                auto msg = buffer.begin();
                apply(msg->second.data(),static_cast<uint16_t>(msg->second.size()));
                buffer.erase(buffer.begin());
            }
        } else {  
                if (expected_seq != requested_seq) {                // gap: sequence jumped — messages were lost
                uint8_t req[20];
                std::memcpy(req, buf, 10);   // session ID at offset 0

                itch::store_be64(req + 10, expected_seq);     // first missing
                itch::store_be16(req + 18, static_cast<uint16_t>(seq - expected_seq));   // count of missing
                std::printf("GAP at seq %llu: expected %llu, lost %llu\n",
                            (unsigned long long)seq,
                            (unsigned long long)expected_seq,
                            (unsigned long long)(seq - expected_seq));
                
                // send a retransmission request for the missing messages
                sockaddr_in req_dest{};
                req_dest.sin_family = AF_INET;
                req_dest.sin_port   = htons(12346);
                ::inet_pton(AF_INET, "127.0.0.1", &req_dest.sin_addr);   // sender's request port
                if (::sendto(sock, req, sizeof(req), 0,
                            reinterpret_cast<sockaddr*>(&req_dest), sizeof(req_dest)) < 0) {
                    std::perror("sendto"); break;
                }
                requested_seq = expected_seq;   // don't request again until we get a response
                }
            buffer.emplace(seq, std::vector<uint8_t>(buf + 22, buf + 22 + len));
        }
    }

    std::printf("received %zu messages | peak %zu  final orders %zu | buffered %zu\n",
                count, book.peak, book.orders.size(), buffer.size());
    ::close(sock);
    return 0;


}