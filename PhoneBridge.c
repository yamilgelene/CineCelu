#include "PhoneBridge.h"

#include <CoreFoundation/CoreFoundation.h>
#include <errno.h>
#include <fcntl.h>
#include <os/lock.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    atomic_llong done;
    atomic_llong total;
    atomic_int file_index;
    atomic_int file_count;
    os_unfair_lock lock;
    char name[160];
} PhonePulse;

typedef struct AMDevice *AMDeviceRef;
typedef struct AFCConnection *AFCConnectionRef;
typedef struct AFCDirectory *AFCDirectoryRef;
typedef struct AFCDictionary *AFCDictionaryRef;

CFArrayRef AMDCreateDeviceList(void);
int AMDeviceConnect(AMDeviceRef device);
int AMDeviceDisconnect(AMDeviceRef device);
int AMDeviceIsPaired(AMDeviceRef device);
int AMDevicePair(AMDeviceRef device);
int AMDeviceValidatePairing(AMDeviceRef device);
int AMDeviceStartSession(AMDeviceRef device);
int AMDeviceStopSession(AMDeviceRef device);
int AMDeviceStartService(AMDeviceRef device, CFStringRef name, CFTypeRef *handle, unsigned int *unknown);
CFStringRef AMDeviceCopyValue(AMDeviceRef device, CFStringRef domain, CFStringRef key);
int AFCConnectionOpen(CFTypeRef handle, uint32_t timeout, AFCConnectionRef *conn);
int AFCConnectionClose(AFCConnectionRef conn);
void AFCConnectionSetIOTimeout(AFCConnectionRef conn, int64_t seconds);
int AFCDirectoryOpen(AFCConnectionRef conn, const char *path, AFCDirectoryRef *dir);
int AFCDirectoryRead(AFCConnectionRef conn, AFCDirectoryRef dir, char **name);
int AFCDirectoryClose(AFCConnectionRef conn, AFCDirectoryRef dir);
int AFCFileInfoOpen(AFCConnectionRef conn, const char *path, AFCDictionaryRef *info);
int AFCKeyValueRead(AFCDictionaryRef info, char **key, char **value);
int AFCKeyValueClose(AFCDictionaryRef info);
int AFCFileRefOpen(AFCConnectionRef conn, const char *path, uint64_t mode, void **ref);
int AFCFileRefRead(AFCConnectionRef conn, void *ref, void *buf, uint64_t *len);
int AFCFileRefClose(AFCConnectionRef conn, void *ref);
int AFCRemovePath(AFCConnectionRef conn, const char *path);

typedef struct {
    CFArrayRef devices;
    AMDeviceRef device;
    AFCConnectionRef afc;
} PhoneSession;

static void set_err(char *err, size_t cap, const char *message) {
    if (!err || cap == 0) return;
    snprintf(err, cap, "%s", message);
}

static void set_err_code(char *err, size_t cap, const char *action, int code) {
    if (!err || cap == 0) return;
    snprintf(err, cap, "%s (código %d)", action, code);
}

static int info_value(AFCConnectionRef afc, const char *path, const char *want, char *out, size_t cap) {
    AFCDictionaryRef info = NULL;
    int rc = AFCFileInfoOpen(afc, path, &info);
    if (rc != 0 || !info) return rc == 0 ? -1 : rc;
    char *key = NULL;
    char *value = NULL;
    int found = -1;
    while (AFCKeyValueRead(info, &key, &value) == 0 && key) {
        if (value && strcmp(key, want) == 0) {
            snprintf(out, cap, "%s", value);
            found = 0;
            break;
        }
    }
    AFCKeyValueClose(info);
    return found;
}

static int64_t parse_mtime(const char *raw) {
    long long value = strtoll(raw, NULL, 10);
    if (value > 10000000000000LL) value /= 1000000000LL;
    return (int64_t)value;
}

static int ends_with(const char *name, const char *suffix) {
    size_t n = strlen(name);
    size_t s = strlen(suffix);
    if (n < s) return 0;
    return strcasecmp(name + n - s, suffix) == 0;
}

static int is_video_name(const char *name) {
    return ends_with(name, ".mov") || ends_with(name, ".mp4") || ends_with(name, ".m4v");
}

static int is_folder_name(const char *name) {
    if (name[0] == '.') return 0;
    if (strchr(name, '.') != NULL) return 0;
    return 1;
}

static int ensure_session(PhoneSession *session, char *err, size_t err_cap) {
    CFArrayRef devices = AMDCreateDeviceList();
    if (!devices || CFArrayGetCount(devices) < 1) {
        if (devices) CFRelease(devices);
        set_err(err, err_cap, "No hay un iPhone conectado.");
        return -1;
    }
    AMDeviceRef device = (AMDeviceRef)CFArrayGetValueAtIndex(devices, 0);
    int rc = AMDeviceConnect(device);
    if (rc != 0) {
        CFRelease(devices);
        set_err_code(err, err_cap, "No pude conectar el iPhone", rc);
        return rc;
    }
    if (!AMDeviceIsPaired(device)) {
        rc = AMDevicePair(device);
        if (rc != 0) {
            AMDeviceDisconnect(device);
            CFRelease(devices);
            set_err(err, err_cap, "El iPhone pidió confianza y no se completó. Desbloquealo y aceptá Confiar.");
            return rc;
        }
    }
    rc = AMDeviceValidatePairing(device);
    if (rc != 0) {
        AMDeviceDisconnect(device);
        CFRelease(devices);
        set_err(err, err_cap, "Hay que desbloquear el iPhone y aceptar Confiar.");
        return rc;
    }
    rc = AMDeviceStartSession(device);
    if (rc != 0) {
        AMDeviceDisconnect(device);
        CFRelease(devices);
        set_err(err, err_cap, "No pude abrir la sesión. Desbloqueá el iPhone.");
        return rc;
    }
    CFTypeRef service = NULL;
    unsigned int unknown = 0;
    rc = AMDeviceStartService(device, CFSTR("com.apple.afc"), &service, &unknown);
    if (rc != 0 || !service) {
        AMDeviceStopSession(device);
        AMDeviceDisconnect(device);
        CFRelease(devices);
        set_err_code(err, err_cap, "No pude abrir la carpeta de la cámara", rc);
        return rc == 0 ? -1 : rc;
    }
    AFCConnectionRef afc = NULL;
    rc = AFCConnectionOpen(service, 0, &afc);
    if (rc == 0 && afc) AFCConnectionSetIOTimeout(afc, 600);
    if (rc != 0 || !afc) {
        AMDeviceStopSession(device);
        AMDeviceDisconnect(device);
        CFRelease(devices);
        set_err_code(err, err_cap, "No pude leer el iPhone", rc);
        return rc == 0 ? -1 : rc;
    }
    session->devices = devices;
    session->device = device;
    session->afc = afc;
    return 0;
}

int phone_open(void **session_out, char *name, size_t name_cap, char *err, size_t err_cap) {
    if (session_out) *session_out = NULL;
    PhoneSession *session = calloc(1, sizeof(PhoneSession));
    if (!session) {
        set_err(err, err_cap, "Sin memoria.");
        return -1;
    }
    int rc = ensure_session(session, err, err_cap);
    if (rc != 0) {
        free(session);
        return rc;
    }
    if (name && name_cap) {
        name[0] = 0;
        CFStringRef value = AMDeviceCopyValue(session->device, NULL, CFSTR("DeviceName"));
        if (value) {
            CFStringGetCString(value, name, name_cap, kCFStringEncodingUTF8);
            CFRelease(value);
        }
        if (name[0] == 0) snprintf(name, name_cap, "iPhone");
    }
    *session_out = session;
    return 0;
}

void phone_close(void *session_ptr) {
    PhoneSession *session = session_ptr;
    if (!session) return;
    if (session->afc) AFCConnectionClose(session->afc);
    if (session->device) {
        AMDeviceStopSession(session->device);
        AMDeviceDisconnect(session->device);
    }
    if (session->devices) CFRelease(session->devices);
    free(session);
}

static int push_video(PhoneVideo **videos, int *count, int *cap, const char *folder, const char *filename, int64_t bytes, int64_t modified, const char *aae) {
    if (*count >= *cap) {
        int next = *cap == 0 ? 64 : *cap * 2;
        PhoneVideo *grown = realloc(*videos, (size_t)next * sizeof(PhoneVideo));
        if (!grown) return -1;
        *videos = grown;
        *cap = next;
    }
    PhoneVideo *video = &(*videos)[*count];
    memset(video, 0, sizeof(*video));
    video->name = strdup(filename);
    char path[768];
    snprintf(path, sizeof path, "/DCIM/%s/%s", folder, filename);
    video->path = strdup(path);
    video->aae = aae ? strdup(aae) : NULL;
    video->bytes = bytes;
    video->modified = modified;
    if (!video->name || !video->path) return -1;
    *count += 1;
    return 0;
}

int phone_list(void *session_ptr, PhoneVideo **videos_out, int *count_out, char *err, size_t err_cap) {
    PhoneSession *session = session_ptr;
    *videos_out = NULL;
    *count_out = 0;
    AFCDirectoryRef root = NULL;
    int rc = AFCDirectoryOpen(session->afc, "/DCIM", &root);
    if (rc != 0 || !root) {
        set_err(err, err_cap, "El iPhone no mostró la carpeta de la cámara.");
        return rc == 0 ? -1 : rc;
    }
    char **folders = NULL;
    int folder_count = 0;
    int folder_cap = 0;
    char *entry = NULL;
    while (AFCDirectoryRead(session->afc, root, &entry) == 0 && entry) {
        if (!is_folder_name(entry)) continue;
        if (folder_count >= folder_cap) {
            int next = folder_cap == 0 ? 32 : folder_cap * 2;
            char **grown = realloc(folders, (size_t)next * sizeof(char *));
            if (!grown) break;
            folders = grown;
            folder_cap = next;
        }
        folders[folder_count++] = strdup(entry);
    }
    AFCDirectoryClose(session->afc, root);

    PhoneVideo *videos = NULL;
    int count = 0;
    int cap = 0;
    for (int folder_index = 0; folder_index < folder_count; folder_index++) {
        char *folder = folders[folder_index];
        char folder_path[320];
        snprintf(folder_path, sizeof folder_path, "/DCIM/%s", folder);
        AFCDirectoryRef sub = NULL;
        if (AFCDirectoryOpen(session->afc, folder_path, &sub) != 0 || !sub) continue;
        char *filename = NULL;
        while (AFCDirectoryRead(session->afc, sub, &filename) == 0 && filename) {
            if (!is_video_name(filename)) continue;
            char full[768];
            snprintf(full, sizeof full, "%s/%s", folder_path, filename);
            char size_raw[64] = {0};
            char time_raw[64] = {0};
            if (info_value(session->afc, full, "st_size", size_raw, sizeof size_raw) != 0) continue;
            info_value(session->afc, full, "st_mtime", time_raw, sizeof time_raw);
            char stem[256];
            snprintf(stem, sizeof stem, "%s", filename);
            char *dot = strrchr(stem, '.');
            if (dot) *dot = 0;
            char aae_path[768];
            snprintf(aae_path, sizeof aae_path, "%s/%s.AAE", folder_path, stem);
            char aae_size[64] = {0};
            const char *aae = NULL;
            if (info_value(session->afc, aae_path, "st_size", aae_size, sizeof aae_size) == 0) {
                aae = aae_path;
            } else {
                snprintf(aae_path, sizeof aae_path, "%s/%s.aae", folder_path, stem);
                if (info_value(session->afc, aae_path, "st_size", aae_size, sizeof aae_size) == 0) aae = aae_path;
            }
            if (push_video(&videos, &count, &cap, folder, filename, strtoll(size_raw, NULL, 10), parse_mtime(time_raw), aae) != 0) {
                AFCDirectoryClose(session->afc, sub);
                phone_videos_free(videos, count);
                for (int i = 0; i < folder_count; i++) free(folders[i]);
                free(folders);
                set_err(err, err_cap, "Sin memoria para listar los videos.");
                return -1;
            }
        }
        AFCDirectoryClose(session->afc, sub);
    }
    for (int i = 0; i < folder_count; i++) free(folders[i]);
    free(folders);
    *videos_out = videos;
    *count_out = count;
    return 0;
}

void phone_videos_free(PhoneVideo *videos, int count) {
    if (!videos) return;
    for (int i = 0; i < count; i++) {
        free(videos[i].name);
        free(videos[i].path);
        free(videos[i].aae);
    }
    free(videos);
}

int phone_size(void *session_ptr, const char *remote, int64_t *out_size, char *err, size_t err_cap) {
    PhoneSession *session = session_ptr;
    char raw[64] = {0};
    int rc = info_value(session->afc, remote, "st_size", raw, sizeof raw);
    if (rc != 0) {
        set_err(err, err_cap, "Ese video ya no está en el iPhone.");
        return rc;
    }
    *out_size = strtoll(raw, NULL, 10);
    return 0;
}

void *phone_pulse_new(void) {
    PhonePulse *pulse = calloc(1, sizeof(PhonePulse));
    if (!pulse) return NULL;
    pulse->lock = OS_UNFAIR_LOCK_INIT;
    return pulse;
}

void phone_pulse_free(void *pulse) {
    free(pulse);
}

void phone_pulse_set_file(void *pulse_ptr, const char *name, int index, int count, int64_t total) {
    PhonePulse *pulse = pulse_ptr;
    if (!pulse) return;
    os_unfair_lock_lock(&pulse->lock);
    snprintf(pulse->name, sizeof pulse->name, "%s", name ? name : "");
    os_unfair_lock_unlock(&pulse->lock);
    atomic_store(&pulse->file_index, index);
    atomic_store(&pulse->file_count, count);
    atomic_store(&pulse->total, total);
    atomic_store(&pulse->done, 0);
}

static void pulse_set_done(PhonePulse *pulse, int64_t done, int64_t total) {
    if (!pulse) return;
    atomic_store(&pulse->total, total);
    atomic_store(&pulse->done, done);
}

void phone_pulse_read(void *pulse_ptr, char *name, size_t name_cap, int64_t *done, int64_t *total, int *index, int *count) {
    PhonePulse *pulse = pulse_ptr;
    if (!pulse) return;
    if (done) *done = atomic_load(&pulse->done);
    if (total) *total = atomic_load(&pulse->total);
    if (index) *index = atomic_load(&pulse->file_index);
    if (count) *count = atomic_load(&pulse->file_count);
    if (name && name_cap) {
        os_unfair_lock_lock(&pulse->lock);
        snprintf(name, name_cap, "%s", pulse->name);
        os_unfair_lock_unlock(&pulse->lock);
    }
}

int phone_copy(void *session_ptr, const char *remote, const char *local, void *pulse_ptr, char *err, size_t err_cap) {
    PhoneSession *session = session_ptr;
    PhonePulse *pulse = pulse_ptr;
    if (!session || !session->afc || !remote || !local) {
        set_err(err, err_cap, "Se cortó la conexión con el iPhone.");
        return 11;
    }
    int64_t total = 0;
    int rc = phone_size(session, remote, &total, err, err_cap);
    if (rc != 0) return rc;

    void *ref = NULL;
    rc = AFCFileRefOpen(session->afc, remote, 1, &ref);
    if (rc != 0 || !ref) {
        set_err_code(err, err_cap, "No pude abrir el video en el iPhone", rc);
        return rc == 0 ? -1 : rc;
    }

    int fd = open(local, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) {
        AFCFileRefClose(session->afc, ref);
        set_err(err, err_cap, "No pude crear el archivo en la carpeta de destino.");
        return -1;
    }

    const uint64_t chunk = 1024 * 1024;
    char *buffer = malloc(chunk);
    if (!buffer) {
        close(fd);
        unlink(local);
        AFCFileRefClose(session->afc, ref);
        set_err(err, err_cap, "Sin memoria para copiar.");
        return -1;
    }

    int64_t done = 0;
    while (done < total) {
        uint64_t got = chunk;
        if ((int64_t)got > total - done) got = (uint64_t)(total - done);
        rc = AFCFileRefRead(session->afc, ref, buffer, &got);
        if (rc != 0 || got == 0 || got > chunk) {
            free(buffer);
            close(fd);
            unlink(local);
            AFCFileRefClose(session->afc, ref);
            set_err_code(err, err_cap, "Se cortó la copia", rc);
            return rc == 0 ? -1 : rc;
        }
        uint64_t wrote_total = 0;
        while (wrote_total < got) {
            ssize_t wrote = write(fd, buffer + wrote_total, (size_t)(got - wrote_total));
            if (wrote < 0) {
                free(buffer);
                close(fd);
                unlink(local);
                AFCFileRefClose(session->afc, ref);
                set_err(err, err_cap, "No pude escribir en el disco de destino.");
                return -1;
            }
            wrote_total += (uint64_t)wrote;
        }
        done += (int64_t)got;
        pulse_set_done(pulse, done, total);
    }
    free(buffer);
    fsync(fd);
    close(fd);
    AFCFileRefClose(session->afc, ref);

    struct stat st;
    if (stat(local, &st) != 0 || (int64_t)st.st_size != total) {
        unlink(local);
        set_err(err, err_cap, "La copia quedó incompleta y la descarté.");
        return -1;
    }
    pulse_set_done(pulse, total, total);
    return 0;
}

int phone_remove(void *session_ptr, const char *remote, char *err, size_t err_cap) {
    PhoneSession *session = session_ptr;
    int rc = AFCRemovePath(session->afc, remote);
    if (rc != 0) {
        set_err_code(err, err_cap, "No pude borrar el archivo del iPhone", rc);
        return rc;
    }
    return 0;
}
