/* crates/component/src/diff — parser, conflicts, inline runs, and the
   viewer's row projection. Hit-tested text selection stays a known gap
   (port-status.md). */

#include "Test.h"

using namespace gpui;
using namespace gpui::component;

static void FreeFiles(Vec<DiffFile*>& files) {
    for (int i = 0; i < len(files); i++) delete files[i];
    VecClear(files);
}

static bool Parse(const char* patch, Vec<DiffFile*>* out) {
    DiffParseError error;
    bool ok = DiffFile::Parse(Str(patch), out, &error);
    if (!ok) FreeFiles(*out);
    return ok;
}

static bool ParseErr(const char* patch) {
    Vec<DiffFile*> files;
    DiffParseError error;
    bool ok = DiffFile::Parse(Str(patch), &files, &error);
    FreeFiles(files);
    return !ok && error.Line() > 0 && len(error.Message()) > 0;
}

static bool Same(Str a, const char* b) {
    return StrEq(a, b);
}

static int CodeRows(const DiffState* state, int file) {
    int n = 0;
    for (int i = 0; i < state->RowCount(); i++) {
        const DiffRow& row = state->Rows()[i];
        if (row.kind == DiffRowKind::Code && row.file == file) n++;
    }
    return n;
}

static int FoldCount(const DiffState* state) {
    int n = 0;
    for (int i = 0; i < state->RowCount(); i++)
        if (state->Rows()[i].kind == DiffRowKind::Fold) n++;
    return n;
}

static bool FoldAt(const DiffState* state, int n, int start, int end) {
    int seen = 0;
    for (int i = 0; i < state->RowCount(); i++) {
        const DiffRow& row = state->Rows()[i];
        if (row.kind != DiffRowKind::Fold) continue;
        if (seen == n) return row.pairsStart == start && row.pairsEnd == end;
        seen++;
    }
    return false;
}

struct DiffFix {
    App* app = nullptr;
    Arena* a = nullptr;
    Entity<DiffState> entity;
    DiffState* state = nullptr;
    Ctx cx = {};
    DiffFix() {
        app = TestAppNew();
        a = ArenaNew();
        entity = EntityNew<DiffState>(app);
        state = entity.Get(app);
        state->Bind(app, entity);
        cx.app = app;
        cx.a = a;
        cx.self = entity.id;
    }
    ~DiffFix() {
        TestAppFree(app);
        ArenaDelete(a);
    }
    bool Load(const char* patch) { return LoadStr(Str(patch)); }
    bool LoadStr(Str patch) {
        Vec<DiffFile*> files;
        DiffParseError error;
        if (!DiffFile::Parse(patch, &files, &error)) {
            FreeFiles(files);
            return false;
        }
        state->SetFiles(files.els, len(files), &cx);
        return true;
    }
};

static void TestParser() {
    TestSuite("diff parser");
    Vec<DiffFile*> files;
    const char* sparse =
        "--- a/src/a.rs\n+++ b/src/a.rs\n@@ -10,2 +20,2 @@ fn first\n "
        "keep\n-old\n+new\n"
        "@@ -100 +110 @@\n-last\n+next\n";
    utassert(Parse(sparse, &files) && len(files) == 1);
    DiffFile* doc = files[0];
    utassert(doc->Additions() == 2 && doc->Deletions() == 2);
    utassert(doc->LinesCount(DiffSide::Original) == 3);
    utassert(doc->LineNumber(DiffSide::Modified, 1) == 21);
    utassert(doc->LineIndex(DiffSide::Original, 100) == 2);
    utassert(doc->LineIndex(DiffSide::Original, 50) < 0);
    Arena* a = ArenaNew();
    utassert(Same(doc->TextForLines(a, DiffSide::Original, 1, 200),
                  "keep\nold\nlast\n"));
    utassert(doc->Hunks()[1].pairsStart == 2);
    utassert(doc->LineNumberDigits() == 3);
    ArenaDelete(a);
    FreeFiles(files);

    const char* multi =
        "diff --git a/new b/new\nnew file mode 100644\n--- /dev/null\n+++ "
        "b/new\n@@ -0,0 +1 @@\n+added\n"
        "diff --git a/old b/old\ndeleted file mode 100644\n--- a/old\n+++ "
        "/dev/null\n@@ -1 +0,0 @@\n-deleted\n"
        "diff --git a/from b/to\nsimilarity index 100%\nrename from "
        "from\nrename to to\n"
        "diff --git a/pic b/pic\nBinary files a/pic and b/pic differ\n";
    utassert(Parse(multi, &files) && len(files) == 4);
    Str path;
    utassert(!files[0]->OriginalPath(&path) && Same(files[0]->Path(), "new"));
    utassert(!files[1]->ModifiedPath(&path) && Same(files[1]->Path(), "old"));
    utassert(files[2]->OriginalPath(&path) && Same(path, "from"));
    utassert(files[2]->ModifiedPath(&path) && Same(path, "to"));
    utassert(files[2]->HasChanges());
    utassert(files[3]->IsBinary() &&
             files[3]->LinesCount(DiffSide::Original) == 0);
    utassert(files[0]->Status() == DiffFileStatus::Added);
    utassert(files[1]->Status() == DiffFileStatus::Deleted);
    utassert(files[2]->Status() == DiffFileStatus::Renamed);
    FreeFiles(files);

    utassert(
        Parse("--- a/a\n+++ b/a\n@@ -1 +1 @@\n-old\n\\ No newline at end of "
              "file\n+new\n"
              "\\ No newline at end of file\n",
              &files));
    utassert(Same(files[0]->Source(DiffSide::Original), "old"));
    utassert(Same(files[0]->Source(DiffSide::Modified), "new"));
    FreeFiles(files);
    utassert(Parse("--- a/a\n+++ b/a\n@@ -1 +1 @@\n-old\r\n+new\r\n", &files));
    utassert(Same(files[0]->Source(DiffSide::Original), "old\r\n"));
    FreeFiles(files);
    utassert(
        Parse("--- a/a\r\n+++ b/a\r\n@@ -1 +1 @@\r\n-old\r\n+new\r\n", &files));
    utassert(Same(files[0]->Source(DiffSide::Original), "old\n"));
    FreeFiles(files);

    utassert(
        Parse("diff --git \"a/\\344\\270\\255.rs\" \"b/\\344\\270\\255.rs\"\n"
              "--- \"a/\\344\\270\\255.rs\"\n+++ \"b/\\344\\270\\255.rs\"\n"
              "@@ -1,2 +1,2 @@\n --- source text\n-old\n+new\n",
              &files));
    utassert(files[0]->OriginalPath(&path) && Same(path, "中.rs"));
    utassert(StrStartsWith(files[0]->Source(DiffSide::Original),
                           "--- source text\n"));
    FreeFiles(files);

    const char* mail =
        "From 1a2b Mon Sep 17 00:00:00 2001\nSubject: [PATCH 1/2] "
        "Change\n\n---\n a.rs | 2 +-\n"
        " 1 file changed\n\ndiff --git a/a.rs b/a.rs\n--- a/a.rs\n+++ "
        "b/a.rs\n@@ -1 +1 @@\n-old\n+new\n"
        "-- \n2.47.0\n\nFrom 3c4d Mon Sep 17 00:00:00 2001\nSubject: [PATCH "
        "2/2] Again\n\n---\n"
        " b.rs | 2 +-\n\ndiff --git a/b.rs b/b.rs\n--- a/b.rs\n+++ b/b.rs\n@@ "
        "-1 +1 @@\n-before\n+after\n"
        "--\n2.47.0\n";
    utassert(Parse(mail, &files) && len(files) == 2);
    utassert(Same(files[0]->Path(), "a.rs") && Same(files[1]->Path(), "b.rs"));
    utassert(Same(files[1]->Source(DiffSide::Modified), "after\n"));
    FreeFiles(files);

    const char* headers[][3] = {
        {"a/src/a.rs b/src/a.rs", "a/src/a.rs", "b/src/a.rs"},
        {"i/src/a.rs w/src/a.rs", "i/src/a.rs", "w/src/a.rs"},
        {"c/src/a.rs i/src/a.rs", "c/src/a.rs", "i/src/a.rs"},
        {"src/a.rs src/a.rs", "src/a.rs", "src/a.rs"},
        {"a/dir with space/a.rs b/dir with space/a.rs", "a/dir with space/a.rs",
         "b/dir with space/a.rs"},
    };
    for (int i = 0; i < 5; i++) {
        TempStr patch =
            fmt("diff --git %s\nindex 1..2 100644\n--- %s\n+++ %s\n@@ -1 +1 "
                "@@\n-old\n+new\n",
                Str(headers[i][0]), Str(headers[i][1]), Str(headers[i][2]));
        utassert(Parse(patch.s, &files));
        const char* expected = i == 4 ? "dir with space/a.rs" : "src/a.rs";
        utassert(files[0]->OriginalPath(&path) && Same(path, expected));
        utassert(files[0]->ModifiedPath(&path) && Same(path, expected));
        FreeFiles(files);
    }
    utassert(Parse(
        "diff --git src/run.sh src/run.sh\nold mode 100644\nnew mode 100755\n",
        &files));
    utassert(Same(files[0]->Path(), "src/run.sh"));
    FreeFiles(files);

    utassert(Parse(
        "--- a/notes.txt\n+++ a/notes.txt\n@@ -1 +1 @@\n-old\n+new\n", &files));
    utassert(Same(files[0]->Path(), "a/notes.txt"));
    FreeFiles(files);
    utassert(Parse(
        "--- a/notes.txt\n+++ b/notes.txt\n@@ -1 +1 @@\n-old\n+new\n", &files));
    utassert(Same(files[0]->Path(), "notes.txt"));
    FreeFiles(files);

    utassert(Parse("--- a/a\n+++ b/a\n@@ -1,3 +1,3 @@\n first\n\n-old\n+new\n",
                   &files));
    utassert(Same(files[0]->Source(DiffSide::Original), "first\n\nold\n"));
    utassert(Same(files[0]->Source(DiffSide::Modified), "first\n\nnew\n"));
    FreeFiles(files);
    utassert(
        Parse("--- a/a\n+++ b/a\n@@ -1 +1 @@\n-old\n+new\n\n--- a/b\n+++ "
              "b/b\n@@ -1 +1 @@\n-x\n+y\n",
              &files));
    utassert(len(files) == 2);
    FreeFiles(files);

    const char* bad[] = {
        "--- a/a\n+++ b/a\n@@ -1,2 +1,2 @@\n-old\n+new\n",
        "--- a/a\n+++ b/a\n@@ -1 +1 @@\n-old\n+new\n+extra\n",
        "--- a/a\n+++ b/a\n@@ -1 +1 @@\n same\n@@ -1 +2 @@\n again\n",
        "--- a/a\n+++ b/a\n@@ -18446744073709551615 +1 @@\n-old\n+new\n",
        "diff --cc a.rs\n@@@ -1 -1 +1 @@@\n",
        "--- /dev/null\n+++ b/a\n@@ -1 +1 @@\n-old\n+new\n",
    };
    for (int i = 0; i < 6; i++) utassert(ParseErr(bad[i]));
    utassert(Parse("", &files) && len(files) == 0);
    FreeFiles(files);
}

static void TestConflictsAndLanguage() {
    TestSuite("diff conflicts");
    DiffFile* file =
        DiffFile::Unchanged(StrL("src/lib.rs"), StrL("one\r\ntwo\nlast"));
    utassert(file->Status() == DiffFileStatus::Unchanged &&
             !file->HasChanges());
    utassert(file->LinesCount(DiffSide::Modified) == 3);
    utassert(file->LinesCount(DiffSide::Original) == 0);
    Arena* a = ArenaNew();
    utassert(Same(file->TextForLines(a, DiffSide::Modified, 1, 3),
                  "one\r\ntwo\nlast"));
    delete file;
    file = DiffFile::Unchanged(StrL("empty"), StrL(""));
    utassert(file->PairCount() == 0);
    delete file;

    const char* merge =
        "fn retry() {\n<<<<<<< HEAD\n    3\n=======\n    5\n>>>>>>> "
        "feature\n}\n";
    DiffParseError error;
    file = DiffFile::ParseConflicts(StrL("src/retry.rs"), Str(merge), &error);
    utassert(file && file->Status() == DiffFileStatus::Conflicted);
    utassert(file->Lines(DiffSide::Modified)[0].lineNumber == 1);
    utassert(file->Lines(DiffSide::Modified)[1].lineNumber == 3);
    utassert(file->Lines(DiffSide::Modified)[2].lineNumber == 5);
    utassert(file->Lines(DiffSide::Modified)[3].lineNumber == 7);
    int start = 0;
    int end = 0;
    utassert(file->Conflicts()[0]
                 .Part(DiffConflictPart::Current, &start, &end) &&
             start == 1 && end == 2);
    utassert(file->Conflicts()[0]
                 .Part(DiffConflictPart::Incoming, &start, &end) &&
             start == 2 && end == 3);
    utassert(
        Same(file->Conflicts()[0].Label(DiffConflictPart::Current), "HEAD"));
    utassert(Same(file->Conflicts()[0].Label(DiffConflictPart::Incoming),
                  "feature"));
    utassert(file->Conflicts()[0].PartOf(2) == DiffConflictPart::Incoming);
    utassert(file->Pairs()[1].changed && !file->Pairs()[0].changed);
    file->Conflicts()[0].Lines(&start, &end);
    utassert(start == 2 && end == 7);
    file->Conflicts()[0].CurrentLines(&start, &end);
    utassert(start == 3 && end == 4);
    file->Conflicts()[0].IncomingLines(&start, &end);
    utassert(start == 5 && end == 6);
    utassert(!file->Conflicts()[0].BaseLines(&start, &end));
    utassert(!file->Conflicts()[0].BaseLabel(nullptr));
    utassert(Same(file->Conflicts()[0].CurrentLabel(), "HEAD"));
    utassert(Same(file->Conflicts()[0].IncomingLabel(), "feature"));
    delete file;

    file = DiffFile::ParseConflicts(
        StrL("a.txt"),
        StrL("<<<<<<< ours\na\n||||||| base\nb\n=======\nc\n>>>>>>> theirs\n"),
        &error);
    utassert(file);
    utassert(file->Conflicts()[0].Part(DiffConflictPart::Base, &start, &end) &&
             start == 1 && end == 2);
    utassert(Same(file->Conflicts()[0].Label(DiffConflictPart::Base), "base"));
    file->Conflicts()[0].Lines(&start, &end);
    utassert(start == 1 && end == 8);
    file->Conflicts()[0].CurrentLines(&start, &end);
    utassert(start == 2 && end == 3);
    utassert(file->Conflicts()[0].BaseLines(&start, &end) && start == 4 &&
             end == 5);
    file->Conflicts()[0].IncomingLines(&start, &end);
    utassert(start == 6 && end == 7);
    Str baseLabel;
    utassert(file->Conflicts()[0].BaseLabel(&baseLabel) &&
             Same(baseLabel, "base"));
    delete file;
    file = DiffFile::ParseConflicts(
        StrL("empty.txt"),
        StrL("<<<<<<< ours\n||||||| base\n=======\n>>>>>>> theirs\n"), &error);
    utassert(file);
    file->Conflicts()[0].Lines(&start, &end);
    utassert(start == 1 && end == 5);
    file->Conflicts()[0].CurrentLines(&start, &end);
    utassert(start == 2 && end == 2);
    utassert(file->Conflicts()[0].BaseLines(&start, &end) && start == 3 &&
             end == 3);
    file->Conflicts()[0].IncomingLines(&start, &end);
    utassert(start == 4 && end == 4);
    delete file;
    utassert(!DiffFile::ParseConflicts(StrL("a.txt"), StrL("<<<<<<< a\nx\n"),
                                       &error) &&
             error.Line() > 0);
    utassert(!DiffFile::ParseConflicts(StrL("a.txt"),
                                       StrL("<<<<<<< a\n<<<<<<< b\n"), &error));
    utassert(!DiffFile::ParseConflicts(StrL("a.txt"), StrL(">>>>>>> stray\n"),
                                       &error));
    file = DiffFile::ParseConflicts(StrL("a.md"), StrL("title\n=======\n"),
                                    &error);
    utassert(file);
    delete file;

    file = DiffFile::Unchanged(StrL("src/main.RS"), StrL("a"));
    utassert(Same(file->Language(), "rust"));
    delete file;
    Vec<DiffFile*> files;
    utassert(Parse(
        "--- a/src/main.RS\n+++ b/src/main.RS\n@@ -1 +1 @@\n-a\n+b\n", &files));
    utassert(Same(files[0]->Language(), "rust"));
    utassert(Same(files[0]->WithLanguage(StrL("text"))->Language(), "text"));
    FreeFiles(files);
    utassert(Parse("--- a/notes\n+++ b/notes\n@@ -1 +1 @@\n-a\n+b\n", &files));
    utassert(Same(files[0]->Language(), "text"));
    utassert(Same(files[0]->WithLanguage(StrL("rust"))->Language(), "rust"));
    FreeFiles(files);
    utassert(
        Parse("--- a/src/components/App.jsx\n+++ b/src/components/App.jsx\n@@ "
              "-1 +1 @@\n-a\n+b\n",
              &files));
    utassert(Same(files[0]->Language(), "javascript"));
    FreeFiles(files);
    utassert(Parse(
        "--- a/CMakeLists.txt\n+++ b/CMakeLists.txt\n@@ -1 +1 @@\n-a\n+b\n",
        &files));
    utassert(Same(files[0]->Language(), "cmake"));
    FreeFiles(files);
    ArenaDelete(a);
}

static bool RunsEq(const Vec<DiffRun>& runs, int n, const int* expect) {
    if (len(runs) != n) return false;
    for (int i = 0; i < n; i++) {
        if (runs[i].start != expect[i * 2] || runs[i].end != expect[i * 2 + 1])
            return false;
    }
    return true;
}

static void TestInlineAndRows() {
    TestSuite("diff rows");
    Vec<DiffRun> oldRuns;
    Vec<DiffRun> newRuns;
    DiffChangedRuns(StrL("let total = price * count;"),
                    StrL("let total = cost * quantity;"), DiffInlineUnit::Word,
                    &oldRuns, &newRuns);
    const int oldExpect[] = {12, 17, 20, 25};
    const int newExpect[] = {12, 16, 19, 27};
    utassert(RunsEq(oldRuns, 2, oldExpect));
    utassert(RunsEq(newRuns, 2, newExpect));
    VecClear(oldRuns);
    VecClear(newRuns);
    DiffChangedRuns(StrL("a b c"), StrL("a x y c"), DiffInlineUnit::Word,
                    &oldRuns, &newRuns);
    utassert(len(newRuns) == 1 && newRuns[0].start == 2 && newRuns[0].end == 5);
    utassert(len(oldRuns) == 0 || (len(oldRuns) == 1 && oldRuns[0].start == 2 &&
                                   oldRuns[0].end == 3));
    VecClear(oldRuns);
    VecClear(newRuns);
    DiffChangedRuns(StrL("colour"), StrL("color"), DiffInlineUnit::Character,
                    &oldRuns, &newRuns);
    utassert(len(oldRuns) == 1 && oldRuns[0].start == 4 &&
             oldRuns[0].end == 5 && len(newRuns) == 0);
    VecClear(oldRuns);
    VecClear(newRuns);
    DiffChangedRuns(StrL("alpha"), StrL("zzzzzzzzz"), DiffInlineUnit::Word,
                    &oldRuns, &newRuns);
    utassert(len(oldRuns) == 0 && len(newRuns) == 0);

    Vec<DiffChunk> chunks;
    DiffDisplayChunks(StrL(""), &chunks);
    utassert(len(chunks) == 0);
    DiffDisplayChunks(StrL("é👩‍💻中"), &chunks);
    utassert(len(chunks) == 1 && chunks[0].start == 0 &&
             chunks[0].end == len(StrL("é👩‍💻中")));

    const char* status =
        "diff --git a/new b/new\nnew file mode 100644\n--- /dev/null\n+++ "
        "b/new\n@@ -0,0 +1 @@\n+a\n"
        "diff --git a/old b/old\ndeleted file mode 100644\n--- a/old\n+++ "
        "/dev/null\n@@ -1 +0,0 @@\n-a\n"
        "diff --git a/from b/to\nsimilarity index 100%\nrename from "
        "from\nrename to to\n"
        "diff --git a/src b/copy\nsimilarity index 100%\ncopy from src\ncopy "
        "to copy\n"
        "diff --git a/edit b/edit\n--- a/edit\n+++ b/edit\n@@ -5,2 +5,2 @@\n "
        "x\n-y\n+z\n@@ -20 +20 @@\n-p\n+q\n";
    Vec<DiffFile*> files;
    utassert(Parse(status, &files) && len(files) == 5);
    utassert(files[3]->Status() == DiffFileStatus::Copied);
    utassert(files[4]->Hunks()[0].HiddenLinesBefore(nullptr) == 4);
    utassert(files[4]->Hunks()[1].HiddenLinesBefore(&files[4]->Hunks()[0]) ==
             13);
    utassert(files[0]->Hunks()[0].HiddenLinesBefore(nullptr) == 0);
    FreeFiles(files);

    DiffFix fix;
    const char* patch =
        "diff --git a/first.txt b/first.txt\n--- a/first.txt\n+++ "
        "b/first.txt\n@@ -1,3 +1,3 @@\n"
        " one\n-two\n+TWO\n three\ndiff --git a/second.txt b/second.txt\n--- "
        "a/second.txt\n+++ b/second.txt\n"
        "@@ -10 +10 @@\n-ten\n+TEN\n";
    utassert(fix.Load(patch));
    utassert(CodeRows(fix.state, 0) > 0);
    fix.state->SetFileCollapsed(StrL("first.txt"), true, &fix.cx);
    utassert(fix.state->IsFileCollapsed(StrL("first.txt")));
    utassert(CodeRows(fix.state, 0) == 0);
    utassert(fix.state->Rows()[0].kind == DiffRowKind::File);
    utassert(fix.state->Rows()[1].kind == DiffRowKind::File &&
             fix.state->Rows()[1].file == 1);
    int before = len(fix.state->emitted);
    fix.state->ToggleFileCollapsed(1, &fix.cx);
    utassert(fix.state->IsFileCollapsed(StrL("second.txt")));
    utassert(len(fix.state->emitted) == before + 1);
    utassert(fix.state->emitted[before].kind == DiffEventKind::FileCollapsed);
    fix.state->ScrollToLine(
        DiffLinePosition::New(StrL("first.txt"), DiffSide::Modified, 2),
        &fix.cx);
    utassert(!fix.state->IsFileCollapsed(StrL("first.txt")));
    fix.state->ScrollToFile(StrL("first.txt"), &fix.cx);
    utassert(fix.state->ScrollItem() >= 0);

    DiffLineRange cross =
        DiffLineRange::New(StrL("first.txt"), DiffSide::Original, 1, 2)
            .WithEndSide(DiffSide::Modified);
    fix.state->SetSelectedLines(true, cross, &fix.cx);
    utassert(Same(fix.state->SelectedText(fix.a), "one\ntwo\nTWO\n"));
    fix.state->SetSelectedLines(
        true,
        DiffLineRange::New(StrL("first.txt"), DiffSide::Original, 2, 99)
            .WithEndSide(DiffSide::Modified),
        &fix.cx);
    utassert(fix.state->SelectedLines().start == 2);
    utassert(Same(fix.state->SelectedText(fix.a), "two\nTWO\nthree\n") ||
             Same(fix.state->SelectedText(fix.a), "two\nTWO\nthree"));

    StrBuilder body;
    body.Append(StrL("--- a/long.txt\n+++ b/long.txt\n@@ -1,60 +1,60 @@\n"));
    for (int line = 1; line <= 60; line++) {
        if (line == 30 || line == 60)
            body.Append(fmt("-%s%d\n+%s%d\n", StrL("line "), line,
                            StrL("changed "), line));
        else
            body.Append(fmt(" %s%d\n", StrL("line "), line));
    }
    DiffFix folds;
    utassert(folds.LoadStr(Str(body.els, body.len)));
    folds.state->SetContextLines(true, 2, &folds.cx);
    folds.state->WithExpansionLines(5);
    utassert(FoldCount(folds.state) == 2);
    utassert(FoldAt(folds.state, 0, 0, 27));
    utassert(FoldAt(folds.state, 1, 32, 57));
    folds.state->ExpandFold(0, 32, 57, DiffFoldExpansion::Down, &folds.cx);
    utassert(FoldAt(folds.state, 1, 37, 57));
    folds.state->ExpandFold(0, 37, 57, DiffFoldExpansion::Up, &folds.cx);
    utassert(FoldAt(folds.state, 1, 37, 52));
    folds.state->ExpandFold(0, 0, 27, DiffFoldExpansion::All, &folds.cx);
    utassert(FoldCount(folds.state) == 1 && FoldAt(folds.state, 0, 37, 52));
    folds.state->CollapseUnchanged(&folds.cx);
    utassert(FoldAt(folds.state, 0, 0, 27) && FoldAt(folds.state, 1, 32, 57));

    const char* one =
        "--- a/a.txt\n+++ b/a.txt\n@@ -1 +1 @@\n-let total = price;\n+let "
        "total = cost;\n";
    DiffFix words;
    utassert(words.Load(one));
    words.state->EnsurePresentation();
    int count = 0;
    utassert(words.state->FileAt(0)->InlineAt(DiffSide::Modified, 0, &count));
    utassert(count == 1);
    words.state->WithInlineUnit(false, DiffInlineUnit::Word);
    words.state->EnsurePresentation();
    utassert(words.state->FileAt(0)->InlineCount(DiffSide::Modified, 0) == 0);
    words.state->SetSelectedLines(
        true, DiffLineRange::New(StrL("a.txt"), DiffSide::Modified, 1, 1),
        &words.cx);
    int events = len(words.state->emitted);
    int scroll = words.state->ScrollItem();
    words.state->SetInlineUnit(false, DiffInlineUnit::Word, &words.cx);
    words.state->SetInlineUnit(true, DiffInlineUnit::Character, &words.cx);
    utassert(words.state->HasInlineUnit() &&
             words.state->InlineUnit() == DiffInlineUnit::Character);
    utassert(words.state->FileAt(0)->InlineCount(DiffSide::Modified, 0) > 0);
    words.state->SetInlineMaxLineLength(5, &words.cx);
    utassert(words.state->FileAt(0)->InlineCount(DiffSide::Modified, 0) == 0);
    words.state->SetInlineMaxLineLength(2048, &words.cx);
    words.state->SetInlineUnit(true, DiffInlineUnit::Word, &words.cx);
    utassert(words.state->FileAt(0)->InlineCount(DiffSide::Modified, 0) == 1);
    words.state->SetSyntaxMaxLineLength(4096, &words.cx);
    words.state->SetExpansionLines(5, &words.cx);
    words.state->SetMinCollapsedLines(4, &words.cx);
    utassert(words.state->HasSelectedLines() && words.state->SelectedLines()
                                                        .start == 1);
    utassert(words.state->ScrollItem() == scroll);
    utassert(words.state->InlineUnit() == DiffInlineUnit::Word);
    utassert(words.state->InlineMaxLineLength() == 2048);
    utassert(words.state->SyntaxMaxLineLength() == 4096);
    utassert(words.state->ExpansionLines() == 5);
    utassert(words.state->MinCollapsedLines() == 4);
    utassert(len(words.state->emitted) == events);
    words.state->SetInlineUnit(false, DiffInlineUnit::Word, &words.cx);
    utassert(!words.state->HasInlineUnit());
    utassert(words.state->FileCount() == 1);

    StrBuilder notes;
    for (int line = 1; line <= 40; line++) notes.Append(fmt("line %d\n", line));
    DiffFile* unchanged =
        DiffFile::Unchanged(StrL("notes.txt"), Str(notes.els, notes.len));
    DiffFix plain;
    plain.state->WithMode(DiffMode::Split);
    plain.state->SetFiles(&unchanged, 1, &plain.cx);
    utassert(CodeRows(plain.state, 0) == 40);
    utassert(FoldCount(plain.state) == 0);
    plain.state->SetSelectedLines(
        true, DiffLineRange::New(StrL("notes.txt"), DiffSide::Modified, 2, 3),
        &plain.cx);
    utassert(Same(plain.state->SelectedText(plain.a), "line 2\nline 3\n"));

    const char* conflict =
        "start\n<<<<<<< HEAD\nours\n=======\ntheirs\n>>>>>>> feature\nmiddle\n"
        "<<<<<<< HEAD\na\n||||||| base\nb\n=======\nc\n>>>>>>> other\nend\n";
    DiffParseError error;
    DiffFile* merged =
        DiffFile::ParseConflicts(StrL("merge.txt"), Str(conflict), &error);
    utassert(merged);
    DiffFix merge;
    merge.state->WithMode(DiffMode::Split);
    merge.state->SetContextLines(false, 0, &merge.cx);
    merge.state->SetFiles(&merged, 1, &merge.cx);
    int heads = 0;
    for (int i = 0; i < merge.state->RowCount(); i++)
        if (merge.state->Rows()[i].kind == DiffRowKind::Conflict) heads++;
    utassert(heads == 5);
    utassert(CodeRows(merge.state, 0) == 8);
    Str resolved;
    utassert(!merge.state->ResolvedText(merge.a, StrL("merge.txt"), &resolved));
    merge.state->ResolveConflict(StrL("merge.txt"), 0, true,
                                 DiffConflictResolution::Incoming, &merge.cx);
    utassert(!merge.state->ResolvedText(merge.a, StrL("merge.txt"), &resolved));
    merge.state
        ->ChooseConflict(0, 1, true, DiffConflictResolution::Both, &merge.cx);
    utassert(merge.state->ResolvedText(merge.a, StrL("merge.txt"), &resolved));
    utassert(Same(resolved, "start\ntheirs\nmiddle\na\nc\nend\n"));
}

// diff.rs next_change_wraps_once_the_list_is_scrolled_to_its_end: once
// the last row sits in the viewport, next change wraps. It still walks
// forward before that, and previous change does not wrap.
static void NextChangeWrapsOnceTheListIsScrolledToItsEnd() {
    DiffFix fix;
    VecAppend(fix.state->changeRows, 10);
    VecAppend(fix.state->changeRows, 500);
    VecAppend(fix.state->changeRows, 990);
    fix.state->listScroll.itemsCount = 1000;
    fix.state->listScroll.contentSize = 20000;
    fix.state->listScroll.viewport = 400;

    fix.state->listScroll.offset = 200;
    fix.state->NextChange(&fix.cx);
    utassert(fix.state->scrollItem == 500);

    fix.state->listScroll.offset = 19600;
    fix.state->PreviousChange(&fix.cx);
    utassert(fix.state->scrollItem == 500);

    fix.state->NextChange(&fix.cx);
    utassert(fix.state->scrollItem == 10);
}

struct DiffBoxView {
    Entity<DiffState> state;
    bool loaded = false;

    static El* Render(DiffBoxView* self, Ctx* cx) {
        if (!self->loaded) {
            self->state = EntityNew<DiffState>(cx->app);
            DiffState* st = self->state.Get(cx);
            st->Bind(cx->app, self->state);
            const char* patch =
                "--- a/src/retry.rs\n"
                "+++ b/src/retry.rs\n"
                "@@ -1 +1 @@\n"
                "-old\n"
                "+new\n";
            Vec<DiffFile*> files;
            DiffParseError error = {};
            if (DiffFile::Parse(Str(patch), &files, &error)) {
                st->SetFiles(files.els, len(files), cx);
            }
            self->loaded = true;
        }
        return Diff::New(cx, self->state)->IntoEl()->H(320);
    }
};

// The list is height 100% of the diff. A parent that is only as tall as
// that percentage collapses it, so a 320px diff reported a 0 viewport and
// painted no rows.
static void TestDiffListFillsItsHeight() {
    App* app = TestAppNew();
    Init(app);
    Entity<DiffBoxView> view = EntityNew<DiffBoxView>(app);
    Window* win = TestWindowOpen(app, view, 480, 400);
    TestDraw(win);
    DiffBoxView* self = view.Get(app);
    DiffState* st = self ? self->state.Get(app) : nullptr;
    utassert(st && st->listScroll.viewport > 100.f);
    TestAppFree(app);
}

// diff/scroll_tests.rs. A 300-line patch, every line changed, in a 400×200
// window. The scroll extent is the whole patch, not the rows painted so far.
struct LongDiffView {
    Entity<DiffState> state;
    bool loaded = false;

    static El* Render(LongDiffView* self, Ctx* cx) {
        if (!self->loaded) {
            self->state = EntityNew<DiffState>(cx->app);
            DiffState* st = self->state.Get(cx);
            st->Bind(cx->app, self->state);
            StrBuilder patch;
            patch.Append(StrL("--- a/long.txt\n+++ b/long.txt\n"));
            patch.Append(StrL("@@ -1,300 +1,300 @@\n"));
            for (int line = 0; line < 300; line++) {
                patch.Append(fmt("-old %d\n+new %d\n", line, line));
            }
            Vec<DiffFile*> files;
            DiffParseError error = {};
            if (DiffFile::Parse(Str(patch.els, patch.len), &files, &error)) {
                st->SetFiles(files.els, len(files), cx);
            }
            self->loaded = true;
        }
        return Diff::New(cx, self->state)->IntoEl()->SizeFull();
    }
};

static void OpenLongDiff(App** appOut, Window** winOut, DiffState** stateOut) {
    App* app = TestAppNew();
    Init(app);
    Entity<LongDiffView> view = EntityNew<LongDiffView>(app);
    Window* win = TestWindowOpen(app, view, 400, 200);
    for (int i = 0; i < 3; i++) TestDraw(win);
    LongDiffView* self = view.Get(app);
    *appOut = app;
    *winOut = win;
    *stateOut = self ? self->state.Get(app) : nullptr;
}

static void TheWholePatchKeepsItsHeightAfterTheFirstLayout() {
    App* app = nullptr;
    Window* win = nullptr;
    DiffState* st = nullptr;
    OpenLongDiff(&app, &win, &st);
    utassert(st && st->listScroll.viewport > 0.f);
    if (st) {
        float max = st->listScroll.contentSize - st->listScroll.viewport;
        utassert(max > st->listScroll.viewport * 10.f);
    }
    TestAppFree(app);
}

static void TheWheelScrollsPastTheRowsRenderedSoFar() {
    App* app = nullptr;
    Window* win = nullptr;
    DiffState* st = nullptr;
    OpenLongDiff(&app, &win, &st);
    utassert(st && st->listScroll.itemsCount > 0);
    for (int i = 0; i < 50; i++) {
        TestSimulateScrollWheel(win, Point{100, 120}, Point{0, -40});
    }
    if (st && st->listScroll.itemsCount > 0) {
        float row = st->listScroll.contentSize / (float)st->listScroll
                                                     .itemsCount;
        int top = row > 0 ? (int)(st->listScroll.offset / row) : 0;
        utassert(top > 40);
    }
    TestAppFree(app);
}

struct HighlightCalls {
    int n = 0;
    char language[8][32];
    char text[8][160];
};

static void Remember(HighlightCalls* calls, Str language, Str text) {
    if (!calls || calls->n >= 8) return;
    int i = calls->n++;
    int n = language.len < 31 ? language.len : 31;
    if (language.s && n > 0) memcpy(calls->language[i], language.s, (size_t)n);
    calls->language[i][n] = 0;
    n = text.len < 159 ? text.len : 159;
    if (text.s && n > 0) memcpy(calls->text[i], text.s, (size_t)n);
    calls->text[i][n] = 0;
}

static bool HasBoth(const char* text) {
    return strstr(text, "open") && strstr(text, "close");
}

static void HighlightLet(void* data, Str language, Str text,
                         Vec<DiffSyntaxStyle>* out) {
    Remember((HighlightCalls*)data, language, text);
    int i = 0;
    while (i + 3 <= text.len) {
        if (text.s[i] == 'l' && text.s[i + 1] == 'e' && text.s[i + 2] == 't') {
            DiffSyntaxStyle style;
            style.start = i;
            style.end = i + 3;
            style.weight = (uint16_t)FontWeight::Bold;
            VecAppend(*out, style);
            i += 3;
        } else {
            i++;
        }
    }
}

static void HighlightSplit(void*, Str, Str, Vec<DiffSyntaxStyle>* out) {
    int starts[] = {0, 5, 4};
    int ends[] = {3, 6, 6};
    for (int i = 0; i < 3; i++) {
        DiffSyntaxStyle style;
        style.start = starts[i];
        style.end = ends[i];
        style.weight = (uint16_t)FontWeight::Bold;
        VecAppend(*out, style);
    }
}

struct WordWeight {
    uint16_t weight = 0;
};

static void HighlightFirstWord(void* data, Str, Str text,
                               Vec<DiffSyntaxStyle>* out) {
    int end = text.len;
    for (int i = 0; i < text.len; i++) {
        if (text.s[i] == ' ') {
            end = i;
            break;
        }
    }
    DiffSyntaxStyle style;
    style.start = 0;
    style.end = end;
    style.weight = ((WordWeight*)data)->weight;
    VecAppend(*out, style);
}

// presentation.rs an_application_highlighter_styles_each_hunk_side_on_its_own
// and tests.rs
// setting_or_clearing_a_syntax_highlighter_prepares_the_files_again. The
// tree-sitter half of the second test is the scanner gap.
static void AnApplicationHighlighterStylesEachHunkSide() {
    const char* patch =
        "--- a/a.rs\n+++ b/a.rs\n@@ -1,2 +1,2 @@\n-let a = 1;\n+let a = "
        "2;\n /* open\n@@ -10,2 +10,2 @@\n close */\n-let old = 0;\n+let new "
        "= 0;\n";
    Vec<DiffFile*> files;
    utassert(Parse(patch, &files) && len(files) == 1);
    files[0]->WithLanguage(StrL("rust"));
    HighlightCalls calls;
    DiffFix fix;
    DiffFile* one[] = {files[0]};
    fix.state->SetFiles(one, 1, &fix.cx);
    files.len = 0;
    fix.state->WithSyntaxHighlighter(HighlightLet, &calls);
    fix.state->EnsurePresentation();
    utassert(calls.n == 4);
    for (int i = 0; i < calls.n; i++) {
        utassert(strcmp(calls.language[i], "rust") == 0);
        utassert(!HasBoth(calls.text[i]));
    }
    utassert(fix.state->SyntaxStyleCount(0, DiffSide::Modified, 0) == 1);
    DiffSyntaxStyle letStyle = fix.state
                                   ->SyntaxStyleAt(0, DiffSide::Modified, 0, 0);
    utassert(letStyle.start == 0 && letStyle.end == 3 &&
             letStyle.weight == (uint16_t)FontWeight::Bold);
    utassert(fix.state->SyntaxStyleCount(0, DiffSide::Original, 3) == 1);
    utassert(fix.state->SyntaxStyleCount(0, DiffSide::Modified, 1) == 0);

    calls.n = 0;
    fix.state->WithSyntaxMaxLineLength(5);
    fix.state->EnsurePresentation();
    utassert(fix.state->SyntaxStyleCount(0, DiffSide::Modified, 0) == 0);

    Vec<DiffFile*> accent;
    utassert(Parse("--- a/a.rs\n+++ b/a.rs\n@@ -1 +1 @@\n-let \xC3\xA9 = "
                   "1;\n+let \xC3\xA9 = 2;\n",
                   &accent) &&
             len(accent) == 1);
    accent[0]->WithLanguage(StrL("rust"));
    DiffFile* accented[] = {accent[0]};
    fix.state->SetFiles(accented, 1, &fix.cx);
    accent.len = 0;
    fix.state->WithSyntaxMaxLineLength(1000);
    fix.state->WithSyntaxHighlighter(HighlightSplit, nullptr);
    fix.state->EnsurePresentation();
    utassert(fix.state->SyntaxStyleCount(0, DiffSide::Modified, 0) == 2);
    DiffSyntaxStyle kept = fix.state
                               ->SyntaxStyleAt(0, DiffSide::Modified, 0, 0);
    DiffSyntaxStyle whole = fix.state
                                ->SyntaxStyleAt(0, DiffSide::Modified, 0, 1);
    utassert(kept.start == 0 && kept.end == 3);
    utassert(whole.start == 4 && whole.end == 6);

    WordWeight weight;
    weight.weight = (uint16_t)FontWeight::Bold;
    Vec<DiffFile*> again;
    utassert(Parse("--- a/a.rs\n+++ b/a.rs\n@@ -1 +1 @@\n-let a = 1;\n+let a "
                   "= 2;\n",
                   &again) &&
             len(again) == 1);
    again[0]->WithLanguage(StrL("rust"));
    DiffFile* doc[] = {again[0]};
    fix.state->SetFiles(doc, 1, &fix.cx);
    again.len = 0;
    fix.state->WithSyntaxHighlighter(HighlightFirstWord, &weight);
    fix.state->EnsurePresentation();
    utassert(fix.state->SyntaxStyleAt(0, DiffSide::Modified, 0, 0)
                 .weight == (uint16_t)FontWeight::Bold);
    weight.weight = (uint16_t)FontWeight::Light;
    fix.state->SetSyntaxHighlighter(HighlightFirstWord, &weight, &fix.cx);
    utassert(fix.state->SyntaxStyleAt(0, DiffSide::Modified, 0, 0)
                 .weight == (uint16_t)FontWeight::Light);
    fix.state->ClearSyntaxHighlighter(&fix.cx);
    utassert(fix.state->SyntaxStyleCount(0, DiffSide::Modified, 0) == 0);
}

void TestDiff() {
    TestParser();
    TestConflictsAndLanguage();
    TestInlineAndRows();
    AnApplicationHighlighterStylesEachHunkSide();
    NextChangeWrapsOnceTheListIsScrolledToItsEnd();
    TestDiffListFillsItsHeight();
    TheWholePatchKeepsItsHeightAfterTheFirstLayout();
    TheWheelScrollsPastTheRowsRenderedSoFar();
}
