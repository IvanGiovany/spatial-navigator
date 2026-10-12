#ifndef SPATIAL_PARSER_H
#define SPATIAL_PARSER_H

#include "graph.h"

int parse_source_file(const char *filepath, Graph *graph);
int read_file_mmap(const char *filepath, char **out_buf, size_t *out_size);
void unmap_file(char *buf, size_t size);

#endif /* SPATIAL_PARSER_H */
