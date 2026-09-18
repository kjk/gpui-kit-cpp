/* Git-backed manifest dependencies, ported from
 * crates/shell/src/dependencies.rs and the `dependencies` half of
 * crates/shell/src/plugin.rs.
 *
 * Nothing here reaches the network: the remote is a repository this file
 * creates in a scratch directory and clones through the same `git` executable
 * the runtime would use, and the editor-link tests fabricate checkouts under
 * the store's own cache without running git at all. */

#include "Test.h"

#include <stdio.h>
#include <stdlib.h>

using namespace gpui::shell;

static Str DepJoin(Str left, Str right) {
    return StrDup(fmt("%s/%s", left, right));
}

// `std::env::temp_dir()`. A cache path carries a 64 character remote key and a
// 40 character commit, and Git then writes its own tree below that, so these
// fixtures cannot live under a deep working directory on Windows.
// Every fixture gets a directory of its own. Reusing one path per name was
// enough on POSIX, where the tree is gone the moment it is unlinked, but on
// Windows a git child that has exited can still hold a handle for a moment,
// so the remove at the next construction silently leaves part of the old
// repository behind and the next case reads its package.json.
static int gDepFixtureSerial = 0;

static Str DepTempDir() {
    const char* names[] = {"TMPDIR", "TEMP", "TMP"};
    for (int i = 0; i < 3; i++) {
        const char* value = getenv(names[i]);
        if (value && *value) {
            Str result = StrDup(Str(value));
            for (int c = 0; c < len(result); c++)
                if (result.s[c] == '\\') result.s[c] = '/';
            while (len(result) > 1 && result.s[len(result) - 1] == '/')
                result.len--;
            return result;
        }
    }
    return StrDup(StrL("/tmp"));
}

static bool DepWrite(Str path, Str contents) {
    if (len(path) >= kMaxPath) return false;
    TempStr name = StrDupTemp(path);
    FILE* file = fopen(name.s, "wb");
    if (!file) return false;
    bool ok = len(contents) == 0 || fwrite(contents.s, 1, (size_t)len(contents),
                                           file) == (size_t)len(contents);
    return fclose(file) == 0 && ok;
}

static bool RunGitFixture(Str directory, const Str* args, int count) {
    ProcessOptions options;
    options.workingDirectory = directory;
    options.inheritEnvironment = true;
    ProcessOutput output;
    Str error = {};
    bool ok = ProcessRunBounded(StrL("git"), args, count, nullptr, &output,
                                &error, &options) &&
              output.code == 0;
    output.Free();
    StrFree(error);
    return ok;
}

static bool GitIsInstalled() {
    Str args[1] = {StrL("--version")};
    return RunGitFixture(StrL("."), args, 1);
}

// --- the cache root ----------------------------------------------------

static void DependencyCacheLivesInTheShellCache() {
    char separator = GPUI_OS_WINDOWS ? '\\' : '/';
    Str root = GitDependencyCacheRoot(StrL("/home/example"));
    utassert(StrEq(root, fmt("/home/example%c.gpui-shell%ccache%cdependencies",
                             separator, separator, separator)));
    StrFree(root);

    Str selected = {};
    Str error = {};
    utassert(GitDependencyUserCacheRoot(StrL("/home/example"), {}, &selected,
                                        &error) &&
             !error);
    Str expected = GitDependencyCacheRoot(StrL("/home/example"));
    utassert(StrEq(selected, expected));
    StrFree(selected);
    StrFree(expected);

    // USERPROFILE answers when HOME is missing, and when it is empty.
    utassert(GitDependencyUserCacheRoot({}, StrL("/profiles/example"),
                                        &selected, &error));
    expected = GitDependencyCacheRoot(StrL("/profiles/example"));
    utassert(StrEq(selected, expected));
    StrFree(selected);
    StrFree(expected);
    utassert(GitDependencyUserCacheRoot(StrL(""), StrL("/profiles/example"),
                                        &selected, &error));
    StrFree(selected);

    utassert(!GitDependencyUserCacheRoot({}, {}, &selected, &error) &&
             StrContains(error, StrL("HOME or USERPROFILE")));
    utassert(!GitDependencyUserCacheRoot(
        StrL("relative/home"), StrL("/profiles/example"), &selected, &error));
    utassert(StrContains(error, StrL("HOME")) &&
             StrContains(error, StrL("absolute")) &&
             StrContains(error, StrL("relative/home")));
    StrFree(error);

    // `digest(&[("git", url)])` is the per-remote cache key: stable, hex, and
    // different for a different remote.
    Str first = GitDependencyRemoteKey(StrL("https://example.test/ui"));
    Str again = GitDependencyRemoteKey(StrL("https://example.test/ui"));
    Str other = GitDependencyRemoteKey(StrL("https://example.test/ui.git"));
    utassert(len(first) == 64 && StrEq(first, again) && !StrEq(first, other));
    for (int i = 0; i < len(first); i++) {
        char c = first.s[i];
        utassert((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    StrFree(first);
    StrFree(again);
    StrFree(other);
}

// --- manifest validation -----------------------------------------------

static Str DependencyManifest(Str body) {
    return StrDup(
        fmt("{\"id\":\"com.example.third-party-ui\","
            "\"name\":\"Third-party UI\",\"entry\":\"main.js\","
            "\"dependencies\":{\"omarchy-ui\":%s}}",
            body));
}

static bool ParsesDependency(Str body, PluginManifest* manifest,
                             ShellError* error) {
    Str source = DependencyManifest(body);
    bool ok = PluginManifestParse(source, manifest, error);
    StrFree(source);
    return ok;
}

static void ManifestsDescribeGitDependenciesBeforeAnythingRuns() {
    {
        PluginManifest manifest;
        ShellError error = {};
        utassert(ParsesDependency(
            StrL("{\"git\":\"https://github.com/huacnlee/omarchy-ui\","
                 "\"branch\":\"main\"}"),
            &manifest, &error));
        utassert(manifest.dependencies.len == 1);
        const GitDependency& dependency = manifest.dependencies[0];
        utassert(StrEq(dependency.name, StrL("omarchy-ui")) &&
                 StrEq(dependency.git,
                       StrL("https://github.com/huacnlee/omarchy-ui")) &&
                 StrEq(dependency.branch, StrL("main")) && !dependency.tag &&
                 StrEq(dependency.entry, StrL("index.js")) &&
                 !dependency.packageEntry);
        ShellErrorClear(&error);
    }
    {
        // The object form keeps its explicit selector and entry contract.
        PluginManifest manifest;
        ShellError error = {};
        utassert(ParsesDependency(
            StrL("{\"git\":\"ssh://git@example.com/omarchy-ui.git\","
                 "\"tag\":\"v1.2.0\",\"entry\":\"src/public.js\"}"),
            &manifest, &error));
        const GitDependency& dependency = manifest.dependencies[0];
        utassert(!dependency.branch && StrEq(dependency.tag, StrL("v1.2.0")) &&
                 StrEq(dependency.entry, StrL("src/public.js")) &&
                 !dependency.packageEntry);
        ShellErrorClear(&error);
    }

    // A string dependency is a Git URL with one optional `#ref`; the entry
    // then comes from the package rather than from the manifest.
    struct StringCase {
        const char* source;
        const char* git;
        const char* reference;
    };
    static const StringCase strings[] = {
        {"\"https://github.com/huacnlee/omarchy-ui#main\"",
         "https://github.com/huacnlee/omarchy-ui", "main"},
        {"\"https://github.com/huacnlee/omarchy-ui#v1.2.0\"",
         "https://github.com/huacnlee/omarchy-ui", "v1.2.0"},
        {"\"https://github.com/huacnlee/omarchy-ui"
         "#0123456789abcdef0123456789abcdef01234567\"",
         "https://github.com/huacnlee/omarchy-ui",
         "0123456789abcdef0123456789abcdef01234567"},
        {"\"https://github.com/huacnlee/omarchy-ui\"",
         "https://github.com/huacnlee/omarchy-ui", nullptr},
        // GitHub shorthand expands to an https remote and defaults to main.
        {"\"huacnlee/omarchy-ui\"", "https://github.com/huacnlee/omarchy-ui",
         "main"},
        {"\"huacnlee/omarchy-ui#stable\"",
         "https://github.com/huacnlee/omarchy-ui", "stable"},
    };
    for (int i = 0; i < (int)(sizeof(strings) / sizeof(strings[0])); i++) {
        PluginManifest manifest;
        ShellError error = {};
        utassert(ParsesDependency(Str(strings[i].source), &manifest, &error));
        const GitDependency& dependency = manifest.dependencies[0];
        utassert(StrEq(dependency.git, strings[i].git));
        utassert(strings[i].reference
                     ? StrEq(dependency.reference, strings[i].reference)
                     : !dependency.reference);
        utassert(dependency.packageEntry &&
                 StrEq(dependency.entry, StrL("index.js")));
        ShellErrorClear(&error);
    }

    // Malformed string syntax must not select an unintended repository.
    static const char* malformed[] = {
        "\"huacnlee\"",
        "\"/omarchy-ui\"",
        "\"huacnlee/\"",
        "\"huacnlee/omarchy-ui/extra\"",
        "\"huacnlee//omarchy-ui\"",
        "\"huacnlee/omarchy-ui#\"",
        "\"https://github.com/huacnlee/omarchy-ui#\"",
        "\"not a git dependency\"",
    };
    for (int i = 0; i < (int)(sizeof(malformed) / sizeof(malformed[0])); i++) {
        PluginManifest manifest;
        ShellError error = {};
        utassert(!ParsesDependency(Str(malformed[i]), &manifest, &error));
        utassert(StrContains(error.message, StrL("Git URL")) ||
                 StrContains(error.message, StrL("owner/repository")) ||
                 StrContains(error.message, StrL("Git ref")));
        ShellErrorClear(&error);
    }

    struct Refusal {
        const char* body;
        const char* expected;
    };
    static const Refusal refusals[] = {
        {"{\"git\":\"https://github.com/example/omarchy-ui.git\","
         "\"branch\":\"main\",\"tag\":\"v1.2.0\"}",
         "either `branch` or `tag`"},
        {"{\"git\":\"https://github.com/example/omarchy-ui.git\"}",
         "one non-empty `branch` or `tag`"},
        {"{\"git\":\"https://github.com/example/omarchy-ui.git\","
         "\"tag\":\"v1.2.0\",\"entry\":\"../private.js\"}",
         "path inside the Git repository"},
        {"{\"git\":\"https://github.com/example/omarchy-ui.git\","
         "\"branch\":\"main:refs/heads/injected\"}",
         "valid Git ref name"},
        {"{\"git\":\"  \",\"branch\":\"main\"}", "must not be empty"},
    };
    for (int i = 0; i < (int)(sizeof(refusals) / sizeof(refusals[0])); i++) {
        PluginManifest manifest;
        ShellError error = {};
        utassert(!ParsesDependency(Str(refusals[i].body), &manifest, &error));
        utassert(StrContains(error.message, Str(refusals[i].expected)));
        ShellErrorClear(&error);
    }

    // A runtime module name is unreachable as a dependency, so it is refused
    // rather than silently shadowed.
    {
        PluginManifest manifest;
        ShellError error = {};
        Str source = StrL(
            "{\"id\":\"com.example.shadow\",\"name\":\"Shadow\","
            "\"entry\":\"main.js\",\"dependencies\":{\"gpui\":"
            "{\"git\":\"https://github.com/example/not-gpui.git\","
            "\"branch\":\"main\"}}}");
        utassert(!PluginManifestParse(source, &manifest, &error));
        utassert(StrContains(error.message, StrL("reserved by gpui-shell")));
        ShellErrorClear(&error);
    }
    {
        PluginManifest manifest;
        ShellError error = {};
        Str source = StrL(
            "{\"id\":\"com.example.shadow\",\"name\":\"Shadow\","
            "\"entry\":\"main.js\",\"dependencies\":{\"../escape\":"
            "{\"git\":\"https://github.com/example/ui.git\","
            "\"branch\":\"main\"}}}");
        utassert(!PluginManifestParse(source, &manifest, &error));
        utassert(StrContains(error.message,
                             StrL("is not a valid bare module name")));
        ShellErrorClear(&error);
    }

    // Manifest order is the map's: dependencies are walked by name.
    {
        PluginManifest manifest;
        ShellError error = {};
        Str source = StrL(
            "{\"id\":\"com.example.two\",\"name\":\"Two\","
            "\"entry\":\"main.js\",\"dependencies\":{"
            "\"zeta\":\"a/zeta\",\"alpha\":\"a/alpha\"}}");
        utassert(PluginManifestParse(source, &manifest, &error));
        utassert(manifest.dependencies.len == 2 &&
                 StrEq(manifest.dependencies[0].name, StrL("alpha")) &&
                 StrEq(manifest.dependencies[1].name, StrL("zeta")));
        ShellErrorClear(&error);
    }
}

// --- editor links ------------------------------------------------------

// A store, an application, and checkouts under the store's own cache —
// everything linking needs, and nothing Git does.
struct LinkFixture {
    Str root;
    Str cache;
    Str app;
    Str modules;

    explicit LinkFixture(const char* name) {
        Str temp = DepTempDir();
        root =
            StrDup(fmt("%s/gsd_%s_%d", temp, Str(name), ++gDepFixtureSerial));
        StrFree(temp);
        cache = DepJoin(root, StrL("c"));
        app = DepJoin(root, StrL("app"));
        modules = DepJoin(app, StrL("node_modules"));
        DependencyRemoveTree(root);
        DependencyMakeDirectories(cache, nullptr);
        DependencyMakeDirectories(app, nullptr);
    }

    ~LinkFixture() {
        DependencyRemoveTree(root);
        StrFree(root);
        StrFree(cache);
        StrFree(app);
        StrFree(modules);
    }

    // A checkout of `name` at `commit`, holding one entry file.
    MaterializedDependency Checkout(const char* name, const char* commit) {
        Str checkouts = DepJoin(cache, StrL("checkouts"));
        Str directory = DepJoin(checkouts, Str(commit));
        DependencyMakeDirectories(directory, nullptr);
        Str entry = DepJoin(directory, StrL("index.js"));
        DepWrite(entry, StrL("export const label = 'linked';\n"));
        MaterializedDependency dependency;
        dependency.name = StrDup(Str(name));
        dependency.root = StrDup(directory);
        dependency.entry = entry;
        StrFree(checkouts);
        StrFree(directory);
        return dependency;
    }

    Str Link(const char* name) { return DepJoin(modules, Str(name)); }
};

static bool LinkResolves(Str link) {
    // Either a symlink into the cache or the re-export package written where
    // a symlink was refused.
    Str target = {};
    bool symlink = DependencyReadDirectoryLink(link, &target);
    StrFree(target);
    if (symlink) return true;
    Str marker = DepJoin(link, StrL(".gpui-shell-link"));
    Str index = DepJoin(link, StrL("index.js"));
    Str manifest = DepJoin(link, StrL("package.json"));
    bool stub = PlatFileExists(marker.s) && PlatFileExists(index.s) &&
                PlatFileExists(manifest.s);
    StrFree(marker);
    StrFree(index);
    StrFree(manifest);
    return stub;
}

static void EditorLinksPointAtTheCheckoutsTheRuntimeWillExecute() {
    LinkFixture fixture("links");
    GitDependencyStore store(fixture.cache);
    MaterializedDependencies dependencies;
    VecAppend(dependencies.items, fixture.Checkout("omarchy-ui", "aaaa"));

    int linked = 0;
    Str error = {};
    utassert(store.LinkForEditor(fixture.app, dependencies, &linked, &error));
    utassert(!error && linked == 1);
    Str link = fixture.Link("omarchy-ui");
    utassert(LinkResolves(link));

    // Linking again is not a change when the platform gave us a symlink: it
    // already points at the checkout this load is going to use. Where a
    // symlink was refused, the re-export package is rewritten, because a
    // directory carrying our marker says who wrote it and not what it names.
    Str existing = {};
    bool isSymlink = DependencyReadDirectoryLink(link, &existing);
    StrFree(existing);
    linked = -1;
    utassert(store.LinkForEditor(fixture.app, dependencies, &linked, &error));
    utassert(linked == (isSymlink ? 0 : 1));

    // An installed package of the same name belongs to whoever installed it.
    MaterializedDependencies second;
    VecAppend(second.items, fixture.Checkout("installed", "bbbb"));
    Str installed = fixture.Link("installed");
    DependencyMakeDirectories(installed, nullptr);
    Str theirs = DepJoin(installed, StrL("package.json"));
    utassert(DepWrite(theirs, StrL("{\"name\":\"installed\"}\n")));
    linked = -1;
    utassert(store.LinkForEditor(fixture.app, second, &linked, &error));
    utassert(linked == 0);
    Str current = {};
    utassert(!DependencyReadDirectoryLink(installed, &current));
    StrFree(current);
    utassert(PlatFileExists(theirs.s));

    // The link of a dependency the manifest no longer declares is pruned, and
    // the installed package beside it is not.
    utassert(!PlatDirExists(link.s) || LinkResolves(link));
    linked = -1;
    utassert(store.LinkForEditor(fixture.app, second, &linked, &error));
    Str stale = {};
    utassert(!DependencyReadDirectoryLink(link, &stale) &&
             !PlatFileExists(DepJoin(link, StrL("index.js")).s));
    StrFree(stale);
    utassert(PlatFileExists(theirs.s));

    StrFree(theirs);
    StrFree(installed);
    StrFree(link);
    StrFree(error);
    dependencies.Free();
    second.Free();
}

// --- git-backed materialization ----------------------------------------

struct GitFixture {
    Str root;
    Str remote;
    Str cache;
    bool ok = false;

    explicit GitFixture(const char* name) {
        Str temp = DepTempDir();
        root =
            StrDup(fmt("%s/gsd_%s_%d", temp, Str(name), ++gDepFixtureSerial));
        StrFree(temp);
        remote = DepJoin(root, StrL("remote"));
        cache = DepJoin(root, StrL("c"));
        DependencyRemoveTree(root);
        DependencyMakeDirectories(remote, nullptr);
        Str init[2] = {StrL("init"), StrL("--initial-branch=main")};
        Str user[3] = {StrL("config"), StrL("user.name"),
                       StrL("gpui-shell test")};
        Str mail[3] = {StrL("config"), StrL("user.email"),
                       StrL("gpui-shell@example.invalid")};
        ok = RunGitFixture(remote, init, 2) && RunGitFixture(remote, user, 3) &&
             RunGitFixture(remote, mail, 3);
    }

    ~GitFixture() {
        DependencyRemoveTree(root);
        StrFree(root);
        StrFree(remote);
        StrFree(cache);
    }

    bool Write(const char* name, Str source) {
        Str path = DepJoin(remote, Str(name));
        int lastSeparator = -1;
        for (int i = 0; i < len(path); i++)
            if (path.s[i] == '/') lastSeparator = i;
        if (lastSeparator > len(remote)) {
            Str parent(path.s, lastSeparator);
            DependencyMakeDirectories(parent, nullptr);
        }
        bool wrote = DepWrite(path, source);
        StrFree(path);
        return wrote;
    }

    bool Commit(const char* message) {
        Str add[2] = {StrL("add"), StrL(".")};
        Str commit[3] = {StrL("commit"), StrL("-m"), Str(message)};
        return RunGitFixture(remote, add, 2) &&
               RunGitFixture(remote, commit, 3);
    }

    bool Tag(const char* name) {
        Str tag[2] = {StrL("tag"), Str(name)};
        return RunGitFixture(remote, tag, 2);
    }

    // The absolute path git was handed, which is also the origin identity the
    // cache is checked against.
    Str Url() {
        TempStr resolved = AllocStrTemp(kMaxPath - 1);
        if (!PlatCanonicalPath(remote.s, resolved.s, len(resolved) + 1))
            return {};
        Str url = StrDup(Str(resolved.s));
        for (int i = 0; i < len(url); i++)
            if (url.s[i] == '\\') url.s[i] = '/';
        return url;
    }
};

static Str ReadWhole(Str path) {
    if (!path || len(path) >= kMaxPath) return {};
    TempStr name = StrDupTemp(path);
    FILE* file = fopen(name.s, "rb");
    if (!file) return {};
    TempStr block = AllocStrTemp(4096);
    size_t read = fread(block.s, 1, (size_t)len(block), file);
    fclose(file);
    return StrDup(Str(block.s, (int)read));
}

static void GitDependenciesResolveRefreshAndStayInsideTheirCheckout() {
    if (!GitIsInstalled()) return;
    GitFixture fixture("git");
    utassert(fixture.ok);
    utassert(fixture.Write("index.js", StrL("export const version = 1;")) &&
             fixture.Commit("first"));
    Str url = fixture.Url();
    utassert(url);

    Str body = StrDup(fmt("{\"git\":\"%s\",\"branch\":\"main\"}", url));
    PluginManifest manifest;
    ShellError manifestError = {};
    utassert(ParsesDependency(body, &manifest, &manifestError));
    ShellErrorClear(&manifestError);

    GitDependencyStore store(fixture.cache);
    MaterializedDependency first;
    Str error = {};
    utassert(store.Materialize(StrL("omarchy-ui"), manifest.dependencies[0],
                               &first, &error));
    utassert(!error);
    Str text = ReadWhole(first.entry);
    utassert(StrEq(text, StrL("export const version = 1;")));
    StrFree(text);

    // A branch dependency refreshes to the remote head, and the checkout an
    // older module generation retained is not mutated by the refresh.
    utassert(fixture.Write("index.js", StrL("export const version = 2;")) &&
             fixture.Commit("second"));
    MaterializedDependency second;
    utassert(store.Materialize(StrL("omarchy-ui"), manifest.dependencies[0],
                               &second, &error));
    text = ReadWhole(second.entry);
    utassert(StrEq(text, StrL("export const version = 2;")));
    StrFree(text);
    text = ReadWhole(first.entry);
    utassert(StrEq(text, StrL("export const version = 1;")));
    StrFree(text);

    // A cache may not silently change repository identity.
    Str mirrors = DepJoin(fixture.cache, StrL("mirrors"));
    Str key = GitDependencyRemoteKey(url);
    Str mirror = DepJoin(mirrors, fmt("%s.git", key));
    Str setUrl[5] = {StrL("remote"), StrL("set-url"), StrL("origin"),
                     StrL("/wrong/remote")};
    utassert(RunGitFixture(mirror, setUrl, 4));
    MaterializedDependency refused;
    utassert(!store.Materialize(StrL("omarchy-ui"), manifest.dependencies[0],
                                &refused, &error));
    utassert(StrContains(error, StrL("cache origin")));
    refused.Free();

    first.Free();
    second.Free();
    StrFree(error);
    StrFree(mirror);
    StrFree(mirrors);
    StrFree(key);
    StrFree(url);
    StrFree(body);
}

static void ATagDependencyStaysAtTheTaggedCommit() {
    if (!GitIsInstalled()) return;
    GitFixture fixture("tag");
    utassert(fixture.ok);
    utassert(fixture.Write("index.js", StrL("export const version = 1;")) &&
             fixture.Commit("tagged") && fixture.Tag("v1"));
    utassert(fixture.Write("index.js", StrL("export const version = 2;")) &&
             fixture.Commit("later"));
    Str url = fixture.Url();
    Str body = StrDup(fmt("{\"git\":\"%s\",\"tag\":\"v1\"}", url));
    PluginManifest manifest;
    ShellError manifestError = {};
    utassert(ParsesDependency(body, &manifest, &manifestError));
    ShellErrorClear(&manifestError);

    GitDependencyStore store(fixture.cache);
    MaterializedDependency package;
    Str error = {};
    utassert(store.Materialize(StrL("omarchy-ui"), manifest.dependencies[0],
                               &package, &error));
    Str text = ReadWhole(package.entry);
    utassert(StrEq(text, StrL("export const version = 1;")));
    StrFree(text);
    package.Free();
    StrFree(error);
    StrFree(url);
    StrFree(body);
}

static void PackageDependenciesReadPackageMainAndRefuseWhatEscapes() {
    if (!GitIsInstalled()) return;
    {
        GitFixture fixture("main");
        utassert(fixture.ok);
        utassert(fixture.Write("dist/public.js",
                               StrL("export const entry = 'package main';")) &&
                 fixture.Write("package.json",
                               StrL("{ \"main\": \"dist/public.js\" }")) &&
                 fixture.Commit("custom package entry"));
        Str url = fixture.Url();
        Str body = StrDup(fmt("\"file:///%s#main\"", url));
        PluginManifest manifest;
        ShellError manifestError = {};
        utassert(ParsesDependency(body, &manifest, &manifestError));
        ShellErrorClear(&manifestError);
        GitDependencyStore store(fixture.cache);
        MaterializedDependency package;
        Str error = {};
        utassert(store.Materialize(StrL("omarchy-ui"), manifest.dependencies[0],
                                   &package, &error));
        Str text = ReadWhole(package.entry);
        utassert(StrEq(text, StrL("export const entry = 'package main';")));
        StrFree(text);
        package.Free();
        StrFree(error);
        StrFree(url);
        StrFree(body);
    }

    struct MainCase {
        const char* packageJson;
        const char* expected;
    };
    static const MainCase cases[] = {
        {"{ not JSON", "valid JSON"},
        {"{ \"main\": 42 }", "string"},
        {"{ \"main\": null }", "string"},
        {"{ \"main\": \"../private.js\" }", "inside"},
        {"{ \"main\": \"dist\" }", "file"},
    };
    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        GitFixture fixture("mainbad");
        utassert(fixture.ok);
        utassert(
            fixture.Write("index.js", StrL("export const executed = true;")) &&
            fixture
                .Write("dist/nested.js", StrL("export const nested = true;")) &&
            fixture.Write("package.json", Str(cases[i].packageJson)) &&
            fixture.Commit("invalid package manifest"));
        Str url = fixture.Url();
        Str body = StrDup(fmt("\"file:///%s#main\"", url));
        PluginManifest manifest;
        ShellError manifestError = {};
        utassert(ParsesDependency(body, &manifest, &manifestError));
        ShellErrorClear(&manifestError);
        GitDependencyStore store(fixture.cache);
        MaterializedDependency package;
        Str error = {};
        utassert(!store.Materialize(
            StrL("omarchy-ui"), manifest.dependencies[0], &package, &error));
        utassert(StrContains(error, Str(cases[i].expected)));
        package.Free();
        StrFree(error);
        StrFree(url);
        StrFree(body);
    }
}

// --- module resolution -------------------------------------------------

static void LoadingAnApplicationFetchesAndResolvesItsGitDependencies() {
    if (!GitIsInstalled()) return;
    GitFixture fixture("module");
    utassert(fixture.ok);
    utassert(fixture.Write("dist/public.js",
                           StrL("export { label } from './theme.js';")) &&
             fixture.Write("dist/theme.js",
                           StrL("export const label = 'downloaded'; "
                                "export const tone = 'dark';")) &&
             fixture.Write("package.json",
                           StrL("{ \"main\": \"dist/public.js\" }")) &&
             fixture.Commit("dependency"));
    Str url = fixture.Url();
    Str application = DepJoin(fixture.root, StrL("application"));
    utassert(DependencyMakeDirectories(application, nullptr));
    Str manifestPath = DepJoin(application, Str(kShellManifestFile));
    utassert(DepWrite(manifestPath,
                      fmt("{\"id\":\"com.example.fetch\",\"name\":\"Fetch\","
                          "\"entry\":\"main.js\",\"dependencies\":"
                          "{\"omarchy-ui\":\"file:///%s#main\"}}",
                          url)));
    Str entryPath = DepJoin(application, StrL("main.js"));
    utassert(DepWrite(
        entryPath,
        StrL("import { View, div } from 'gpui';\n"
             "import { label } from 'omarchy-ui';\n"
             "import { tone } from 'omarchy-ui/dist/theme.js';\n"
             "export default class Main extends View {\n"
             "  render(cx) { return div().child(`${label}:${tone}`); }\n"
             "}\n")));

    App app;
    Window window;
    window.app = &app;
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    utassert(runtime != nullptr);
    if (runtime) runtime->SetDependencyCacheRoot(fixture.cache);
    ViewType* type =
        runtime ? runtime->LoadApp(application, StrL("main.js"), &error)
                : nullptr;
    utassert(type != nullptr && !error.IsSet());
    ViewObject* object =
        type ? runtime->Instantiate(type, &window, &app, nullptr, &error)
             : nullptr;
    Arena* output = ArenaNew();
    Str rendered = object ? runtime->RenderToSpec(output, object, &window, &app,
                                                  {}, nullptr, &error)
                          : Str{};
    utassert(!error.IsSet() && StrContains(rendered, StrL("downloaded:dark")));
    ViewObjectRelease(object);
    ViewTypeRelease(type);
    ArenaDelete(output);

    // A dependency's relative import may not leave its own checkout, and the
    // application's own modules stay inside the application root.
    utassert(DepWrite(entryPath, StrL("import 'omarchy-ui/../../escape.js';\n"
                                      "export default class Main {}\n")));
    ShellErrorClear(&error);
    ViewType* refused =
        runtime ? runtime->LoadApp(application, StrL("main.js"), &error)
                : nullptr;
    utassert(refused == nullptr && error.IsSet());
    ShellErrorClear(&error);

    if (runtime) runtime->Release();
    AppGlobalClear(&app);
    StrFree(manifestPath);
    StrFree(entryPath);
    StrFree(application);
    StrFree(url);
}

// A reload inherits the checkouts the running application materialized. The
// watcher only scans .js and .mjs, so a reload cannot follow a manifest
// change, and re-materializing meant a `git fetch` per dependency on the UI
// thread every time a source file was saved.
//
// What makes this a test rather than an assertion about intent: the remote is
// deleted before the reload. A reload that still fetched would fail, because
// the URL it was resolved from no longer exists.
static void ReloadingAnApplicationReusesItsMaterializedDependencies() {
    if (!GitIsInstalled()) return;
    GitFixture fixture("reload");
    utassert(fixture.ok);
    utassert(fixture.Write("dist/public.js",
                           StrL("export const label = 'first';")) &&
             fixture.Write("package.json",
                           StrL("{ \"main\": \"dist/public.js\" }")) &&
             fixture.Commit("dependency"));
    Str url = fixture.Url();
    Str application = DepJoin(fixture.root, StrL("application"));
    utassert(DependencyMakeDirectories(application, nullptr));
    Str manifestPath = DepJoin(application, Str(kShellManifestFile));
    utassert(DepWrite(manifestPath,
                      fmt("{\"id\":\"com.example.reload\",\"name\":\"Reload\","
                          "\"entry\":\"main.js\",\"dependencies\":"
                          "{\"omarchy-ui\":\"file:///%s#main\"}}",
                          url)));
    Str entryPath = DepJoin(application, StrL("main.js"));
    utassert(DepWrite(
        entryPath, StrL("import { View, div } from 'gpui';\n"
                        "import { label } from 'omarchy-ui';\n"
                        "export default class Main extends View {\n"
                        "  render(cx) { return div().child(`one:${label}`); }\n"
                        "}\n")));

    App app;
    Window window;
    window.app = &app;
    VecAppend(app.windows, &window);
    component::Init(&app);
    ShellError error = {};
    ShellRuntime* runtime = ShellRuntime::New(&app, &error);
    utassert(runtime != nullptr);
    if (runtime) runtime->SetDependencyCacheRoot(fixture.cache);
    ViewType* type =
        runtime ? runtime->LoadApp(application, StrL("main.js"), &error)
                : nullptr;
    utassert(type != nullptr && !error.IsSet());
    Entity<ScriptView> view =
        type ? ScriptView::New(&app, runtime, type) : Entity<ScriptView>{};
    utassert(view.IsValid());
    ViewTypeRelease(type);

    Arena* frame = ArenaNew();
    window.frameArena = frame;
    El* root = EntityRender(&app, &window, frame, view.id);
    utassert(root != nullptr);

    // The remote goes away, and the source changes. A reload that re-fetched
    // would have nothing to fetch from.
    DependencyRemoveTree(fixture.remote);
    utassert(DepWrite(
        entryPath, StrL("import { View, div } from 'gpui';\n"
                        "import { label } from 'omarchy-ui';\n"
                        "export default class Main extends View {\n"
                        "  render(cx) { return div().child(`two:${label}`); }\n"
                        "}\n")));

    ScriptView* self = view.Get(&app);
    utassert(self != nullptr);
    Ctx cx = {&app, &window, frame, view.id};
    ShellErrorClear(&error);
    utassert(
        ScriptView::Reload(self, &cx, application, StrL("main.js"), &error));
    utassert(!error.IsSet());

    // The new source is live and the dependency still resolves, from the
    // checkout the first load left behind.
    Arena* output = ArenaNew();
    ScriptView* reloaded = view.Get(&app);
    ViewObject* object = reloaded ? reloaded->object : nullptr;
    Str rendered = object ? runtime->RenderToSpec(output, object, &window, &app,
                                                  {}, nullptr, &error)
                          : Str{};
    utassert(!error.IsSet() && StrContains(rendered, StrL("two:first")));
    ArenaDelete(output);

    EntityDrop(&app, view.id);
    app.windows.len = 0;
    ArenaDelete(frame);
    if (runtime) runtime->Release();
    AppGlobalClear(&app);
    ShellErrorClear(&error);
    StrFree(manifestPath);
    StrFree(entryPath);
    StrFree(application);
    StrFree(url);
}

// --- the fetch method --------------------------------------------------

static void FetchNamesAMethodAndRefusesWhatIsNotOne() {
    // `parse_method` is a token check, not a list.
    utassert(FetchIsHttpMethod(StrL("GET")) && FetchIsHttpMethod(StrL("PUT")) &&
             FetchIsHttpMethod(StrL("PATCH")) &&
             FetchIsHttpMethod(StrL("X-CUSTOM_1!")));
    utassert(!FetchIsHttpMethod(StrL("")) && !FetchIsHttpMethod({}) &&
             !FetchIsHttpMethod(StrL("GET ")) &&
             !FetchIsHttpMethod(StrL("a method")) &&
             !FetchIsHttpMethod(StrL("\"GET\"")) &&
             !FetchIsHttpMethod(StrL("GET/1")));
}

void TestShellDependencies() {
    TestSuite("shell_dependencies");
    DependencyCacheLivesInTheShellCache();
    ManifestsDescribeGitDependenciesBeforeAnythingRuns();
    EditorLinksPointAtTheCheckoutsTheRuntimeWillExecute();
    FetchNamesAMethodAndRefusesWhatIsNotOne();
    GitDependenciesResolveRefreshAndStayInsideTheirCheckout();
    ATagDependencyStaysAtTheTaggedCommit();
    PackageDependenciesReadPackageMainAndRefuseWhatEscapes();
    LoadingAnApplicationFetchesAndResolvesItsGitDependencies();
    ReloadingAnApplicationReusesItsMaterializedDependencies();
}
