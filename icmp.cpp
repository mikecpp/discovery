#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include "icmp.h"

#define DATA_LENGTH           64
#define PACKET_LENGTH         sizeof(struct icmphdr) + DATA_LENGTH

Icmp::Icmp(int timeout) : m_lenData(DATA_LENGTH), m_timeout(timeout), m_sequence(0)
{
    m_s = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
    if (m_s < 0) { 
        printf("socket() fail !\n");
        return;  
    } 
}

Icmp::~Icmp()
{
    ::close(m_s);
}

int Icmp::ping(string ip)
{
    // ICMP Header
    struct icmphdr icmp_hdr;   
    memset(&icmp_hdr, 0, sizeof(icmp_hdr));
    icmp_hdr.type       = ICMP_ECHO;
    icmp_hdr.un.echo.id = 1234;

    static char packet[PACKET_LENGTH];
    int ret = 0;

    // Send ICMP packet
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    icmp_hdr.un.echo.sequence = m_sequence++;
    memcpy(packet, &icmp_hdr, sizeof(icmp_hdr));
    char* ptr = packet + sizeof(icmp_hdr);
    for (int i=0; i<DATA_LENGTH; i++) {
        *ptr = i;
    }
    ret = ::sendto(m_s, packet, PACKET_LENGTH, 0, (struct sockaddr*) &addr, sizeof(addr));
    if (ret <= 0) // send fail
        return ICMP_SEND_FAIL;

    // Select for timeout
    fd_set fdRead;
    struct timeval timeout = {m_timeout, 0};
    memset(&fdRead, 0, sizeof(fdRead));
    FD_SET(m_s, &fdRead);
    ret = ::select(m_s + 1, &fdRead, NULL, NULL, &timeout);
    if (ret == 0) // time out
        return ICMP_TIMEOUT;

    if (ret < 0)  // seelct fail
        return ICMP_SELECT_FAIL;

    // Recv ICMP packet
    int length;
    ret = ::recvfrom(m_s, packet, sizeof(packet), 0, NULL, (socklen_t*) &length);
    if (ret <= 0) // recv fail
        return ICMP_RECV_FAIL;

    struct icmphdr hdrRecv;       
    memcpy(&hdrRecv, packet, sizeof(packet));
    if (hdrRecv.type != ICMP_ECHOREPLY) 
        return ICMP_TYPE_FAIL;

    return ICMP_OK;
}
