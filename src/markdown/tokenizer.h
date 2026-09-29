/* src/tokenizer.rs, src/parser.rs, src/subtokenize.rs, src/resolve.rs — the
   machine the constructs are written against.

   Part of the C++ port of markdown-rs 1.0.0 (see src/markdown/readme.md).

   Deviations from the Rust, each forced by this tree's rules:
     - `Option<u8>` for the current and previous byte is an `int32_t`, -1 for
       none; `Option<usize>` indices are -1 the same way.
     - The `Option<T>` fields of `TokenizeState` that are not indices carry a
       `has` flag beside the value, so the struct stays POD-ish and copies.
     - `Vec<String>` is `Vec<Str>` pointing into the parse arena. Nothing in
       here frees a string one at a time.
     - `&'static [u8] markers` is a pointer and a length. */

#ifndef GPUI_MARKDOWN_TOKENIZER_H_
#define GPUI_MARKDOWN_TOKENIZER_H_

#include "markdown/markdown.h"
#include "markdown/state.h"
#include "markdown/util.h"

namespace markdown {

// tokenizer.rs Container.
enum class Container : uint8_t {
    BlockQuote,
    ListItem,
    GfmFootnoteDefinition,
};

// tokenizer.rs ContainerState.
struct ContainerState {
    Container kind = Container::BlockQuote;
    bool blankInitial = false;
    int32_t size = 0;
};

// tokenizer.rs LabelKind.
enum class LabelKind : uint8_t {
    Image,
    Link,
    GfmFootnote,
    GfmUndefinedFootnote,
};

// tokenizer.rs LabelStart, renamed: the state named `LabelStart` is a
// function here, and the two cannot share a name. `start` is Rust's
// `(usize, usize)` pair of event indices.
struct LabelStartMark {
    LabelKind kind = LabelKind::Image;
    int32_t startA = 0;
    int32_t startB = 0;
    bool inactive = false;
};

// tokenizer.rs Label.
struct Label {
    LabelKind kind = LabelKind::Image;
    int32_t startA = 0;
    int32_t startB = 0;
    int32_t endA = 0;
    int32_t endB = 0;
};

// resolve.rs Name.
enum class ResolveName : uint8_t {
    Label,
    Attention,
    GfmTable,
    HeadingAtx,
    HeadingSetext,
    ListItem,
    Content,
    Data,
    String,
    Text,
};

// subtokenize.rs Subresult.
struct Subresult {
    bool done = false;
    Vec<Str> gfmFootnoteDefinitions;
    Vec<Str> definitions;
};

// Moves everything in `src` onto the end of `dst` and empties `src`, which is
// Rust's `Vec::append`.
void SubresultAppend(Subresult& dst, Subresult& src);

struct Tokenizer;

// parser.rs ParseState. `location` is gone with MDX, which was its only user.
struct ParseState {
    // What the tree and its strings are allocated from: the arena the caller
    // handed `ToMdast`.
    Arena* a = nullptr;
    // What the parse's own working memory comes from — the edit maps, the
    // definition labels, the attention stacks. Thrown away whole when the
    // parse is done, so none of it ends up in the caller's arena.
    Arena* scratch = nullptr;
    const ParseOptions* options = nullptr;
    Str bytes = {};
    Vec<Str> definitions;
    Vec<Str> gfmFootnoteDefinitions;
};

// tokenizer.rs TokenizeState: the scratch space the constructs share. Every
// field belongs to whoever is running; a construct may only touch the ones
// its own documentation claims.
struct TokenizeState {
    Tokenizer* documentChild = nullptr;
    State documentChildState = {};
    bool documentChildStateSome = false;
    Vec<ContainerState> documentContainerStack;
    int32_t documentContinued = 0;
    // Rust's `Option<usize>`.
    int32_t documentDataIndex = -1;
    // `Vec<Option<Vec<Event>>>`: a slot per line, empty when there is nothing
    // to exit there.
    Vec<ArenaVec<Event>> documentExits;
    bool documentLazyAcceptingBefore = false;
    bool documentAtFirstParagraphOfListItem = false;

    ContentKind spaceOrTabEolContent = ContentKind::Flow;
    bool spaceOrTabEolContentSome = false;
    bool spaceOrTabEolConnect = false;
    bool spaceOrTabEolOk = false;
    bool spaceOrTabConnect = false;
    ContentKind spaceOrTabContent = ContentKind::Flow;
    bool spaceOrTabContentSome = false;
    int32_t spaceOrTabMin = 0;
    int32_t spaceOrTabMax = 0;
    int32_t spaceOrTabSize = 0;
    Name spaceOrTabToken = Name::SpaceOrTab;

    Vec<LabelStartMark> labelStarts;
    Vec<LabelStartMark> labelStartsLoose;
    Vec<Label> labels;
    Vec<Str> definitions;
    Vec<Str> gfmFootnoteDefinitions;

    bool connect = false;
    uint8_t marker = 0;
    uint8_t markerB = 0;
    const uint8_t* markers = nullptr;
    int32_t markersLen = 0;
    bool seen = false;
    int32_t size = 0;
    int32_t sizeB = 0;
    int32_t sizeC = 0;
    int32_t start = 0;
    int32_t end = 0;
    Name token1 = Name::Data;
    Name token2 = Name::Data;
    Name token3 = Name::Data;
    Name token4 = Name::Data;
    Name token5 = Name::Data;
    Name token6 = Name::Data;
};

// An (index, vs) pair: a position in the source that a half-consumed tab can
// sit inside of. Rust writes it as a tuple.
struct IndexVs {
    int32_t index = 0;
    int32_t vs = 0;
};

// tokenizer.rs Progress: what an attempt restores when it fails.
struct Progress {
    int32_t eventsLen = 0;
    int32_t stackLen = 0;
    int32_t previous = -1;
    int32_t current = -1;
    Point point = {};
};

// tokenizer.rs Attempt.
struct Attempt {
    State ok = {};
    State nok = {};
    // `Check` throws its progress away even when it succeeds.
    bool check = false;
    bool hasProgress = false;
    Progress progress = {};
};

// tokenizer.rs Tokenizer.
struct Tokenizer {
    // (index, vs) of the first byte of each line, for `define_skip`.
    Vec<IndexVs> columnStart;
    int32_t firstLine = 1;
    Point lineStart = {};
    bool consumed = true;
    Vec<Attempt> attempts;

    // Rust's `Option<u8>`: -1 is `None`.
    int32_t current = -1;
    int32_t previous = -1;
    Point point = {};
    Vec<Event> events;
    Vec<Name> stack;
    EditMap map;
    Vec<ResolveName> resolvers;
    ParseState* parseState = nullptr;
    TokenizeState tokenizeState;
    bool interrupt = false;
    bool concrete = false;
    bool pierce = false;
    bool lazy = false;
};

Tokenizer* TokenizerNew(Point point, ParseState* parseState);
void TokenizerFree(Tokenizer* tokenizer);

void RegisterResolver(Tokenizer* t, ResolveName name);
void RegisterResolverBefore(Tokenizer* t, ResolveName name);
void DefineSkip(Tokenizer* t, Point point);
void Consume(Tokenizer* t);
void Enter(Tokenizer* t, Name name);
void EnterLink(Tokenizer* t, Name name, Link link);
void Exit(Tokenizer* t, Name name);
// Rust's `tokenizer.check` / `tokenizer.attempt`, which cannot be free
// functions called `Check` / `Attempt` without colliding with `Attempt`.
void TokenizerCheck(Tokenizer* t, State ok, State nok);
void TokenizerAttempt(Tokenizer* t, State ok, State nok);
// `from` and `to` are Rust's `(index, vs)` pairs.
State Push(Tokenizer* t, int32_t fromIndex, int32_t fromVs, int32_t toIndex,
           int32_t toVs, State state);
Subresult Flush(Tokenizer* t, State state, bool resolve);

// resolve.rs `call`.
bool ResolveCall(Tokenizer* t, ResolveName name, Subresult* out);

// subtokenize.rs.
void SubtokenizeLink(Vec<Event>& events, int32_t index);
void SubtokenizeLinkTo(Vec<Event>& events, int32_t previous, int32_t next);
// Rust's `filter: Option<&Content>` is the pair of trailing arguments.
Subresult Subtokenize(Vec<Event>& events, ParseState* parseState,
                      bool hasFilter, ContentKind filter);
void DivideEvents(EditMap& map, const Vec<Event>& events, int32_t linkIndex,
                  Vec<Event>& childEvents, int32_t* accA, int32_t* accB);

// parser.rs `parse`.
Vec<Event> Parse(ParseState* parseState);

// to_mdast.rs `compile`.
// `positions`, when not null, is filled with each pushed node's offsets.
Node* ToMdastCompile(const Vec<Event>& events, ParseState* parseState,
                     NodePositions* positions);

} // namespace markdown

#endif // GPUI_MARKDOWN_TOKENIZER_H_
