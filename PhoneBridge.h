#pragma once

#include <stdint.h>
#include <stddef.h>

typedef struct {
    char *name;
    char *path;
    char *aae;
    int64_t bytes;
    int64_t modified;
} PhoneVideo;

int phone_open(void **session, char *name, size_t name_cap, char *err, size_t err_cap);
void phone_close(void *session);
int phone_list(void *session, PhoneVideo **videos, int *count, char *err, size_t err_cap);
void phone_videos_free(PhoneVideo *videos, int count);
void *phone_pulse_new(void);
void phone_pulse_free(void *pulse);
void phone_pulse_set_file(void *pulse, const char *name, int index, int count, int64_t total);
void phone_pulse_read(void *pulse, char *name, size_t name_cap, int64_t *done, int64_t *total, int *index, int *count);
int phone_copy(void *session, const char *remote, const char *local, void *pulse, char *err, size_t err_cap);
int phone_size(void *session, const char *remote, int64_t *out_size, char *err, size_t err_cap);
int phone_remove(void *session, const char *remote, char *err, size_t err_cap);
