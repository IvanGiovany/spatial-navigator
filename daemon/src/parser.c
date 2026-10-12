#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include "parser.h"
#include "graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <tree_sitter/api.h>

/* Declaration for tree-sitter C language grammar entrypoint */
const TSLanguage *tree_sitter_c(void);

int read_file_mmap(const char *filepath, char **out_buf, size_t *out_size) {
    if (!filepath || !out_buf || !out_size) {
        return -1;
    }

    *out_buf = NULL;
    *out_size = 0;

    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        perror("[spatial_parser] open");
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("[spatial_parser] fstat");
        close(fd);
        return -1;
    }

    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "[spatial_parser] '%s' is not a regular file\n", filepath);
        close(fd);
        return -1;
    }

    if (st.st_size == 0) {
        close(fd);
        return 0;
    }

    void *addr = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (addr == MAP_FAILED) {
        perror("[spatial_parser] mmap");
        return -1;
    }

    *out_buf = (char *)addr;
    *out_size = (size_t)st.st_size;
    return 0;
}

void unmap_file(char *buf, size_t size) {
    if (buf && size > 0) {
        munmap(buf, size);
    }
}

static void extract_identifier(TSNode node, const char *src, char *out, size_t out_sz) {
    if (ts_node_is_null(node) || !src || !out || out_sz == 0) {
        return;
    }

    const char *type = ts_node_type(node);
    if (strcmp(type, "identifier") == 0) {
        uint32_t start = ts_node_start_byte(node);
        uint32_t end = ts_node_end_byte(node);
        size_t len = (end > start) ? (size_t)(end - start) : 0;
        if (len >= out_sz) {
            len = out_sz - 1;
        }
        memcpy(out, src + start, len);
        out[len] = '\0';
        return;
    }

    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        TSNode child = ts_node_child(node, i);
        extract_identifier(child, src, out, out_sz);
        if (out[0] != '\0') {
            return;
        }
    }
}

static float rand_range(float min_val, float max_val) {
    return min_val + ((float)rand() / (float)RAND_MAX) * (max_val - min_val);
}

static void traverse_functions(TSTreeCursor *cursor, const char *src, Graph *graph) {
    TSNode node = ts_tree_cursor_current_node(cursor);
    const char *type = ts_node_type(node);

    if (strcmp(type, "function_definition") == 0) {
        TSNode decl = ts_node_child_by_field_name(node, "declarator", 10);
        char func_name[128] = {0};
        if (!ts_node_is_null(decl)) {
            extract_identifier(decl, src, func_name, sizeof(func_name));
        }

        if (func_name[0] != '\0') {
            uint32_t start = ts_node_start_byte(node);
            uint32_t end = ts_node_end_byte(node);
            uint32_t byte_size = (end > start) ? (end - start) : 1;

            float radius = 5.0f + sqrtf((float)byte_size) * 0.5f;
            if (radius > 35.0f) {
                radius = 35.0f;
            }

            Vec3 pos = {
                rand_range(-50.0f, 50.0f),
                rand_range(-50.0f, 50.0f),
                rand_range(-50.0f, 50.0f)
            };

            graph_add_node(graph, func_name, radius, pos);
        }
    }

    if (ts_tree_cursor_goto_first_child(cursor)) {
        do {
            traverse_functions(cursor, src, graph);
        } while (ts_tree_cursor_goto_next_sibling(cursor));
        ts_tree_cursor_goto_parent(cursor);
    }
}

static void scan_call_expressions(TSNode node, const char *src, int caller_id, Graph *graph) {
    const char *type = ts_node_type(node);
    if (strcmp(type, "call_expression") == 0) {
        TSNode func_node = ts_node_child_by_field_name(node, "function", 8);
        if (!ts_node_is_null(func_node)) {
            char callee_name[128] = {0};
            extract_identifier(func_node, src, callee_name, sizeof(callee_name));
            if (callee_name[0] != '\0') {
                int callee_id = graph_find_node_by_name(graph, callee_name);
                if (callee_id >= 0) {
                    graph_add_edge(graph, caller_id, callee_id);
                }
            }
        }
    }

    uint32_t count = ts_node_child_count(node);
    for (uint32_t i = 0; i < count; ++i) {
        scan_call_expressions(ts_node_child(node, i), src, caller_id, graph);
    }
}

static void traverse_calls(TSTreeCursor *cursor, const char *src, Graph *graph) {
    TSNode node = ts_tree_cursor_current_node(cursor);
    const char *type = ts_node_type(node);

    if (strcmp(type, "function_definition") == 0) {
        TSNode decl = ts_node_child_by_field_name(node, "declarator", 10);
        char func_name[128] = {0};
        if (!ts_node_is_null(decl)) {
            extract_identifier(decl, src, func_name, sizeof(func_name));
        }

        if (func_name[0] != '\0') {
            int caller_id = graph_find_node_by_name(graph, func_name);
            if (caller_id >= 0) {
                TSNode body = ts_node_child_by_field_name(node, "body", 4);
                if (!ts_node_is_null(body)) {
                    scan_call_expressions(body, src, caller_id, graph);
                } else {
                    scan_call_expressions(node, src, caller_id, graph);
                }
            }
        }
    }

    if (ts_tree_cursor_goto_first_child(cursor)) {
        do {
            traverse_calls(cursor, src, graph);
        } while (ts_tree_cursor_goto_next_sibling(cursor));
        ts_tree_cursor_goto_parent(cursor);
    }
}

int parse_source_file(const char *filepath, Graph *graph) {
    if (!filepath || !graph) {
        return -1;
    }

    char *buf = NULL;
    size_t size = 0;
    if (read_file_mmap(filepath, &buf, &size) < 0) {
        return -1;
    }

    if (size == 0) {
        /* Empty file yields an empty graph */
        return 0;
    }

    TSParser *parser = ts_parser_new();
    if (!parser) {
        fprintf(stderr, "[spatial_parser] Failed to create TSParser\n");
        unmap_file(buf, size);
        return -1;
    }

    if (!ts_parser_set_language(parser, tree_sitter_c())) {
        fprintf(stderr, "[spatial_parser] Failed to set tree_sitter_c language\n");
        ts_parser_delete(parser);
        unmap_file(buf, size);
        return -1;
    }

    TSTree *tree = ts_parser_parse_string(parser, NULL, buf, (uint32_t)size);
    if (!tree) {
        fprintf(stderr, "[spatial_parser] Failed to parse syntax tree\n");
        ts_parser_delete(parser);
        unmap_file(buf, size);
        return -1;
    }

    TSNode root = ts_tree_root_node(tree);
    TSTreeCursor cursor = ts_tree_cursor_new(root);

    /* Use deterministic seed so unit tests have stable coordinates */
    srand(42);

    /* Pass 1: Extract all function definition nodes */
    traverse_functions(&cursor, buf, graph);

    /* Pass 2: Traverse call expressions to populate adjacency list */
    ts_tree_cursor_reset(&cursor, root);
    traverse_calls(&cursor, buf, graph);

    /* Cleanup resources */
    ts_tree_cursor_delete(&cursor);
    ts_tree_delete(tree);
    ts_parser_delete(parser);
    unmap_file(buf, size);

    return 0;
}
