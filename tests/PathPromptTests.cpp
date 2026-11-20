/* The shared halves of the path prompts: the extensions a file type's
   patterns name, the default extension a save prompt adds, and the macOS
   Sequoia workaround of gpui_macos platform.rs prompt_for_new_path. */

#include "Test.h"

static void PatternExts() {
    Vec<Str> exts;
    utassert(PathPatternExts(StrL("*.pdf;*.xps"), &exts));
    utassert(len(exts) == 2);
    utassert(StrEq(exts[0], "pdf") && StrEq(exts[1], "xps"));

    // spaces around a pattern, a whole name, an empty pattern
    VecReset(exts);
    utassert(PathPatternExts(StrL(" *.txt ; read.me;;*.fb2.zip"), &exts));
    utassert(len(exts) == 2);
    utassert(StrEq(exts[0], "txt") && StrEq(exts[1], "fb2.zip"));

    // a pattern that takes any file takes back what came before it
    VecReset(exts);
    VecAppend(exts, StrL("kept"));
    utassert(!PathPatternExts(StrL("*.png;*.*"), &exts));
    utassert(len(exts) == 1 && StrEq(exts[0], "kept"));
    utassert(!PathPatternExts(StrL("*"), &exts));
    utassert(len(exts) == 1);

    VecReset(exts);
    utassert(PathPatternExts(StrL(""), &exts));
    utassert(len(exts) == 0);
}

static void DefaultExt() {
    utassert(
        StrEq(PathWithDefaultExtTemp(StrL("/d/a"), StrL("pdf")), "/d/a.pdf"));
    utassert(
        StrEq(PathWithDefaultExtTemp(StrL("/d/a"), StrL(".pdf")), "/d/a.pdf"));
    utassert(StrEq(PathWithDefaultExtTemp(StrL("/d/a.txt"), StrL("pdf")),
                   "/d/a.txt"));
    // a dot in the folder is not the file's extension
    utassert(StrEq(PathWithDefaultExtTemp(StrL("/d.x/a"), StrL("pdf")),
                   "/d.x/a.pdf"));
    utassert(StrEq(PathWithDefaultExtTemp(StrL("C:\\d.x\\a"), StrL("pdf")),
                   "C:\\d.x\\a.pdf"));
    // a hidden file's leading dot is its name
    utassert(StrEq(PathWithDefaultExtTemp(StrL("/d/.notes"), StrL("txt")),
                   "/d/.notes.txt"));
    utassert(StrEq(PathWithDefaultExtTemp(StrL("/d/a"), StrL("")), "/d/a"));
    // a folder, no name: nothing to add to
    utassert(StrEq(PathWithDefaultExtTemp(StrL("/d/"), StrL("pdf")), "/d/"));
}

// The cases zed#16969 names, and names that keep their extensions.
static void RepeatedExt() {
    utassert(StrEq(PathDropRepeatedExtTemp(StrL("/d/a.sql.s")), "/d/a.sql"));
    utassert(
        StrEq(PathDropRepeatedExtTemp(StrL("/d/a.txtx.txt")), "/d/a.txtx"));
    utassert(
        StrEq(PathDropRepeatedExtTemp(StrL("/d/a.tar.gz")), "/d/a.tar.gz"));
    utassert(StrEq(PathDropRepeatedExtTemp(StrL("/d/a.sql")), "/d/a.sql"));
    utassert(StrEq(PathDropRepeatedExtTemp(StrL("/d/a.b.c.b")), "/d/a.b.c.b"));
    utassert(StrEq(PathDropRepeatedExtTemp(StrL("/d.sql.s/a")), "/d.sql.s/a"));
}

void TestPathPrompts() {
    TestSuite("PathPrompts");
    PatternExts();
    DefaultExt();
    RepeatedExt();
}
