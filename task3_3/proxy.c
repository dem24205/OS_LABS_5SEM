#define _GNU_SOURCE

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "cache.h"
#include "picohttpparser.h"

#define DEFAULT_PORT 80
#define MAX_PORT 65535
#define LISTEN_BACKLOG 128

#define REQ_MAX 65536   
#define BUF_SIZE 16384   
#define OUT_MAX 4096    
#define ERR_MAX 256 
#define URL_MAX 2048
#define HOST_MAX 256
#define PORT_MAX 16
#define MAX_HEADERS 64

#define HTTP_URL_PREFIX "http://"
#define HTTP_URL_PREFIX_LEN 7
#define HTTP_DEFAULT_PORT "80"
#define HTTP_METHOD_GET "GET"
#define HTTP_METHOD_GET_LEN 3
#define HTTP_STATUS_OK 200

#define HTTP_MINOR_POS 7      
#define CHAR_ZERO '0'
#define CHAR_ONE '1'

#define DEFAULT_CACHE_MB 256
#define BYTES_PER_MB (1024UL * 1024UL)

#define REQUEST_FMT \
    "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n"
#define REQUEST_FMT_PORT \
    "GET %s HTTP/1.0\r\nHost: %s:%s\r\nConnection: close\r\n\r\n"

#define STATUS_BAD_REQUEST "400 Bad Request"
#define STATUS_NOT_IMPLEMENTED "501 Not Implemented"
#define STATUS_INTERNAL_ERROR "500 Internal Server Error"
#define STATUS_BAD_GATEWAY "502 Bad Gateway"

static int send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static void send_error(int fd, const char *status) {
    char msg[ERR_MAX];
    int n = snprintf(msg, sizeof(msg),
        "HTTP/1.0 %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", status);
    send_all(fd, msg, (size_t)n);
}

static int connect_to_host(const char *host, const char *port) {
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) {
        return -1;
    }

    int fd = -1;
    for (p = res; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

struct request {
    char url[URL_MAX];  
    char host[HOST_MAX];
    char port[PORT_MAX];
    const char *path;
};

static int parse_url(const char *url, char *host, size_t hsz,
                     char *port, size_t psz, const char **path_out) {
    if (strncmp(url, HTTP_URL_PREFIX, HTTP_URL_PREFIX_LEN) != 0) {
        return -1;
    }
    const char *h = url + HTTP_URL_PREFIX_LEN;
    const char *slash = strchr(h, '/');
    size_t hostport_len = slash ? (size_t)(slash - h) : strlen(h);
    if (hostport_len == 0) {
        return -1;
    }

    const char *colon = memchr(h, ':', hostport_len);
    size_t host_len = colon ? (size_t)(colon - h) : hostport_len;
    if (host_len == 0 || host_len >= hsz) {
        return -1;
    }
    memcpy(host, h, host_len);
    host[host_len] = '\0';

    if (colon) {
        size_t plen = hostport_len - host_len - 1;
        if (plen == 0 || plen >= psz) {
            return -1;
        }
        memcpy(port, colon + 1, plen);
        port[plen] = '\0';
    } else {
        strcpy(port, HTTP_DEFAULT_PORT);
    }
    *path_out = slash ? slash : "/";
    return 0;
}

static int read_request(int fd, struct request *r) {
    char req[REQ_MAX];
    size_t len = 0;
    const char *method, *path;
    size_t method_len, path_len;
    int minor_version;
    struct phr_header headers[MAX_HEADERS];
    size_t num_headers;

    for (;;) {
        ssize_t n = recv(fd, req + len, REQ_MAX - len, 0);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return -1;
        }
        size_t prev = len;
        len += (size_t)n;
        num_headers = MAX_HEADERS;
        int pret = phr_parse_request(req, len, &method, &method_len, &path,
                                     &path_len, &minor_version, headers,
                                     &num_headers, prev);
        if (pret > 0) {
            break;
        }
        if (pret == -1 || len == REQ_MAX) {
            send_error(fd, STATUS_BAD_REQUEST);
            return -1;
        }
    }

    if (method_len != HTTP_METHOD_GET_LEN ||
        memcmp(method, HTTP_METHOD_GET, HTTP_METHOD_GET_LEN) != 0) {
        send_error(fd, STATUS_NOT_IMPLEMENTED);
        return -1;
    }

    if (path_len >= sizeof(r->url)) {
        send_error(fd, STATUS_BAD_REQUEST);
        return -1;
    }
    memcpy(r->url, path, path_len);
    r->url[path_len] = '\0';

    if (parse_url(r->url, r->host, sizeof(r->host),
                  r->port, sizeof(r->port), &r->path) != 0) {
        send_error(fd, STATUS_BAD_REQUEST);
        return -1;
    }
    return 0;
}

struct dl_arg {
    struct entry *entry;
    char host[HOST_MAX];
    char port[PORT_MAX];
    char path[URL_MAX];
};

static int send_request(int fd, const struct dl_arg *a) {
    char out[OUT_MAX];
    int on;
    if (strcmp(a->port, HTTP_DEFAULT_PORT) == 0) {
        on = snprintf(out, sizeof(out), REQUEST_FMT, a->path, a->host);
    } else {
        on = snprintf(out, sizeof(out), REQUEST_FMT_PORT, a->path, a->host, a->port);
    }
    if (on < 0 || (size_t)on >= sizeof(out)) {
        return -1;
    }
    return send_all(fd, out, (size_t)on);
}

static int read_headers(int fd, struct entry *e, int *status) {
    char hdr[REQ_MAX];
    size_t len = 0;
    int minor_version;
    const char *msg;
    size_t msg_len;
    struct phr_header headers[MAX_HEADERS];
    size_t num_headers;

    for (;;) {
        ssize_t n = recv(fd, hdr + len, REQ_MAX - len, 0);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return -1;
        }
        if (entry_append(e, hdr + len, (size_t)n) != 0) {
            return -1;
        }
        size_t prev = len;
        len += (size_t)n;
        num_headers = MAX_HEADERS;
        int pret = phr_parse_response(hdr, len, &minor_version, status,
                                      &msg, &msg_len, headers, &num_headers, prev);
        if (pret > 0) {
            return 0;
        }
        if (pret == -1 || len == REQ_MAX) {
            return -1;
        }
    }
}

static int pump_body(int fd, struct entry *e) {
    char buf[BUF_SIZE];
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            return 0;
        }
        if (entry_append(e, buf, (size_t)n) != 0) {
            return -1;
        }
    }
}

static int download(struct entry *e, const struct dl_arg *a) {
    int fd = connect_to_host(a->host, a->port);
    if (fd < 0) {
        return -1;
    }

    int rc = -1;
    int status = 0;
    if (send_request(fd, a) == 0 && read_headers(fd, e, &status) == 0) {
        if (status != HTTP_STATUS_OK) {
            cache_remove(e); 
        }
        rc = pump_body(fd, e);
    }
    close(fd);
    return rc;
}

static void *downloader_thread(void *p) {
    struct dl_arg *a = p;
    struct entry *e = a->entry;
    fprintf(stderr, "[miss] %s\n", e->key);
    int rc = download(e, a);
    if (rc != 0) {
        cache_remove(e);      
    }
    entry_finish(e, rc == 0 ? ENTRY_DONE : ENTRY_FAILED);
    free(a);
    entry_release(e);         
    return NULL;
}

static void abort_download(struct entry *e) {
    cache_remove(e);
    entry_finish(e, ENTRY_FAILED);
    entry_release(e);         
}

static int start_download(struct entry *e, const struct request *r) {
    struct dl_arg *a = malloc(sizeof(*a));
    if (!a) {
        abort_download(e);
        return -1;
    }
    a->entry = e;
    snprintf(a->host, sizeof(a->host), "%s", r->host);
    snprintf(a->port, sizeof(a->port), "%s", r->port);
    snprintf(a->path, sizeof(a->path), "%s", r->path);

    pthread_t tid;
    if (pthread_create(&tid, NULL, downloader_thread, a) != 0) {
        free(a);
        abort_download(e);
        return -1;
    }
    pthread_detach(tid);
    return 0;
}

static void downgrade_version(char *buf, size_t n, size_t sent) {
    if (sent > HTTP_MINOR_POS) {
        return;
    }
    size_t i = HTTP_MINOR_POS - sent;
    if (i < n && buf[i] == CHAR_ONE) {
        buf[i] = CHAR_ZERO;
    }
}

static void stream_to_client(int fd, struct entry *e) {
    char buf[BUF_SIZE];
    size_t sent = 0;
    for (;;) {
        ssize_t n = entry_read(e, sent, buf, sizeof(buf));
        if (n == 0) {
            break;
        }
        if (n < 0) {
            if (sent == 0) {
                send_error(fd, STATUS_BAD_GATEWAY);
            }
            break;
        }
        downgrade_version(buf, (size_t)n, sent);
        if (send_all(fd, buf, (size_t)n) != 0) {
            break;
        }
        sent += (size_t)n;
    }
}

static void serve_request(int fd, const struct request *r) {
    int created = 0;
    struct entry *e = cache_acquire(r->url, &created);
    if (!e) {
        send_error(fd, STATUS_INTERNAL_ERROR);
        return;
    }

    if (created) {
        if (start_download(e, r) != 0) {
            send_error(fd, STATUS_INTERNAL_ERROR);
            entry_release(e);  
            return;
        }
    } else {
        fprintf(stderr, "[hit] %s\n", r->url);
    }

    stream_to_client(fd, e);
    entry_release(e);        
}

static void *client_thread(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    struct request r;
    if (read_request(client_fd, &r) == 0) {
        serve_request(client_fd, &r);
    }
    close(client_fd);
    return NULL;
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-p port] [-m cache_mb]\n"
                    "  -p port       port to listen on (default %d)\n"
                    "  -m cache_mb   cache size limit in MB (default %d)\n"
                    "  -h            show this help\n",
                    prog, DEFAULT_PORT, DEFAULT_CACHE_MB);
}

static int create_listener(int port) {
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        perror("socket");
        return -1;
    }
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(lfd);
        return -1;
    }
    if (listen(lfd, LISTEN_BACKLOG) < 0) {
        perror("listen");
        close(lfd);
        return -1;
    }
    return lfd;
}

static void spawn_client(int cfd, const pthread_attr_t *attr) {
    int *arg = malloc(sizeof(*arg));
    if (!arg) {
        close(cfd);
        return;
    }
    *arg = cfd;
    pthread_t tid;
    if (pthread_create(&tid, attr, client_thread, arg) != 0) {
        free(arg);
        close(cfd);
    }
}

int main(int argc, char **argv) {
        int port = DEFAULT_PORT, opt;
    long cache_mb = DEFAULT_CACHE_MB;
    while ((opt = getopt(argc, argv, "p:m:h")) != -1) {
        switch (opt) {
        case 'p':
            port = atoi(optarg);
            break;
        case 'm':
            cache_mb = atol(optarg);
            break;
        case 'h':
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 1;
        }
    }
    if (port <= 0 || port > MAX_PORT) {
        fprintf(stderr, "bad port\n");
        return 1;
    }
    if (cache_mb <= 0) {
        fprintf(stderr, "bad cache size\n");
        return 1;
    }
    cache_set_limit((size_t)cache_mb * BYTES_PER_MB);
    if (cache_start_monitor() != 0) {
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    int lfd = create_listener(port);
    if (lfd < 0) {
        return 1;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    fprintf(stderr, "proxy listening on port %d\n", port);
    for (;;) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) {
            if (errno != EINTR) {
                perror("accept");
            }
            continue;
        }
        spawn_client(cfd, &attr);
    }
}