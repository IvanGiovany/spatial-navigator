#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>

#include "parser.h"
#include "graph.h"
#include "render.h"

#define SOCKET_PATH "/tmp/spatial_nav.sock"
#define MAX_EVENTS 64
#define BUFFER_SIZE 4096

static volatile sig_atomic_t g_running = 1;

static void handle_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("[spatial_daemon] fcntl F_GETFL");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("[spatial_daemon] fcntl F_SETFL O_NONBLOCK");
        return -1;
    }
    return 0;
}

static void handle_client_request(int client_fd, int epoll_fd) {
    char buf[BUFFER_SIZE];
    ssize_t bytes_read = read(client_fd, buf, sizeof(buf) - 1);

    if (bytes_read > 0) {
        buf[bytes_read] = '\0';

        /* Strip trailing newlines/carriage returns */
        while (bytes_read > 0 && (buf[bytes_read - 1] == '\n' || buf[bytes_read - 1] == '\r')) {
            buf[--bytes_read] = '\0';
        }

        printf("[spatial_daemon] Received request for file: %s\n", buf);

        /* Phase 2: Parse source file and build call graph */
        Graph graph;
        graph_init(&graph);
        if (parse_source_file(buf, &graph) == 0) {
            printf("[spatial_daemon] Successfully parsed '%s': %d nodes\n", buf, graph.node_count);
            graph_print_summary(&graph);
        } else {
            fprintf(stderr, "[spatial_daemon] Warning: parse_source_file failed for '%s'\n", buf);
        }
        graph_free(&graph);

        char response[BUFFER_SIZE + 32];
        int resp_len = snprintf(response, sizeof(response), "ACK: %s\n", buf);
        if (resp_len > 0) {
            ssize_t total_written = 0;
            while (total_written < resp_len) {
                ssize_t written = write(client_fd, response + total_written, (size_t)(resp_len - total_written));
                if (written < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        /* For Phase 1 ACK responses, the buffer fits well within SO_SNDBUF */
                        continue;
                    }
                    perror("[spatial_daemon] write");
                    break;
                }
                total_written += written;
            }
        }
    } else if (bytes_read == 0) {
        /* Client closed connection without payload */
    } else {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("[spatial_daemon] read");
        }
    }

    /* Phase 1: Close connection after responding */
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
    close(client_fd);
}

int main(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Ignore SIGPIPE to avoid termination when writing to closed sockets */
    signal(SIGPIPE, SIG_IGN);

    /* Step 1.2: Socket Server Initialization */
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[spatial_daemon] socket");
        return EXIT_FAILURE;
    }

    if (set_nonblocking(server_fd) < 0) {
        close(server_fd);
        return EXIT_FAILURE;
    }

    /* Ensure socket path is cleared before binding */
    unlink(SOCKET_PATH);

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[spatial_daemon] bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("[spatial_daemon] listen");
        close(server_fd);
        unlink(SOCKET_PATH);
        return EXIT_FAILURE;
    }

    /* Step 1.3: Non-blocking epoll event loop */
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("[spatial_daemon] epoll_create1");
        close(server_fd);
        unlink(SOCKET_PATH);
        return EXIT_FAILURE;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        perror("[spatial_daemon] epoll_ctl server_fd");
        close(epoll_fd);
        close(server_fd);
        unlink(SOCKET_PATH);
        return EXIT_FAILURE;
    }

    printf("[spatial_daemon] Server listening on %s (epoll loop started)\n", SOCKET_PATH);

    struct epoll_event events[MAX_EVENTS];

    while (g_running) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 500);
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("[spatial_daemon] epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == server_fd) {
                /* Accept incoming client connections */
                while (1) {
                    struct sockaddr_un client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break; /* All connections accepted */
                        }
                        if (errno == EINTR) {
                            continue;
                        }
                        perror("[spatial_daemon] accept");
                        break;
                    }

                    if (set_nonblocking(client_fd) < 0) {
                        close(client_fd);
                        continue;
                    }

                    struct epoll_event client_ev;
                    memset(&client_ev, 0, sizeof(client_ev));
                    client_ev.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR;
                    client_ev.data.fd = client_fd;

                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_ev) < 0) {
                        perror("[spatial_daemon] epoll_ctl add client");
                        close(client_fd);
                    }
                }
            } else {
                int client_fd = events[i].data.fd;
                uint32_t ev_flags = events[i].events;

                if (ev_flags & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    close(client_fd);
                    continue;
                }

                if (ev_flags & EPOLLIN) {
                    /* Step 1.4: Request Handling */
                    handle_client_request(client_fd, epoll_fd);
                }
            }
        }
    }

    printf("[spatial_daemon] Shutting down cleanly...\n");
    close(epoll_fd);
    close(server_fd);
    unlink(SOCKET_PATH);
    printf("[spatial_daemon] Socket unlinked. Goodbye.\n");

    return EXIT_SUCCESS;
}
