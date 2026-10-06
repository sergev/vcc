/*
 * <time.h> — date and time (C11 §7.27), hosted macOS: libSystem's struct tm, with its
 * two extra members, which mktime and localtime_r write, and an unsigned clock_t.
 */
#ifndef _TIME_H
#define _TIME_H

#include <stddef.h>

typedef long time_t;
typedef unsigned long clock_t;

#define CLOCKS_PER_SEC ((clock_t)1000000)
#define TIME_UTC       1

struct tm {
    int         tm_sec;
    int         tm_min;
    int         tm_hour;
    int         tm_mday;
    int         tm_mon;
    int         tm_year;
    int         tm_wday;
    int         tm_yday;
    int         tm_isdst;
    long        tm_gmtoff;
    const char *tm_zone;
};

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

clock_t clock(void);
time_t  time(time_t *t);
double  difftime(time_t end, time_t start);
time_t  mktime(struct tm *tm);
int     timespec_get(struct timespec *ts, int base);

struct tm *localtime(const time_t *timep);
struct tm *gmtime(const time_t *timep);
char      *asctime(const struct tm *tm);
char      *ctime(const time_t *timep);
size_t     strftime(char *s, size_t max, const char *format, const struct tm *tm);

#endif /* _TIME_H */
