/* 
 * File:   udperf.c
 * Author: khoai
 *
 * Created on July 13, 2015, 5:16 PM
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <netdb.h> 
#include <time.h>
#include <getopt.h>

char content_[] = "0123456789";
int ncontent_ = 10;
int UDP_MSS = 1470;
int64_t SECOND = 1e6; // second to nanosecond

struct Dest {
    int fd;
    struct sockaddr_in serveraddr;
    unsigned int serverlen;
};

void error(char *msg) {
    perror(msg);
    exit(0);
}

void init_socket(struct Dest *dest, char *hostname, int portno) {
    struct hostent *server;
    dest->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (dest->fd < 0)
        error((char*) "ERROR opening socket");
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr, "ERROR, no such host as %s\n", hostname);
        exit(0);
    }
    bzero((char *) &dest->serveraddr, sizeof (dest->serveraddr));
    dest->serveraddr.sin_family = AF_INET;
    bcopy((char *) server->h_addr,
        (char *) &dest->serveraddr.sin_addr.s_addr, server->h_length);
    dest->serveraddr.sin_port = htons(portno);
    dest->serverlen = sizeof (dest->serveraddr);
}

size_t send_bytes(struct Dest *dest, size_t size) {
    char *buf = malloc(size);
    // build buf
    size_t n = 0;
    memset(buf, 0, size);
    while (n < size) {
        int n_cpy = size - n > ncontent_ ? ncontent_ : size - n;
        memcpy(buf + n, content_, n_cpy);
        n += n_cpy;
    }
    n = sendto(dest->fd, buf, size, 0, (const struct sockaddr *) &dest->serveraddr, dest->serverlen);
    free(buf);
    if (n < 0)
        error("send to");
    return n;
}

void receive_bytes(struct Dest *dest, int is_print) {
    char buf[4096] = {0};
    /* print the server's reply */
    int n = recvfrom(dest->fd, buf, sizeof (buf), 0, (struct sockaddr *) &dest->serveraddr, &dest->serverlen);
    if (n < 0)
        error("ERROR in recvfrom");
    if (is_print)
        printf("Echo from server: %s\n", buf);
}

char* readable_fs(size_t size, char *buf) {
    int i = 0;
    static const char* units_size[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
    float rec_size = size;
    while (rec_size > 1024) {
        rec_size /= 1024;
        i++;
    }
    sprintf(buf, "%.*f%s", i, rec_size, units_size[i]);
    return buf;
}

char* readable_time(int64_t time, char *buf) {
    // time in us
    int i = 0;
    static const char* units_time[] = {"us", "ms", "s"};
    float rec_time = time;
    while (rec_time > 1000) {
        rec_time /= 1000;
        i++;
    }
    sprintf(buf, "%.*f%s", i, rec_time, units_time[i]);
    return buf;
}

#define PKT_TIE 50

size_t send_by_bandwitdh(struct Dest *dest, size_t size, int64_t time) {
    size_t npacket = size / UDP_MSS;
    size_t nsent = 0;
    int i = 0;
    struct timespec begin, curr, end = {0, 0};
    int64_t sleep_time = time / npacket;
    clock_gettime(CLOCK_MONOTONIC_RAW, &begin);
    int total_tie = npacket / PKT_TIE + 1;
    for (; i < npacket; i++) {
        if (i % PKT_TIE == 0) {
            clock_gettime(CLOCK_MONOTONIC_RAW, &curr);
            int64_t time_span = (curr.tv_sec - begin.tv_sec) * 1e6
                + (curr.tv_nsec - begin.tv_nsec) / 1e3;
            int remain_tie = (npacket - i) / PKT_TIE + 1;
            sleep_time = (time - time_span) / remain_tie;
            if (sleep_time < 0 || sleep_time > time) {
                //printf("sleep_time too large\n");
                sleep_time = 0;
            }
            int ratio = 2 * total_tie / (total_tie - remain_tie + 1);
            usleep(sleep_time / ratio);
        }
        nsent += send_bytes(dest, UDP_MSS);
    }
    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    int64_t time_span = (end.tv_sec - begin.tv_sec) * 1e6
        + (end.tv_nsec - begin.tv_nsec) / 1e3;
    char time_hrdr[20] = {0}; // time human-readable
    readable_time(time_span, time_hrdr);
    // timer debug
    //printf("[-] DONE: tick=%s\n", time_hrdr);
    return nsent;
}

static void show_help(void) {
    const char content[] = "udperf 0.1 - small tool generate UDP bandwidth for test\n" \
        "Usage:\n" \
        " -c, --client <host>           connecting to <host>\n" \
        " -p, --port <port>             server port to connect to\n" \
        " -b, --bandwidth <bandwidth>   test bandwidth (default = '10M')\n" \
        " -t, --time <time>             test time (default = '10' second)\n" \
        " -r, --raise                   is raise bandwidth time (default not raise)\n" \
        " -h, --help                    show help\n" \
    ;
    printf(content);
    exit(EXIT_FAILURE);
}

static struct option long_options[] = {
    {"client", required_argument, 0, 'c'},
    {"port", required_argument, 0, 'p'},
    {"time", required_argument, 0, 't'},
    {"bandwidth", required_argument, 0, 'b'},
    {"raise", no_argument, 0, 'r'},
    {"help", no_argument, 0, 'h'},
    {NULL, 0, NULL, 0}
};

size_t read_size(const char* sz_size) {
    if (!sz_size || !strlen(sz_size))
        error("input size invalid");
    char last_char = sz_size[strlen(sz_size) - 1];
    size_t size = atoi(sz_size);
    switch (last_char) {
        case 'B':
            return size;
        case 'K':
            return 1024 * size;
        case 'M':
            return 1024 * 1024 * size;
        case 'G':
            return 1024 * 1024 * 1024 * size;
        default:
            error("input size unit invalid");
    }
    return size;
}

void raise_bandwidth(struct Dest *dest, size_t bandwidth) {
    printf("[-] Raise bandwidth start\n");
    size_t total_sent = 0;
    int64_t sleep_time;
    char size_hrdr[20] = {0}; // size human-readable
    char time_hrdr[20] = {0}; // time human-readable
    struct timespec last_time = {0, 0};
    struct timespec curr_time = {0, 0};
    struct timespec begin, end = {0, 0};
    size_t bw = UDP_MSS;
    clock_gettime(CLOCK_MONOTONIC_RAW, &begin);
    while (1) {
        // sleep time
        sleep_time = SECOND;
        if (last_time.tv_nsec && last_time.tv_sec) {
            clock_gettime(CLOCK_MONOTONIC_RAW, &curr_time);
            sleep_time -= (curr_time.tv_sec - last_time.tv_sec)* 1e6
                + (curr_time.tv_nsec - last_time.tv_nsec) / 1e3;
            last_time = curr_time;
        }
        if (sleep_time < 0)
            error("sleep_time is minus");
        usleep(sleep_time);
        clock_gettime(CLOCK_MONOTONIC_RAW, &last_time);
        // raise
        readable_fs(bw, size_hrdr);
        printf(" - raise: %s\n", size_hrdr);
        size_t nsent = send_by_bandwitdh(dest, bw, SECOND * 0.9);
        total_sent += nsent;
        if (bw == bandwidth)
            break;
        bw *= 2;
        if (bw > bandwidth)
            bw = bandwidth;
    }
    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    int64_t time_span = (end.tv_sec - begin.tv_sec) * 1e6
        + (end.tv_nsec - begin.tv_nsec) / 1e3;
    readable_fs(total_sent, size_hrdr);
    readable_time(time_span, time_hrdr);
    printf(" - total_sent=%s/%s\n", size_hrdr, time_hrdr);
    printf("[-] Raise bandwidth done\n");
}

int main(int argc, char** argv) {
    char *host_ = NULL;
    int port_ = 0;
    int time_ = 10;
    size_t bandwidth_ = 10 * 1024 * 1024;
    char is_raise_ = 0;

    int c;
    int option_index = 0;
    while ((c = getopt_long(argc, argv, "p:c:t:b:rh:",
        long_options, &option_index)) != -1) {
        switch (c) {
            case 'c':
                if (!optarg)
                    show_help();
                host_ = strdup(optarg);
                break;
            case 'p':
                port_ = atoi((const char*) optarg);
                break;
            case 't':
                time_ = atoi((const char*) optarg);
                break;
            case 'b':
                bandwidth_ = read_size((const char*) optarg);
                break;
            case 'r':
                is_raise_ = 1;
                break;
            case 'h':
            default:
                show_help();
                exit(EXIT_FAILURE);
                break;
        }
    }

    if (!host_ || !port_) {
        printf("Host or port is invalid.\nTry `--help' for more information.");
        exit(EXIT_FAILURE);
    }

    int cnt = 0;
    char size_hrdr[20] = {0}; // size human-readable
    char time_hrdr[20] = {0}; // time human-readable
    readable_fs(bandwidth_, size_hrdr);
    printf("[-] Config: host=%s:%d, bandwidth=%s, time=%ds, is_raise=%d\n",
        host_, port_, size_hrdr, time_, is_raise_);
    struct Dest dest;
    init_socket(&dest, host_, port_);

    // raise bandwidth
    if (is_raise_)
        raise_bandwidth(&dest, bandwidth_);

    // generate bandwidth
    printf("[-] Bandwidth test\n");
    size_t total_sent = 0;
    int64_t sleep_time = SECOND;
    struct timespec last_time = {0, 0};
    struct timespec curr_time = {0, 0};
    struct timespec begin, end = {0, 0};
    clock_gettime(CLOCK_MONOTONIC_RAW, &begin);
    while (1) {
        sleep_time = SECOND;
        if (last_time.tv_nsec && last_time.tv_sec) {
            clock_gettime(CLOCK_MONOTONIC_RAW, &curr_time);
            sleep_time -= (curr_time.tv_sec - last_time.tv_sec)* 1e6
                + (curr_time.tv_nsec - last_time.tv_nsec) / 1e3;
            last_time = curr_time;
        }
        if (sleep_time < 0)
            error("sleep_time is minus");
        usleep(sleep_time);
        clock_gettime(CLOCK_MONOTONIC_RAW, &last_time);
        size_t nsent = send_by_bandwitdh(&dest, bandwidth_, SECOND * 0.9);
        total_sent += nsent;
        readable_fs(nsent, size_hrdr);
        printf("[%d] sent:%s\n", ++cnt, size_hrdr);
        if (cnt >= time_) {
            break;
        }
    }
    clock_gettime(CLOCK_MONOTONIC_RAW, &end);
    int64_t time_span = (end.tv_sec - begin.tv_sec) * 1e6
        + (end.tv_nsec - begin.tv_nsec) / 1e3;
    readable_fs(total_sent, size_hrdr);
    readable_time(time_span, time_hrdr);
    printf("[-] DONE: total_sent=%s/%s\n", size_hrdr, time_hrdr);
    return 0;
}