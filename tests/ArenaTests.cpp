/* Not a port — the arena is this tree's, taken from SumatraPDF rather than
   from gpui-kit.

   What is checked here is the chaining: an arena reserves a block and hands
   out of it until it does not fit, and then reserves another. The sizes of
   that next block are the arena's, not the last block's, which is what an
   allocation too big for any block would otherwise leave behind. */

#include "Test.h"

// An allocation bigger than a block gets a block sized for it alone, and that
// block stays the current one. The next small allocation chains off it and
// must not inherit its size: reserving is cheap, but a block made for a big
// allocation commits everything it reserves, so inheriting would commit the
// whole of it for a handful of bytes.
static void AChainedBlockTakesItsSizeFromTheArena() {
    TestSuite("arena");
    Arena* a = ArenaNew();
    uint64_t reserveChunk = a->reserveChunkSize;
    uint64_t commitChunk = a->commitChunkSize;
    utassert(reserveChunk > 0 && commitChunk > 0);

    // One allocation too big for a block of the usual size.
    void* big = a->Push(reserveChunk + 4096, 8, false);
    utassert(big);
    utassert(a->current->reserved > reserveChunk);
    utassert(a->current != a);

    // Fill what page alignment left over at the end of it, so the next push
    // is the one that chains.
    uint64_t left = a->current->reserved - a->current->pos;
    if (left > 0) {
        utassert(a->Push(left, 1, false));
    }

    void* tail = a->Push(64, 8, false);
    utassert(tail);
    utassert(a->current->reserved == reserveChunk);
    utassert(a->current->committed == commitChunk);
    ArenaDelete(a);
}

static void AllocAndMemberAllocShareOnePath() {
    Arena* a = ArenaNew();
    void* viaFree = Alloc(a, 64);
    void* viaMember = a->Alloc(64);
    utassert(viaFree && viaMember);
    utassert(viaFree != viaMember);
    // Heap fallback is the same wrapper with a null arena.
    void* heap = Alloc((Arena*)nullptr, 32);
    utassert(heap);
    Free(nullptr, heap);
    utassert(Alloc(a, 0) == nullptr);
    utassert(a->Alloc(0) == nullptr);
    utassert(Alloc(a, -1) == nullptr);
    ArenaDelete(a);
}

void TestArena() {
    AChainedBlockTakesItsSizeFromTheArena();
    AllocAndMemberAllocShareOnePath();
}
