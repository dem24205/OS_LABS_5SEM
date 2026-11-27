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
#include "picohttpparser.h"
#include "cache.h"

#define DEFAULT_PORT 80
#define LISTEN_BACKLOG 128
#define REQ_MAX 65536
#define MAX_HEADERS 64
#define BUF_SIZE 16384
#define URL_MAX 2048
#define HOST_MAX 256
#define PORT_MAX 16
#define OUT_MAX 4096
#define ERR_MAX 256
#define HTTP_DEFAULT_PORT "80"
#define HTTP_MINOR_POS  7      /* индекс цифры в "HTTP/1.1" */
#define CHAR_ZERO       '0'
#define CHAR_ONE        '1'

static int send_all(int fd, const char *buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static void send_error(int fd, const char *status) {
    char msg[ERR_MAX];
    int n = snprintf(msg, sizeof msg,
        "HTTP/1.0 %s\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", status);
    send_all(fd, msg, (size_t)n);
}

static int connect_to_host(const char *host, const char *port) {
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;

    int fd = -1;
    for (p = res; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static int parse_url(const char *url, char *host, size_t hsz,
                     char *port, size_t psz, const char **path_out) {
    if (strncmp(url, "http://", 7) != 0) return -1;
    const char *h = url + 7;
    const char *slash = strchr(h, '/');
    size_t hostport_len = slash ? (size_t)(slash - h) : strlen(h);
    if (hostport_len == 0) return -1;

    const char *colon = memchr(h, ':', hostport_len);
    size_t host_len = colon ? (size_t)(colon - h) : hostport_len;
    if (host_len == 0 || host_len >= hsz) return -1;
    memcpy(host, h, host_len);
    host[host_len] = '\0';

    if (colon) {
        size_t plen = hostport_len - host_len - 1;
        if (plen == 0 || plen >= psz) return -1;
        memcpy(port, colon + 1, plen);
        port[plen] = '\0';
    } else {
        strcpy(port, HTTP_DEFAULT_PORT);
    }
    *path_out = slash ? slash : "/";
    return 0;
}

struct dl_arg {
    struct entry *entry;
    char host[HOST_MAX];
    char port[PORT_MAX];
    char path[URL_MAX];
};

static int download(struct entry *e, const struct dl_arg *a) {
    int fd = connect_to_host(a->host, a->port);
    if (fd < 0) return -1;

    char out[OUT_MAX];
    int on;
    if (strcmp(a->port, HTTP_DEFAULT_PORT) == 0)
        on = snprintf(out, sizeof out,
            "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", a->path, a->host);
    else
        on = snprintf(out, sizeof out,
            "GET %s HTTP/1.0\r\nHost: %s:%s\r\nConnection: close\r\n\r\n", a->path, a->host, a->port);
    if (on < 0 || (size_t)on >= sizeof out || send_all(fd, out, (size_t)on) != 0) {
        close(fd);
        return -1;
    }

    char hdr[REQ_MAX];
    size_t len = 0;
    int minor_version, status;
    const char *msg;
    size_t msg_len;
    struct phr_header headers[MAX_HEADERS];
    size_t num_headers;
    int pret;

    for (;;) {
        ssize_t n = recv(fd, hdr + len, REQ_MAX - len, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); return -1; }
        if (entry_append(e, hdr + len, (size_t)n) != 0) { close(fd); return -1; }
        size_t prev = len;
        len += (size_t)n;
        num_headers = MAX_HEADERS;
        pret = phr_parse_response(hdr, len, &minor_version, &status,
                                  &msg, &msg_len, headers, &num_headers, prev);
        if (pret > 0) break;
        if (pret == -1) { close(fd); return -1; }
        if (len == REQ_MAX) { close(fd); return -1; }
    }

    if (status != 200) {
        cache_remove(e);
    }

    char buf[BUF_SIZE];
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { close(fd); return -1; }
        if (n == 0) { close(fd); return 0; }
        if (entry_append(e, buf, (size_t)n) != 0) { close(fd); return -1; }
    }
}

static void *downloader_thread(void *p) {
    struct dl_arg *a = p;
    struct entry *e = a->entry;
    fprintf(stderr, "[miss] %s\n", e->key);
    int rc = download(e, a);
    if (rc != 0) cache_remove(e);
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

static void *client_thread(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);
    struct entry *e = NULL;

    char req[REQ_MAX];
    size_t len = 0;
    const char *method, *path;
    size_t method_len, path_len;
    int minor_version;
    struct phr_header headers[MAX_HEADERS];
    size_t num_headers;
    int pret;

    for (;;) {
        ssize_t n = recv(client_fd, req + len, REQ_MAX - len, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto cleanup;
        size_t prev = len;
        len += (size_t)n;
        num_headers = MAX_HEADERS;
        pret = phr_parse_request(req, len, &method, &method_len, &path,
                                 &path_len, &minor_version, headers,
                                 &num_headers, prev);
        if (pret > 0) break;
        if (pret == -1) { send_error(client_fd, "400 Bad Request"); goto cleanup; }
        if (len == REQ_MAX) { send_error(client_fd, "400 Bad Request"); goto cleanup; }
    }

    if (method_len != 3 || memcmp(method, "GET", 3) != 0) {
        send_error(client_fd, "501 Not Implemented");
        goto cleanup;
    }

    char url[URL_MAX];
    if (path_len >= sizeof url) { send_error(client_fd, "400 Bad Request"); goto cleanup; }
    memcpy(url, path, path_len);
    url[path_len] = '\0';

    char host[HOST_MAX], port[PORT_MAX];
    const char *url_path;
    if (parse_url(url, host, sizeof host, port, sizeof port, &url_path) != 0) {
        send_error(client_fd, "400 Bad Request");
        goto cleanup;
    }

    int created = 0;
    e = cache_acquire(url, &created);
    if (!e) { send_error(client_fd, "500 Internal Server Error"); goto cleanup; }

    if (created) {
        struct dl_arg *a = malloc(sizeof *a);
        if (!a) {
            abort_download(e);
            send_error(client_fd, "500 Internal Server Error");
            goto cleanup;
        }
        a->entry = e;
        snprintf(a->host, sizeof a->host, "%s", host);
        snprintf(a->port, sizeof a->port, "%s", port);
        snprintf(a->path, sizeof a->path, "%s", url_path);

        pthread_t tid;
        if (pthread_create(&tid, NULL, downloader_thread, a) != 0) {
            free(a);
            abort_download(e);
            send_error(client_fd, "500 Internal Server Error");
            goto cleanup;
        }
        pthread_detach(tid);
    }
    else {
        fprintf(stderr, "[hit] %s\n", url);
    }

    char buf[BUF_SIZE];
    size_t sent = 0;
    for (;;) {
        ssize_t n = entry_read(e, sent, buf, sizeof buf);
        if (n == 0) break;
        if (n < 0) {
            if (sent == 0) send_error(client_fd, "502 Bad Gateway");
            break;
        }
        if (sent <= HTTP_MINOR_POS && HTTP_MINOR_POS - sent < (size_t)n &&
            buf[HTTP_MINOR_POS - sent] == CHAR_ONE)
            buf[HTTP_MINOR_POS - sent] = CHAR_ZERO;
        if (send_all(client_fd, buf, (size_t)n) != 0) break;
        sent += (size_t)n;
    }

cleanup:
    if (e) entry_release(e);
    close(client_fd);
    return NULL;
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-p port]\n"
                    "  -p port   port to listen on (default %d)\n"
                    "  -h        show this help\n", prog, DEFAULT_PORT);
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT, opt;
    while ((opt = getopt(argc, argv, "p:h")) != -1) {
        switch (opt) {
        case 'p': port = atoi(optarg); break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "bad port\n");
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) { perror("socket"); return 1; }
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("bind"); return 1; }
    if (listen(lfd, LISTEN_BACKLOG) < 0) { perror("listen"); return 1; }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    fprintf(stderr, "proxy listening on port %d\n", port);
    for (;;) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) {
            if (errno != EINTR) perror("accept");
            continue;
        }
        int *arg = malloc(sizeof *arg);
        if (!arg) { close(cfd); continue; }
        *arg = cfd;
        pthread_t tid;
        if (pthread_create(&tid, &attr, client_thread, arg) != 0) {
            free(arg);
            close(cfd);
        }
    }
}