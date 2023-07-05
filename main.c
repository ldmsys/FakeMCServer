#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#ifndef SOL_TCP
#define SOL_TCP IPPROTO_TCP
#endif
#define SEGMENT_BITS 0x7f
#define CONTINUE_BIT 0x80
#define KICKMODE 0x00
#define MTU 1500
#define PORT 25565
#define DEBUG
#ifdef DEBUG
#define PRINTF_DEBUG printf
#else
void PRINTF_DEBUG(const char * _, ...) {}
#endif

#ifndef htonll
uint64_t htonll(uint64_t value)
{
    // Source: https://stackoverflow.com/questions/3022552
    // The answer is 42
    static const int num = 42;

    // Check the endianness
    if (*reinterpret_cast<const char*>(&num) == num)
    {
        const uint32_t high_part = htonl(static_cast<uint32_t>(value >> 32));
        const uint32_t low_part = htonl(static_cast<uint32_t>(value & 0xFFFFFFFFLL));

        return (static_cast<uint64_t>(low_part) << 32) | high_part;
    } else
    {
        return value;
    }
}
#endif

size_t varintSize(unsigned char varint[4]) {
    int i;
    for(i=0;i<4;i++) if((varint[i] & CONTINUE_BIT) == 0) break;
    return i+1;
}

int varintToint(unsigned char varint[4]) {
    int value = 0;
    char currentByte;

    for(int i=0;i<4;i++) {
        currentByte = varint[i];
        value |= (currentByte & SEGMENT_BITS) << i*7;

        if ((currentByte & CONTINUE_BIT) == 0) break;
    }

    return value;
}

size_t intTovarint(int data, unsigned char* varint) {
    int i;
    for(i=0;i<4;i++) {
        if ((data & ~SEGMENT_BITS) == 0) {
            varint[i] = data;
            break;
        }

        varint[i] = (data & SEGMENT_BITS) | CONTINUE_BIT;

        data >>= 7;
    }
    return i+1;
}

size_t appendLengthvarint(char* string, size_t length, char* mcstring) {
    unsigned char len[4];
    size_t headerlen = intTovarint(length, len);
    memcpy(mcstring, len, headerlen);
    memcpy(mcstring+headerlen, string, length);
    
    return headerlen+length;
}

void handle_session(int datafd);

int main(void) {
    signal(SIGCHLD,SIG_IGN);
    int sockfd, datafd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        perror("socket()"); return 1;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    unsigned int yes = 1;
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&yes, sizeof(yes)) < 0) {
        perror("setsockopt(): SO_REUSEADDR"); return 1;
    }

    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (char *)&yes, sizeof(yes)) < 0) {
        perror("setsockopt(): SO_REUSEPORT"); return 1;
    }

    if(setsockopt(sockfd, SOL_TCP, TCP_NODELAY, (char *)&yes, sizeof(yes)) < 0) {
        perror("setsockopt(): TCP_NODELAY"); return 1;
    }


    if(bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind()"); return 1;
    }

    listen(sockfd, 5);
    clilen = sizeof(cli_addr);
    printf("FakeMCServer 1.12.2(Protocol Version 340)\n");
    printf("\n");
    printf("Written by Dongmin Lee with <3, Distributed under MIT Licence\n");
    printf("Acknowledgement to wiki.vg contributors (https://wiki.vg/Protocol?oldid=13367)\n");
    printf("\n");
    printf("Server is listening on port %d\n", PORT);

    while(1) {
        datafd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if(datafd < 0) {
            perror("accept()"); close(sockfd); return 1;
        }
        int pid = fork();
        if(pid == -1) {
            perror("fork()"); close(sockfd); return 1;
        } else if(pid == 0) {
            // Child process
            handle_session(datafd);
            PRINTF_DEBUG("Handle done!\n");
            exit(0);
        } else {
            // Parent process

        }
    }
    close(sockfd);
    return 0;
}

void handle_session(int datafd) {
        int n, dn, expected_packet_size, mode, nocounter, keepalivecounter, yebiopt, outdated, logged_in;
        n = expected_packet_size = mode = nocounter = keepalivecounter = yebiopt = outdated = logged_in = 0;
        socklen_t clilen;
        unsigned char buf[MTU], returnbuf[MTU], yebibuf[MTU], tmpbuf[MTU];
        struct sockaddr_in serv_addr, cli_addr;
        struct iovec vec[10];

        struct timeval tv_timeo = { 2, 100000 };
        if(setsockopt(datafd, SOL_SOCKET, SO_RCVTIMEO, &tv_timeo, sizeof(tv_timeo)) < 0) {
            perror("setsockopt(): SO_RCVTIMEO"); return;
        }
        memset(&buf, 0, MTU);
        PRINTF_DEBUG("One connected!\n");

        while(1) {
            if(yebiopt) {
                PRINTF_DEBUG("Restoring from yebi! %d\n", yebiopt);
                n = yebiopt;
                memset(&buf, 0, MTU);
                memcpy(buf, yebibuf, yebiopt);
                yebiopt = 0;
            } else {
                PRINTF_DEBUG("Reading... %d, %d\n", mode, nocounter);
                if(nocounter >= 5) {
                    shutdown(datafd, SHUT_RDWR); /*close(datafd);*/ return;
                } else if(keepalivecounter >= 5 && logged_in) {
                        keepalivecounter = 0;

                        PRINTF_DEBUG("Sending keep-alive packet!\n");

                        unsigned char packetID = 0x1f; // Keep Alive
                        long long dummydata = 0;
                        vec[1].iov_base = &packetID;
                        vec[1].iov_len = sizeof(packetID);
                        vec[2].iov_base = &dummydata;
                        vec[2].iov_len = sizeof(dummydata);

                        size_t payloadsize = 0;
                        for(int i=1;i<3;i++) {
                            payloadsize += vec[i].iov_len;
                        }
                        unsigned char payloadsize_varint[4];
                        vec[0].iov_base = payloadsize_varint;
                        vec[0].iov_len = intTovarint(payloadsize, payloadsize_varint);

                        writev(datafd, vec, 3);
                }
                dn = recv(datafd, buf+n, 1500-n, 0);
                if(dn < 0 && !(errno == EAGAIN && logged_in)) {
                    perror("recv()");
                    close(datafd); break;
                } else if (dn == 0) { nocounter++; continue; }
                else if (dn < 0 && errno == EAGAIN && logged_in) { keepalivecounter++; continue; };
                n += dn;
                PRINTF_DEBUG("Read! +%d=%d\n", dn,n);
            }
            
            if(expected_packet_size == 0) {
                expected_packet_size = varintToint(buf);
                PRINTF_DEBUG("Expected packet size: %d\n", expected_packet_size);
            }
            if(expected_packet_size == 0) continue;
            
            if(n < expected_packet_size + varintSize(buf)) {
                PRINTF_DEBUG("Johnbeo! %d < %d\n", n, expected_packet_size + varintSize(buf));
                continue;
            } else if(n > expected_packet_size + varintSize(buf)) {
                PRINTF_DEBUG("Moving! %d > %d\n", n, expected_packet_size + varintSize(buf));
                memset(yebibuf, 0, MTU);
                memcpy(yebibuf, buf+expected_packet_size+varintSize(buf), n-expected_packet_size);
                yebiopt = n-expected_packet_size-varintSize(buf);
                n = expected_packet_size + varintSize(buf);
            }

                // Complete Packet Received
                memset(&returnbuf, 0, MTU);
                unsigned char packetID = buf[varintSize(buf)];
                PRINTF_DEBUG("Packet done! %x %x %x %x %x (%d)\n", buf[0], buf[1], buf[2], buf[3], buf[4], varintSize(buf));
                if(mode == 0) { // pre-handshaking
                    int protocol = varintToint(buf+varintSize(buf)+1);
                    mode = buf[expected_packet_size]; // new mode
                    PRINTF_DEBUG("Handshaking! %d\n", mode);
                    if(packetID != 0 || mode > 2 || mode == 0) {
                        // Undefined behavior
                        PRINTF_DEBUG("Undefined behavior! %d %d\n", packetID, mode);
                        close(datafd); break;
                    }
                    if(protocol != 340) {
                        outdated = 1;
                    }
                } else if(mode == 1) { // Ping Mode
                    PRINTF_DEBUG("Ping Mode!\n");
                    if(packetID == 0) { // MOTD Request
                        char* json = "{\"version\": {\"name\": \"ldmsys 1.12.2\", \"protocol\":340},\"players\":{\"max\":99999,\"online\": 0,\"sample\":[]},\"description\":{\"text\": \"§b§lFakeMCServer Test\"}}";
                        char* packedJSON = (char*)malloc(strlen(json)+6);
                        unsigned char packetID = 0x00;

                        vec[1].iov_base = &packetID;
                        vec[1].iov_len = 1;
                        vec[2].iov_base = packedJSON;
                        vec[2].iov_len = appendLengthvarint(json, strlen(json), packedJSON);

                        size_t payloadsize = 0;
                        for(int i=1;i<=2;i++) {
                            payloadsize += vec[i].iov_len;
                        }
                        unsigned char payloadsize_varint[4];
                        vec[0].iov_base = payloadsize_varint;
                        vec[0].iov_len = intTovarint(payloadsize, payloadsize_varint);

                        writev(datafd, vec, 3);

                    } else if(packetID == 1) { // Ping Request
                        send(datafd, buf, expected_packet_size+varintSize(buf), 0); // Send back the full packet
                    }
                } else if(mode == 2) {
                    if(packetID == 0) { // Login Request
                        if(outdated) {
                            char* json = "{\"text\": \"Outdated server! (Only supports 1.12.2)\"}";

                            char* packedJSON = (char*)malloc(strlen(json)+6);
                            unsigned char packetID = 0x00;

                            vec[1].iov_base = &packetID;
                            vec[1].iov_len = 1;
                            vec[2].iov_base = packedJSON;
                            vec[2].iov_len = appendLengthvarint(json, strlen(json), packedJSON);

                            size_t payloadsize = 0;
                            for(int i=1;i<3;i++) {
                                payloadsize += vec[i].iov_len;
                            }
                            unsigned char payloadsize_varint[4];
                            vec[0].iov_base = payloadsize_varint;
                            vec[0].iov_len = intTovarint(payloadsize, payloadsize_varint);

                            writev(datafd, vec, 3);
                        } else if(KICKMODE) {
                            char* json = "{\"text\": \"You are not white-listed on this server!\"}";

                            char* packedJSON = (char*)malloc(strlen(json)+6);
                            unsigned char packetID = 0x00;

                            vec[1].iov_base = &packetID;
                            vec[1].iov_len = 1;
                            vec[2].iov_base = packedJSON;
                            vec[2].iov_len = appendLengthvarint(json, strlen(json), packedJSON);

                            size_t payloadsize = 0;
                            for(int i=1;i<3;i++) {
                                payloadsize += vec[i].iov_len;
                            }
                            unsigned char payloadsize_varint[4];
                            vec[0].iov_base = payloadsize_varint;
                            vec[0].iov_len = intTovarint(payloadsize, payloadsize_varint);

                            writev(datafd, vec, 3);
                        } else if(!logged_in) {
                            char* uuid = "00000000-0000-0000-0000-000000000000"; // Player UUID

                            char* packedUUID = (char*)malloc(strlen(uuid)+6);
                            unsigned char packetID = 0x02; // Login Success

                            vec[1].iov_base = &packetID;
                            vec[1].iov_len = 1;
                            vec[2].iov_base = packedUUID;
                            vec[2].iov_len = appendLengthvarint(uuid, strlen(uuid), packedUUID);
                            vec[3].iov_base = buf+varintSize(buf)+1;
                            vec[3].iov_len = expected_packet_size-1;

                            size_t payloadsize = 0;
                            for(int i=1;i<4;i++) {
                                payloadsize += vec[i].iov_len;
                            }
                            unsigned char payloadsize_varint[4];
                            vec[0].iov_base = payloadsize_varint;
                            vec[0].iov_len = intTovarint(payloadsize, payloadsize_varint);

                            writev(datafd, vec, 4);

                            packetID = 0x23; // Join Game
                            unsigned int entityID = htonl(0); // Entity ID
                            char gamemode = htons(0); // Survival
                            int dimension = htonl(0); // Overworld
                            char difficulty = htons(0); // Peaceful
                            char unused = 0; // Unused (old max players)
                            char leveltype[10]; size_t leveltypeLen = appendLengthvarint("default", 7, leveltype);
                            char reduced_debug_info = htons(0); // Reduced debug info bit

                            vec[1].iov_base = &packetID;
                            vec[1].iov_len = 1;
                            vec[2].iov_base = &entityID;
                            vec[2].iov_len = sizeof(entityID);
                            vec[3].iov_base = &gamemode;
                            vec[3].iov_len = sizeof(gamemode);
                            vec[4].iov_base = &dimension;
                            vec[4].iov_len = sizeof(dimension);
                            vec[5].iov_base = &difficulty;
                            vec[5].iov_len = sizeof(difficulty);
                            vec[6].iov_base = &unused;
                            vec[6].iov_len = sizeof(unused);
                            vec[7].iov_base = leveltype;
                            vec[7].iov_len = leveltypeLen;
                            vec[8].iov_base = &reduced_debug_info;
                            vec[8].iov_len = sizeof(reduced_debug_info);

                            payloadsize = 0;
                            for(int i=1;i<9;i++) {
                                payloadsize += vec[i].iov_len;
                            }
                            vec[0].iov_base = payloadsize_varint;
                            vec[0].iov_len = intTovarint(payloadsize, payloadsize_varint);

                            writev(datafd, vec, 9);

                            packetID = 0x2f;
                            double X = 0.0f, Y = 99.9f, Z = 0.0f;
                            float yaw = 0.0f, pitch = 0.0f;
                            unsigned char flag = htonl(0);
                            unsigned char teleportId[4]; size_t teleportIdLen = intTovarint(0, teleportId);
                            
                            long long Xn = htonll(*(long long*)&X), Yn = htonll(*(long long*)&Y), Zn = htonll(*(long long*)&Z);
                            int yaw_n = htonl(*(int*)&yaw), pitch_n = htonl(*(int*)&pitch);

                            vec[1].iov_base = &packetID;
                            vec[1].iov_len = 1;
                            vec[2].iov_base = &Xn;
                            vec[2].iov_len = sizeof(Xn);
                            vec[3].iov_base = &Yn;
                            vec[3].iov_len = sizeof(Yn);
                            vec[4].iov_base = &Zn;
                            vec[4].iov_len = sizeof(Zn);
                            vec[5].iov_base = &yaw_n;
                            vec[5].iov_len = sizeof(yaw_n);
                            vec[6].iov_base = &pitch_n;
                            vec[6].iov_len = sizeof(pitch_n);
                            vec[7].iov_base = &flag;
                            vec[7].iov_len = sizeof(flag);
                            vec[8].iov_base = teleportId;
                            vec[8].iov_len = teleportIdLen;

                            payloadsize = 0;
                            for(int i=1;i<9;i++) {
                                payloadsize += vec[i].iov_len;
                            }
                            vec[0].iov_base = payloadsize_varint;
                            vec[0].iov_len = intTovarint(payloadsize, payloadsize_varint);

                            writev(datafd, vec, 9);

                            packetID = 0x0f; // push chat
                            char* json = "{\"translate\":\"chat.type.announcement\",\"with\":[\"Test\",\"It works!\"]}";
                            char type = 0; // Chat (chat box)
                            
                            char* packedJSON = (char*)malloc(strlen(json)+6);

                            vec[1].iov_base = &packetID;
                            vec[1].iov_len = 1;
                            vec[2].iov_base = packedJSON;
                            vec[2].iov_len = appendLengthvarint(json, strlen(json), packedJSON);
                            vec[3].iov_base = &type;
                            vec[3].iov_len = 1;

                            payloadsize = 0;
                            for(int i=1;i<4;i++) {
                                payloadsize += vec[i].iov_len;
                            }
                            vec[0].iov_base = payloadsize_varint;
                            vec[0].iov_len = intTovarint(payloadsize, payloadsize_varint);

                            writev(datafd, vec, 4);

                            logged_in = 1;
                        }
                    }
                }

                n = expected_packet_size = 0;
                memset(&buf, 0, MTU);

        }
    close(datafd);
}