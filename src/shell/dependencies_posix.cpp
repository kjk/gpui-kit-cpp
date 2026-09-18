#include "shell/dependencies.h"

#if !GPUI_OS_WINDOWS

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gpui::shell {

static void DependencyError(Str* error, Str message) {
    if (!error) return;
    StrFree(*error);
    *error = StrDup(message);
}

bool DependencyMakeDirectories(Str path, Str* error) {
    if (!path || len(path) >= kMaxPath) {
        DependencyError(error,
                        StrL("dependency cache path is empty or too long"));
        return false;
    }
    TempStr buffer = StrDupTemp(path);
    for (int i = 1; i <= len(path); i++) {
        if (i < len(path) && buffer.s[i] != '/') continue;
        char saved = buffer.s[i];
        buffer.s[i] = 0;
        if (mkdir(buffer.s, 0700) != 0 && errno != EEXIST) {
            buffer.s[i] = saved;
            DependencyError(error, fmt("creating %s failed", path));
            return false;
        }
        buffer.s[i] = saved;
    }
    return true;
}

static void RemoveTreeAt(const char* path) {
    struct stat info;
    if (lstat(path, &info) != 0) return;
    if (!S_ISDIR(info.st_mode)) {
        unlink(path);
        return;
    }
    DIR* dir = opendir(path);
    if (dir) {
        for (struct dirent* entry = readdir(dir); entry; entry = readdir(dir)) {
            Str name = Str(entry->d_name);
            if (StrEq(name, StrL(".")) || StrEq(name, StrL(".."))) continue;
            TempStr child = fmt("%s/%s", Str(path), name);
            if (len(child) < kMaxPath) RemoveTreeAt(child.s);
        }
        closedir(dir);
    }
    rmdir(path);
}

void DependencyRemoveTree(Str path) {
    if (!path || len(path) >= kMaxPath) return;
    TempStr buffer = StrDupTemp(path);
    RemoveTreeAt(buffer.s);
}

bool DependencyRenameDirectory(Str from, Str to) {
    if (!from || !to) return false;
    // rename() over an existing non-empty directory fails, which is the
    // "another process published it first" arm upstream takes.
    return rename(from.s, to.s) == 0;
}

bool DependencyLockAcquire(Str path, Str name, DependencyLock* out,
                           Str* error) {
    if (out) *out = {};
    int fd = open(path.s, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        DependencyError(
            error, fmt("opening Git dependency cache lock %s failed", path));
        return false;
    }
    double started = TimeNow();
    for (;;) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
            if (out) out->handle = (intptr_t)fd + 1;
            return true;
        }
        if (errno != EWOULDBLOCK && errno != EINTR) {
            close(fd);
            DependencyError(error, StrL("locking Git dependency cache failed"));
            return false;
        }
        if (TimeNow() - started >= kGitDependencyLockTimeout) {
            close(fd);
            DependencyError(
                error, fmt("timed out waiting for another process to finish "
                           "Git dependency `%s`",
                           name));
            return false;
        }
        PlatSleepMs(25);
    }
}

void DependencyLockRelease(DependencyLock* lock) {
    if (!lock || lock->handle == 0) return;
    int fd = (int)(lock->handle - 1);
    flock(fd, LOCK_UN);
    close(fd);
    lock->handle = 0;
}

bool DependencySymlinkDirectory(Str target, Str link) {
    return target && link && symlink(target.s, link.s) == 0;
}

bool DependencyRemoveDirectoryLink(Str link) {
    return link && unlink(link.s) == 0;
}

bool DependencyReadDirectoryLink(Str link, Str* target) {
    if (target) *target = {};
    if (!link) return false;
    TempStr buffer = AllocStrTemp(kMaxPath - 1);
    ssize_t n = readlink(link.s, buffer.s, (size_t)len(buffer));
    if (n <= 0) return false;
    buffer.s[n] = 0;
    if (target) *target = StrDup(Str(buffer.s, (int)n));
    return true;
}

uint32_t DependencyProcessId() {
    return (uint32_t)getpid();
}

} // namespace gpui::shell

#endif
