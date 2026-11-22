/* The macOS-only half of the Base platform layer. Everything POSIX shares
   with Linux is in Base_posix.cpp; what is left is dyld and Mach. */

#include "base.h"

#include <errno.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>

// Exported by libSystem; declared in no public header. Same call the Rust
// speech example makes in examples/speech/src/responsibility.rs.
extern "C" int responsibility_spawnattrs_setdisclaim(posix_spawnattr_t* attrs,
                                                     int disclaim);
// C linkage, same as base_posix.cpp. The amalgam compiles both into one
// translation unit; a C++ declaration here is a different symbol.
extern "C" char** environ;

namespace base {

// kPlatFacts.exePath.
static bool ExePath(char* out, int cap) {
    uint32_t n = (uint32_t)cap;
    if (_NSGetExecutablePath(out, &n) != 0) return false;
    out[cap - 1] = 0;
    return out[0] != 0;
}

// examples/speech/src/responsibility.rs. The parent waits and the caller
// exits with the child's status. A failed relaunch prints and keeps going.
// kPlatFacts.disclaimSpeechResponsibility.
static bool DisclaimSpeechResponsibility(int argc, char** argv, int* status) {
    if (getenv("SPEECH_RESPONSIBILITY_DISCLAIMED")) {
        return true;
    }
    char path[kMaxPath];
    if (!PlatGetExePath(path, kMaxPath)) {
        fprintf(stderr,
                "Speech could not disclaim the terminal's responsibility: "
                "current executable path is unknown\n");
        return true;
    }
    if (argc < 0) {
        argc = 0;
    }
    char** childArgv = new char*[argc + 1];
    for (int i = 0; i < argc; i++) {
        childArgv[i] = argv ? argv[i] : nullptr;
    }
    childArgv[argc] = nullptr;

    int nenv = 0;
    if (environ) {
        while (environ[nenv]) {
            nenv++;
        }
    }
    char** envp = new char*[nenv + 2];
    for (int i = 0; i < nenv; i++) {
        envp[i] = environ[i];
    }
    envp[nenv] = const_cast<char*>("SPEECH_RESPONSIBILITY_DISCLAIMED=1");
    envp[nenv + 1] = nullptr;

    posix_spawnattr_t attrs = nullptr;
    pid_t pid = 0;
    int err = posix_spawnattr_init(&attrs);
    if (err == 0) {
        err = responsibility_spawnattrs_setdisclaim(&attrs, 1);
        if (err == 0) {
            err = posix_spawn(&pid, path, nullptr, &attrs, childArgv, envp);
        }
        posix_spawnattr_destroy(&attrs);
    }
    delete[] childArgv;
    delete[] envp;
    if (err != 0) {
        fprintf(stderr,
                "Speech could not disclaim the terminal's responsibility: "
                "%s\n",
                strerror(err));
        return true;
    }

    int raw = 0;
    for (;;) {
        if (waitpid(pid, &raw, 0) != -1) {
            break;
        }
        if (errno != EINTR) {
            fprintf(stderr,
                    "Speech could not disclaim the terminal's responsibility: "
                    "%s\n",
                    strerror(errno));
            return true;
        }
    }
    int code = WIFEXITED(raw) ? WEXITSTATUS(raw) : 128 + WTERMSIG(raw);
    if (status) {
        *status = code;
    }
    return false;
}

const PlatFacts kPlatFacts = {
    .secondaryIsCommand = true,
    .wordDeleteStopsAtWhitespace = false,
    // The traffic lights are the window's, not the title bar's.
    .showsWindowControls = false,
    .scrollBounce = true,
    .overscrollPhaselessStops = true,
    .monoFontName = "Menlo",
    .shellDataDir = "/Library/Application Support",
    .shellPlatformName = "macos",
    .exePath = ExePath,
    .disclaimSpeechResponsibility = DisclaimSpeechResponsibility,
};

} // namespace base
