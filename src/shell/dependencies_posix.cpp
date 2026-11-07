#include "shell/dependencies.h"

#include "shell/error.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace gpui::shell {

bool DependencyMakeDirectory(Str path) {
    TempStr buffer = StrDupTemp(path);
    return mkdir(buffer.s, 0700) == 0 || errno == EEXIST;
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

bool DependencyLockOpen(Str path, DependencyLock* lock) {
    int fd = open(path.s, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    lock->handle = (intptr_t)fd + 1;
    return true;
}

int DependencyLockTry(DependencyLock* lock) {
    if (flock((int)(lock->handle - 1), LOCK_EX | LOCK_NB) == 0) return 1;
    return errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
}

void DependencyLockClose(DependencyLock* lock) {
    close((int)(lock->handle - 1));
    lock->handle = 0;
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
