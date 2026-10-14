#include "shell/failure.h"

#include "base/button.h"
#include "base/theme_tokens.h"
#include "shell/theme_tokens.h"
#include "shell/view.h"

#include <stdio.h>
#include <string.h>

namespace gpui {

void SpokenDuration(double seconds, char* out, int cap) {
    if (!out || cap <= 0) return;
    if (seconds < 1.0) {
        int ms = (int)(seconds * 1000.0 + 0.5);
        snprintf(out, (size_t)cap, "%d ms", ms);
    } else {
        snprintf(out, (size_t)cap, "%d s", (int)seconds);
    }
}

static Str Dup(Arena* arena, Str text) {
    return arena ? StrDup(arena, text) : Str{};
}

Str ExplainInterruption(Arena* arena, Str message, ScriptInterruption* slot) {
    if (!slot || !slot->set || !StrEq(message, StrL("interrupted"))) return {};
    ScriptInterruption taken = *slot;
    slot->set = false;
    char limit[32];
    SpokenDuration(taken.budget, limit, (int)sizeof(limit));
    const char* text = nullptr;
    if (!taken.scoped) {
        text =
            "Loading the script ran for %s without finishing and was "
            "stopped.\n"
            "Check for a loop that never ends; defer slow work at module "
            "load to init() or a task.";
    } else if (taken.phase == ScopePhase::Render) {
        text =
            "render() ran for %s without returning and was stopped.\n"
            "Check for a loop that never ends; move slow work such as "
            "scanning or parsing into a task and render its result.";
    } else if (taken.phase == ScopePhase::Layout) {
        text =
            "A list item ran for %s without returning and was stopped.\n"
            "Check for a loop that never ends; prepare item data ahead of "
            "time so rendering one item stays cheap.";
    } else if (taken.phase == ScopePhase::Event) {
        text =
            "An event handler ran for %s without returning and was "
            "stopped.\n"
            "Check for a loop that never ends; move slow work into a task "
            "and await between chunks of it.";
    } else {
        text =
            "A task ran for %s without awaiting and was stopped.\n"
            "Check for a loop that never ends; await between chunks of "
            "work so the window stays responsive.";
    }
    return Dup(arena, fmt(text, Str(limit)));
}

static bool LineBlank(Str line) {
    for (int i = 0; i < len(line); i++) {
        char c = line.s[i];
        if (c != ' ' && c != '\t' && c != '\r') return false;
    }
    return true;
}

Str TidyStack(Arena* arena, Str stack) {
    if (!arena || !stack) return {};
    struct Frame {
        Str line;
        int count = 1;
    };
    Vec<Frame> frames;
    int at = 0;
    while (at <= len(stack)) {
        int end = at;
        while (end < len(stack) && stack.s[end] != '\n') end++;
        Str raw(stack.s + at, end - at);
        at = end < len(stack) ? end + 1 : end + 1;
        if (LineBlank(raw)) {
            if (end >= len(stack)) break;
            continue;
        }
        StrBuilder line(arena);
        int i = 0;
        while (i < len(raw)) {
            if (i + 3 <= len(raw) && raw.s[i] == '?' && raw.s[i + 1] == 'v' &&
                raw.s[i + 2] == '=') {
                int digits = 0;
                while (i + 3 + digits < len(raw) &&
                       raw.s[i + 3 + digits] >= '0' &&
                       raw.s[i + 3 + digits] <= '9') {
                    digits++;
                }
                if (digits == 0) line.Append(StrL("?v="));
                i += 3 + digits;
                continue;
            }
            line.AppendChar(raw.s[i]);
            i++;
        }
        Str cleaned = line.TakeStr();
        if (len(frames) > 0 && StrEq(frames[len(frames) - 1].line, cleaned)) {
            frames[len(frames) - 1].count++;
        } else {
            Frame frame;
            frame.line = cleaned;
            frame.count = 1;
            VecAppend(frames, frame);
        }
        if (end >= len(stack)) break;
    }
    StrBuilder out(arena);
    for (int f = 0; f < len(frames); f++) {
        if (f) out.AppendChar('\n');
        out.Append(frames[f].line);
        if (frames[f].count > 1) {
            char times[16];
            snprintf(times, sizeof(times), " \xC3\x97%d", frames[f].count);
            out.Append(Str(times));
        }
    }
    return out.TakeStr();
}

static bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' ||
           c == '\f';
}

static Str Trim(Arena* arena, Str text) {
    int lo = 0;
    int hi = len(text);
    while (lo < hi && IsSpace(text.s[lo])) lo++;
    while (hi > lo && IsSpace(text.s[hi - 1])) hi--;
    return Dup(arena, Str(text.s + lo, hi - lo));
}

static int CharCount(Str text) {
    int n = 0;
    for (int i = 0; i < len(text);) {
        unsigned char c = (unsigned char)text.s[i];
        int w = 1;
        if ((c & 0xE0) == 0xC0)
            w = 2;
        else if ((c & 0xF0) == 0xE0)
            w = 3;
        else if ((c & 0xF8) == 0xF0)
            w = 4;
        i += w;
        if (i > len(text)) break;
        n++;
    }
    return n;
}

static void ReplacePrefix(StrBuilder& out, Str text, Str prefix) {
    if (!prefix || len(prefix) == 0) {
        out.Append(text);
        return;
    }
    int at = 0;
    while (at < len(text)) {
        if (at + len(prefix) <= len(text) &&
            memcmp(text.s + at, prefix.s, (size_t)len(prefix)) == 0) {
            at += len(prefix);
            continue;
        }
        out.AppendChar(text.s[at]);
        at++;
    }
}

// Rust replaces `{cwd}/`. A Windows path uses `\`, and the same prefix
// with that separator is what a message from this process contains, so
// both are stripped. An external path is left alone.
static Str RelativeErrorPaths(Arena* arena, Str message) {
    char cwd[kMaxPath];
    PlatGetCwd(cwd, kMaxPath);
    int cwdLen = (int)strlen(cwd);
    if (cwdLen <= 0 || cwdLen + 2 >= kMaxPath) return Dup(arena, message);
    char slash[kMaxPath];
    char back[kMaxPath];
    memcpy(slash, cwd, (size_t)cwdLen);
    slash[cwdLen] = '/';
    slash[cwdLen + 1] = 0;
    memcpy(back, cwd, (size_t)cwdLen);
    back[cwdLen] = '\\';
    back[cwdLen + 1] = 0;
    StrBuilder once(arena);
    ReplacePrefix(once, message, Str(slash));
    StrBuilder twice(arena);
    ReplacePrefix(twice, once.TakeStr(), Str(back));
    return twice.TakeStr();
}

FailureText FailureTextParse(Arena* arena, Str message) {
    FailureText text;
    Str trimmed = Trim(arena, message);
    Str relative = RelativeErrorPaths(arena, trimmed);
    int split = 0;
    while (split < len(relative) && relative.s[split] != '\n') split++;
    Str summary(relative.s, split);
    int hi = len(summary);
    while (hi > 0 && IsSpace(summary.s[hi - 1]) && summary.s[hi - 1] != '\n')
        hi--;
    text.summary = Dup(arena, Str(summary.s, hi));
    if (split < len(relative)) {
        int lo = split + 1;
        int end = len(relative);
        while (lo < end && relative.s[lo] == '\n') lo++;
        while (end > lo && relative.s[end - 1] == '\n') end--;
        Str detail(relative.s + lo, end - lo);
        bool blank = true;
        for (int i = 0; i < len(detail); i++) {
            if (!IsSpace(detail.s[i])) {
                blank = false;
                break;
            }
        }
        if (!blank) text.detail = Dup(arena, detail);
    }
    // A single line this long will almost certainly be cut off, so it
    // earns a disclosure even with nothing below it.
    text.expandable = (bool)text.detail || CharCount(text.summary) > 96;
    return text;
}

const char* ViewFailureLabel(ViewFailure failure) {
    return failure == ViewFailure::Reload ? "Reload failed" : "Render failed";
}
const char* ViewFailureHeading(ViewFailure failure) {
    return failure == ViewFailure::Reload
               ? "This application could not be reloaded"
               : "This view could not be re-rendered";
}
const char* ViewFailureKeptNote(ViewFailure failure) {
    return failure == ViewFailure::Reload
               ? "Still running the code from before this change. Save a "
                 "fix to reload."
               : "Showing the last version that rendered. Save a fix to "
                 "re-render.";
}
const char* ViewFailureOverlayHeading(ViewFailure failure) {
    return failure == ViewFailure::Reload
               ? "This application could not be reloaded"
               : "This view could not be rendered";
}
const char* ViewFailureRecovery(ViewFailure failure) {
    return failure == ViewFailure::Reload
               ? "Fix the script and save; it reloads on the next change."
               : "Fix the script and save; the view re-renders on the next "
                 "change.";
}

struct FailureColors {
    Rgba background = {};
    Rgba surface = {};
    Rgba foreground = {};
    Rgba muted = {};
    Rgba border = {};
    Rgba accent = {};
    Rgba hover = {};
};

static Rgba TokenOr(const char* name, Rgba fallback) {
    Hsla color;
    if (shell::ThemeTokenColor(Str(name), &color)) return HslaToRgba(color);
    return fallback;
}

static FailureColors FailureColorsResolve() {
    ColorTokens defaults = ColorTokens::Light();
    FailureColors colors;
    colors.background = TokenOr("background", defaults.background);
    colors.surface = TokenOr("surface", defaults.surface);
    colors.foreground = TokenOr("foreground", defaults.foreground);
    colors.muted = TokenOr("muted_foreground", defaults.mutedForeground);
    colors.border = TokenOr("border", defaults.border);
    colors.accent = TokenOr("destructive", defaults.destructive);
    colors.hover = TokenOr("muted", defaults.muted);
    return colors;
}

static El* ShadowSm(Arena* a, El* element) {
    BoxShadow* shadows = (BoxShadow*)Alloc(a, (int)sizeof(BoxShadow) * 2);
    if (!shadows) return element;
    shadows[0] = {0, 1, 3, 0, Rgba8(0, 0, 0, 26), false};
    shadows[1] = {0, 1, 2, -1, Rgba8(0, 0, 0, 26), false};
    return element->Shadows(shadows, 2);
}

static El* StatusDot(Ctx* cx, Rgba accent) {
    float side = Rems(cx, 0.375f);
    return Div(cx->a)
        ->Shrink0()
        ->W(side)
        ->H(side)
        ->Radius(kRadiusFull)
        ->Bg(accent);
}

static FailureChrome* ChromeFor(Ctx* cx, Str name, Str payload) {
    FailureChrome* chrome = FrameKeyedState<FailureChrome>(
        cx, KeyedKey(KeyedName(cx, name), HashClickId(StrL("FailureChrome"))));
    if (!chrome) return nullptr;
    if (!StrEq(chrome->payload, payload)) {
        StrFree(chrome->payload);
        chrome->payload = StrDup(payload);
    }
    return chrome;
}

void ScriptView::OnFailureToggle(ScriptView*, Ctx* cx, const ClickEvent*,
                                 int64_t slot) {
    auto* chrome = (FailureChrome*)(intptr_t)slot;
    if (!chrome || !cx) return;
    chrome->expanded = !chrome->expanded;
    Notify(cx);
}

void ScriptView::OnFailureCopy(ScriptView*, Ctx* cx, const ClickEvent*,
                               int64_t slot) {
    auto* chrome = (FailureChrome*)(intptr_t)slot;
    if (!chrome || !cx) return;
    if (chrome->payload) ClipboardSetText(cx->win, chrome->payload);
    chrome->copied = true;
    Notify(cx);
}

static El* DetailWell(Ctx* cx, Str key, Str detail,
                      const FailureColors& colors) {
    float padX = Rems(cx, 0.75f);
    float padY = Rems(cx, 0.5f);
    return Div(cx->a)
        ->Click(HashClickId(key))
        ->MaxH(Rems(cx, 16.f))
        ->ScrollY(0)
        ->PadX(padX)
        ->PadY(padY)
        ->Bg(colors.background)
        ->Border(1, colors.border)
        ->FontFamily(Str(PlatMonoFontName()))
        ->Font(Rems(cx, 0.6875f))
        ->LineHeight(1.6f)
        ->Fg(colors.muted)
        ->Child(TextEl(cx->a, detail)->Wrap()->Selectable());
}

static El* DetailsButton(Ctx* cx, Str id, FailureChrome* chrome,
                         const FailureColors& colors) {
    bool expanded = chrome && chrome->expanded;
    El* button = Button::New(
        cx, id, false,
        Listen(cx, &ScriptView::OnFailureToggle, (int64_t)(intptr_t)chrome));
    button->H(Rems(cx, 1.625f))
        ->Shrink0()
        ->Gap(Rems(cx, 0.25f))
        ->PadL(Rems(cx, 0.5f))
        ->PadR(Rems(cx, 0.375f))
        ->Fg(colors.foreground)
        ->HoverBg(colors.hover)
        ->AriaLabel(expanded ? StrL("Hide details") : StrL("Show details"))
        ->AriaExpanded(expanded)
        ->Child(TextEl(cx->a, StrL("Details"))->Font(Rems(cx, 0.75f)))
        ->Child(IconEl(cx->a,
                       expanded ? IconName::ChevronUp : IconName::ChevronDown,
                       Rems(cx, 0.75f))
                    ->Fg(colors.muted));
    return button;
}

static El* CopyButton(Ctx* cx, FailureChrome* chrome,
                      const FailureColors& colors) {
    bool copied = chrome && chrome->copied;
    El* button = Button::New(
        cx, StrL("shell-failure-copy"), false,
        Listen(cx, &ScriptView::OnFailureCopy, (int64_t)(intptr_t)chrome));
    button->W(Rems(cx, 1.625f))
        ->H(Rems(cx, 1.625f))
        ->Shrink0()
        ->HoverBg(colors.hover)
        ->AriaLabel(StrL("Copy error"))
        ->Child(IconEl(cx->a, copied ? IconName::Check : IconName::Copy,
                       Rems(cx, 0.875f))
                    ->Fg(copied ? colors.muted : colors.foreground));
    return button;
}

static El* SummaryLine(Ctx* cx, Str summary, bool expanded, Rgba muted) {
    El* line = Div(cx->a)->Flex1()->MinW(0)->Fg(muted);
    if (!expanded) line->Truncate();
    El* text = TextEl(cx->a, summary)->Font(Rems(cx, 0.75f))->LineHeight(1.5f);
    if (expanded) text->Wrap();
    return line->Child(text);
}

El* ShellErrorBanner(Ctx* cx, ViewFailure failure, Str message) {
    if (!cx) return nullptr;
    Arena* a = cx->a;
    FailureColors colors = FailureColorsResolve();
    FailureText text = FailureTextParse(a, message);
    Str payload =
        Dup(a, fmt("%s\n\n%s", Str(ViewFailureHeading(failure)), message));
    FailureChrome* chrome = ChromeFor(cx, StrL("shell-banner"), payload);
    bool expanded = chrome && chrome->expanded;
    El* row = Div(a)
                  ->Flex()
                  ->ItemsCenter()
                  ->Gap(Rems(cx, 0.75f))
                  ->PadL(Rems(cx, 1.f))
                  ->PadR(Rems(cx, 0.5f))
                  ->PadY(Rems(cx, 0.375f))
                  ->Font(Rems(cx, 0.75f))
                  ->LineHeight(1.5f);
    row->Child(Div(a)
                   ->Flex()
                   ->Shrink0()
                   ->ItemsCenter()
                   ->Gap(Rems(cx, 0.5f))
                   ->Fg(colors.foreground)
                   ->Child(StatusDot(cx, colors.accent))
                   ->Child(TextEl(a, Str(ViewFailureLabel(failure)))));
    row->Child(SummaryLine(cx, text.summary, expanded, colors.muted));
    El* actions =
        Div(a)->Flex()->Shrink0()->ItemsCenter()->Gap(Rems(cx, 0.25f));
    if (text.expandable)
        actions->Child(
            DetailsButton(cx, StrL("shell-banner-details"), chrome, colors));
    actions->Child(CopyButton(cx, chrome, colors));
    row->Child(actions);

    El* banner = Div(a)
                     ->Absolute()
                     ->Top(0)
                     ->Left(0)
                     ->Right(0)
                     ->FlexCol()
                     ->Bg(colors.surface)
                     ->BorderB(1, colors.border);
    ShadowSm(a, banner);
    banner->Child(row);
    if (expanded) {
        El* extra = Div(a)
                        ->FlexCol()
                        ->Gap(Rems(cx, 0.5f))
                        ->PadX(Rems(cx, 1.f))
                        ->PadT(Rems(cx, 0.25f))
                        ->PadB(Rems(cx, 0.75f));
        if (text.detail)
            extra->Child(DetailWell(cx, StrL("shell-banner-detail"),
                                    text.detail, colors));
        extra->Child(TextEl(a, Str(ViewFailureKeptNote(failure)))
                         ->Font(Rems(cx, 0.75f))
                         ->LineHeight(1.5f)
                         ->Fg(colors.muted)
                         ->Wrap());
        banner->Child(extra);
    }
    return banner;
}

El* ShellErrorOverlay(Ctx* cx, ViewFailure failure, Str message) {
    if (!cx) return nullptr;
    Arena* a = cx->a;
    FailureColors colors = FailureColorsResolve();
    FailureText text = FailureTextParse(a, message);
    Str payload = Dup(
        a, fmt("%s\n\n%s", Str(ViewFailureOverlayHeading(failure)), message));
    FailureChrome* chrome = ChromeFor(cx, StrL("shell-failure"), payload);
    bool expanded = chrome && chrome->expanded;

    El* body = Div(a)
                   ->FlexCol()
                   ->Gap(Rems(cx, 0.375f))
                   ->PadX(Rems(cx, 1.25f))
                   ->PadT(Rems(cx, 1.f))
                   ->PadB(Rems(cx, 0.75f));
    body->Child(
        Div(a)
            ->Flex()
            ->ItemsCenter()
            ->Gap(Rems(cx, 0.5f))
            ->Font(Rems(cx, 0.875f))
            ->LineHeight(1.4f)
            ->Fg(colors.foreground)
            ->Child(StatusDot(cx, colors.accent))
            ->Child(TextEl(a, Str(ViewFailureOverlayHeading(failure)))));
    body->Child(SummaryLine(cx, text.summary, expanded, colors.muted));
    if (expanded && text.detail) {
        body->Child(Div(a)
                        ->PadT(Rems(cx, 0.25f))
                        ->Child(DetailWell(cx, StrL("shell-failure-detail"),
                                           text.detail, colors)));
    }

    El* actions =
        Div(a)->Flex()->Shrink0()->ItemsCenter()->Gap(Rems(cx, 0.25f));
    if (text.expandable)
        actions->Child(
            DetailsButton(cx, StrL("shell-failure-details"), chrome, colors));
    actions->Child(CopyButton(cx, chrome, colors));
    El* footer = Div(a)
                     ->Flex()
                     ->ItemsCenter()
                     ->Gap(Rems(cx, 1.f))
                     ->PadL(Rems(cx, 1.25f))
                     ->PadR(Rems(cx, 0.75f))
                     ->PadY(Rems(cx, 0.5f))
                     ->BorderT(1, colors.border);
    footer
        ->Child(Div(a)->Flex1()->MinW(0)->Child(
            TextEl(a, Str(ViewFailureRecovery(failure)))
                ->Font(Rems(cx, 0.75f))
                ->LineHeight(1.5f)
                ->Fg(colors.muted)
                ->Wrap()))
        ->Child(actions);

    El* panel = Div(a)
                    ->FlexCol()
                    ->W(kFill)
                    ->MaxW(Rems(cx, 42.f))
                    ->Bg(colors.surface)
                    ->Border(1, colors.border)
                    ->Child(body)
                    ->Child(footer);
    return Div(a)
        ->SizeFull()
        ->Flex()
        ->ItemsCenter()
        ->JustifyCenter()
        ->Bg(colors.background)
        ->Pad(Rems(cx, 2.f))
        ->Child(panel);
}

} // namespace gpui
