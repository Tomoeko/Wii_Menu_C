#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include "texture_source.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WM_MAX_URL_LENGTH = 1024 };

bool wm_texture_source_url_valid(const char *url, size_t *length) {
    if (!url || !url[0]) {
        return false;
    }
    size_t size = strnlen(url, WM_MAX_URL_LENGTH + 1);
    if (size < 5 || size > WM_MAX_URL_LENGTH || strcmp(url + size - 4, ".png") != 0) {
        return false;
    }

    size_t segment_start = 0;
    for (size_t index = 0; index <= size; ++index) {
        unsigned char character = (unsigned char)url[index];
        if (character == '/' || character == '\0') {
            size_t segment_length = index - segment_start;
            if (segment_length == 0 ||
                (segment_length == 1 && url[segment_start] == '.') ||
                (segment_length == 2 && url[segment_start] == '.' &&
                 url[segment_start + 1] == '.')) {
                return false;
            }
            segment_start = index + 1;
        } else if (character < 32 || character == 127 || character == '\\' ||
                   character == ':' || character == '?' || character == '#' ||
                   character == '%') {
            return false;
        }
    }
    *length = size;
    return true;
}

static bool path_within_root(const char *raw_root, const char *path) {
    size_t root_length = strlen(raw_root);
    if (root_length == 1 && raw_root[0] == '/') {
        return path[0] == '/' && path[1] != '\0';
    }
    return strncmp(path, raw_root, root_length) == 0 && path[root_length] == '/';
}

char *wm_texture_source_resolve(const char *raw_root, const char *url,
                                size_t url_length) {
    size_t root_length = strlen(raw_root);
    if (root_length > SIZE_MAX - url_length - 3) {
        return NULL;
    }
    char *candidate = malloc(root_length + url_length + 3);
    if (!candidate) {
        return NULL;
    }
    memcpy(candidate, raw_root, root_length);
    candidate[root_length] = '/';
    memcpy(candidate + root_length + 1, url, url_length - 4);
    memcpy(candidate + root_length + 1 + url_length - 4, ".wmra", 6);

    char *canonical = realpath(candidate, NULL);
    free(candidate);
    if (!canonical) {
        return NULL;
    }
    if (!path_within_root(raw_root, canonical)) {
        free(canonical);
        return NULL;
    }
    return canonical;
}

static uint32_t read_little_endian_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

/* Check the declared size before decoding, so an image above the GPU budget
 * never creates a large temporary pixel allocation. The bounded image reader
 * still checks the complete versioned file and its exact payload length. */
bool wm_texture_source_declared_bytes(const char *path, size_t *bytes) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    uint8_t header[16];
    bool valid = fread(header, 1, sizeof(header), file) == sizeof(header);
    fclose(file);
    if (!valid || memcmp(header, "WMRA", 4) != 0 ||
        read_little_endian_u32(header + 4) != 1) {
        return false;
    }
    uint32_t width = read_little_endian_u32(header + 8);
    uint32_t height = read_little_endian_u32(header + 12);
    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        return false;
    }
    uint64_t count = (uint64_t)width * height * 4;
    if (count > SIZE_MAX || count > 256ULL * 1024 * 1024) {
        return false;
    }
    *bytes = (size_t)count;
    return true;
}
