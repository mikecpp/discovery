#include <stdio.h>
#include <vector>
#include <arpa/inet.h>
#include "icmp.h"

using namespace std;

int getIpRange(vector<string>&ipList, string start, string end)
{
    struct in_addr ip_start, ip_end;

    // Convert string to long 
    ip_start.s_addr = htonl(inet_addr(start.c_str()));
    ip_end.s_addr   = htonl(inet_addr(end.c_str()));

    if (ip_start.s_addr == -1 || ip_end.s_addr == -1) 
        return -1;

    if (ip_start.s_addr >= ip_end.s_addr) 
        return -2;

    struct in_addr ip_addr;
    string ipStr;
    for (long i=ip_start.s_addr; i<=ip_end.s_addr; i++) {
        ip_addr.s_addr = ntohl(i); 
        ipStr = inet_ntoa(ip_addr);
        ipList.push_back(ipStr.c_str());
    }

    return 0;
}

int main(int argc, char* argv[])
{
    if (argc != 3) {
        printf("syntax: discover <ip-start> <ip-end>\n");
        return -1;
    }

    vector<string> ipList;
    if (getIpRange(ipList, argv[1], argv[2]) != 0) {
        printf("getIpRange() fail!\n");
        return -1;
    }

    Icmp icmp;
    vector<string>::iterator it;
    for (it = ipList.begin(); it != ipList.end(); ++it) {
        if (icmp.ping(it->c_str()) == 0) 
            printf("%s is alive\n", it->c_str());
        else 
            printf("%s is time out\n", it->c_str());
    }

    return 0;
}
