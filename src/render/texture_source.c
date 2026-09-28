#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include "texture_source.h"

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { WM_MAX_URL_LENGTH = 1024 };

bool wm_texture_source_url_valid(const char *url, size_t *length) {
    if (!url || !url[0] || !length) {
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

FILE *wm_texture_source_open(int root_directory, const char *url,
                             size_t url_length) {
    size_t checked_length;
    if (root_directory < 0 ||
        !wm_texture_source_url_valid(url, &checked_length) ||
        checked_length != url_length) {
        return NULL;
    }

    int directory = root_directory;
    bool close_directory = false;
    size_t segment_start = 0;
    for (size_t index = 0; index <= url_length; ++index) {
        if (index != url_length && url[index] != '/') {
            continue;
        }

        bool is_file = index == url_length;
        size_t segment_length = index - segment_start;
        char name[WM_MAX_URL_LENGTH + 2];
        if (is_file) {
            size_t stem_length = segment_length - 4;
            memcpy(name, url + segment_start, stem_length);
            memcpy(name + stem_length, ".wmra", sizeof(".wmra"));
        } else {
            memcpy(name, url + segment_start, segment_length);
            name[segment_length] = '\0';
        }

        int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW;
        flags |= is_file ? O_NONBLOCK : O_DIRECTORY;
        int descriptor = openat(directory, name, flags);
        if (close_directory) {
            close(directory);
        }
        if (descriptor < 0) {
            return NULL;
        }
        if (is_file) {
            struct stat information;
            if (fstat(descriptor, &information) != 0 ||
                !S_ISREG(information.st_mode)) {
                close(descriptor);
                return NULL;
            }
            FILE *stream = fdopen(descriptor, "rb");
            if (!stream) {
                close(descriptor);
            }
            return stream;
        }

        directory = descriptor;
        close_directory = true;
        segment_start = index + 1;
    }
    return NULL;
}
