#ifndef __ICMP_H__
#define __ICMP_H__

#include <string>

using namespace std;

#define ICMP_OK                  0
#define ICMP_TIMEOUT             1

#define ICMP_SEND_FAIL          -1
#define ICMP_SELECT_FAIL        -2
#define ICMP_RECV_FAIL          -3
#define ICMP_TYPE_FAIL          -4

#define DEFAULT_TIMEOUT          1

class Icmp {
public:
    Icmp(int timeout = DEFAULT_TIMEOUT);
    ~Icmp();

	int ping(string ip); 

public:
	int m_lenData;
	int m_timeout;

private:
    int m_sequence;
    int m_s;
};

#endif//__ICMP_H__
