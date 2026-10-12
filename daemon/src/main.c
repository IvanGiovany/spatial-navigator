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

        int req_width = DEFAULT_VIEW_WIDTH;
        int req_height = DEFAULT_VIEW_HEIGHT;
        float req_yaw = 0.0f;
        float req_pitch = 0.0f;
        float req_zoom = 450.0f;
        bool req_braille = false;
        char file_path[BUFFER_SIZE] = {0};

        if (strncmp(buf, "REQ ", 4) == 0) {
            /* Format: REQ W=800 H=600 YAW=0.0 PITCH=0.0 ZOOM=450 [BRAILLE=1] PATH=/path/to/file */
            char *p = buf + 4;
            char *token = strtok(p, " ");
            while (token) {
                if (strncmp(token, "W=", 2) == 0) {
                    req_width = atoi(token + 2);
                } else if (strncmp(token, "H=", 2) == 0) {
                    req_height = atoi(token + 2);
                } else if (strncmp(token, "YAW=", 4) == 0) {
                    req_yaw = (float)atof(token + 4);
                } else if (strncmp(token, "PITCH=", 6) == 0) {
                    req_pitch = (float)atof(token + 6);
                } else if (strncmp(token, "ZOOM=", 5) == 0) {
                    req_zoom = (float)atof(token + 5);
                } else if (strncmp(token, "BRAILLE=", 8) == 0) {
                    req_braille = (atoi(token + 8) == 1);
                } else if (strncmp(token, "PATH=", 5) == 0) {
                    strncpy(file_path, token + 5, sizeof(file_path) - 1);
                }
                token = strtok(NULL, " ");
            }
        } else {
            /* Backward compatibility: payload is raw file path */
            strncpy(file_path, buf, sizeof(file_path) - 1);
        }

        if (req_width <= 0 || req_width > 3840) req_width = DEFAULT_VIEW_WIDTH;
        if (req_height <= 0 || req_height > 2160) req_height = DEFAULT_VIEW_HEIGHT;
        if (req_zoom <= 10.0f) req_zoom = 450.0f;

        printf("[spatial_daemon] Processing request: target='%s', viewport=%dx%d, yaw=%.2f, pitch=%.2f, zoom=%.1f\n",
               file_path, req_width, req_height, req_yaw, req_pitch, req_zoom);

        /* Phase 2: Parse source file and build call graph */
        Graph graph;
        graph_init(&graph);
        if (file_path[0] != '\0' && parse_source_file(file_path, &graph) == 0) {
            printf("[spatial_daemon] Successfully parsed '%s': %d nodes\n", file_path, graph.node_count);
            graph_print_summary(&graph);

            /* Phase 3: 3D Physics Engine (Fruchterman-Reingold) */
            calculate_forces(graph.nodes, graph.node_count);
            printf("[spatial_daemon] 3D physics simulation complete (150 iterations)\n");
            graph_print_summary(&graph);
        } else {
            fprintf(stderr, "[spatial_daemon] Warning: parse_source_file failed or empty for '%s'\n", file_path);
        }

        /* Version 2 High-Fidelity Rendering Pipeline */
        RasterContext raster_ctx;
        if (raster_init(&raster_ctx, req_width, req_height) == 0) {
            Camera cam;
            camera_init(&cam, req_width, req_height);
            camera_set_orbit(&cam, req_yaw, req_pitch, req_zoom);

            render_graph_v2(&graph, &cam, &raster_ctx);

            if (req_braille) {
                printf("[spatial_daemon] Streaming UNICODE BRAILLE fallback payload to client...\n");
                render_braille_stream(&raster_ctx, client_fd);
            } else {
                printf("[spatial_daemon] Streaming SIXEL payload to client...\n");
                render_sixel_stream(&raster_ctx, client_fd);
            }
            printf("[spatial_daemon] Stream complete.\n");

            raster_free(&raster_ctx);
        } else {
            fprintf(stderr, "[spatial_daemon] Error: raster_init failed for %dx%d\n", req_width, req_height);
        }

        graph_free(&graph);

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
