/* The GPUI-shaped GPU backend for Paint.h; see paintgpu.h for its compile-time
   selection, ALL-build runtime selector, and remaining gaps.

   The frame is one instance buffer and as few draws as the content allows.
   Every rounded rect, border, ellipse, glyph, image and gradient is 96 bytes
   in that buffer — bounds, colour, radius, border width, kind, content mask,
   uv — and the pixel shader evaluates the shape analytically, which is what
   `directx_renderer.rs` and Blade do and what D2D does not let us do. A batch
   is only broken when the pipeline or a bound texture has to change: an
   image, a path, or the end of the frame.

   Three pipelines:

   - quads. Four vertices per instance from SV_VertexID, instance data read
     out of a StructuredBuffer, shape and coverage from an SDF. This is nearly
     everything a frame draws.
   - triangles. Explicit vertices with a colour and a content mask, for the
     two things a quad cannot be: a stroke expanded on the CPU, and the
     stencil pass of a path fill.
   - stencil-and-cover for path fills, which is how you fill an arbitrary
     path on a GPU without a tessellator: draw the triangle fan of every
     contour into the stencil buffer with INVERT (even-odd) or with wrapping
     increment and decrement (nonzero), then draw the path's bounding box
     through a stencil test and let it write the colour. The fan is allowed
     to self-overlap, which is the whole point — no library has to untangle
     the outline first.

   Antialiasing comes from two places. Quads and glyphs carry their own: the
   SDF gives analytic coverage, and a glyph is a coverage texture. Paths and
   strokes have no analytic form here, so they get theirs from the sample
   count — __msaa, 4 by default. That is the one real architectural
   difference from Blade, which renders paths to an antialiased mask instead;
   MSAA is a prototype's version of the same answer, and having it as a knob
   is what makes its cost visible.

   Dashes are expanded on the CPU for lines and for rounded-rect outlines.
   Text uses
   DirectWrite's RGB masks, display gamma/contrast and third-pixel phases. The
   four shaders are checked-in FXC bytecode generated from paintgpu_win.hlsl by
   cmd/update-win-shaders.ts. */

#include "gpui/paintgpu.h"
#include "gpui/scene.h"

#if GPUI_OS_WINDOWS && WIN_BACKEND_GPU

#include <d3d11.h>
#include <d3d12.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <math.h>

namespace gpui {

bool PaintGpuOn() {
    return WinPaintOptionsGet().backend != WinPaintBackend::Direct2D;
}

bool PaintD3d12On() {
    return WinPaintOptionsGet().backend == WinPaintBackend::D3D12;
}

int PaintGpuSamples() {
    return (int)WinPaintOptionsGet().msaa;
}

namespace gpuw {

template <typename T>
static void Rel(T** p) {
    if (p && *p) {
        (*p)->Release();
        *p = nullptr;
    }
}

// ─── the shaders ─────────────────────────────────────────────────────────

// Defined by generated paintgpu_shaders_win.cpp. Printable basE95 keeps the
// checked-in source about five times smaller than comma-separated hex; these
// BSS arrays receive the decoded DXBC once, when the first GPU device opens.
extern const char kShaderVSQuad95[];
extern const int kShaderVSQuadSize;
extern uint8_t kShaderVSQuadBytes[];
extern const char kShaderPSQuad95[];
extern const int kShaderPSQuadSize;
extern uint8_t kShaderPSQuadBytes[];
extern const char kShaderVSTri95[];
extern const int kShaderVSTriSize;
extern uint8_t kShaderVSTriBytes[];
extern const char kShaderPSTri95[];
extern const int kShaderPSTriSize;
extern uint8_t kShaderPSTriBytes[];

static bool DecodeBase95(const char* encoded, uint8_t* out, int expected) {
    uint32_t bits = 0;
    int bitCount = 0;
    int pending = -1;
    int at = 0;
    bool lineStart = true;
    for (const uint8_t* p = (const uint8_t*)encoded; *p; p++) {
        // The generator wraps at 100 columns. CR/LF are the only bytes in a
        // generated raw string that are not in the printable-ASCII alphabet.
        if (*p == '\r' || *p == '\n') {
            lineStart = true;
            continue;
        }
        if (lineStart) {
            if (*p != '.') {
                return false;
            }
            lineStart = false;
            continue;
        }
        if (*p < 32 || *p > 126) {
            return false;
        }
        int digit = *p - 32;
        if (pending < 0) {
            pending = digit;
            continue;
        }
        uint32_t value = (uint32_t)(pending + digit * 95);
        bits |= value << bitCount;
        bitCount += ((value & 8191) > 832) ? 13 : 14;
        while (bitCount >= 8) {
            if (at >= expected) {
                return false;
            }
            out[at++] = (uint8_t)(bits & 255);
            bits >>= 8;
            bitCount -= 8;
        }
        pending = -1;
    }
    if (pending >= 0) {
        if (at >= expected) {
            return false;
        }
        out[at++] = (uint8_t)((bits | ((uint32_t)pending << bitCount)) & 255);
    }
    return at == expected;
}

static bool EnsureShaderBytes() {
    static int result = -1;
    if (result >= 0) {
        return result != 0;
    }
    bool ok =
        DecodeBase95(kShaderVSQuad95, kShaderVSQuadBytes, kShaderVSQuadSize) &&
        DecodeBase95(kShaderPSQuad95, kShaderPSQuadBytes, kShaderPSQuadSize) &&
        DecodeBase95(kShaderVSTri95, kShaderVSTriBytes, kShaderVSTriSize) &&
        DecodeBase95(kShaderPSTri95, kShaderPSTriBytes, kShaderPSTriSize);
    result = ok ? 1 : 0;
    if (!ok) {
        logf("paint/gpu: embedded shader bytecode is invalid");
    }
    return ok;
}

// Kinds, matching the switch in PSQuad.
enum : uint8_t {
    kQuadRect = 0,    // rounded rect fill; radius 0 is a plain rect
    kQuadBorder = 1,  // the same outline, `border` wide, inside the edge
    kQuadEllipse = 2, // fill
    kQuadEllipseB = 3,
    kQuadGlyph = 4,    // coverage out of the atlas
    kQuadImage = 5,    // premultiplied BGRA out of the bound image
    kQuadGradient = 6, // linear, between two points in DIP space
    kQuadSolid = 7     // no shaping at all; the cover pass of a path fill
};

// ─── instance and vertex data ────────────────────────────────────────────

struct Inst {
    float rect[4];
    float color[4];
    float misc[4];
    float clip[4];
    float uv[4];
    float color2[4];
};

struct TriVert {
    float x, y;
    float color[4];
    float clip[4];
};

static void SetColor(float* out, Rgba c) {
    out[0] = (float)c.r / 255.f;
    out[1] = (float)c.g / 255.f;
    out[2] = (float)c.b / 255.f;
    out[3] = (float)c.a / 255.f;
}

// ─── the glyph atlas ─────────────────────────────────────────────────────
//
// One RGBA texture, filled by a shelf allocator: DirectWrite's ClearType
// coverage occupies RGB, and a glyph goes on the current row until it will
// not fit, then a new row starts under the tallest of the last. Good enough
// for text that is nearly all one or two sizes, which is what a UI is. A full
// atlas is cleared and refilled rather than grown.

constexpr int kAtlasDim = 1024;

struct GlyphKey {
    void* face = nullptr;
    uint32_t glyph = 0;
    // The em size in 1/4 px, so 13.5 and 14 are different entries.
    uint32_t size4 = 0;
    // DirectWrite positions ClearType glyphs on thirds of a pixel.
    uint32_t phase3 = 0;
};

struct GlyphEntry {
    GlyphKey key;
    bool used = false;
    // Where it landed, and where it sits relative to the pen.
    int x = 0, y = 0, w = 0, h = 0;
    int bearingX = 0, bearingY = 0;
};

constexpr int kGlyphSlots = 4096; // power of two, open addressed

struct Atlas {
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    int penX = 0;
    int penY = 0;
    int rowH = 0;
    GlyphEntry slots[kGlyphSlots] = {};
};

static uint32_t GlyphHash(const GlyphKey& k) {
    uint64_t h = (uint64_t)(uintptr_t)k.face * 0x9e3779b97f4a7c15ull;
    h ^= (uint64_t)k.glyph * 0xc2b2ae3d27d4eb4full;
    h ^= (uint64_t)k.size4 * 0x165667b19e3779f9ull;
    h ^= (uint64_t)k.phase3 * 0x85ebca77c2b2ae63ull;
    h ^= h >> 29;
    return (uint32_t)(h & (kGlyphSlots - 1));
}

// ─── paths ───────────────────────────────────────────────────────────────

// Paint.h's `Path` is opaque, and the D2D backend defines it. This is the
// GPU backend's own; the entry points take and return the opaque one and
// cast, which is what P() below is for.
struct GpuPath {
    // Every contour flattened to a polyline, laid end to end, with `starts`
    // saying where each begins. Curves are flattened as they arrive, which is
    // what a GPU wants and what the D2D backend does not have to do.
    Vec<float> pts; // x, y pairs
    Vec<int> starts;
    bool winding = true;
    bool open = false;
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
};

// ─── the target ──────────────────────────────────────────────────────────

struct GpuTarget {
    HWND hwnd = nullptr;
    IDXGISwapChain1* swap = nullptr;
    ID3D11Texture2D* backTex = nullptr;
    ID3D11RenderTargetView* backRtv = nullptr;
    // The multisampled surface everything is drawn into, resolved into the
    // back buffer at the end of the frame. Null when the sample count is 1,
    // and then the back buffer is drawn into directly.
    ID3D11Texture2D* msaaTex = nullptr;
    ID3D11RenderTargetView* msaaRtv = nullptr;
    ID3D11Texture2D* dsTex = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    // The offscreen half: a plain texture plus the staging copy the pixels
    // are read back through.
    ID3D11Texture2D* offTex = nullptr;
    ID3D11RenderTargetView* offRtv = nullptr;
    ID3D11Texture2D* stage = nullptr;
    bool offscreen = false;
    int pxW = 0;
    int pxH = 0;
    int samples = 1;
    uint64_t generation = 0;
};

// ─── process-wide GPU state ──────────────────────────────────────────────

struct Gpu {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGIFactory2* factory = nullptr;
    IDWriteFactory* dwrite = nullptr;
    float textGamma[4] = {};
    float textContrast = 0.f;
    float clearTypeLevel = 1.f;
    bool textBgr = false;

    ID3D11VertexShader* vsQuad = nullptr;
    ID3D11PixelShader* psQuad = nullptr;
    ID3D11VertexShader* vsTri = nullptr;
    ID3D11PixelShader* psTri = nullptr;
    ID3D11InputLayout* triLayout = nullptr;

    ID3D11Buffer* cb = nullptr;
    ID3D11Buffer* instBuf = nullptr;
    ID3D11ShaderResourceView* instSrv = nullptr;
    int instCap = 0;
    ID3D11Buffer* triBuf = nullptr;
    int triCap = 0;

    ID3D11BlendState* blend = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    ID3D11RasterizerState* rasterMsaa = nullptr;
    ID3D11SamplerState* samp = nullptr;
    // Off; even-odd (invert); nonzero (two-sided wrap); cover-and-clear.
    ID3D11DepthStencilState* dsOff = nullptr;
    ID3D11DepthStencilState* dsEvenOdd = nullptr;
    ID3D11DepthStencilState* dsNonZero = nullptr;
    ID3D11DepthStencilState* dsCover = nullptr;

    Atlas atlas;
    // A 1x1 white texture, so the image slot is always bound to something.
    ID3D11ShaderResourceView* white = nullptr;

    bool ready = false;
};

static Gpu gGpu;

// DirectWrite's native compositor corrects coverage for the display gamma,
// enhanced contrast and foreground brightness. These ratios are the compact
// polynomial approximation published by Microsoft Terminal (MIT); doing the
// correction in the shader leaves the atlas independent of text colour.
static void InitTextParams(Gpu* g) {
    static const float ratios[13][4] = {
        {0.0000f / 4.f, 0.0000f / 4.f, 0.0000f / 4.f, 0.0000f / 4.f},
        {0.0166f / 4.f, -0.0807f / 4.f, 0.2227f / 4.f, -0.0751f / 4.f},
        {0.0350f / 4.f, -0.1760f / 4.f, 0.4325f / 4.f, -0.1370f / 4.f},
        {0.0543f / 4.f, -0.2821f / 4.f, 0.6302f / 4.f, -0.1876f / 4.f},
        {0.0739f / 4.f, -0.3963f / 4.f, 0.8167f / 4.f, -0.2287f / 4.f},
        {0.0933f / 4.f, -0.5161f / 4.f, 0.9926f / 4.f, -0.2616f / 4.f},
        {0.1121f / 4.f, -0.6395f / 4.f, 1.1588f / 4.f, -0.2877f / 4.f},
        {0.1300f / 4.f, -0.7649f / 4.f, 1.3159f / 4.f, -0.3080f / 4.f},
        {0.1469f / 4.f, -0.8911f / 4.f, 1.4644f / 4.f, -0.3234f / 4.f},
        {0.1627f / 4.f, -1.0170f / 4.f, 1.6051f / 4.f, -0.3347f / 4.f},
        {0.1773f / 4.f, -1.1420f / 4.f, 1.7385f / 4.f, -0.3426f / 4.f},
        {0.1908f / 4.f, -1.2652f / 4.f, 1.8650f / 4.f, -0.3476f / 4.f},
        {0.2031f / 4.f, -1.3864f / 4.f, 1.9851f / 4.f, -0.3501f / 4.f},
    };
    IDWriteRenderingParams* p = nullptr;
    float gamma = 1.f;
    if (g->dwrite && SUCCEEDED(g->dwrite->CreateRenderingParams(&p)) && p) {
        gamma = p->GetGamma();
        g->textContrast = p->GetEnhancedContrast();
        g->clearTypeLevel = p->GetClearTypeLevel();
        g->textBgr = p->GetPixelGeometry() == DWRITE_PIXEL_GEOMETRY_BGR;
        p->Release();
    }
    int ix = (int)floorf(gamma * 10.f + 0.5f) - 10;
    ix = std::max(0, std::min(ix, 12));
    const float norm13 = (65536.f / (255.f * 255.f)) * 4.f;
    const float norm24 = (256.f / 255.f) * 4.f;
    g->textGamma[0] = norm13 * ratios[ix][0];
    g->textGamma[1] = norm24 * ratios[ix][1];
    g->textGamma[2] = norm13 * ratios[ix][2];
    g->textGamma[3] = norm24 * ratios[ix][3];
}

constexpr int kD12FrameCount = 3;
constexpr int kD12ImageSlots = 128;
constexpr UINT64 kD12UploadBytes = 16ull * 1024 * 1024;

struct D12Frame {
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12Resource* upload = nullptr;
    uint8_t* mapped = nullptr;
    UINT64 uploadAt = 0;
    UINT64 fenceValue = 0;
    // Dedicated uploads for an image larger than the per-frame stream. They
    // stay alive until this frame's fence completes, then are released.
    Vec<ID3D12Resource*> textureUploads;
};

struct D12Pipelines {
    int samples = 0;
    ID3D12PipelineState* quad = nullptr;
    ID3D12PipelineState* cover = nullptr;
    ID3D12PipelineState* tri = nullptr;
    ID3D12PipelineState* evenOdd = nullptr;
    ID3D12PipelineState* nonZero = nullptr;
};

struct D12ImageSlot {
    uint64_t imageGeneration = 0;
    int frameIndex = 0;
    ID3D12Resource* tex = nullptr;
    int descriptor = -1;
    uint64_t usedInCommands = 0;
    // A final image release can happen while this texture is referenced by
    // the open command list. Zero the lookup identity immediately, then drop
    // the resource after the fence assigned at submission completes.
    UINT64 retireFence = 0;
    bool retireOnSubmit = false;
};

struct D12Gpu {
    ID3D12Device* dev = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGIFactory4* factory = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12RootSignature* root = nullptr;
    ID3D12DescriptorHeap* srvHeap = nullptr;
    UINT srvStep = 0;
    ID3D12Fence* fence = nullptr;
    HANDLE fenceEvent = nullptr;
    UINT64 nextFence = 1;
    ID3D12Resource* atlas = nullptr;
    D3D12_RESOURCE_STATES atlasState = D3D12_RESOURCE_STATE_COPY_DEST;
    D12Pipelines pipes[4] = {};
    D12ImageSlot images[kD12ImageSlots] = {};
    int imageNext = 0;
    uint64_t commandGeneration = 0;
    IDWriteFactory* dwrite = nullptr;
    bool ready = false;
};

struct D12Target {
    HWND hwnd = nullptr;
    IDXGISwapChain3* swap = nullptr;
    ID3D12DescriptorHeap* rtvHeap = nullptr;
    ID3D12DescriptorHeap* dsvHeap = nullptr;
    ID3D12Resource* back[kD12FrameCount] = {};
    ID3D12Resource* msaa = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* offTex = nullptr;
    ID3D12Resource* readback = nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT readbackLayout = {};
    UINT64 readbackBytes = 0;
    D12Frame frames[kD12FrameCount];
    int frameIx = 0;
    bool offscreen = false;
    bool clearStencil = true;
    int pxW = 0;
    int pxH = 0;
    int samples = 1;
    uint64_t generation = 0;
};

static D12Gpu gD12;
static uint64_t gDeviceGeneration = 1;
static int gPresentedFrames = 0;

// What is being accumulated right now. Painter order is preserved by
// flushing whenever the pipeline or a bound texture has to change, so the
// batch is only ever the run of primitives that can go out together.
struct Batch {
    Vec<Inst> insts;
    Vec<TriVert> tris;
    ID3D11ShaderResourceView* image = nullptr;
    int image12 = -1;
    void* target = nullptr;
    int pxW = 0;
    int pxH = 0;
    bool offscreen = false;
    // The content mask, as a stack. GPUI carries one per primitive rather
    // than setting a scissor, which is what lets a clip change cost nothing.
    Vec<float> clipStack; // x0, y0, x1, y1 per entry
    float clip[4] = {0, 0, 1e6f, 1e6f};
    FrameStats stats;
};

static Batch gB;
static FrameStats gLastStats;

static void RecoverDevice(PaintCtx* ctx, bool removed);

const FrameStats& LastFrameStats() {
    return gLastStats;
}

// ─── device setup ────────────────────────────────────────────────────────

static D3D12_HEAP_PROPERTIES D12Heap(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES h;
    memset(&h, 0, sizeof(h));
    h.Type = type;
    h.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    h.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    h.CreationNodeMask = 1;
    h.VisibleNodeMask = 1;
    return h;
}

static D3D12_RESOURCE_DESC D12Buffer(UINT64 bytes) {
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_UNKNOWN;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}

static D3D12_RESOURCE_DESC D12Texture(int w, int h, DXGI_FORMAT format,
                                      int samples, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = (UINT64)w;
    d.Height = (UINT)h;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = format;
    d.SampleDesc.Count = (UINT)samples;
    d.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    d.Flags = flags;
    return d;
}

static void D12Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* r,
                       D3D12_RESOURCE_STATES before,
                       D3D12_RESOURCE_STATES after) {
    if (!list || !r || before == after) {
        return;
    }
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    list->ResourceBarrier(1, &b);
}

static D3D12_CPU_DESCRIPTOR_HANDLE D12SrvCpu(int ix) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = gD12.srvHeap
                                        ->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)ix * gD12.srvStep;
    return h;
}

static D3D12_GPU_DESCRIPTOR_HANDLE D12SrvGpu(int ix) {
    D3D12_GPU_DESCRIPTOR_HANDLE h = gD12.srvHeap
                                        ->GetGPUDescriptorHandleForHeapStart();
    h.ptr += (UINT64)ix * gD12.srvStep;
    return h;
}

static int D12PipeIx(int samples) {
    return samples == 2 ? 1 : samples == 4 ? 2 : samples == 8 ? 3 : 0;
}

static D3D12_BLEND_DESC D12Blend() {
    D3D12_BLEND_DESC b;
    memset(&b, 0, sizeof(b));
    b.RenderTarget[0].BlendEnable = TRUE;
    b.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    b.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC1_COLOR;
    b.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    b.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    b.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC1_ALPHA;
    b.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    b.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return b;
}

static D3D12_RASTERIZER_DESC D12Raster(int samples) {
    D3D12_RASTERIZER_DESC r;
    memset(&r, 0, sizeof(r));
    r.FillMode = D3D12_FILL_MODE_SOLID;
    r.CullMode = D3D12_CULL_MODE_NONE;
    r.DepthClipEnable = TRUE;
    r.MultisampleEnable = samples > 1;
    return r;
}

static D3D12_DEPTH_STENCIL_DESC D12DepthOff() {
    D3D12_DEPTH_STENCIL_DESC d;
    memset(&d, 0, sizeof(d));
    d.DepthEnable = FALSE;
    d.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    d.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    d.StencilEnable = FALSE;
    d.StencilReadMask = 0xff;
    d.StencilWriteMask = 0xff;
    return d;
}

static D3D12_DEPTH_STENCILOP_DESC D12StencilOp(D3D12_STENCIL_OP pass,
                                               D3D12_COMPARISON_FUNC func) {
    D3D12_DEPTH_STENCILOP_DESC o;
    memset(&o, 0, sizeof(o));
    o.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    o.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    o.StencilPassOp = pass;
    o.StencilFunc = func;
    return o;
}

static bool D12MakePipelines(int samples) {
    D12Gpu* g = &gD12;
    D12Pipelines* p = &g->pipes[D12PipeIx(samples)];
    if (p->quad) {
        return true;
    }
    p->samples = samples;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d;
    memset(&d, 0, sizeof(d));
    d.pRootSignature = g->root;
    d.VS = {kShaderVSQuadBytes, (SIZE_T)kShaderVSQuadSize};
    d.PS = {kShaderPSQuadBytes, (SIZE_T)kShaderPSQuadSize};
    d.BlendState = D12Blend();
    d.SampleMask = UINT_MAX;
    d.RasterizerState = D12Raster(samples);
    d.DepthStencilState = D12DepthOff();
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    d.SampleDesc.Count = (UINT)samples;
    if (FAILED(g->dev->CreateGraphicsPipelineState(
            &d, __uuidof(ID3D12PipelineState), (void**)&p->quad))) {
        return false;
    }

    D3D12_DEPTH_STENCIL_DESC cover = D12DepthOff();
    cover.StencilEnable = TRUE;
    cover.FrontFace =
        D12StencilOp(D3D12_STENCIL_OP_ZERO, D3D12_COMPARISON_FUNC_NOT_EQUAL);
    cover.BackFace = cover.FrontFace;
    d.DepthStencilState = cover;
    if (FAILED(g->dev->CreateGraphicsPipelineState(
            &d, __uuidof(ID3D12PipelineState), (void**)&p->cover))) {
        return false;
    }

    D3D12_INPUT_ELEMENT_DESC input[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    d.InputLayout = {input, 3};
    d.VS = {kShaderVSTriBytes, (SIZE_T)kShaderVSTriSize};
    d.PS = {kShaderPSTriBytes, (SIZE_T)kShaderPSTriSize};
    d.DepthStencilState = D12DepthOff();
    if (FAILED(g->dev->CreateGraphicsPipelineState(
            &d, __uuidof(ID3D12PipelineState), (void**)&p->tri))) {
        return false;
    }

    d.PS = {};
    d.BlendState.RenderTarget[0].BlendEnable = FALSE;
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
    D3D12_DEPTH_STENCIL_DESC stencil = D12DepthOff();
    stencil.StencilEnable = TRUE;
    stencil.FrontFace =
        D12StencilOp(D3D12_STENCIL_OP_INVERT, D3D12_COMPARISON_FUNC_ALWAYS);
    stencil.BackFace = stencil.FrontFace;
    d.DepthStencilState = stencil;
    if (FAILED(g->dev->CreateGraphicsPipelineState(
            &d, __uuidof(ID3D12PipelineState), (void**)&p->evenOdd))) {
        return false;
    }
    stencil.FrontFace =
        D12StencilOp(D3D12_STENCIL_OP_INCR, D3D12_COMPARISON_FUNC_ALWAYS);
    stencil.BackFace =
        D12StencilOp(D3D12_STENCIL_OP_DECR, D3D12_COMPARISON_FUNC_ALWAYS);
    d.DepthStencilState = stencil;
    if (FAILED(g->dev->CreateGraphicsPipelineState(
            &d, __uuidof(ID3D12PipelineState), (void**)&p->nonZero))) {
        return false;
    }
    return true;
}

#if WIN_BACKEND_D3D12
static bool D12EnsureGpu(PaintApp* pa) {
    D12Gpu* g = &gD12;
    if (g->ready) {
        return true;
    }
    if (!pa || !EnsureShaderBytes()) {
        return false;
    }
    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&factory))) {
        return false;
    }
    IDXGIAdapter1* chosen = nullptr;
    for (UINT i = 0;; i++) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc = {};
        adapter->GetDesc1(&desc);
        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0,
                                        __uuidof(ID3D12Device), nullptr))) {
            chosen = adapter;
            break;
        }
        adapter->Release();
    }
    HRESULT hr =
        chosen ? D3D12CreateDevice(chosen, D3D_FEATURE_LEVEL_11_0,
                                   __uuidof(ID3D12Device), (void**)&g->dev)
               : E_FAIL;
    if (chosen) {
        chosen->Release();
    }
    if (FAILED(hr)) {
        IDXGIAdapter* warp = nullptr;
        if (SUCCEEDED(factory->EnumWarpAdapter(__uuidof(IDXGIAdapter),
                                               (void**)&warp))) {
            hr = D3D12CreateDevice(warp, D3D_FEATURE_LEVEL_11_0,
                                   __uuidof(ID3D12Device), (void**)&g->dev);
            warp->Release();
        }
    }
    if (FAILED(hr) || !g->dev) {
        factory->Release();
        logf("paint/d3d12: D3D12CreateDevice failed %08x", (unsigned)hr);
        return false;
    }
    g->factory = factory;
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(g->dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue),
                                          (void**)&g->queue))) {
        return false;
    }
    ID3D12CommandAllocator* bootstrap = nullptr;
    if (FAILED(g->dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              __uuidof(ID3D12CommandAllocator),
                                              (void**)&bootstrap)) ||
        FAILED(g->dev->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, bootstrap, nullptr,
            __uuidof(ID3D12GraphicsCommandList), (void**)&g->list))) {
        Rel(&bootstrap);
        return false;
    }
    g->list->Close();
    bootstrap->Release();

    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = 1 + kD12ImageSlots;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(g->dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap),
                                            (void**)&g->srvHeap))) {
        return false;
    }
    g->srvStep = g->dev->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    for (int i = 0; i < 2; i++) {
        ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[i].NumDescriptors = 1;
        ranges[i].BaseShaderRegister = (UINT)(i + 1);
        ranges[i].OffsetInDescriptorsFromTableStart = 0;
    }
    D3D12_ROOT_PARAMETER rp[4] = {};
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rp[0].Constants.ShaderRegister = 0;
    rp[0].Constants.Num32BitValues = 12;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rp[1].Descriptor.ShaderRegister = 0;
    rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    for (int i = 0; i < 2; i++) {
        rp[i + 2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rp[i + 2].DescriptorTable.NumDescriptorRanges = 1;
        rp[i + 2].DescriptorTable.pDescriptorRanges = &ranges[i];
        rp[i + 2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_STATIC_SAMPLER_DESC samp;
    memset(&samp, 0, sizeof(samp));
    samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samp.MaxLOD = D3D12_FLOAT32_MAX;
    samp.ShaderRegister = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rsd = {};
    rsd.NumParameters = 4;
    rsd.pParameters = rp;
    rsd.NumStaticSamplers = 1;
    rsd.pStaticSamplers = &samp;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* sig = nullptr;
    ID3DBlob* err = nullptr;
    hr = D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig,
                                     &err);
    if (FAILED(hr)) {
        if (err) {
            logf("paint/d3d12: root signature failed: %s",
                 Str((const char*)err->GetBufferPointer()));
            err->Release();
        }
        return false;
    }
    hr = g->dev->CreateRootSignature(
        0, sig->GetBufferPointer(), sig->GetBufferSize(),
        __uuidof(ID3D12RootSignature), (void**)&g->root);
    sig->Release();
    if (err) {
        err->Release();
    }
    if (FAILED(hr)) {
        return false;
    }
    D3D12_HEAP_PROPERTIES heap = D12Heap(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC atlas =
        D12Texture(kAtlasDim, kAtlasDim, DXGI_FORMAT_R8G8B8A8_UNORM, 1,
                   D3D12_RESOURCE_FLAG_NONE);
    if (FAILED(g->dev->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &atlas, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, __uuidof(ID3D12Resource), (void**)&g->atlas))) {
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    g->dev->CreateShaderResourceView(g->atlas, &sv, D12SrvCpu(0));
    if (FAILED(g->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                   __uuidof(ID3D12Fence), (void**)&g->fence))) {
        return false;
    }
    g->fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g->fenceEvent) {
        return false;
    }
    g->dwrite = (IDWriteFactory*)PaintSharedDwrite(pa);
    gGpu.dwrite = g->dwrite;
    InitTextParams(&gGpu);
    g->ready = true;
    return true;
}
#else
static bool D12EnsureGpu(PaintApp*) {
    return false;
}
#endif

static bool MakeAtlas(Gpu* g) {
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = kAtlasDim;
    td.Height = kAtlasDim;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(g->dev->CreateTexture2D(&td, nullptr, &g->atlas.tex))) {
        return false;
    }
    return SUCCEEDED(
        g->dev->CreateShaderResourceView(g->atlas.tex, nullptr, &g->atlas.srv));
}

static bool MakeWhite(Gpu* g) {
    uint32_t px = 0xffffffff;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = 1;
    td.Height = 1;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = &px;
    sd.SysMemPitch = 4;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g->dev->CreateTexture2D(&td, &sd, &tex))) {
        return false;
    }
    HRESULT hr = g->dev->CreateShaderResourceView(tex, nullptr, &g->white);
    tex->Release();
    return SUCCEEDED(hr);
}

// The four stencil states the path fill needs. The colour write is turned
// off for the two that only mark coverage; the cover state tests what they
// wrote and zeroes it on the way past, so the buffer is clean for the next
// path without a clear.
static bool MakeStencilStates(Gpu* g) {
    D3D11_DEPTH_STENCIL_DESC d;
    memset(&d, 0, sizeof(d));
    d.DepthEnable = FALSE;
    d.StencilEnable = FALSE;
    if (FAILED(g->dev->CreateDepthStencilState(&d, &g->dsOff))) {
        return false;
    }

    d.StencilEnable = TRUE;
    d.StencilReadMask = 0xff;
    d.StencilWriteMask = 0xff;
    D3D11_DEPTH_STENCILOP_DESC inv;
    memset(&inv, 0, sizeof(inv));
    inv.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    inv.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    inv.StencilPassOp = D3D11_STENCIL_OP_INVERT;
    inv.StencilFunc = D3D11_COMPARISON_ALWAYS;
    d.FrontFace = inv;
    d.BackFace = inv;
    if (FAILED(g->dev->CreateDepthStencilState(&d, &g->dsEvenOdd))) {
        return false;
    }

    D3D11_DEPTH_STENCILOP_DESC incr = inv, decr = inv;
    incr.StencilPassOp = D3D11_STENCIL_OP_INCR;
    decr.StencilPassOp = D3D11_STENCIL_OP_DECR;
    d.FrontFace = incr;
    d.BackFace = decr;
    if (FAILED(g->dev->CreateDepthStencilState(&d, &g->dsNonZero))) {
        return false;
    }

    D3D11_DEPTH_STENCILOP_DESC cover;
    memset(&cover, 0, sizeof(cover));
    cover.StencilFailOp = D3D11_STENCIL_OP_ZERO;
    cover.StencilDepthFailOp = D3D11_STENCIL_OP_ZERO;
    cover.StencilPassOp = D3D11_STENCIL_OP_ZERO;
    cover.StencilFunc = D3D11_COMPARISON_NOT_EQUAL;
    d.FrontFace = cover;
    d.BackFace = cover;
    return SUCCEEDED(g->dev->CreateDepthStencilState(&d, &g->dsCover));
}

static bool EnsureGpu(PaintApp* pa) {
    if (PaintD3d12On()) {
        return D12EnsureGpu(pa);
    }
    Gpu* g = &gGpu;
    if (g->ready) {
        return true;
    }
    g->dev = (ID3D11Device*)PaintSharedD3dDevice(pa);
    g->factory = (IDXGIFactory2*)PaintSharedDxgiFactory(pa);
    g->dwrite = (IDWriteFactory*)PaintSharedDwrite(pa);
    if (!g->dev || !g->factory || !g->dwrite) {
        return false;
    }
    InitTextParams(g);
    g->dev->GetImmediateContext(&g->ctx);
    if (!g->ctx) {
        return false;
    }

    bool ok =
        EnsureShaderBytes() &&
        SUCCEEDED(g->dev->CreateVertexShader(kShaderVSQuadBytes,
                                             (SIZE_T)kShaderVSQuadSize, nullptr,
                                             &g->vsQuad)) &&
        SUCCEEDED(g->dev->CreatePixelShader(kShaderPSQuadBytes,
                                            (SIZE_T)kShaderPSQuadSize, nullptr,
                                            &g->psQuad)) &&
        SUCCEEDED(g->dev->CreateVertexShader(
            kShaderVSTriBytes, (SIZE_T)kShaderVSTriSize, nullptr, &g->vsTri)) &&
        SUCCEEDED(g->dev->CreatePixelShader(
            kShaderPSTriBytes, (SIZE_T)kShaderPSTriSize, nullptr, &g->psTri));
    if (ok) {
        D3D11_INPUT_ELEMENT_DESC el[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
        };
        ok = SUCCEEDED(g->dev->CreateInputLayout(
            el, 3, kShaderVSTriBytes, (SIZE_T)kShaderVSTriSize, &g->triLayout));
    }
    if (!ok) {
        return false;
    }

    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = 48;
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g->dev->CreateBuffer(&cbd, nullptr, &g->cb))) {
        return false;
    }

    // Premultiplied source. Target 1 carries the per-channel destination
    // attenuation; for non-text pixels all four channels contain alpha, so
    // this is the ordinary premultiplied blend.
    D3D11_BLEND_DESC bd;
    memset(&bd, 0, sizeof(bd));
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC1_COLOR;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC1_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(g->dev->CreateBlendState(&bd, &g->blend))) {
        return false;
    }

    D3D11_RASTERIZER_DESC rd;
    memset(&rd, 0, sizeof(rd));
    rd.FillMode = D3D11_FILL_SOLID;
    // No culling: a path's stencil fan is deliberately wound both ways, and
    // the nonzero rule reads the winding out of the front/back stencil ops
    // rather than out of a cull.
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(g->dev->CreateRasterizerState(&rd, &g->raster))) {
        return false;
    }
    rd.MultisampleEnable = TRUE;
    if (FAILED(g->dev->CreateRasterizerState(&rd, &g->rasterMsaa))) {
        return false;
    }

    D3D11_SAMPLER_DESC sd;
    memset(&sd, 0, sizeof(sd));
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(g->dev->CreateSamplerState(&sd, &g->samp))) {
        return false;
    }

    if (!MakeStencilStates(g) || !MakeAtlas(g) || !MakeWhite(g)) {
        return false;
    }
    g->ready = true;
    return true;
}

// The instance buffer, grown to hold `n`. A StructuredBuffer rather than a
// vertex buffer so the vertex shader can index it by SV_InstanceID and the
// quad needs no vertex data at all.
static bool EnsureInstBuf(Gpu* g, int n) {
    if (g->instCap >= n && g->instBuf) {
        return true;
    }
    int cap = g->instCap > 0 ? g->instCap : 4096;
    while (cap < n) {
        cap *= 2;
    }
    Rel(&g->instSrv);
    Rel(&g->instBuf);
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(cap * sizeof(Inst));
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(Inst);
    if (FAILED(g->dev->CreateBuffer(&bd, nullptr, &g->instBuf))) {
        return false;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = DXGI_FORMAT_UNKNOWN;
    sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sv.Buffer.NumElements = (UINT)cap;
    if (FAILED(g->dev
                   ->CreateShaderResourceView(g->instBuf, &sv, &g->instSrv))) {
        return false;
    }
    g->instCap = cap;
    return true;
}

static bool EnsureTriBuf(Gpu* g, int n) {
    if (g->triCap >= n && g->triBuf) {
        return true;
    }
    int cap = g->triCap > 0 ? g->triCap : 4096;
    while (cap < n) {
        cap *= 2;
    }
    Rel(&g->triBuf);
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(cap * sizeof(TriVert));
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g->dev->CreateBuffer(&bd, nullptr, &g->triBuf))) {
        return false;
    }
    g->triCap = cap;
    return true;
}

static void D12Wait(UINT64 value) {
    if (!value || !gD12.fence || !gD12.fenceEvent ||
        gD12.fence->GetCompletedValue() >= value) {
        return;
    }
    if (SUCCEEDED(gD12.fence->SetEventOnCompletion(value, gD12.fenceEvent))) {
        WaitForSingleObject(gD12.fenceEvent, INFINITE);
    }
}

static void D12CollectImageEvictions() {
    UINT64 completed = gD12.fence ? gD12.fence->GetCompletedValue() : 0;
    for (int i = 0; i < kD12ImageSlots; i++) {
        D12ImageSlot* slot = &gD12.images[i];
        if (slot->retireFence && completed >= slot->retireFence) {
            Rel(&slot->tex);
            slot->retireFence = 0;
            slot->usedInCommands = 0;
            slot->frameIndex = 0;
        }
    }
}

static void D12WaitTarget(D12Target* t) {
    if (!t) {
        return;
    }
    for (int i = 0; i < kD12FrameCount; i++) {
        D12Wait(t->frames[i].fenceValue);
        t->frames[i].fenceValue = 0;
    }
}

static bool D12MakeFrames(D12Target* t) {
    D3D12_HEAP_PROPERTIES uploadHeap = D12Heap(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC upload = D12Buffer(kD12UploadBytes);
    for (int i = 0; i < kD12FrameCount; i++) {
        D12Frame* f = &t->frames[i];
        if (FAILED(gD12.dev->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                __uuidof(ID3D12CommandAllocator), (void**)&f->allocator)) ||
            FAILED(gD12.dev->CreateCommittedResource(
                &uploadHeap, D3D12_HEAP_FLAG_NONE, &upload,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                __uuidof(ID3D12Resource), (void**)&f->upload)) ||
            FAILED(f->upload->Map(0, nullptr, (void**)&f->mapped))) {
            return false;
        }
    }
    return true;
}

static void D12FreeSurfaces(D12Target* t, bool wait = true) {
    if (!t) {
        return;
    }
    if (wait) {
        D12WaitTarget(t);
    }
    for (int i = 0; i < kD12FrameCount; i++) {
        Rel(&t->back[i]);
    }
    Rel(&t->msaa);
    Rel(&t->depth);
    Rel(&t->offTex);
    Rel(&t->readback);
    Rel(&t->rtvHeap);
    Rel(&t->dsvHeap);
}

static void D12FreeTarget(D12Target* t, bool wait = true) {
    if (!t) {
        return;
    }
    D12FreeSurfaces(t, wait);
    for (int i = 0; i < kD12FrameCount; i++) {
        if (t->frames[i].upload && t->frames[i].mapped) {
            t->frames[i].upload->Unmap(0, nullptr);
        }
        for (ID3D12Resource* upload : t->frames[i].textureUploads) {
            Rel(&upload);
        }
        Rel(&t->frames[i].upload);
        Rel(&t->frames[i].allocator);
    }
    Rel(&t->swap);
    delete t;
}

static D3D12_CPU_DESCRIPTOR_HANDLE D12Rtv(D12Target* t, int ix) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = t->rtvHeap
                                        ->GetCPUDescriptorHandleForHeapStart();
    h.ptr += (SIZE_T)ix * gD12.dev->GetDescriptorHandleIncrementSize(
                              D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    return h;
}

static D3D12_CPU_DESCRIPTOR_HANDLE D12Dsv(D12Target* t) {
    return t->dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

static bool D12MakeSurfaceHeaps(D12Target* t) {
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 4;
    if (FAILED(gD12.dev->CreateDescriptorHeap(
            &hd, __uuidof(ID3D12DescriptorHeap), (void**)&t->rtvHeap))) {
        return false;
    }
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    hd.NumDescriptors = 1;
    return SUCCEEDED(gD12.dev->CreateDescriptorHeap(
        &hd, __uuidof(ID3D12DescriptorHeap), (void**)&t->dsvHeap));
}

static int D12SupportedSamples(int wanted) {
    int n = wanted;
    while (n > 1) {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q = {};
        q.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        q.SampleCount = (UINT)n;
        if (SUCCEEDED(gD12.dev->CheckFeatureSupport(
                D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q))) &&
            q.NumQualityLevels > 0) {
            return n;
        }
        n /= 2;
    }
    return 1;
}

static bool D12MakeWindowSurfaces(D12Target* t) {
    if (!D12MakeSurfaceHeaps(t)) {
        return false;
    }
    for (int i = 0; i < kD12FrameCount; i++) {
        if (FAILED(t->swap->GetBuffer((UINT)i, __uuidof(ID3D12Resource),
                                      (void**)&t->back[i]))) {
            return false;
        }
        gD12.dev->CreateRenderTargetView(t->back[i], nullptr, D12Rtv(t, i));
    }
    t->samples = D12SupportedSamples(t->samples);
    D3D12_HEAP_PROPERTIES heap = D12Heap(D3D12_HEAP_TYPE_DEFAULT);
    if (t->samples > 1) {
        D3D12_RESOURCE_DESC color =
            D12Texture(t->pxW, t->pxH, DXGI_FORMAT_B8G8R8A8_UNORM, t->samples,
                       D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        D3D12_CLEAR_VALUE cv = {};
        cv.Format = color.Format;
        if (FAILED(gD12.dev->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &color,
                D3D12_RESOURCE_STATE_RENDER_TARGET, &cv,
                __uuidof(ID3D12Resource), (void**)&t->msaa))) {
            return false;
        }
        D3D12_RENDER_TARGET_VIEW_DESC rv = {};
        rv.Format = color.Format;
        rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
        gD12.dev->CreateRenderTargetView(t->msaa, &rv, D12Rtv(t, 3));
    }
    D3D12_RESOURCE_DESC depth =
        D12Texture(t->pxW, t->pxH, DXGI_FORMAT_D24_UNORM_S8_UINT, t->samples,
                   D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE dv = {};
    dv.Format = depth.Format;
    dv.DepthStencil.Depth = 1.f;
    if (FAILED(gD12.dev->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &depth,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &dv, __uuidof(ID3D12Resource),
            (void**)&t->depth))) {
        return false;
    }
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
    dsv.Format = depth.Format;
    dsv.ViewDimension = t->samples > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS
                                       : D3D12_DSV_DIMENSION_TEXTURE2D;
    gD12.dev->CreateDepthStencilView(t->depth, &dsv, D12Dsv(t));
    t->clearStencil = true;
    return D12MakePipelines(t->samples);
}

static bool D12MakeOffscreenSurfaces(D12Target* t) {
    if (!D12MakeSurfaceHeaps(t)) {
        return false;
    }
    D3D12_HEAP_PROPERTIES heap = D12Heap(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC color =
        D12Texture(t->pxW, t->pxH, DXGI_FORMAT_B8G8R8A8_UNORM, 1,
                   D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    D3D12_CLEAR_VALUE cv = {};
    cv.Format = color.Format;
    if (FAILED(gD12.dev->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &color,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &cv, __uuidof(ID3D12Resource),
            (void**)&t->offTex))) {
        return false;
    }
    gD12.dev->CreateRenderTargetView(t->offTex, nullptr, D12Rtv(t, 3));
    UINT rows = 0;
    UINT64 rowBytes = 0;
    gD12.dev->GetCopyableFootprints(&color, 0, 1, 0, &t->readbackLayout, &rows,
                                    &rowBytes, &t->readbackBytes);
    D3D12_HEAP_PROPERTIES readHeap = D12Heap(D3D12_HEAP_TYPE_READBACK);
    D3D12_RESOURCE_DESC read = D12Buffer(t->readbackBytes);
    if (FAILED(gD12.dev->CreateCommittedResource(
            &readHeap, D3D12_HEAP_FLAG_NONE, &read,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
            (void**)&t->readback))) {
        return false;
    }
    t->samples = 1;
    D3D12_RESOURCE_DESC depth =
        D12Texture(t->pxW, t->pxH, DXGI_FORMAT_D24_UNORM_S8_UINT, 1,
                   D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
    D3D12_CLEAR_VALUE dv = {};
    dv.Format = depth.Format;
    dv.DepthStencil.Depth = 1.f;
    if (FAILED(gD12.dev->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &depth,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &dv, __uuidof(ID3D12Resource),
            (void**)&t->depth))) {
        return false;
    }
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
    dsv.Format = depth.Format;
    dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    gD12.dev->CreateDepthStencilView(t->depth, &dsv, D12Dsv(t));
    t->clearStencil = true;
    return D12MakePipelines(1);
}

static bool D12BeginCommands(D12Target* t, bool continuing = false) {
    D12CollectImageEvictions();
    gD12.commandGeneration++;
    if (gD12.commandGeneration == 0) {
        gD12.commandGeneration++;
    }
    t->frameIx = t->offscreen ? 0 : (int)t->swap->GetCurrentBackBufferIndex();
    D12Frame* f = &t->frames[t->frameIx];
    D12Wait(f->fenceValue);
    f->fenceValue = 0;
    for (ID3D12Resource* upload : f->textureUploads) {
        Rel(&upload);
    }
    f->textureUploads.len = 0;
    f->uploadAt = 0;
    if (FAILED(f->allocator->Reset()) ||
        FAILED(gD12.list->Reset(f->allocator, nullptr))) {
        return false;
    }
    ID3D12DescriptorHeap* heaps[] = {gD12.srvHeap};
    gD12.list->SetDescriptorHeaps(1, heaps);
    gD12.list->SetGraphicsRootSignature(gD12.root);
    float constants[12] = {
        (float)t->pxW,
        (float)t->pxH,
        0,
        0,
        gGpu.textGamma[0],
        gGpu.textGamma[1],
        gGpu.textGamma[2],
        gGpu.textGamma[3],
        gGpu.textContrast,
        gGpu.clearTypeLevel,
        0,
        0,
    };
    gD12.list->SetGraphicsRoot32BitConstants(0, 12, constants, 0);
    gD12.list->SetGraphicsRootDescriptorTable(2, D12SrvGpu(0));
    gD12.list->SetGraphicsRootDescriptorTable(3, D12SrvGpu(0));
    D3D12_VIEWPORT vp = {};
    vp.Width = (float)t->pxW;
    vp.Height = (float)t->pxH;
    vp.MaxDepth = 1.f;
    gD12.list->RSSetViewports(1, &vp);
    D3D12_RECT scissor = {0, 0, t->pxW, t->pxH};
    gD12.list->RSSetScissorRects(1, &scissor);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        t->offscreen ? D12Rtv(t, 3)
                     : (t->msaa ? D12Rtv(t, 3) : D12Rtv(t, t->frameIx));
    if (!continuing && !t->offscreen && !t->msaa) {
        D12Barrier(gD12.list, t->back[t->frameIx], D3D12_RESOURCE_STATE_PRESENT,
                   D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = D12Dsv(t);
    gD12.list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    if (t->clearStencil) {
        gD12.list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_STENCIL, 1.f, 0,
                                         0, nullptr);
        t->clearStencil = false;
    }
    if (gD12.atlasState == D3D12_RESOURCE_STATE_COPY_DEST) {
        D12Barrier(gD12.list, gD12.atlas, gD12.atlasState,
                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        gD12.atlasState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    gB.target = t;
    gB.image = nullptr;
    gB.image12 = -1;
    gB.pxW = t->pxW;
    gB.pxH = t->pxH;
    gB.offscreen = t->offscreen;
    gB.insts.len = 0;
    gB.tris.len = 0;
    if (!continuing) {
        gB.clipStack.len = 0;
        gB.clip[0] = 0;
        gB.clip[1] = 0;
        gB.clip[2] = (float)t->pxW;
        gB.clip[3] = (float)t->pxH;
        gB.stats = FrameStats{};
    }
    return true;
}

static bool D12FinishCommands(D12Target* t, bool wait) {
    D12Frame* f = &t->frames[t->frameIx];
    if (FAILED(gD12.list->Close())) {
        return false;
    }
    ID3D12CommandList* lists[] = {gD12.list};
    gD12.queue->ExecuteCommandLists(1, lists);
    UINT64 value = gD12.nextFence++;
    if (FAILED(gD12.queue->Signal(gD12.fence, value))) {
        return false;
    }
    f->fenceValue = value;
    for (int i = 0; i < kD12ImageSlots; i++) {
        D12ImageSlot* slot = &gD12.images[i];
        if (slot->retireOnSubmit) {
            slot->retireOnSubmit = false;
            slot->retireFence = value;
        }
    }
    if (wait) {
        D12Wait(value);
        f->fenceValue = 0;
        D12CollectImageEvictions();
    }
    return true;
}

static void* D12Upload(UINT64 bytes, UINT64 align,
                       D3D12_GPU_VIRTUAL_ADDRESS* gpu) {
    D12Target* t = (D12Target*)gB.target;
    if (!t || !bytes) {
        return nullptr;
    }
    D12Frame* f = &t->frames[t->frameIx];
    UINT64 at = (f->uploadAt + align - 1) & ~(align - 1);
    if (at + bytes > kD12UploadBytes) {
        logf("paint/d3d12: frame upload exhausted (%llu bytes)",
             (unsigned long long)(at + bytes));
        return nullptr;
    }
    f->uploadAt = at + bytes;
    if (gpu) {
        *gpu = f->upload->GetGPUVirtualAddress() + at;
    }
    return f->mapped + at;
}

static bool D12UploadTexture(ID3D12Resource* texture,
                             D3D12_RESOURCE_STATES* state, DXGI_FORMAT format,
                             int x, int y, int w, int h, int bytesPerPixel,
                             const uint8_t* pixels, int srcPitch) {
    D12Target* t = (D12Target*)gB.target;
    if (!t || !texture || !state || !pixels || w <= 0 || h <= 0) {
        return false;
    }
    UINT rowPitch = (UINT)((w * bytesPerPixel + 255) & ~255);
    UINT64 bytes = (UINT64)rowPitch * (UINT64)h;
    D12Frame* f = &t->frames[t->frameIx];
    UINT64 at = (f->uploadAt + 511) & ~511ull;
    ID3D12Resource* source = f->upload;
    UINT64 offset = at;
    uint8_t* dst = nullptr;
    if (at + bytes <= kD12UploadBytes) {
        f->uploadAt = at + bytes;
        dst = f->mapped + at;
    } else {
        D3D12_HEAP_PROPERTIES heap = D12Heap(D3D12_HEAP_TYPE_UPLOAD);
        D3D12_RESOURCE_DESC desc = D12Buffer(bytes);
        if (FAILED(gD12.dev->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                __uuidof(ID3D12Resource), (void**)&source)) ||
            FAILED(source->Map(0, nullptr, (void**)&dst))) {
            Rel(&source);
            return false;
        }
        offset = 0;
        if (!VecAppend(f->textureUploads, source)) {
            source->Unmap(0, nullptr);
            Rel(&source);
            return false;
        }
    }
    for (int row = 0; row < h; row++) {
        memcpy(dst + (size_t)row * rowPitch,
               pixels + (size_t)row * (size_t)srcPitch,
               (size_t)w * (size_t)bytesPerPixel);
    }
    if (source != f->upload) {
        source->Unmap(0, nullptr);
    }
    D12Barrier(gD12.list, texture, *state, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = source;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = offset;
    src.PlacedFootprint.Footprint.Format = format;
    src.PlacedFootprint.Footprint.Width = (UINT)w;
    src.PlacedFootprint.Footprint.Height = (UINT)h;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = rowPitch;
    D3D12_TEXTURE_COPY_LOCATION dest = {};
    dest.pResource = texture;
    dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    gD12.list->CopyTextureRegion(&dest, (UINT)x, (UINT)y, 0, &src, nullptr);
    D12Barrier(gD12.list, texture, D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    *state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    return true;
}

enum TriMode : uint8_t {
    kTriColor,
    kTriEvenOdd,
    kTriNonZero,
};

static void D12SubmitQuads(bool cover) {
    if (gB.insts.len == 0 || !gB.target) {
        return;
    }
    D12Target* t = (D12Target*)gB.target;
    UINT64 bytes = (UINT64)gB.insts.len * sizeof(Inst);
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
    void* dst = D12Upload(bytes, 16, &gpu);
    if (!dst) {
        gB.insts.len = 0;
        return;
    }
    memcpy(dst, gB.insts.els, (size_t)bytes);
    D12Pipelines* p = &gD12.pipes[D12PipeIx(t->samples)];
    gD12.list->SetPipelineState(cover ? p->cover : p->quad);
    gD12.list->SetGraphicsRootShaderResourceView(1, gpu);
    gD12.list->SetGraphicsRootDescriptorTable(2, D12SrvGpu(0));
    gD12.list->SetGraphicsRootDescriptorTable(
        3, D12SrvGpu(gB.image12 >= 0 ? gB.image12 : 0));
    gD12.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    gD12.list->OMSetStencilRef(0);
    gD12.list->DrawInstanced(4, (UINT)gB.insts.len, 0, 0);
    gB.stats.instances += gB.insts.len;
    gB.stats.draws++;
    gB.insts.len = 0;
}

static void D12SubmitTris(D3D12_PRIMITIVE_TOPOLOGY topology, TriMode mode) {
    if (gB.tris.len == 0 || !gB.target) {
        return;
    }
    D12Target* t = (D12Target*)gB.target;
    UINT64 bytes = (UINT64)gB.tris.len * sizeof(TriVert);
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
    void* dst = D12Upload(bytes, 16, &gpu);
    if (!dst) {
        gB.tris.len = 0;
        return;
    }
    memcpy(dst, gB.tris.els, (size_t)bytes);
    D12Pipelines* p = &gD12.pipes[D12PipeIx(t->samples)];
    ID3D12PipelineState* state = mode == kTriEvenOdd   ? p->evenOdd
                                 : mode == kTriNonZero ? p->nonZero
                                                       : p->tri;
    gD12.list->SetPipelineState(state);
    D3D12_VERTEX_BUFFER_VIEW vb = {};
    vb.BufferLocation = gpu;
    vb.SizeInBytes = (UINT)bytes;
    vb.StrideInBytes = sizeof(TriVert);
    gD12.list->IASetVertexBuffers(0, 1, &vb);
    gD12.list->IASetPrimitiveTopology(topology);
    gD12.list->OMSetStencilRef(0);
    gD12.list->DrawInstanced((UINT)gB.tris.len, 1, 0, 0);
    gB.stats.pathTriangles += gB.tris.len / 3;
    gB.stats.draws++;
    gB.tris.len = 0;
}

// ─── drawing ─────────────────────────────────────────────────────────────

static GpuTarget* T11(PaintCtx* ctx) {
    return ctx ? (GpuTarget*)ctx->rt : nullptr;
}

static void SetViewportCb(Gpu* g, float w, float h) {
    D3D11_MAPPED_SUBRESOURCE m = {};
    if (SUCCEEDED(g->ctx->Map(g->cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        float v[12] = {
            w,
            h,
            0,
            0,
            g->textGamma[0],
            g->textGamma[1],
            g->textGamma[2],
            g->textGamma[3],
            g->textContrast,
            g->clearTypeLevel,
            0,
            0,
        };
        memcpy(m.pData, v, sizeof(v));
        g->ctx->Unmap(g->cb, 0);
    }
}

static void FlushQuads();
static void FlushTris(D3D_PRIMITIVE_TOPOLOGY topo, TriMode mode);

// Everything queued, in order. Called before anything that would change what
// a draw would do, and at the end of the frame.
static void Flush() {
    FlushQuads();
    FlushTris(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, kTriColor);
}

static void FlushQuads() {
    Gpu* g = &gGpu;
    if (gB.insts.len == 0 || !gB.target) {
        return;
    }
    if (PaintD3d12On()) {
        D12SubmitQuads(false);
        return;
    }
    if (!EnsureInstBuf(g, gB.insts.len)) {
        gB.insts.len = 0;
        return;
    }
    D3D11_MAPPED_SUBRESOURCE m = {};
    if (FAILED(g->ctx->Map(g->instBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        gB.insts.len = 0;
        return;
    }
    memcpy(m.pData, gB.insts.els, (size_t)gB.insts.len * sizeof(Inst));
    g->ctx->Unmap(g->instBuf, 0);

    ID3D11ShaderResourceView* srvs[3] = {g->instSrv, g->atlas.srv,
                                         gB.image ? gB.image : g->white};
    g->ctx->VSSetShader(g->vsQuad, nullptr, 0);
    g->ctx->PSSetShader(g->psQuad, nullptr, 0);
    g->ctx->VSSetShaderResources(0, 3, srvs);
    g->ctx->PSSetShaderResources(0, 3, srvs);
    g->ctx->IASetInputLayout(nullptr);
    g->ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    g->ctx->OMSetDepthStencilState(g->dsOff, 0);
    g->ctx->DrawInstanced(4, (UINT)gB.insts.len, 0, 0);

    gB.stats.instances += gB.insts.len;
    gB.stats.draws++;
    gB.insts.len = 0;
}

static void FlushTris(D3D_PRIMITIVE_TOPOLOGY topo, TriMode mode) {
    Gpu* g = &gGpu;
    if (gB.tris.len == 0 || !gB.target) {
        return;
    }
    if (PaintD3d12On()) {
        D12SubmitTris(topo, mode);
        return;
    }
    if (!EnsureTriBuf(g, gB.tris.len)) {
        gB.tris.len = 0;
        return;
    }
    D3D11_MAPPED_SUBRESOURCE m = {};
    if (FAILED(g->ctx->Map(g->triBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        gB.tris.len = 0;
        return;
    }
    memcpy(m.pData, gB.tris.els, (size_t)gB.tris.len * sizeof(TriVert));
    g->ctx->Unmap(g->triBuf, 0);

    UINT stride = sizeof(TriVert);
    UINT off = 0;
    g->ctx->VSSetShader(g->vsTri, nullptr, 0);
    bool colorWrite = mode == kTriColor;
    ID3D11DepthStencilState* ds = mode == kTriEvenOdd   ? g->dsEvenOdd
                                  : mode == kTriNonZero ? g->dsNonZero
                                                        : g->dsOff;
    g->ctx->PSSetShader(colorWrite ? g->psTri : nullptr, nullptr, 0);
    g->ctx->IASetInputLayout(g->triLayout);
    g->ctx->IASetVertexBuffers(0, 1, &g->triBuf, &stride, &off);
    g->ctx->IASetPrimitiveTopology(topo);
    g->ctx->OMSetDepthStencilState(ds, 0);
    g->ctx->Draw((UINT)gB.tris.len, 0);

    gB.stats.pathTriangles += gB.tris.len / 3;
    gB.stats.draws++;
    gB.tris.len = 0;
    g->ctx->OMSetDepthStencilState(g->dsOff, 0);
    if (!colorWrite) {
        g->ctx->PSSetShader(g->psTri, nullptr, 0);
    }
}

// Painter order is the whole contract: the tree draws back to front, so
// whichever of the two buffers is holding something has to go out before the
// other one starts filling. Every entry point declares which pipeline it is
// about to use.
static void EnsureQuadPhase() {
    if (gB.tris.len > 0) {
        FlushTris(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, kTriColor);
    }
}

static void EnsureTriPhase() {
    if (gB.insts.len > 0) {
        FlushQuads();
    }
}

static void Push(const Inst& i) {
    EnsureQuadPhase();
    VecAppend(gB.insts, i);
}

// The one place a primitive is made, so the content mask and the opacity in
// force are applied in exactly one place, the way GPUI applies them as the
// primitive is handed to the backend.
static void Quad(PaintCtx* ctx, int kind, float x, float y, float w, float h,
                 Rgba c, float radius = 0, float border = 0) {
    if (!gB.target || w <= 0 || h <= 0) {
        return;
    }
    c = PaintFade(ctx, c);
    if (c.a == 0) {
        return;
    }
    Inst i = {};
    i.rect[0] = x;
    i.rect[1] = y;
    i.rect[2] = w;
    i.rect[3] = h;
    SetColor(i.color, c);
    i.misc[0] = radius;
    i.misc[1] = border;
    i.misc[2] = (float)kind;
    memcpy(i.clip, gB.clip, sizeof(i.clip));
    Push(i);
}

static void TriVertex(float x, float y, Rgba c) {
    EnsureTriPhase();
    TriVert v = {};
    v.x = x;
    v.y = y;
    SetColor(v.color, c);
    memcpy(v.clip, gB.clip, sizeof(v.clip));
    VecAppend(gB.tris, v);
}

// ─── target lifecycle ────────────────────────────────────────────────────

static void FreeSurfaces(GpuTarget* t) {
    Rel(&t->backRtv);
    Rel(&t->backTex);
    Rel(&t->msaaRtv);
    Rel(&t->msaaTex);
    Rel(&t->dsv);
    Rel(&t->dsTex);
}

void PaintTargetFree(PaintCtx* ctx) {
    if (PaintD3d12On()) {
        D12Target* t = ctx ? (D12Target*)ctx->rt : nullptr;
        D12FreeTarget(t, !t || t->generation == gDeviceGeneration);
        if (ctx) {
            ctx->rt = nullptr;
        }
        if (gB.target == t) {
            gB.target = nullptr;
        }
        return;
    }
    GpuTarget* t = T11(ctx);
    if (!t) {
        return;
    }
    FreeSurfaces(t);
    Rel(&t->offRtv);
    Rel(&t->offTex);
    Rel(&t->stage);
    Rel(&t->swap);
    if (gB.target == t) {
        gB.target = nullptr;
    }
    delete t;
    ctx->rt = nullptr;
}

// The multisampled colour surface and the stencil beside it. Both are
// remade on a resize, and both are skipped at one sample, where the back
// buffer is drawn into directly.
static bool MakeRenderSurfaces(Gpu* g, GpuTarget* t) {
    Rel(&t->msaaRtv);
    Rel(&t->msaaTex);
    Rel(&t->dsv);
    Rel(&t->dsTex);

    UINT quality = 0;
    int samples = t->samples;
    while (samples > 1) {
        if (SUCCEEDED(g->dev->CheckMultisampleQualityLevels(
                DXGI_FORMAT_B8G8R8A8_UNORM, (UINT)samples, &quality)) &&
            quality > 0) {
            break;
        }
        samples /= 2;
    }
    t->samples = samples < 1 ? 1 : samples;

    if (t->samples > 1) {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = (UINT)t->pxW;
        td.Height = (UINT)t->pxH;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = (UINT)t->samples;
        td.SampleDesc.Quality = 0;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(g->dev->CreateTexture2D(&td, nullptr, &t->msaaTex)) ||
            FAILED(g->dev->CreateRenderTargetView(t->msaaTex, nullptr,
                                                  &t->msaaRtv))) {
            return false;
        }
    }

    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width = (UINT)t->pxW;
    dd.Height = (UINT)t->pxH;
    dd.MipLevels = 1;
    dd.ArraySize = 1;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = (UINT)t->samples;
    dd.SampleDesc.Quality = 0;
    dd.Usage = D3D11_USAGE_DEFAULT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(g->dev->CreateTexture2D(&dd, nullptr, &t->dsTex)) ||
        FAILED(g->dev->CreateDepthStencilView(t->dsTex, nullptr, &t->dsv))) {
        return false;
    }
    // The only clear it gets: from here on the cover pass keeps it at zero.
    g->ctx->ClearDepthStencilView(t->dsv, D3D11_CLEAR_STENCIL, 1.f, 0);
    return true;
}

static bool BindBack(Gpu* g, GpuTarget* t) {
    Rel(&t->backRtv);
    Rel(&t->backTex);
    if (FAILED(t->swap->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                  (void**)&t->backTex))) {
        return false;
    }
    return SUCCEEDED(
        g->dev->CreateRenderTargetView(t->backTex, nullptr, &t->backRtv));
}

// Bind whichever surface this frame draws into, set the viewport and the
// pipeline state that does not change inside a frame.
static void BeginFrameState(Gpu* g, GpuTarget* t) {
    ID3D11RenderTargetView* rtv =
        t->offscreen ? t->offRtv : (t->msaaRtv ? t->msaaRtv : t->backRtv);
    g->ctx->OMSetRenderTargets(1, &rtv, t->dsv);
    D3D11_VIEWPORT vp = {};
    vp.Width = (float)t->pxW;
    vp.Height = (float)t->pxH;
    vp.MaxDepth = 1.f;
    g->ctx->RSSetViewports(1, &vp);
    g->ctx->RSSetState(t->samples > 1 ? g->rasterMsaa : g->raster);
    float bf[4] = {0, 0, 0, 0};
    g->ctx->OMSetBlendState(g->blend, bf, 0xffffffff);
    g->ctx->OMSetDepthStencilState(g->dsOff, 0);
    g->ctx->PSSetSamplers(0, 1, &g->samp);
    g->ctx->VSSetConstantBuffers(0, 1, &g->cb);
    g->ctx->PSSetConstantBuffers(0, 1, &g->cb);
    SetViewportCb(g, (float)t->pxW, (float)t->pxH);
    // The stencil is not cleared per frame. It is cleared once, when the
    // surface is made, and every path's cover pass zeroes what its own
    // stencil pass wrote — the cover quad is the path's bounding box, which
    // is by definition everything the fan could have touched. A full-surface
    // D24S8 clear every frame is pure bandwidth, and on a light scene it was
    // most of what the frame cost.
    gB.target = t;
    gB.image = nullptr;
    gB.image12 = -1;
    gB.pxW = t->pxW;
    gB.pxH = t->pxH;
    gB.offscreen = t->offscreen;
    gB.insts.len = 0;
    gB.tris.len = 0;
    gB.clipStack.len = 0;
    gB.clip[0] = 0;
    gB.clip[1] = 0;
    gB.clip[2] = (float)t->pxW;
    gB.clip[3] = (float)t->pxH;
    gB.stats = FrameStats{};
}

static bool D12PaintTargetBegin(PaintCtx* ctx, void* native, int pxW, int pxH) {
    if (!ctx || !ctx->pa || !D12EnsureGpu(ctx->pa)) {
        return false;
    }
    HWND hwnd = (HWND)native;
    if (!hwnd || pxW <= 0 || pxH <= 0) {
        return false;
    }
    D12Target* t = (D12Target*)ctx->rt;
    if (t && t->generation != gDeviceGeneration) {
        scene::Reset(ctx);
        D12FreeTarget(t, false);
        ctx->rt = nullptr;
        t = nullptr;
    }
    if (t && (t->hwnd != hwnd || t->offscreen)) {
        D12FreeTarget(t);
        ctx->rt = nullptr;
        t = nullptr;
    }
    if (!t) {
        t = new D12Target();
        t->hwnd = hwnd;
        t->pxW = pxW;
        t->pxH = pxH;
        t->samples = PaintGpuSamples();
        t->generation = gDeviceGeneration;
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = (UINT)pxW;
        desc.Height = (UINT)pxH;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = kD12FrameCount;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        desc.Scaling = DXGI_SCALING_NONE;
        IDXGISwapChain1* swap1 = nullptr;
        HRESULT hr = gD12.factory->CreateSwapChainForHwnd(
            gD12.queue, hwnd, &desc, nullptr, nullptr, &swap1);
        if (SUCCEEDED(hr)) {
            hr = swap1->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&t
                                                                      ->swap);
            swap1->Release();
        }
        if (FAILED(hr) || !t->swap || !D12MakeFrames(t) ||
            !D12MakeWindowSurfaces(t)) {
            D12FreeTarget(t);
            return false;
        }
        gD12.factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        ctx->rt = (PaintTarget*)t;
    } else if (t->pxW != pxW || t->pxH != pxH) {
        D12FreeSurfaces(t);
        t->pxW = pxW;
        t->pxH = pxH;
        scene::Invalidate(ctx);
        if (FAILED(t->swap->ResizeBuffers(kD12FrameCount, (UINT)pxW, (UINT)pxH,
                                          DXGI_FORMAT_B8G8R8A8_UNORM, 0)) ||
            !D12MakeWindowSurfaces(t)) {
            D12FreeTarget(t);
            ctx->rt = nullptr;
            return false;
        }
    }
    return D12BeginCommands(t);
}

static bool D12PaintTargetEnd(PaintCtx* ctx) {
    D12Target* t = ctx ? (D12Target*)ctx->rt : nullptr;
    if (!t || t->offscreen) {
        return false;
    }
    Flush();
    bool skip = scene::SkipPresent(ctx);
    ID3D12Resource* back = t->back[t->frameIx];
    if (t->msaa) {
        if (!skip) {
            D12Barrier(gD12.list, t->msaa, D3D12_RESOURCE_STATE_RENDER_TARGET,
                       D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            D12Barrier(gD12.list, back, D3D12_RESOURCE_STATE_PRESENT,
                       D3D12_RESOURCE_STATE_RESOLVE_DEST);
            gD12.list->ResolveSubresource(back, 0, t->msaa, 0,
                                          DXGI_FORMAT_B8G8R8A8_UNORM);
            D12Barrier(gD12.list, back, D3D12_RESOURCE_STATE_RESOLVE_DEST,
                       D3D12_RESOURCE_STATE_PRESENT);
            D12Barrier(gD12.list, t->msaa, D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
                       D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
    } else {
        D12Barrier(gD12.list, back, D3D12_RESOURCE_STATE_RENDER_TARGET,
                   D3D12_RESOURCE_STATE_PRESENT);
    }
    gLastStats = gB.stats;
    gB.target = nullptr;
    if (!D12FinishCommands(t, false)) {
        return false;
    }
    if (skip) {
        return true;
    }
    HRESULT hr = t->swap->Present(0, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        RecoverDevice(ctx, true);
        return false;
    }
    if (SUCCEEDED(hr)) {
        int every = WinPaintOptionsGet().gpuResetEvery;
        if (every > 0 && ++gPresentedFrames >= every) {
            RecoverDevice(ctx, false);
        }
    }
    return SUCCEEDED(hr);
}

static bool D12PaintTargetBeginOffscreen(PaintCtx* ctx, int pxW, int pxH) {
    if (!ctx || !ctx->pa || !D12EnsureGpu(ctx->pa) || pxW <= 0 || pxH <= 0) {
        return false;
    }
    if (ctx->rt) {
        D12FreeTarget((D12Target*)ctx->rt);
        ctx->rt = nullptr;
    }
    D12Target* t = new D12Target();
    t->offscreen = true;
    t->pxW = pxW;
    t->pxH = pxH;
    t->generation = gDeviceGeneration;
    if (!D12MakeFrames(t) || !D12MakeOffscreenSurfaces(t)) {
        D12FreeTarget(t);
        return false;
    }
    ctx->rt = (PaintTarget*)t;
    if (!D12BeginCommands(t)) {
        D12FreeTarget(t);
        ctx->rt = nullptr;
        return false;
    }
    float clear[4] = {0, 0, 0, 0};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = D12Rtv(t, 3);
    gD12.list->ClearRenderTargetView(rtv, clear, 0, nullptr);
    return true;
}

static bool D12PaintTargetEndOffscreen(PaintCtx* ctx, uint8_t* outBgra) {
    D12Target* t = ctx ? (D12Target*)ctx->rt : nullptr;
    if (!t || !t->offscreen) {
        return false;
    }
    Flush();
    D12Barrier(gD12.list, t->offTex, D3D12_RESOURCE_STATE_RENDER_TARGET,
               D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = t->offTex;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = t->readback;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = t->readbackLayout;
    gD12.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    gB.target = nullptr;
    bool ok = D12FinishCommands(t, true);
    if (ok && outBgra) {
        uint8_t* mapped = nullptr;
        D3D12_RANGE read = {0, (SIZE_T)t->readbackBytes};
        if (SUCCEEDED(t->readback->Map(0, &read, (void**)&mapped))) {
            for (int y = 0; y < t->pxH; y++) {
                memcpy(outBgra + (size_t)y * (size_t)t->pxW * 4,
                       mapped + (size_t)y * t->readbackLayout.Footprint
                                                .RowPitch,
                       (size_t)t->pxW * 4);
            }
            D3D12_RANGE wrote = {0, 0};
            t->readback->Unmap(0, &wrote);
        } else {
            ok = false;
        }
    }
    D12FreeTarget(t);
    ctx->rt = nullptr;
    return ok;
}

bool PaintTargetBegin(PaintCtx* ctx, void* native, int pxW, int pxH) {
    if (PaintD3d12On()) {
        return D12PaintTargetBegin(ctx, native, pxW, pxH);
    }
    if (!ctx || !ctx->pa || !EnsureGpu(ctx->pa)) {
        return false;
    }
    HWND hwnd = (HWND)native;
    if (!hwnd || pxW <= 0 || pxH <= 0) {
        return false;
    }
    Gpu* g = &gGpu;
    GpuTarget* t = T11(ctx);
    if (t && t->generation != gDeviceGeneration) {
        scene::Reset(ctx);
        gpuw::PaintTargetFree(ctx);
        t = nullptr;
    }
    if (t && (t->hwnd != hwnd || t->offscreen)) {
        gpuw::PaintTargetFree(ctx);
        t = nullptr;
    }
    if (!t) {
        t = new GpuTarget();
        t->hwnd = hwnd;
        t->pxW = pxW;
        t->pxH = pxH;
        t->samples = PaintGpuSamples();
        t->generation = gDeviceGeneration;
        // The same chain the D2D path presents through — three buffers,
        // flip-sequential — so the two are compared on the same terms and a
        // screenshot still comes off the redirection surface.
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        desc.Width = (UINT)pxW;
        desc.Height = (UINT)pxH;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 3;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        desc.Scaling = DXGI_SCALING_NONE;
        if (FAILED(g->factory->CreateSwapChainForHwnd(
                g->dev, hwnd, &desc, nullptr, nullptr, &t->swap))) {
            delete t;
            return false;
        }
        g->factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        if (!BindBack(g, t) || !MakeRenderSurfaces(g, t)) {
            FreeSurfaces(t);
            Rel(&t->swap);
            delete t;
            return false;
        }
        ctx->rt = (PaintTarget*)t;
    } else if (t->pxW != pxW || t->pxH != pxH) {
        FreeSurfaces(t);
        t->pxW = pxW;
        t->pxH = pxH;
        // New surfaces with nothing in them: the multisampled one a partial
        // redraw would have built on is gone with the rest.
        scene::Invalidate(ctx);
        if (FAILED(t->swap->ResizeBuffers(0, (UINT)pxW, (UINT)pxH,
                                          DXGI_FORMAT_UNKNOWN, 0)) ||
            !BindBack(g, t) || !MakeRenderSurfaces(g, t)) {
            gpuw::PaintTargetFree(ctx);
            return false;
        }
    }
    BeginFrameState(g, T11(ctx));
    return true;
}

bool PaintTargetEnd(PaintCtx* ctx) {
    if (PaintD3d12On()) {
        return D12PaintTargetEnd(ctx);
    }
    GpuTarget* t = T11(ctx);
    if (!t) {
        return false;
    }
    Flush();
    Gpu* g = &gGpu;
    if (t->msaaTex && t->backTex) {
        g->ctx->ResolveSubresource(t->backTex, 0, t->msaaTex, 0,
                                   DXGI_FORMAT_B8G8R8A8_UNORM);
    }
    gLastStats = gB.stats;
    gB.target = nullptr;
    // A frame the scene found identical to the last one is not presented.
    // The multisampled surface it would have resolved still holds the frame
    // before it, which is what is already on screen.
    if (scene::SkipPresent(ctx)) {
        return true;
    }
    // Sync interval 0, the way the D2D path presents, so the comparison is
    // between the drawing and nothing else.
    HRESULT hr = t->swap->Present(0, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        RecoverDevice(ctx, true);
        return false;
    }
    int every = WinPaintOptionsGet().gpuResetEvery;
    if (every > 0 && ++gPresentedFrames >= every) {
        RecoverDevice(ctx, false);
    }
    return SUCCEEDED(hr);
}

bool PaintTargetBeginOffscreen(PaintCtx* ctx, int pxW, int pxH) {
    if (PaintD3d12On()) {
        return D12PaintTargetBeginOffscreen(ctx, pxW, pxH);
    }
    if (!ctx || !ctx->pa || !EnsureGpu(ctx->pa) || pxW <= 0 || pxH <= 0) {
        return false;
    }
    Gpu* g = &gGpu;
    gpuw::PaintTargetFree(ctx);
    auto* t = new GpuTarget();
    t->offscreen = true;
    t->pxW = pxW;
    t->pxH = pxH;
    // One sample: the pixels are read straight back, so there is nothing to
    // resolve them into.
    t->samples = 1;
    t->generation = gDeviceGeneration;
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)pxW;
    td.Height = (UINT)pxH;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(g->dev->CreateTexture2D(&td, nullptr, &t->offTex)) ||
        FAILED(g->dev
                   ->CreateRenderTargetView(t->offTex, nullptr, &t->offRtv))) {
        Rel(&t->offRtv);
        Rel(&t->offTex);
        delete t;
        return false;
    }
    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(g->dev->CreateTexture2D(&td, nullptr, &t->stage))) {
        Rel(&t->offRtv);
        Rel(&t->offTex);
        delete t;
        return false;
    }
    if (!MakeRenderSurfaces(g, t)) {
        Rel(&t->stage);
        Rel(&t->offRtv);
        Rel(&t->offTex);
        delete t;
        return false;
    }
    ctx->rt = (PaintTarget*)t;
    BeginFrameState(g, t);
    float clear[4] = {0, 0, 0, 0};
    g->ctx->ClearRenderTargetView(t->offRtv, clear);
    return true;
}

bool PaintTargetEndOffscreen(PaintCtx* ctx, uint8_t* outBgra) {
    if (PaintD3d12On()) {
        return D12PaintTargetEndOffscreen(ctx, outBgra);
    }
    GpuTarget* t = T11(ctx);
    if (!t || !t->offscreen) {
        return false;
    }
    Flush();
    Gpu* g = &gGpu;
    g->ctx->CopyResource(t->stage, t->offTex);
    if (outBgra) {
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (SUCCEEDED(g->ctx->Map(t->stage, 0, D3D11_MAP_READ, 0, &m))) {
            for (int y = 0; y < t->pxH; y++) {
                memcpy(outBgra + (size_t)y * (size_t)t->pxW * 4,
                       (const uint8_t*)m.pData + (size_t)y * m.RowPitch,
                       (size_t)t->pxW * 4);
            }
            g->ctx->Unmap(t->stage, 0);
        }
    }
    gB.target = nullptr;
    gpuw::PaintTargetFree(ctx);
    return true;
}

// ─── canvas ──────────────────────────────────────────────────────────────

void CanvasClear(PaintCtx* ctx, Rgba c) {
    if (!gB.target) {
        return;
    }
    // A clear only reaches the whole surface when nothing is clipping it; a
    // clip in force makes this an ordinary filled rect, the way cairo's
    // paint-under-clip is.
    bool clipped = gB.clipStack.len > 0;
    if (!clipped) {
        Flush();
        Rgba f = PaintFade(ctx, c);
        float col[4] = {(float)f.r / 255.f, (float)f.g / 255.f,
                        (float)f.b / 255.f, (float)f.a / 255.f};
        if (PaintD3d12On()) {
            D12Target* t = (D12Target*)gB.target;
            D3D12_CPU_DESCRIPTOR_HANDLE rtv =
                t->offscreen ? D12Rtv(t, 3)
                             : (t->msaa ? D12Rtv(t, 3) : D12Rtv(t, t->frameIx));
            gD12.list->ClearRenderTargetView(rtv, col, 0, nullptr);
            return;
        }
        GpuTarget* t = (GpuTarget*)gB.target;
        ID3D11RenderTargetView* rtv =
            t->offscreen ? t->offRtv : (t->msaaRtv ? t->msaaRtv : t->backRtv);
        gGpu.ctx->ClearRenderTargetView(rtv, col);
        return;
    }
    Quad(ctx, kQuadRect, 0, 0, (float)gB.pxW, (float)gB.pxH, c);
}

void CanvasFillRect(PaintCtx* ctx, float x, float y, float w, float h, Rgba c) {
    Quad(ctx, kQuadRect, x, y, w, h, c);
}

void CanvasFillRound(PaintCtx* ctx, float x, float y, float w, float h, float r,
                     Rgba c) {
    Quad(ctx, kQuadRect, x, y, w, h, c, r);
}

static void StrokeSegment(PaintCtx* ctx, float x1, float y1, float x2, float y2,
                          float wdt, Rgba c);

// Walk a polyline with the same on/off pattern CanvasLine uses. `phase`
// is how far into the current dash the previous segment ended, so corners
// of a rounded rect keep one pattern.
static void DashFeed(PaintCtx* ctx, float x1, float y1, float x2, float y2,
                     float stroke, Rgba c, float on, float off, float* phase) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-6f) {
        return;
    }
    dx /= len;
    dy /= len;
    float at = 0;
    float p = *phase;
    while (at < len) {
        bool drawing = p < on;
        float slot = drawing ? on - p : off - (p - on);
        if (slot < 1e-6f) {
            p = drawing ? on : 0;
            continue;
        }
        float take = slot;
        if (take > len - at) {
            take = len - at;
        }
        if (drawing) {
            StrokeSegment(ctx, x1 + dx * at, y1 + dy * at,
                          x1 + dx * (at + take), y1 + dy * (at + take), stroke,
                          c);
        }
        at += take;
        p += take;
        float period = on + off;
        if (period > 0 && p >= period) {
            p -= period;
        }
    }
    *phase = p;
}

void CanvasStrokeRound(PaintCtx* ctx, float x, float y, float w, float h,
                       float r, float stroke, Rgba c, const float* dash) {
    if (stroke <= 0 || w <= 0 || h <= 0) {
        return;
    }
    if (!dash) {
        Quad(ctx, kQuadBorder, x, y, w, h, c, r, stroke);
        return;
    }
    c = PaintFade(ctx, c);
    if (c.a == 0) {
        return;
    }
    float on = dash[0] * stroke;
    float off = dash[1] * stroke;
    if (on <= 0 || off <= 0) {
        Quad(ctx, kQuadBorder, x, y, w, h, c, r, stroke);
        return;
    }
    // Inset by half the stroke, matching D2D's centred outline.
    float half = stroke * 0.5f;
    x += half;
    y += half;
    w -= stroke;
    h -= stroke;
    if (w <= 0 || h <= 0) {
        return;
    }
    float rmax = (w < h ? w : h) * 0.5f;
    if (r > rmax) {
        r = rmax;
    }
    float phase = 0;
    if (r <= 0) {
        DashFeed(ctx, x, y, x + w, y, stroke, c, on, off, &phase);
        DashFeed(ctx, x + w, y, x + w, y + h, stroke, c, on, off, &phase);
        DashFeed(ctx, x + w, y + h, x, y + h, stroke, c, on, off, &phase);
        DashFeed(ctx, x, y + h, x, y, stroke, c, on, off, &phase);
        return;
    }
    const int kSeg = 8;
    auto arc = [&](float cx, float cy, float a0, float a1) {
        for (int i = 0; i < kSeg; i++) {
            float t0 = a0 + (a1 - a0) * (float)i / kSeg;
            float t1 = a0 + (a1 - a0) * (float)(i + 1) / kSeg;
            DashFeed(ctx, cx + cosf(t0) * r, cy + sinf(t0) * r,
                     cx + cosf(t1) * r, cy + sinf(t1) * r, stroke, c, on, off,
                     &phase);
        }
    };
    DashFeed(ctx, x + r, y, x + w - r, y, stroke, c, on, off, &phase);
    arc(x + w - r, y + r, -kPi / 2, 0);
    DashFeed(ctx, x + w, y + r, x + w, y + h - r, stroke, c, on, off, &phase);
    arc(x + w - r, y + h - r, 0, kPi / 2);
    DashFeed(ctx, x + w - r, y + h, x + r, y + h, stroke, c, on, off, &phase);
    arc(x + r, y + h - r, kPi / 2, kPi);
    DashFeed(ctx, x, y + h - r, x, y + r, stroke, c, on, off, &phase);
    arc(x + r, y + r, kPi, 3 * kPi / 2);
}

// A segment as a quad, which is what the triangle pipeline is for: the quad
// pipeline only draws boxes that are square with the screen.
static void StrokeSegment(PaintCtx* ctx, float x1, float y1, float x2, float y2,
                          float wdt, Rgba c) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-6f) {
        return;
    }
    float nx = -dy / len * wdt * 0.5f;
    float ny = dx / len * wdt * 0.5f;
    (void)ctx;
    TriVertex(x1 + nx, y1 + ny, c);
    TriVertex(x2 + nx, y2 + ny, c);
    TriVertex(x2 - nx, y2 - ny, c);
    TriVertex(x1 + nx, y1 + ny, c);
    TriVertex(x2 - nx, y2 - ny, c);
    TriVertex(x1 - nx, y1 - ny, c);
}

// A disc, as the fan a round cap or a round join is. Twelve segments is past
// the point where another one shows at the widths this tree strokes at.
static void StrokeDisc(float cx, float cy, float r, Rgba c) {
    const int kSeg = 12;
    for (int i = 0; i < kSeg; i++) {
        float a0 = (float)i / kSeg * 2.f * kPi;
        float a1 = (float)(i + 1) / kSeg * 2.f * kPi;
        TriVertex(cx, cy, c);
        TriVertex(cx + cosf(a0) * r, cy + sinf(a0) * r, c);
        TriVertex(cx + cosf(a1) * r, cy + sinf(a1) * r, c);
    }
}

void CanvasLine(PaintCtx* ctx, float x1, float y1, float x2, float y2,
                float stroke, Rgba c, const float* dash) {
    if (!gB.target || stroke <= 0) {
        return;
    }
    c = PaintFade(ctx, c);
    if (c.a == 0) {
        return;
    }
    if (!dash) {
        StrokeSegment(ctx, x1, y1, x2, y2, stroke, c);
        return;
    }
    // The pattern is in stroke widths, the way D2D measures it. Cut the
    // segment up on the CPU, which is what a dash is.
    float on = dash[0] * stroke;
    float off = dash[1] * stroke;
    if (on <= 0 || off < 0) {
        StrokeSegment(ctx, x1, y1, x2, y2, stroke, c);
        return;
    }
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-6f) {
        return;
    }
    dx /= len;
    dy /= len;
    for (float at = 0; at < len;) {
        float end = at + on;
        if (end > len) {
            end = len;
        }
        StrokeSegment(ctx, x1 + dx * at, y1 + dy * at, x1 + dx * end,
                      y1 + dy * end, stroke, c);
        at = end + off;
    }
}

void CanvasEllipse(PaintCtx* ctx, float cx, float cy, float rx, float ry,
                   float stroke, Rgba c) {
    if (rx <= 0 || ry <= 0) {
        return;
    }
    if (stroke > 0) {
        Quad(ctx, kQuadEllipseB, cx - rx, cy - ry, rx * 2, ry * 2, c, 0,
             stroke);
    } else {
        Quad(ctx, kQuadEllipse, cx - rx, cy - ry, rx * 2, ry * 2, c);
    }
}

void CanvasPushClip(PaintCtx* ctx, float x, float y, float w, float h) {
    (void)ctx;
    if (!gB.target) {
        return;
    }
    // Nothing is flushed: the mask rides on the instance, so a clip change
    // costs four floats and never breaks a batch. That is the whole reason
    // GPUI carries a content mask per primitive.
    for (int i = 0; i < 4; i++) {
        VecAppend(gB.clipStack, gB.clip[i]);
    }
    float x0 = x > gB.clip[0] ? x : gB.clip[0];
    float y0 = y > gB.clip[1] ? y : gB.clip[1];
    float x1 = (x + w) < gB.clip[2] ? (x + w) : gB.clip[2];
    float y1 = (y + h) < gB.clip[3] ? (y + h) : gB.clip[3];
    gB.clip[0] = x0;
    gB.clip[1] = y0;
    gB.clip[2] = x1 > x0 ? x1 : x0;
    gB.clip[3] = y1 > y0 ? y1 : y0;
}

void CanvasPopClip(PaintCtx* ctx) {
    (void)ctx;
    if (gB.clipStack.len < 4) {
        return;
    }
    for (int i = 0; i < 4; i++) {
        gB.clip[3 - i] = gB.clipStack[gB.clipStack.len - 1 - i];
    }
    gB.clipStack.len -= 4;
}

// ─── paths ───────────────────────────────────────────────────────────────

static GpuPath* P(Path* p) {
    return (GpuPath*)p;
}

Path* PathNew(PaintCtx* ctx, bool winding) {
    if (!ctx) {
        return nullptr;
    }
    auto* p = new GpuPath();
    p->winding = winding;
    return (Path*)p;
}

void PathFree(Path* path) {
    delete P(path);
}

static void PathPoint(GpuPath* p, float x, float y) {
    VecAppend(p->pts, x);
    VecAppend(p->pts, y);
    if (x < p->minX) {
        p->minX = x;
    }
    if (y < p->minY) {
        p->minY = y;
    }
    if (x > p->maxX) {
        p->maxX = x;
    }
    if (y > p->maxY) {
        p->maxY = y;
    }
}

static void MoveTo(GpuPath* p, float x, float y) {
    VecAppend(p->starts, p->pts.len / 2);
    PathPoint(p, x, y);
    p->open = true;
}

void PathMoveTo(Path* path, float x, float y) {
    if (path) {
        MoveTo(P(path), x, y);
    }
}

void PathLineTo(Path* path, float x, float y) {
    GpuPath* p = P(path);
    if (!p) {
        return;
    }
    if (!p->open) {
        MoveTo(p, x, y);
        return;
    }
    PathPoint(p, x, y);
}

void PathCubicTo(Path* path, float x1, float y1, float x2, float y2, float x,
                 float y) {
    GpuPath* p = P(path);
    if (!p) {
        return;
    }
    if (!p->open) {
        MoveTo(p, x, y);
        return;
    }
    float x0 = p->pts[p->pts.len - 2];
    float y0 = p->pts[p->pts.len - 1];
    // A fixed subdivision rather than an adaptive one. The curves here are
    // icon outlines and chart shoulders a few tens of pixels across, where
    // sixteen segments is already under a pixel of error.
    const int kSeg = 16;
    for (int i = 1; i <= kSeg; i++) {
        float t = (float)i / kSeg;
        float u = 1.f - t;
        float bx = u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 +
                   t * t * t * x;
        float by = u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 +
                   t * t * t * y;
        PathPoint(p, bx, by);
    }
}

void PathArcTo(Path* path, float cx, float cy, float r, float a0, float a1,
               bool clockwise) {
    GpuPath* p = P(path);
    if (!p) {
        return;
    }
    float sweep = a1 - a0;
    if (clockwise && sweep < 0) {
        sweep += 2.f * kPi;
    }
    if (!clockwise && sweep > 0) {
        sweep -= 2.f * kPi;
    }
    // One segment per six degrees, and never fewer than two.
    int n = (int)(fabsf(sweep) / (kPi / 30.f)) + 2;
    if (n > 256) {
        n = 256;
    }
    for (int i = 0; i <= n; i++) {
        float a = a0 + sweep * ((float)i / (float)n);
        float x = cx + cosf(a) * r;
        float y = cy + sinf(a) * r;
        if (i == 0 && !p->open) {
            MoveTo(p, x, y);
        } else {
            PathPoint(p, x, y);
        }
    }
    p->open = true;
}

void PathClose(Path* path) {
    GpuPath* p = P(path);
    if (!p || !p->open) {
        return;
    }
    // A contour is closed by the fill rule, not by a repeated point: the fan
    // below always joins the last point back to the first.
    p->open = false;
}

static int ContourEnd(const GpuPath* p, int ci) {
    return ci + 1 < p->starts.len ? p->starts[ci + 1] : p->pts.len / 2;
}

// The stencil pass: for every contour, the fan (first, i, i+1). It may
// self-overlap and wind either way — that is what the stencil op is counting.
static void PathStencil(PaintCtx* ctx, GpuPath* p, float dx, float dy) {
    (void)ctx;
    Rgba none = {};
    for (int ci = 0; ci < p->starts.len; ci++) {
        int a = p->starts[ci];
        int b = ContourEnd(p, ci);
        if (b - a < 3) {
            continue;
        }
        float ax = p->pts[a * 2] + dx;
        float ay = p->pts[a * 2 + 1] + dy;
        for (int i = a + 1; i + 1 < b; i++) {
            TriVertex(ax, ay, none);
            TriVertex(p->pts[i * 2] + dx, p->pts[i * 2 + 1] + dy, none);
            TriVertex(p->pts[(i + 1) * 2] + dx, p->pts[(i + 1) * 2 + 1] + dy,
                      none);
        }
    }
}

static void PathCover(PaintCtx* ctx, GpuPath* p, const Inst& proto, float dx,
                      float dy) {
    Gpu* g = &gGpu;
    // The bounding box, one pixel out so an antialiased edge is not clipped
    // by its own cover quad.
    // PathFillWith flushed before the stencil pass, so this is the only
    // instance in the buffer and SV_InstanceID will read it at 0 — D3D11 does
    // not fold StartInstanceLocation into that id.
    Inst i = proto;
    i.rect[0] = p->minX + dx - 1;
    i.rect[1] = p->minY + dy - 1;
    i.rect[2] = (p->maxX - p->minX) + 2;
    i.rect[3] = (p->maxY - p->minY) + 2;
    memcpy(i.clip, gB.clip, sizeof(i.clip));
    gB.insts.len = 0;
    VecAppend(gB.insts, i);
    (void)ctx;

    if (PaintD3d12On()) {
        D12SubmitQuads(true);
        return;
    }

    // Straight out rather than through FlushQuads: this one draw needs the
    // stencil test, and the state goes back afterwards.
    if (!EnsureInstBuf(g, gB.insts.len)) {
        gB.insts.len = 0;
        return;
    }
    D3D11_MAPPED_SUBRESOURCE m = {};
    if (FAILED(g->ctx->Map(g->instBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        gB.insts.len = 0;
        return;
    }
    memcpy(m.pData, gB.insts.els, (size_t)gB.insts.len * sizeof(Inst));
    g->ctx->Unmap(g->instBuf, 0);
    ID3D11ShaderResourceView* srvs[3] = {g->instSrv, g->atlas.srv, g->white};
    g->ctx->VSSetShader(g->vsQuad, nullptr, 0);
    g->ctx->PSSetShader(g->psQuad, nullptr, 0);
    g->ctx->VSSetShaderResources(0, 3, srvs);
    g->ctx->PSSetShaderResources(0, 3, srvs);
    g->ctx->IASetInputLayout(nullptr);
    g->ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    g->ctx->OMSetDepthStencilState(g->dsCover, 0);
    g->ctx->DrawInstanced(4, 1, 0, 0);
    g->ctx->OMSetDepthStencilState(g->dsOff, 0);
    gB.stats.instances++;
    gB.stats.draws++;
    gB.insts.len = 0;
}

static void PathFillWith(PaintCtx* ctx, Path* path, const Inst& proto, float dx,
                         float dy) {
    GpuPath* p = P(path);
    if (!gB.target || !p || p->pts.len < 6) {
        return;
    }
    Flush();
    PathStencil(ctx, p, dx, dy);
    FlushTris(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
              p->winding ? kTriNonZero : kTriEvenOdd);
    PathCover(ctx, p, proto, dx, dy);
}

// Nothing to do: a GpuPath is already the flattened contours the stencil
// pass draws, and it was built once. What the D2D backend gains here the GPU
// one gets by construction.
void PathRealize(PaintCtx* ctx, Path* p) {
    (void)ctx;
    (void)p;
}

void PathFill(PaintCtx* ctx, Path* p, Rgba c, float dx, float dy) {
    c = PaintFade(ctx, c);
    if (c.a == 0) {
        return;
    }
    Inst proto = {};
    SetColor(proto.color, c);
    proto.misc[2] = (float)kQuadSolid;
    PathFillWith(ctx, p, proto, dx, dy);
}

void PathFillGradient(PaintCtx* ctx, Path* p, float x0, float y0, float x1,
                      float y1, Rgba from, Rgba to, float dx, float dy) {
    from = PaintFade(ctx, from);
    to = PaintFade(ctx, to);
    Inst proto = {};
    SetColor(proto.color, from);
    SetColor(proto.color2, to);
    proto.misc[2] = (float)kQuadGradient;
    proto.uv[0] = x0 + dx;
    proto.uv[1] = y0 + dy;
    proto.uv[2] = x1 + dx;
    proto.uv[3] = y1 + dy;
    PathFillWith(ctx, p, proto, dx, dy);
}

void PathStroke(PaintCtx* ctx, Path* path, float stroke, Rgba c, bool roundCaps,
                float dx, float dy) {
    GpuPath* p = P(path);
    if (!gB.target || !p || stroke <= 0) {
        return;
    }
    c = PaintFade(ctx, c);
    if (c.a == 0) {
        return;
    }
    for (int ci = 0; ci < p->starts.len; ci++) {
        int a = p->starts[ci];
        int b = ContourEnd(p, ci);
        for (int i = a; i + 1 < b; i++) {
            StrokeSegment(ctx, p->pts[i * 2] + dx, p->pts[i * 2 + 1] + dy,
                          p->pts[(i + 1) * 2] + dx,
                          p->pts[(i + 1) * 2 + 1] + dy, stroke, c);
        }
        // A disc at every joint, which is a round join. The tree strokes
        // chart lines and icon outlines, where a miter and a round join are a
        // fraction of a pixel apart at these widths.
        if (stroke > 1.5f) {
            int first = roundCaps ? a : a + 1;
            int last = roundCaps ? b : b - 1;
            for (int i = first; i < last; i++) {
                StrokeDisc(p->pts[i * 2] + dx, p->pts[i * 2 + 1] + dy,
                           stroke * 0.5f, c);
            }
        }
    }
}

// ─── images ──────────────────────────────────────────────────────────────
//
// The WIC decode is shared with the D2D backend; what is not shared is the
// texture, which is made once per image and hung off a small table rather
// than off the Image, whose layout belongs to the other backend.

constexpr int kImageSlots = 32;

struct ImageSlot {
    uint64_t imageGeneration = 0;
    int frameIndex = 0;
    ID3D11ShaderResourceView* srv = nullptr;
};

static ImageSlot gImages[kImageSlots];
static int gImageNext = 0;

static void FreeD3d11Gpu(bool removed) {
    Gpu* g = &gGpu;
    if (g->ctx && !removed) {
        g->ctx->ClearState();
        g->ctx->Flush();
    }
    for (int i = 0; i < kImageSlots; i++) {
        Rel(&gImages[i].srv);
        gImages[i].imageGeneration = 0;
        gImages[i].frameIndex = 0;
    }
    gImageNext = 0;
    Rel(&g->white);
    Rel(&g->atlas.srv);
    Rel(&g->atlas.tex);
    Rel(&g->dsCover);
    Rel(&g->dsNonZero);
    Rel(&g->dsEvenOdd);
    Rel(&g->dsOff);
    Rel(&g->samp);
    Rel(&g->rasterMsaa);
    Rel(&g->raster);
    Rel(&g->blend);
    Rel(&g->triBuf);
    Rel(&g->instSrv);
    Rel(&g->instBuf);
    Rel(&g->cb);
    Rel(&g->triLayout);
    Rel(&g->psTri);
    Rel(&g->vsTri);
    Rel(&g->psQuad);
    Rel(&g->vsQuad);
    Rel(&g->ctx);
    memset(g, 0, sizeof(*g));
    g->clearTypeLevel = 1.f;
}

static void FreeD3d12Gpu(bool removed) {
    D12Gpu* g = &gD12;
    if (!removed && g->queue && g->fence) {
        UINT64 value = g->nextFence++;
        if (SUCCEEDED(g->queue->Signal(g->fence, value))) {
            D12Wait(value);
        }
    }
    for (int i = 0; i < kD12ImageSlots; i++) {
        Rel(&g->images[i].tex);
    }
    for (int i = 0; i < 4; i++) {
        Rel(&g->pipes[i].nonZero);
        Rel(&g->pipes[i].evenOdd);
        Rel(&g->pipes[i].tri);
        Rel(&g->pipes[i].cover);
        Rel(&g->pipes[i].quad);
    }
    Rel(&g->atlas);
    Rel(&g->fence);
    if (g->fenceEvent) {
        CloseHandle(g->fenceEvent);
    }
    Rel(&g->srvHeap);
    Rel(&g->root);
    Rel(&g->list);
    Rel(&g->queue);
    Rel(&g->factory);
    Rel(&g->dev);
    memset(g, 0, sizeof(*g));
    g->nextFence = 1;
    // D3D12 shares the CPU atlas index and DirectWrite rendering parameters
    // stored in gGpu, even though none of gGpu's D3D11 objects were made.
    memset(&gGpu, 0, sizeof(gGpu));
    gGpu.clearTypeLevel = 1.f;
}

void PaintAppFree() {
    // ALL builds may have initialized both backends while switching them in
    // tests. D3D12 borrows gGpu's CPU glyph table and text parameters, so
    // release D3D11 before D3D12 clears that shared state.
    FreeD3d11Gpu(false);
    FreeD3d12Gpu(false);
    VecReset(gB.insts);
    VecReset(gB.tris);
    VecReset(gB.clipStack);
    gB.image = nullptr;
    gB.image12 = -1;
    gB.target = nullptr;
    gB.pxW = 0;
    gB.pxH = 0;
    gB.offscreen = false;
    gB.clip[0] = 0;
    gB.clip[1] = 0;
    gB.clip[2] = 1e6f;
    gB.clip[3] = 1e6f;
    gB.stats = {};
    gLastStats = {};
    gPresentedFrames = 0;
    gDeviceGeneration++;
    if (gDeviceGeneration == 0) {
        gDeviceGeneration++;
    }
}

static void RecoverDevice(PaintCtx* ctx, bool removed) {
    if (ctx) {
        scene::Reset(ctx);
    }
    if (PaintD3d12On()) {
        D12Target* t = ctx ? (D12Target*)ctx->rt : nullptr;
        D12FreeTarget(t, !removed);
        if (ctx) {
            ctx->rt = nullptr;
        }
        FreeD3d12Gpu(removed);
    } else {
        if (ctx) {
            gpuw::PaintTargetFree(ctx);
        }
        FreeD3d11Gpu(removed);
        PaintSharedD3dDeviceReset(ctx ? ctx->pa : nullptr);
    }
    gB.target = nullptr;
    gB.image = nullptr;
    gB.image12 = -1;
    gB.insts.len = 0;
    gB.tris.len = 0;
    gPresentedFrames = 0;
    gDeviceGeneration++;
    if (gDeviceGeneration == 0) {
        gDeviceGeneration++;
    }
    Str backend = PaintD3d12On() ? StrL("d3d12") : StrL("d3d11");
    Str reason = removed ? StrL("DXGI") : StrL("injected");
    logf("paint/%s: %s device recovery", backend, reason);
    if (ctx && ctx->window) {
        AppInvalidate(ctx->window);
    }
}

static int D12ImageDescriptor(const RenderImage* img, int frameIndex) {
    uint64_t generation = RenderImageGeneration(img);
    for (int i = 0; i < kD12ImageSlots; i++) {
        if (gD12.images[i].imageGeneration == generation &&
            gD12.images[i].frameIndex == frameIndex) {
            gD12.images[i].usedInCommands = gD12.commandGeneration;
            return gD12.images[i].descriptor;
        }
    }
    const uint8_t* bgra = nullptr;
    int w = 0, h = 0;
    if (!PaintImagePixels(img, &bgra, &w, &h, frameIndex) || !bgra || w <= 0 ||
        h <= 0) {
        return -1;
    }
    D12ImageSlot* slot = nullptr;
    int slotIx = -1;
    for (int i = 0; i < kD12ImageSlots; i++) {
        int ix = (gD12.imageNext + i) % kD12ImageSlots;
        D12ImageSlot* candidate = &gD12.images[ix];
        if (!candidate->tex ||
            candidate->usedInCommands != gD12.commandGeneration) {
            slot = candidate;
            slotIx = ix;
            break;
        }
    }
    // A descriptor referenced by the open command list cannot be rewritten.
    // Submit that part of the frame before reusing the heap. Waiting is rare
    // (only after 128 distinct images) and preserves both painter order and
    // the already-rendered target while the command allocator is reset.
    if (!slot) {
        Flush();
        D12Target* t = (D12Target*)gB.target;
        if (!t || !D12FinishCommands(t, true) || !D12BeginCommands(t, true)) {
            return -1;
        }
        for (int i = 0; i < kD12ImageSlots; i++) {
            int ix = (gD12.imageNext + i) % kD12ImageSlots;
            D12ImageSlot* candidate = &gD12.images[ix];
            if (!candidate->tex ||
                candidate->usedInCommands != gD12.commandGeneration) {
                slot = candidate;
                slotIx = ix;
                break;
            }
        }
        if (!slot) {
            return -1;
        }
    }
    if (slot->tex) {
        // Submitted lists share this shader-visible heap. Wait through the
        // most recently submitted list before replacing its resource and SRV.
        D12Wait(gD12.nextFence > 1 ? gD12.nextFence - 1 : 0);
        Rel(&slot->tex);
    }
    slot->imageGeneration = 0;
    slot->frameIndex = 0;
    slot->retireFence = 0;
    slot->retireOnSubmit = false;
    slot->descriptor = 1 + slotIx;
    D3D12_HEAP_PROPERTIES heap = D12Heap(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC td = D12Texture(w, h, DXGI_FORMAT_B8G8R8A8_UNORM, 1,
                                        D3D12_RESOURCE_FLAG_NONE);
    if (FAILED(gD12.dev->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, __uuidof(ID3D12Resource), (void**)&slot->tex))) {
        return -1;
    }
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
    if (!D12UploadTexture(slot->tex, &state, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 0,
                          w, h, 4, bgra, w * 4)) {
        Rel(&slot->tex);
        return -1;
    }
    slot->imageGeneration = generation;
    slot->frameIndex = frameIndex;
    slot->usedInCommands = gD12.commandGeneration;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    gD12.dev
        ->CreateShaderResourceView(slot->tex, &sv, D12SrvCpu(slot->descriptor));
    gD12.imageNext = (slotIx + 1) % kD12ImageSlots;
    return slot->descriptor;
}

void RenderImageFree(uint64_t imageGeneration) {
    if (!imageGeneration) {
        return;
    }
    for (int i = 0; i < kImageSlots; i++) {
        ImageSlot* slot = &gImages[i];
        if (slot->imageGeneration == imageGeneration) {
            if (gB.image == slot->srv) {
                FlushQuads();
                gB.image = nullptr;
            }
            Rel(&slot->srv);
            slot->imageGeneration = 0;
            slot->frameIndex = 0;
        }
    }
    for (int i = 0; i < kD12ImageSlots; i++) {
        D12ImageSlot* slot = &gD12.images[i];
        if (slot->imageGeneration != imageGeneration) {
            continue;
        }
        slot->imageGeneration = 0;
        if (gB.target && slot->usedInCommands == gD12.commandGeneration) {
            slot->retireOnSubmit = true;
        } else {
            slot->retireFence = gD12.nextFence > 1 ? gD12.nextFence - 1 : 0;
            if (!slot->retireFence) {
                Rel(&slot->tex);
                slot->usedInCommands = 0;
                slot->frameIndex = 0;
            }
        }
    }
    D12CollectImageEvictions();
}

int RenderImageCacheCountForTest(uint64_t imageGeneration) {
    int count = 0;
    for (int i = 0; i < kImageSlots; i++) {
        count += gImages[i].imageGeneration == imageGeneration ? 1 : 0;
    }
    for (int i = 0; i < kD12ImageSlots; i++) {
        count += gD12.images[i].imageGeneration == imageGeneration ? 1 : 0;
    }
    return count;
}

static ID3D11ShaderResourceView* ImageSrv(const RenderImage* img,
                                          int frameIndex) {
    uint64_t generation = RenderImageGeneration(img);
    for (int i = 0; i < kImageSlots; i++) {
        if (gImages[i].imageGeneration == generation &&
            gImages[i].frameIndex == frameIndex) {
            return gImages[i].srv;
        }
    }
    const uint8_t* bgra = nullptr;
    int w = 0, h = 0;
    if (!PaintImagePixels(img, &bgra, &w, &h, frameIndex) || !bgra || w <= 0 ||
        h <= 0) {
        return nullptr;
    }
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = bgra;
    sd.SysMemPitch = (UINT)(w * 4);
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(gGpu.dev->CreateTexture2D(&td, &sd, &tex))) {
        return nullptr;
    }
    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = gGpu.dev->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();
    if (FAILED(hr)) {
        return nullptr;
    }
    ImageSlot* s = &gImages[gImageNext];
    gImageNext = (gImageNext + 1) % kImageSlots;
    Rel(&s->srv);
    s->imageGeneration = generation;
    s->frameIndex = frameIndex;
    s->srv = srv;
    return srv;
}

void RenderImageDraw(PaintCtx* ctx, RenderImage* img, Bounds bounds,
                     Bounds imageBounds, int frameIndex, float radius,
                     bool grayscale) {
    if (!gB.target || !img || bounds.w <= 0 || bounds.h <= 0 ||
        imageBounds.w <= 0 || imageBounds.h <= 0) {
        return;
    }
    float x0 = imageBounds.x > bounds.x ? imageBounds.x : bounds.x;
    float y0 = imageBounds.y > bounds.y ? imageBounds.y : bounds.y;
    float x1 = imageBounds.x + imageBounds.w;
    float y1 = imageBounds.y + imageBounds.h;
    float bx1 = bounds.x + bounds.w;
    float by1 = bounds.y + bounds.h;
    x1 = x1 < bx1 ? x1 : bx1;
    y1 = y1 < by1 ? y1 : by1;
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    EnsureQuadPhase();
    if (PaintD3d12On()) {
        int descriptor = D12ImageDescriptor(img, frameIndex);
        if (descriptor < 0) {
            return;
        }
        if (gB.image12 != descriptor) {
            FlushQuads();
            gB.image12 = descriptor;
        }
    } else {
        ID3D11ShaderResourceView* srv = ImageSrv(img, frameIndex);
        if (!srv) {
            return;
        }
        // A different texture is the one thing that has to break the batch.
        if (gB.image != srv) {
            FlushQuads();
            gB.image = srv;
        }
    }
    Inst i = {};
    i.rect[0] = x0;
    i.rect[1] = y0;
    i.rect[2] = x1 - x0;
    i.rect[3] = y1 - y0;
    float op = ctx->opacity < 0 ? 0 : (ctx->opacity > 1 ? 1 : ctx->opacity);
    i.color[3] = op;
    i.misc[2] = (float)kQuadImage;
    // When ObjectFit leaves letterbox space the image does not reach the
    // outer corners. Fill, Cover and an oversized None do, and are rounded
    // against that visible outer box.
    bool covers = x0 == bounds.x && y0 == bounds.y && x1 == bx1 && y1 == by1;
    float half = (bounds.w < bounds.h ? bounds.w : bounds.h) * 0.5f;
    i.misc[0] =
        covers ? (radius > half ? half : (radius > 0 ? radius : 0.f)) : 0.f;
    i.misc[1] = grayscale ? 1.f : 0.f;
    i.uv[0] = (x0 - imageBounds.x) / imageBounds.w;
    i.uv[1] = (y0 - imageBounds.y) / imageBounds.h;
    i.uv[2] = (x1 - imageBounds.x) / imageBounds.w;
    i.uv[3] = (y1 - imageBounds.y) / imageBounds.h;
    memcpy(i.clip, gB.clip, sizeof(i.clip));
    if (i.clip[0] < bounds.x) i.clip[0] = bounds.x;
    if (i.clip[1] < bounds.y) i.clip[1] = bounds.y;
    if (i.clip[2] > bx1) i.clip[2] = bx1;
    if (i.clip[3] > by1) i.clip[3] = by1;
    Push(i);
    FlushQuads();
    gB.image = nullptr;
    gB.image12 = -1;
}

// ─── text ────────────────────────────────────────────────────────────────
//
// The layout is an IDWriteTextLayout, exactly as it is on the D2D path — so
// shaping, measurement, hit-testing and the range rects are literally the
// same code, and only the drawing is different. IDWriteTextLayout::Draw hands
// its glyph runs to the renderer below, which turns each glyph into a quad
// against the atlas.

static GlyphEntry* AtlasFind(const GlyphKey& k) {
    uint32_t at = GlyphHash(k);
    for (int probe = 0; probe < 32; probe++) {
        GlyphEntry* e = &gGpu.atlas.slots[(at + probe) & (kGlyphSlots - 1)];
        if (!e->used) {
            return e; // free slot, and therefore not present
        }
        if (e->key.face == k.face && e->key.glyph == k.glyph &&
            e->key.size4 == k.size4 && e->key.phase3 == k.phase3) {
            return e;
        }
    }
    return nullptr;
}

// Rasterize one glyph at one of DirectWrite's three horizontal subpixel
// phases and shelf its RGB ClearType mask. The second shader output carries
// that mask into dual-source blending, where it attenuates the destination
// per channel just as DirectWrite's ClearType compositor does.
static bool AtlasRasterize(IDWriteFontFace* face, float em, uint16_t glyph,
                           const GlyphKey& key, GlyphEntry* e) {
    Gpu* g = &gGpu;
    float advance = 0;
    DWRITE_GLYPH_OFFSET offset = {};
    DWRITE_GLYPH_RUN run = {};
    run.fontFace = face;
    run.fontEmSize = em;
    run.glyphCount = 1;
    run.glyphIndices = &glyph;
    run.glyphAdvances = &advance;
    run.glyphOffsets = &offset;

    IDWriteGlyphRunAnalysis* an = nullptr;
    HRESULT hr = g->dwrite->CreateGlyphRunAnalysis(
        &run, 1.f, nullptr, DWRITE_RENDERING_MODE_NATURAL,
        DWRITE_MEASURING_MODE_NATURAL, (float)key.phase3 / 3.f, 0.f, &an);
    if (FAILED(hr) || !an) {
        return false;
    }
    RECT r = {};
    hr = an->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &r);
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (FAILED(hr) || w <= 0 || h <= 0 || w > 512 || h > 512) {
        an->Release();
        // A blank glyph — a space — is a real answer: nothing to draw, and
        // the entry stops it being asked for again.
        e->used = true;
        e->key = key;
        e->w = 0;
        e->h = 0;
        return SUCCEEDED(hr);
    }
    // Static scratch: a glyph is rasterized once and this is the only place
    // that touches these, so the buffers are reused rather than grown per
    // glyph.
    static Vec<uint8_t> rgb;
    static Vec<uint8_t> rgba;
    rgb.len = 0;
    if (!VecAppendBlanks(rgb, w * h * 3)) {
        an->Release();
        return false;
    }
    hr = an->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &r, rgb.els,
                                (UINT32)(w * h * 3));
    an->Release();
    if (FAILED(hr)) {
        return false;
    }
    rgba.len = 0;
    if (!VecAppendBlanks(rgba, w * h * 4)) {
        return false;
    }
    for (int i = 0; i < w * h; i++) {
        uint8_t r8 = rgb[i * 3];
        uint8_t g8 = rgb[i * 3 + 1];
        uint8_t b8 = rgb[i * 3 + 2];
        if (g->textBgr) {
            std::swap(r8, b8);
        }
        rgba[i * 4] = r8;
        rgba[i * 4 + 1] = g8;
        rgba[i * 4 + 2] = b8;
        rgba[i * 4 + 3] = std::max(r8, std::max(g8, b8));
    }

    Atlas* a = &g->atlas;
    if (a->penX + w + 1 > kAtlasDim) {
        a->penX = 0;
        a->penY += a->rowH + 1;
        a->rowH = 0;
    }
    if (a->penY + h + 1 > kAtlasDim) {
        // Full. Start over rather than grow: a UI's glyph set is small, and
        // this only ever happens on a document that changed size a lot.
        memset(a->slots, 0, sizeof(a->slots));
        a->penX = 0;
        a->penY = 0;
        a->rowH = 0;
        e = AtlasFind(key);
        if (!e) {
            return false;
        }
    }
    if (PaintD3d12On()) {
        if (!D12UploadTexture(gD12.atlas, &gD12.atlasState,
                              DXGI_FORMAT_R8G8B8A8_UNORM, a->penX, a->penY, w,
                              h, 4, rgba.els, w * 4)) {
            return false;
        }
    } else {
        D3D11_BOX box = {};
        box.left = (UINT)a->penX;
        box.top = (UINT)a->penY;
        box.right = (UINT)(a->penX + w);
        box.bottom = (UINT)(a->penY + h);
        box.back = 1;
        g->ctx->UpdateSubresource(a->tex, 0, &box, rgba.els, (UINT)(w * 4), 0);
    }

    e->used = true;
    e->key = key;
    e->x = a->penX;
    e->y = a->penY;
    e->w = w;
    e->h = h;
    e->bearingX = r.left;
    e->bearingY = r.top;
    a->penX += w + 1;
    if (h > a->rowH) {
        a->rowH = h;
    }
    gB.stats.glyphsRasterized++;
    return true;
}

static void DrawGlyph(IDWriteFontFace* face, float em, uint16_t glyph, float x,
                      float y, Rgba c) {
    float x3 = floorf(x * 3.f + 0.5f) / 3.f;
    float pixelX = floorf(x3);
    uint32_t phase3 = (uint32_t)lroundf((x3 - pixelX) * 3.f);
    if (phase3 == 3) {
        phase3 = 0;
        pixelX += 1.f;
    }
    GlyphKey k = {face, glyph, (uint32_t)lroundf(em * 4.f), phase3};
    GlyphEntry* e = AtlasFind(k);
    if (!e) {
        return;
    }
    if (!e->used && !AtlasRasterize(face, em, glyph, k, e)) {
        return;
    }
    if (e->w <= 0 || e->h <= 0) {
        return;
    }
    // The fractional phase is already baked into the mask's raster bounds;
    // placing those integer bounds at the pixel origin must not add it again.
    float gx = pixelX + (float)e->bearingX;
    float gy = floorf(y + 0.5f) + (float)e->bearingY;

    Inst i = {};
    i.rect[0] = gx;
    i.rect[1] = gy;
    i.rect[2] = (float)e->w;
    i.rect[3] = (float)e->h;
    SetColor(i.color, c);
    i.misc[2] = (float)kQuadGlyph;
    i.uv[0] = (float)e->x / kAtlasDim;
    i.uv[1] = (float)e->y / kAtlasDim;
    i.uv[2] = (float)(e->x + e->w) / kAtlasDim;
    i.uv[3] = (float)(e->y + e->h) / kAtlasDim;
    memcpy(i.clip, gB.clip, sizeof(i.clip));
    Push(i);
}

// The minimum IDWriteTextRenderer that a layout will talk to. No refcount
// worth the name: one of these lives on the stack for the length of a Draw.
struct GlyphSink : public IDWriteTextRenderer {
    Rgba color = {};
    PaintCtx* ctx = nullptr;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** obj) override {
        if (iid == __uuidof(IUnknown) ||
            iid == __uuidof(IDWritePixelSnapping) ||
            iid == __uuidof(IDWriteTextRenderer)) {
            *obj = this;
            return S_OK;
        }
        *obj = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }

    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*,
                                                      BOOL* out) override {
        *out = FALSE;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*,
                                                  DWRITE_MATRIX* m) override {
        DWRITE_MATRIX id = {1, 0, 0, 1, 0, 0};
        *m = id;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* out) override {
        *out = 1.f;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*, FLOAT baselineX,
                                           FLOAT baselineY,
                                           DWRITE_MEASURING_MODE,
                                           const DWRITE_GLYPH_RUN* run,
                                           const DWRITE_GLYPH_RUN_DESCRIPTION*,
                                           IUnknown*) override {
        if (!run || !run->fontFace || run->glyphCount == 0) {
            return S_OK;
        }
        bool rtl = (run->bidiLevel & 1) != 0;
        float x = baselineX;
        for (UINT32 i = 0; i < run->glyphCount; i++) {
            float adv = run->glyphAdvances ? run->glyphAdvances[i] : 0.f;
            float ox = 0, oy = 0;
            if (run->glyphOffsets) {
                ox = run->glyphOffsets[i].advanceOffset;
                oy = run->glyphOffsets[i].ascenderOffset;
            }
            if (rtl) {
                x -= adv;
            }
            DrawGlyph(run->fontFace, run->fontEmSize, run->glyphIndices[i],
                      x + (rtl ? -ox : ox), baselineY - oy, color);
            if (!rtl) {
                x += adv;
            }
        }
        return S_OK;
    }

    // An underline or a strikethrough is a rule under a pixel tall — 0.8 at
    // the sizes here — and left to the SDF it comes out at the coverage of a
    // 0.8 px band, which is visibly fainter than the same rule on the D2D
    // path. DirectWrite snaps its own rules to the pixel grid; so does this.
    void Rule(float x, float y, float w, float thickness) {
        float t = thickness > 1.f ? thickness : 1.f;
        Quad(ctx, kQuadRect, x, floorf(y + 0.5f), w, t, color);
    }

    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT baselineX,
                                            FLOAT baselineY,
                                            const DWRITE_UNDERLINE* u,
                                            IUnknown*) override {
        if (u) {
            Rule(baselineX, baselineY + u->offset, u->width, u->thickness);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT baselineX,
                                                FLOAT baselineY,
                                                const DWRITE_STRIKETHROUGH* s,
                                                IUnknown*) override {
        if (s) {
            Rule(baselineX, baselineY + s->offset, s->width, s->thickness);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT,
                                               IDWriteInlineObject*, BOOL, BOOL,
                                               IUnknown*) override {
        return E_NOTIMPL;
    }
};

void TextLayoutDraw(PaintCtx* ctx, TextLayout* tl, float x, float y, Rgba c,
                    bool clip, float clipW) {
    (void)clipW;
    if (!gB.target || !tl) {
        return;
    }
    auto* layout = (IDWriteTextLayout*)PaintTextLayoutNative(tl);
    Rgba faded = PaintFade(ctx, c);
    if (faded.a == 0) {
        return;
    }
    bool pushed = false;
    if (clip) {
        DWRITE_TEXT_METRICS m = {};
        if (SUCCEEDED(layout->GetMetrics(&m))) {
            float w = layout->GetMaxWidth();
            gpuw::CanvasPushClip(ctx, x, y, w > 0 ? w : m.width, m.height);
            pushed = true;
        }
    }
    GlyphSink sink;
    sink.color = faded;
    sink.ctx = ctx;
    layout->Draw(nullptr, &sink, x, y);
    if (pushed) {
        gpuw::CanvasPopClip(ctx);
    }
}

} // namespace gpuw
} // namespace gpui

#elif GPUI_OS_WINDOWS

// A Direct2D-only build keeps paint_win.cpp's dispatch shape but gives the
// compiler/linker concrete dead-branch targets. No Direct3D renderer source
// or API header is compiled in this configuration.
namespace gpui {

bool PaintGpuOn() {
    return false;
}
bool PaintD3d12On() {
    return false;
}
int PaintGpuSamples() {
    return (int)WinPaintOptionsGet().msaa;
}

namespace gpuw {

void PaintAppFree() {}
bool PaintTargetBegin(PaintCtx*, void*, int, int) {
    return false;
}
bool PaintTargetBeginOffscreen(PaintCtx*, int, int) {
    return false;
}
bool PaintTargetEndOffscreen(PaintCtx*, uint8_t*) {
    return false;
}
bool PaintTargetEnd(PaintCtx*) {
    return false;
}
void PaintTargetFree(PaintCtx*) {}
void CanvasClear(PaintCtx*, Rgba) {}
void CanvasFillRect(PaintCtx*, float, float, float, float, Rgba) {}
void CanvasFillRound(PaintCtx*, float, float, float, float, float, Rgba) {}
void CanvasStrokeRound(PaintCtx*, float, float, float, float, float, float,
                       Rgba, const float*) {}
void CanvasLine(PaintCtx*, float, float, float, float, float, Rgba,
                const float*) {}
void CanvasEllipse(PaintCtx*, float, float, float, float, float, Rgba) {}
void CanvasPushClip(PaintCtx*, float, float, float, float) {}
void CanvasPopClip(PaintCtx*) {}
Path* PathNew(PaintCtx*, bool) {
    return nullptr;
}
void PathFree(Path*) {}
void PathMoveTo(Path*, float, float) {}
void PathLineTo(Path*, float, float) {}
void PathCubicTo(Path*, float, float, float, float, float, float) {}
void PathArcTo(Path*, float, float, float, float, float, bool) {}
void PathClose(Path*) {}
void PathFill(PaintCtx*, Path*, Rgba, float, float) {}
void PathFillGradient(PaintCtx*, Path*, float, float, float, float, Rgba, Rgba,
                      float, float) {}
void PathStroke(PaintCtx*, Path*, float, Rgba, bool, float, float) {}
void PathRealize(PaintCtx*, Path*) {}
void RenderImageDraw(PaintCtx*, RenderImage*, Bounds, Bounds, int, float,
                     bool) {}
void RenderImageFree(uint64_t) {}
int RenderImageCacheCountForTest(uint64_t) {
    return 0;
}
void TextLayoutDraw(PaintCtx*, TextLayout*, float, float, Rgba, bool, float) {}
static FrameStats gEmptyStats;
const FrameStats& LastFrameStats() {
    return gEmptyStats;
}

} // namespace gpuw
} // namespace gpui

#endif // GPUI_OS_WINDOWS
