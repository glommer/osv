#include <osv/types.h>
#include "drivers/clock.hh"

extern "C" {
    #include <time.h>
    #include <bsd/porting/netport.h>
}

#define NANOSEC (1000000000L)

void getmicrotime(struct timeval *tvp)
{
    u64 ntm = clock::get()->time();

    tvp->tv_sec = ntm/NANOSEC;
    tvp->tv_usec = (ntm - (tvp->tv_sec*NANOSEC))/1000;
}
