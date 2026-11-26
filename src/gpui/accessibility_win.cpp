/* Project the portable frame accessibility tree through Windows UI
   Automation. This is a raw fragment provider: nodes are light COM handles
   containing only the stable frame id, and every query resolves that id in
   the latest Window::accessibility rather than retaining arena strings. */

#include "gpui/accessibility_win.h"
#include "base/text_boundary.h"

#include <uiautomation.h>

namespace gpui {

struct WinAccessibilityNode;
struct WinTextRange;

static BSTR AccessibilityBstr(Str value) {
    if (!value.s || len(value) <= 0) {
        return SysAllocStringLen(nullptr, 0);
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, value.s, len(value), nullptr, 0);
    if (n <= 0) {
        return SysAllocStringLen(nullptr, 0);
    }
    BSTR out = SysAllocStringLen(nullptr, (UINT)n);
    if (!out) {
        return nullptr;
    }
    MultiByteToWideChar(CP_UTF8, 0, value.s, len(value), out, n);
    return out;
}

static void VariantInt(VARIANT* out, int value) {
    VariantInit(out);
    out->vt = VT_I4;
    out->lVal = value;
}

static void VariantBool(VARIANT* out, bool value) {
    VariantInit(out);
    out->vt = VT_BOOL;
    out->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
}

static void VariantDouble(VARIANT* out, double value) {
    VariantInit(out);
    out->vt = VT_R8;
    out->dblVal = value;
}

static void VariantString(VARIANT* out, Str value) {
    VariantInit(out);
    out->vt = VT_BSTR;
    out->bstrVal = AccessibilityBstr(value);
}

// The roles UI Automation exposes as an Edit control with a Value pattern.
static bool AccessibilityTextRole(AccessibilityRole role) {
    return AccessibilityRoleIsTextField(role);
}

static int AccessibilityControlType(AccessibilityRole role) {
    if (AccessibilityTextRole(role)) {
        return UIA_EditControlTypeId;
    }
    switch (role) {
        case AccessibilityRole::Alert:
        case AccessibilityRole::Heading:
        case AccessibilityRole::Label:
        case AccessibilityRole::TextRun:
            return UIA_TextControlTypeId;
        case AccessibilityRole::Document:
            return UIA_DocumentControlTypeId;
        case AccessibilityRole::AlertDialog:
        case AccessibilityRole::Dialog:
            return UIA_WindowControlTypeId;
        case AccessibilityRole::Button:
            return UIA_ButtonControlTypeId;
        case AccessibilityRole::Cell:
        case AccessibilityRole::Row:
            return UIA_DataItemControlTypeId;
        case AccessibilityRole::CheckBox:
        case AccessibilityRole::Switch:
            return UIA_CheckBoxControlTypeId;
        case AccessibilityRole::ColumnHeader:
            return UIA_HeaderItemControlTypeId;
        case AccessibilityRole::ComboBox:
            return UIA_ComboBoxControlTypeId;
        case AccessibilityRole::Link:
            return UIA_HyperlinkControlTypeId;
        case AccessibilityRole::List:
        case AccessibilityRole::ListBox:
            return UIA_ListControlTypeId;
        case AccessibilityRole::ListBoxOption:
        case AccessibilityRole::ListItem:
            return UIA_ListItemControlTypeId;
        case AccessibilityRole::Menu:
            return UIA_MenuControlTypeId;
        case AccessibilityRole::MenuBar:
            return UIA_MenuBarControlTypeId;
        case AccessibilityRole::MenuItem:
            return UIA_MenuItemControlTypeId;
        case AccessibilityRole::ProgressIndicator:
            return UIA_ProgressBarControlTypeId;
        case AccessibilityRole::RadioButton:
            return UIA_RadioButtonControlTypeId;
        case AccessibilityRole::Slider:
            return UIA_SliderControlTypeId;
        case AccessibilityRole::SpinButton:
            return UIA_SpinnerControlTypeId;
        case AccessibilityRole::Status:
            return UIA_StatusBarControlTypeId;
        case AccessibilityRole::Tab:
            return UIA_TabItemControlTypeId;
        case AccessibilityRole::Table:
            return UIA_DataGridControlTypeId;
        case AccessibilityRole::TabList:
            return UIA_TabControlTypeId;
        case AccessibilityRole::Tree:
            return UIA_TreeControlTypeId;
        case AccessibilityRole::TreeItem:
            return UIA_TreeItemControlTypeId;
        case AccessibilityRole::Toolbar:
            return UIA_ToolBarControlTypeId;
        case AccessibilityRole::Tooltip:
            return UIA_ToolTipControlTypeId;
        case AccessibilityRole::Group:
        case AccessibilityRole::Navigation:
        case AccessibilityRole::RadioGroup:
        case AccessibilityRole::Region:
        case AccessibilityRole::RowGroup:
        case AccessibilityRole::None:
        default:
            return UIA_GroupControlTypeId;
    }
}

static bool AccessibilityIsDialog(AccessibilityRole role) {
    return role == AccessibilityRole::Dialog ||
           role == AccessibilityRole::AlertDialog;
}

static const wchar_t* AccessibilityRoleName(AccessibilityRole role) {
    if (AccessibilityRoleIsTextField(role)) {
        return L"textbox";
    }
    switch (role) {
        case AccessibilityRole::Alert:
            return L"alert";
        case AccessibilityRole::AlertDialog:
            return L"alertdialog";
        case AccessibilityRole::Button:
            return L"button";
        case AccessibilityRole::Cell:
            return L"cell";
        case AccessibilityRole::CheckBox:
            return L"checkbox";
        case AccessibilityRole::ColumnHeader:
            return L"columnheader";
        case AccessibilityRole::ComboBox:
            return L"combobox";
        case AccessibilityRole::Dialog:
            return L"dialog";
        case AccessibilityRole::Document:
            return L"document";
        case AccessibilityRole::Heading:
            return L"heading";
        case AccessibilityRole::Link:
            return L"link";
        case AccessibilityRole::List:
            return L"list";
        case AccessibilityRole::ListBox:
            return L"listbox";
        case AccessibilityRole::ListBoxOption:
            return L"option";
        case AccessibilityRole::ListItem:
            return L"listitem";
        case AccessibilityRole::Menu:
            return L"menu";
        case AccessibilityRole::MenuBar:
            return L"menubar";
        case AccessibilityRole::MenuItem:
            return L"menuitem";
        case AccessibilityRole::Navigation:
            return L"navigation";
        case AccessibilityRole::ProgressIndicator:
            return L"progressbar";
        case AccessibilityRole::RadioButton:
            return L"radio";
        case AccessibilityRole::RadioGroup:
            return L"radiogroup";
        case AccessibilityRole::Region:
            return L"region";
        case AccessibilityRole::Row:
            return L"row";
        case AccessibilityRole::RowGroup:
            return L"rowgroup";
        case AccessibilityRole::Slider:
            return L"slider";
        case AccessibilityRole::SpinButton:
            return L"spinbutton";
        case AccessibilityRole::Status:
            return L"status";
        case AccessibilityRole::Switch:
            return L"switch";
        case AccessibilityRole::Tab:
            return L"tab";
        case AccessibilityRole::Table:
            return L"table";
        case AccessibilityRole::TabList:
            return L"tablist";
        case AccessibilityRole::Tree:
            return L"tree";
        case AccessibilityRole::TreeItem:
            return L"treeitem";
        case AccessibilityRole::Toolbar:
            return L"toolbar";
        case AccessibilityRole::Tooltip:
            return L"tooltip";
        case AccessibilityRole::Group:
        case AccessibilityRole::None:
        default:
            return L"group";
    }
}

static bool AccessibilitySelectionItemRole(AccessibilityRole role) {
    return role == AccessibilityRole::ListBoxOption ||
           role == AccessibilityRole::RadioButton ||
           role == AccessibilityRole::Tab ||
           role == AccessibilityRole::TreeItem;
}

static bool AccessibilitySelectionContainerRole(AccessibilityRole role) {
    return role == AccessibilityRole::ListBox ||
           role == AccessibilityRole::RadioGroup ||
           role == AccessibilityRole::TabList ||
           role == AccessibilityRole::Tree;
}

static bool AccessibilityTextPattern(const AccessibilityNode& node) {
    return WindowAccessibilityHasText(node);
}

static bool AccessibilityInvokePattern(const AccessibilityNode& node) {
    if (!(node.actions & AccessibilityActionDefault)) {
        return false;
    }
    if (node.info.role == AccessibilityRole::Button ||
        node.info.role == AccessibilityRole::Link ||
        node.info.role == AccessibilityRole::MenuItem) {
        return true;
    }
    // Controls with a more specific UIA action pattern should not also claim
    // Invoke merely because both route to the same portable Default action.
    return node.info.toggled == AccessibilityToggled::Unset &&
           !node.info.hasExpanded &&
           !(node.info.hasSelected &&
             AccessibilitySelectionItemRole(node.info.role));
}

// Node bounds are DIPs; UI Automation speaks physical screen pixels.
static float DipScale(const Window* win) {
    return win && win->paint.dpi > 0 ? win->paint.dpi / 96.f : 1.f;
}

// The IRawElementProviderSimple and IRawElementProviderFragment halves the
// root and every node implement, each its own way.
#define WIN_UIA_FRAGMENT_METHODS                                               \
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out)        \
        override;                                                              \
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** out) \
        override;                                                              \
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* out)    \
        override;                                                              \
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(                      \
        IRawElementProviderSimple** out) override;                             \
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,            \
                                       IRawElementProviderFragment** out)      \
        override;                                                              \
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override;          \
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override;    \
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out)        \
        override;                                                              \
    HRESULT STDMETHODCALLTYPE SetFocus() override;                             \
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(                                \
        IRawElementProviderFragmentRoot** out) override;

struct WinNodeProperties {
    AccessibilityInfo info;
    uint8_t actions;
    bool hasRange;
    double value;
    double minimum;
    double maximum;

    explicit WinNodeProperties(const AccessibilityNode& node)
        : info(node.info),
          actions(node.actions),
          hasRange(node.slider != nullptr),
          value(hasRange ? node.slider->value.End() : 0),
          minimum(hasRange ? node.slider->min : 0),
          maximum(hasRange ? node.slider->max : 0) {}
};

struct WinNodeSnapshot {
    uint32_t id;
    uint32_t parent;
    int index;
    WinNodeProperties properties;
    // UIA's Text pattern uses the exposed value or excerpt. Lazy sources
    // carry only their revision counters until a client asks for text.
    Str text = {};
    int textStart = 0;
    Selection selection = {-1, -1};
    int caret = -1;
    bool lazyText = false;
    uint64_t textRevision = 0;
    uint64_t selectionRevision = 0;
    uint32_t selectionContainer = 0;
    int selectedCount = 0;
    uint32_t selectedChild = 0;

    bool IsSelectedItem() const {
        const auto& info = properties.info;
        return info.hasSelected && info.selected &&
               AccessibilitySelectionItemRole(info.role);
    }
};

struct WinAccessibility
    : ComObject<IRawElementProviderSimple, IRawElementProviderFragment,
                IRawElementProviderFragmentRoot> {
    Window* win = nullptr;
    HWND hwnd = nullptr;
    Vec<WinNodeSnapshot> snapshots[2];
    Arena* snapshotStrings[2] = {};
    int snapshotFrame = 0;

    WinAccessibility(Window* window, HWND handle) : win(window), hwnd(handle) {}
    ~WinAccessibility() override {
        for (Arena* arena : snapshotStrings) {
            if (arena) ArenaDelete(arena);
        }
    }

    WIN_UIA_FRAGMENT_METHODS
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(
        double x, double y, IRawElementProviderFragment** out) override;
    HRESULT STDMETHODCALLTYPE
    GetFocus(IRawElementProviderFragment** out) override;

    const AccessibilityNode* Node(uint32_t id) const {
        return win ? WindowAccessibilityNode(win, id) : nullptr;
    }
    int NodeIndex(uint32_t id) const {
        if (!win) {
            return -1;
        }
        for (int i = 0; i < len(win->accessibility); i++) {
            if (win->accessibility[i].id == id) {
                return i;
            }
        }
        return -1;
    }
    IRawElementProviderFragment* NewNode(int index);
    // Part `part` of node `index`'s document source; null when there is none.
    IRawElementProviderFragment* NewPart(int index, int part);
};

struct WinAccessibilityNode
    : ComObject<IRawElementProviderSimple, IRawElementProviderFragment,
                IInvokeProvider, IToggleProvider, IValueProvider,
                IRangeValueProvider, IExpandCollapseProvider,
                ISelectionProvider, ISelectionItemProvider, ITextProvider,
                IGridProvider, IGridItemProvider, ITableProvider,
                ITableItemProvider, IWindowProvider> {
    WinAccessibility* root = nullptr;
    uint32_t id = 0;

    WinAccessibilityNode(WinAccessibility* owner, uint32_t nodeId)
        : root(owner), id(nodeId) {
        root->AddRef();
    }
    ~WinAccessibilityNode() override { root->Release(); }

    const AccessibilityNode* Node() const { return root->Node(id); }
    HRESULT Missing() const {
        return Node() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    // The node, for a pattern only a node of `role` supports: gone is
    // ELEMENTNOTAVAILABLE, another role INVALIDOPERATION.
    HRESULT NodeOfRole(AccessibilityRole role,
                       const AccessibilityNode** out) const {
        *out = Node();
        if (!*out) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        return (*out)->info.role == role ? S_OK : UIA_E_INVALIDOPERATION;
    }
    // The node, for a pattern only a text input supports.
    HRESULT TextNode(const AccessibilityNode** out) const {
        *out = Node();
        if (!*out) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        return AccessibilityTextPattern(**out) ? S_OK : UIA_E_INVALIDOPERATION;
    }
    HRESULT DialogNode() const {
        const auto* node = Node();
        if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
        return AccessibilityIsDialog(node->info.role) ? S_OK
                                                      : UIA_E_INVALIDOPERATION;
    }
    HRESULT UnsupportedDialogOperation() const {
        HRESULT hr = DialogNode();
        return FAILED(hr) ? hr : UIA_E_NOTSUPPORTED;
    }
    // The preamble of a getter returning through `out`: a null `out` is
    // INVALIDARG, otherwise it is cleared before the node is looked up.
    template <typename T>
    HRESULT ClearedNode(T** out, const AccessibilityNode** node) const {
        if (!out) {
            return E_INVALIDARG;
        }
        *out = nullptr;
        *node = Node();
        return *node ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    template <typename T>
    HRESULT ClearedTextNode(T** out, const AccessibilityNode** node) const {
        if (!out) {
            return E_INVALIDARG;
        }
        *out = nullptr;
        return TextNode(node);
    }
    // This node's index in the window's tree, after clearing `out`.
    template <typename T>
    HRESULT ClearedIndex(T** out, int* index) const {
        if (!out) {
            return E_INVALIDARG;
        }
        *out = nullptr;
        *index = root->NodeIndex(id);
        return root->win && *index >= 0 ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    // A cell's index and its table's.
    HRESULT CellTable(int* index, int* table) const;

    WIN_UIA_FRAGMENT_METHODS
    HRESULT STDMETHODCALLTYPE Invoke() override;
    HRESULT STDMETHODCALLTYPE Toggle() override;
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* out) override;
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override;
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override;
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override;
    HRESULT STDMETHODCALLTYPE SetValue(double value) override;
    HRESULT STDMETHODCALLTYPE get_Value(double* out) override;
    HRESULT STDMETHODCALLTYPE get_Maximum(double* out) override;
    HRESULT STDMETHODCALLTYPE get_Minimum(double* out) override;
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* out) override;
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* out) override;
    HRESULT STDMETHODCALLTYPE Expand() override;
    HRESULT STDMETHODCALLTYPE Collapse() override;
    HRESULT STDMETHODCALLTYPE
    get_ExpandCollapseState(ExpandCollapseState* out) override;
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* out) override;
    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* out) override;
    HRESULT STDMETHODCALLTYPE Select() override;
    HRESULT STDMETHODCALLTYPE AddToSelection() override;
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override;
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* out) override;
    HRESULT STDMETHODCALLTYPE
    get_SelectionContainer(IRawElementProviderSimple** out) override;
    HRESULT STDMETHODCALLTYPE GetVisibleRanges(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE RangeFromChild(IRawElementProviderSimple* child,
                                             ITextRangeProvider** out) override;
    HRESULT STDMETHODCALLTYPE RangeFromPoint(UiaPoint point,
                                             ITextRangeProvider** out) override;
    HRESULT STDMETHODCALLTYPE
    get_DocumentRange(ITextRangeProvider** out) override;
    HRESULT STDMETHODCALLTYPE
    get_SupportedTextSelection(SupportedTextSelection* out) override;
    HRESULT STDMETHODCALLTYPE GetItem(int row, int column,
                                      IRawElementProviderSimple** out) override;
    HRESULT STDMETHODCALLTYPE get_RowCount(int* out) override;
    HRESULT STDMETHODCALLTYPE get_ColumnCount(int* out) override;
    HRESULT STDMETHODCALLTYPE get_Row(int* out) override;
    HRESULT STDMETHODCALLTYPE get_Column(int* out) override;
    HRESULT STDMETHODCALLTYPE get_RowSpan(int* out) override;
    HRESULT STDMETHODCALLTYPE get_ColumnSpan(int* out) override;
    HRESULT STDMETHODCALLTYPE
    get_ContainingGrid(IRawElementProviderSimple** out) override;
    HRESULT STDMETHODCALLTYPE GetRowHeaders(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE GetColumnHeaders(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE
    get_RowOrColumnMajor(RowOrColumnMajor* out) override;
    HRESULT STDMETHODCALLTYPE GetRowHeaderItems(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE GetColumnHeaderItems(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE SetVisualState(WindowVisualState) override;
    HRESULT STDMETHODCALLTYPE Close() override;
    HRESULT STDMETHODCALLTYPE WaitForInputIdle(int, BOOL* out) override;
    HRESULT STDMETHODCALLTYPE get_CanMaximize(BOOL* out) override;
    HRESULT STDMETHODCALLTYPE get_CanMinimize(BOOL* out) override;
    HRESULT STDMETHODCALLTYPE get_IsModal(BOOL* out) override;
    HRESULT STDMETHODCALLTYPE
    get_WindowVisualState(WindowVisualState* out) override;
    HRESULT STDMETHODCALLTYPE
    get_WindowInteractionState(WindowInteractionState* out) override;
    HRESULT STDMETHODCALLTYPE get_IsTopmost(BOOL* out) override;
};

// A text range stores UTF-16 offsets, which is what UI Automation speaks.
// The owner and node id are stable; every operation resolves the current
// InputState and clamps the offsets after an edit instead of retaining text.
struct WinTextRange : ComObject<ITextRangeProvider> {
    WinAccessibility* root = nullptr;
    uint32_t id = 0;
    int start = 0;
    int end = 0;

    WinTextRange(WinAccessibility* owner, uint32_t nodeId, int lo, int hi)
        : root(owner), id(nodeId), start(lo), end(hi) {
        root->AddRef();
    }
    ~WinTextRange() override { root->Release(); }

    const AccessibilityNode* Node() const { return root->Node(id); }
    Str Text() const {
        const AccessibilityNode* node = Node();
        return node ? WindowAccessibilityTextValue(*node) : Str{};
    }
    int Length() const { return RopeOffsetToOffsetUtf16(Text(), len(Text())); }
    void Clamp() {
        int n = Length();
        start = std::max(0, std::min(start, n));
        end = std::max(start, std::min(end, n));
    }
    // Clamps the range and copies the document it is in, which the caller
    // frees; ELEMENTNOTAVAILABLE once the node is gone.
    HRESULT ClampedText(BSTR* doc) {
        *doc = nullptr;
        if (!Text().s) {
            return UIA_E_ELEMENTNOTAVAILABLE;
        }
        Clamp();
        *doc = AccessibilityBstr(Text());
        return *doc ? S_OK : E_OUTOFMEMORY;
    }

    HRESULT STDMETHODCALLTYPE Clone(ITextRangeProvider** out) override;
    HRESULT STDMETHODCALLTYPE Compare(ITextRangeProvider* range,
                                      BOOL* out) override;
    HRESULT STDMETHODCALLTYPE CompareEndpoints(
        TextPatternRangeEndpoint endpoint, ITextRangeProvider* target,
        TextPatternRangeEndpoint targetEndpoint, int* out) override;
    HRESULT STDMETHODCALLTYPE ExpandToEnclosingUnit(TextUnit unit) override;
    HRESULT STDMETHODCALLTYPE FindAttribute(TEXTATTRIBUTEID attribute,
                                            VARIANT value, BOOL backward,
                                            ITextRangeProvider** out) override;
    HRESULT STDMETHODCALLTYPE FindText(BSTR text, BOOL backward,
                                       BOOL ignoreCase,
                                       ITextRangeProvider** out) override;
    HRESULT STDMETHODCALLTYPE GetAttributeValue(TEXTATTRIBUTEID attribute,
                                                VARIANT* out) override;
    HRESULT STDMETHODCALLTYPE GetBoundingRectangles(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE
    GetEnclosingElement(IRawElementProviderSimple** out) override;
    HRESULT STDMETHODCALLTYPE GetText(int maxLength, BSTR* out) override;
    HRESULT STDMETHODCALLTYPE Move(TextUnit unit, int count, int* out) override;
    HRESULT STDMETHODCALLTYPE
    MoveEndpointByUnit(TextPatternRangeEndpoint endpoint, TextUnit unit,
                       int count, int* out) override;
    HRESULT STDMETHODCALLTYPE MoveEndpointByRange(
        TextPatternRangeEndpoint endpoint, ITextRangeProvider* target,
        TextPatternRangeEndpoint targetEndpoint) override;
    HRESULT STDMETHODCALLTYPE Select() override;
    HRESULT STDMETHODCALLTYPE AddToSelection() override;
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override;
    HRESULT STDMETHODCALLTYPE ScrollIntoView(BOOL alignToTop) override;
    HRESULT STDMETHODCALLTYPE GetChildren(SAFEARRAY** out) override;
};

// A document source's part (AccessibilityDocumentSource::partCount): a
// read-only element whose name, text and bounds are asked of the source.
// It follows its node's element children, like orig SumatraPDF's pages.
struct WinAccessibilityPart
    : ComObject<IRawElementProviderSimple, IRawElementProviderFragment,
                IValueProvider> {
    WinAccessibility* root = nullptr;
    uint32_t id = 0;
    int part = 0;

    WinAccessibilityPart(WinAccessibility* owner, uint32_t nodeId, int at)
        : root(owner), id(nodeId), part(at) {
        root->AddRef();
    }
    ~WinAccessibilityPart() override { root->Release(); }

    // The owning node while it still has this part.
    const AccessibilityNode* Node() const {
        const AccessibilityNode* node = root->Node(id);
        if (!node || part >= WindowAccessibilityPartCount(*node)) {
            return nullptr;
        }
        return node;
    }

    WIN_UIA_FRAGMENT_METHODS
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override;
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override;
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override;
};

IRawElementProviderFragment* WinAccessibility::NewNode(int index) {
    if (!win || index < 0 || index >= len(win->accessibility)) {
        return nullptr;
    }
    return static_cast<IRawElementProviderFragment*>(
        new WinAccessibilityNode(this, win->accessibility[index].id));
}

IRawElementProviderFragment* WinAccessibility::NewPart(int index, int part) {
    if (!win || index < 0 || index >= len(win->accessibility)) {
        return nullptr;
    }
    const AccessibilityNode& node = win->accessibility[index];
    if (part < 0 || part >= WindowAccessibilityPartCount(node)) {
        return nullptr;
    }
    return static_cast<IRawElementProviderFragment*>(
        new WinAccessibilityPart(this, node.id, part));
}

// Window coordinates (DIPs) to a UIA screen rectangle.
static void AccessibilityScreenRect(const WinAccessibility* root,
                                    const Bounds& b, UiaRect* out) {
    POINT origin = {};
    ClientToScreen(root->hwnd, &origin);
    float scale = DipScale(root->win);
    out->left = (float)origin.x + b.x * scale;
    out->top = (float)origin.y + b.y * scale;
    out->width = b.w * scale;
    out->height = b.h * scale;
}

static int AccessibilityAncestor(const WinAccessibility* root, int index,
                                 AccessibilityRole role, bool includeSelf) {
    if (!root || !root->win || index < 0 ||
        index >= len(root->win->accessibility)) {
        return -1;
    }
    int at = includeSelf ? index : root->win->accessibility[index].parent;
    while (at >= 0 && at < len(root->win->accessibility)) {
        if (root->win->accessibility[at].info.role == role) {
            return at;
        }
        at = root->win->accessibility[at].parent;
    }
    return -1;
}

static int AccessibilityGridRow(const WinAccessibility* root, int index) {
    if (!root || !root->win) {
        return -1;
    }
    int at = index;
    while (at >= 0 && at < len(root->win->accessibility)) {
        const AccessibilityInfo& info = root->win->accessibility[at].info;
        if (info.hasRowIndex) {
            return std::max(0, info.rowIndex - 1);
        }
        at = root->win->accessibility[at].parent;
    }
    return -1;
}

static int AccessibilityGridColumn(const WinAccessibility* root, int index) {
    if (!root || !root->win || index < 0 ||
        index >= len(root->win->accessibility)) {
        return -1;
    }
    const AccessibilityInfo& info = root->win->accessibility[index].info;
    return info.hasColumnIndex ? std::max(0, info.columnIndex - 1) : -1;
}

HRESULT WinAccessibilityNode::CellTable(int* index, int* table) const {
    const AccessibilityNode* node = nullptr;
    if (HRESULT hr = NodeOfRole(AccessibilityRole::Cell, &node); FAILED(hr)) {
        return hr;
    }
    *index = root->NodeIndex(id);
    *table =
        AccessibilityAncestor(root, *index, AccessibilityRole::Table, false);
    return S_OK;
}

static HRESULT AccessibilitySimpleAt(WinAccessibility* root, int index,
                                     IRawElementProviderSimple** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    IRawElementProviderFragment* fragment =
        root ? root->NewNode(index) : nullptr;
    if (!fragment) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT hr = fragment->QueryInterface(__uuidof(IRawElementProviderSimple),
                                          (void**)out);
    fragment->Release();
    return hr;
}

static HRESULT AccessibilityProviderArray(WinAccessibility* root,
                                          int tableIndex,
                                          AccessibilityRole role, int column,
                                          SAFEARRAY** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    if (!root || !root->win || tableIndex < 0) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    int count = 0;
    for (int i = 0; i < len(root->win->accessibility); i++) {
        const AccessibilityNode& node = root->win->accessibility[i];
        if (node.info.role == role &&
            AccessibilityAncestor(root, i, AccessibilityRole::Table, false) ==
                tableIndex &&
            (column < 0 || AccessibilityGridColumn(root, i) == column)) {
            count++;
        }
    }
    SAFEARRAY* values = SafeArrayCreateVector(VT_UNKNOWN, 0, count);
    if (!values) {
        return E_OUTOFMEMORY;
    }
    LONG at = 0;
    for (int i = 0; i < len(root->win->accessibility); i++) {
        const AccessibilityNode& node = root->win->accessibility[i];
        if (node.info.role != role ||
            AccessibilityAncestor(root, i, AccessibilityRole::Table, false) !=
                tableIndex ||
            (column >= 0 && AccessibilityGridColumn(root, i) != column)) {
            continue;
        }
        IRawElementProviderSimple* provider = nullptr;
        HRESULT hr = AccessibilitySimpleAt(root, i, &provider);
        if (FAILED(hr) || !provider ||
            FAILED(SafeArrayPutElement(values, &at, provider))) {
            if (provider) {
                provider->Release();
            }
            SafeArrayDestroy(values);
            return FAILED(hr) ? hr : E_OUTOFMEMORY;
        }
        provider->Release();
        at++;
    }
    *out = values;
    return S_OK;
}

static HRESULT AccessibilityEmptyProviderArray(SAFEARRAY** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT AccessibilityRangeArray(WinTextRange* range, SAFEARRAY** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = SafeArrayCreateVector(VT_UNKNOWN, 0, range ? 1 : 0);
    if (!*out) {
        if (range) {
            range->Release();
        }
        return E_OUTOFMEMORY;
    }
    if (range) {
        LONG at = 0;
        IUnknown* value = static_cast<ITextRangeProvider*>(range);
        HRESULT hr = SafeArrayPutElement(*out, &at, value);
        range->Release();
        if (FAILED(hr)) {
            SafeArrayDestroy(*out);
            *out = nullptr;
            return hr;
        }
    }
    return S_OK;
}

HRESULT WinAccessibility::get_ProviderOptions(ProviderOptions* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = ProviderOptions_ServerSideProvider;
    return S_OK;
}

HRESULT WinAccessibility::GetPatternProvider(PATTERNID, IUnknown** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    return S_OK;
}

HRESULT WinAccessibility::GetPropertyValue(PROPERTYID property, VARIANT* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    VariantInit(out);
    if (!win || !hwnd) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (property == UIA_ControlTypePropertyId) {
        VariantInt(out, UIA_PaneControlTypeId);
    } else if (property == UIA_NamePropertyId) {
        int n = GetWindowTextLengthW(hwnd);
        BSTR value = SysAllocStringLen(nullptr, (UINT)n);
        if (!value && n > 0) {
            return E_OUTOFMEMORY;
        }
        if (value) {
            GetWindowTextW(hwnd, value, n + 1);
        }
        out->vt = VT_BSTR;
        out->bstrVal = value;
    } else if (property == UIA_IsControlElementPropertyId ||
               property == UIA_IsContentElementPropertyId ||
               property == UIA_IsEnabledPropertyId ||
               property == UIA_IsKeyboardFocusablePropertyId) {
        VariantBool(out, true);
    } else if (property == UIA_HasKeyboardFocusPropertyId) {
        VariantBool(out, ::GetFocus() == hwnd);
    }
    return S_OK;
}

HRESULT WinAccessibility::get_HostRawElementProvider(
    IRawElementProviderSimple** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    return hwnd ? UiaHostProviderFromHwnd(hwnd, out)
                : UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT WinAccessibility::Navigate(NavigateDirection direction,
                                   IRawElementProviderFragment** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    if (!win) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (direction != NavigateDirection_FirstChild &&
        direction != NavigateDirection_LastChild) {
        return S_OK;
    }
    int found = -1;
    for (int i = 0; i < len(win->accessibility); i++) {
        if (win->accessibility[i].parent != -1) {
            continue;
        }
        found = i;
        if (direction == NavigateDirection_FirstChild) {
            break;
        }
    }
    *out = NewNode(found);
    return S_OK;
}

HRESULT WinAccessibility::GetRuntimeId(SAFEARRAY** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    return S_OK;
}

HRESULT WinAccessibility::get_BoundingRectangle(UiaRect* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = {};
    if (!hwnd) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    RECT rect = {};
    POINT origin = {};
    GetClientRect(hwnd, &rect);
    ClientToScreen(hwnd, &origin);
    out->left = origin.x;
    out->top = origin.y;
    out->width = rect.right - rect.left;
    out->height = rect.bottom - rect.top;
    return S_OK;
}

HRESULT WinAccessibility::GetEmbeddedFragmentRoots(SAFEARRAY** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    return S_OK;
}

HRESULT WinAccessibility::SetFocus() {
    if (!hwnd) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    ::SetFocus(hwnd);
    return S_OK;
}

HRESULT WinAccessibility::get_FragmentRoot(
    IRawElementProviderFragmentRoot** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = static_cast<IRawElementProviderFragmentRoot*>(this);
    AddRef();
    return S_OK;
}

HRESULT WinAccessibility::ElementProviderFromPoint(
    double x, double y, IRawElementProviderFragment** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    if (!win || !hwnd) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    POINT point = {(LONG)x, (LONG)y};
    ScreenToClient(hwnd, &point);
    float scale = DipScale(win);
    float px = (float)point.x / scale;
    float py = (float)point.y / scale;
    int found = -1;
    for (int i = 0; i < len(win->accessibility); i++) {
        const Bounds& b = win->accessibility[i].bounds;
        if (px >= b.x && px <= b.Right() && py >= b.y && py <= b.Bottom()) {
            // The tree is preorder; a later containing node is a deeper child
            // or a later painted sibling and is the one hit-testing should use.
            found = i;
        }
    }
    // a document's part under the point, when no element child is
    int parts = found >= 0
                    ? WindowAccessibilityPartCount(win->accessibility[found])
                    : 0;
    for (int part = 0; part < parts; part++) {
        Bounds b;
        if (!WindowAccessibilityPartBounds(win->accessibility[found], part,
                                           &b)) {
            continue;
        }
        if (px >= b.x && px <= b.Right() && py >= b.y && py <= b.Bottom()) {
            *out = NewPart(found, part);
            return S_OK;
        }
    }
    *out = NewNode(found);
    return S_OK;
}

HRESULT WinAccessibility::GetFocus(IRawElementProviderFragment** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    if (!win) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    for (int i = 0; i < len(win->accessibility); i++) {
        const AccessibilityNode& node = win->accessibility[i];
        if (node.focusId && node.focusId == win->focusId) {
            *out = NewNode(i);
            break;
        }
    }
    return S_OK;
}

HRESULT WinAccessibilityNode::get_ProviderOptions(ProviderOptions* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = ProviderOptions_ServerSideProvider;
    return S_OK;
}

HRESULT WinAccessibilityNode::GetPatternProvider(PATTERNID pattern,
                                                 IUnknown** out) {
    const AccessibilityNode* node = nullptr;
    if (HRESULT hr = ClearedNode(out, &node); FAILED(hr)) {
        return hr;
    }
    if (pattern == UIA_InvokePatternId && AccessibilityInvokePattern(*node)) {
        return QueryInterface(__uuidof(IInvokeProvider), (void**)out);
    }
    if (pattern == UIA_TogglePatternId &&
        node->info.toggled != AccessibilityToggled::Unset) {
        return QueryInterface(__uuidof(IToggleProvider), (void**)out);
    }
    if (pattern == UIA_ValuePatternId &&
        AccessibilityTextRole(node->info.role)) {
        return QueryInterface(__uuidof(IValueProvider), (void**)out);
    }
    if (pattern == UIA_RangeValuePatternId && node->slider &&
        node->info.hasNumericValue) {
        return QueryInterface(__uuidof(IRangeValueProvider), (void**)out);
    }
    if (pattern == UIA_ExpandCollapsePatternId && node->info.hasExpanded) {
        return QueryInterface(__uuidof(IExpandCollapseProvider), (void**)out);
    }
    if (pattern == UIA_SelectionPatternId &&
        AccessibilitySelectionContainerRole(node->info.role)) {
        return QueryInterface(__uuidof(ISelectionProvider), (void**)out);
    }
    if (pattern == UIA_SelectionItemPatternId && node->info.hasSelected &&
        AccessibilitySelectionItemRole(node->info.role)) {
        return QueryInterface(__uuidof(ISelectionItemProvider), (void**)out);
    }
    if (pattern == UIA_TextPatternId && AccessibilityTextPattern(*node)) {
        return QueryInterface(__uuidof(ITextProvider), (void**)out);
    }
    if (pattern == UIA_WindowPatternId &&
        AccessibilityIsDialog(node->info.role)) {
        return QueryInterface(__uuidof(IWindowProvider), (void**)out);
    }
    if (pattern == UIA_GridPatternId &&
        node->info.role == AccessibilityRole::Table && node->info.hasRowCount &&
        node->info.hasColumnCount) {
        return QueryInterface(__uuidof(IGridProvider), (void**)out);
    }
    if (pattern == UIA_TablePatternId &&
        node->info.role == AccessibilityRole::Table && node->info.hasRowCount &&
        node->info.hasColumnCount) {
        return QueryInterface(__uuidof(ITableProvider), (void**)out);
    }
    if (pattern == UIA_GridItemPatternId &&
        node->info.role == AccessibilityRole::Cell &&
        node->info.hasColumnIndex &&
        AccessibilityGridRow(root, root->NodeIndex(id)) >= 0) {
        return QueryInterface(__uuidof(IGridItemProvider), (void**)out);
    }
    if (pattern == UIA_TableItemPatternId &&
        node->info.role == AccessibilityRole::Cell &&
        node->info.hasColumnIndex &&
        AccessibilityGridRow(root, root->NodeIndex(id)) >= 0) {
        return QueryInterface(__uuidof(ITableItemProvider), (void**)out);
    }
    return S_OK;
}

struct WinPropertyValue {
    VARTYPE type = VT_EMPTY;
    Str text;
    const WCHAR* wide = nullptr;
    int integer = 0;
    double number = 0;

    WinPropertyValue() = default;
    WinPropertyValue(Str value) : type(VT_BSTR), text(value) {}
    WinPropertyValue(const WCHAR* value) : type(VT_BSTR), wide(value) {}
    WinPropertyValue(bool value) : type(VT_BOOL), integer(value) {}
    WinPropertyValue(int value) : type(VT_I4), integer(value) {}
    WinPropertyValue(double value) : type(VT_R8), number(value) {}

    bool SameAs(const WinPropertyValue& other) const {
        if (type != other.type) return false;
        if (type == VT_R8) return number == other.number;
        if (type != VT_BSTR) return integer == other.integer;
        return wide || other.wide
                   ? wide && other.wide && wcscmp(wide, other.wide) == 0
                   : StrEq(text, other.text);
    }

    void ToVariant(VARIANT* out) const {
        switch (type) {
            case VT_BOOL:
                VariantBool(out, integer != 0);
                break;
            case VT_I4:
                VariantInt(out, integer);
                break;
            case VT_R8:
                VariantDouble(out, number);
                break;
            case VT_BSTR:
                if (wide) {
                    VariantInit(out);
                    out->vt = VT_BSTR;
                    out->bstrVal = SysAllocString(wide);
                } else {
                    VariantString(out, text);
                }
                break;
            default:
                VariantInit(out);
                break;
        }
    }
};

static ToggleState WinToggleState(AccessibilityToggled toggled) {
    return toggled == AccessibilityToggled::Mixed  ? ToggleState_Indeterminate
           : toggled == AccessibilityToggled::True ? ToggleState_On
                                                   : ToggleState_Off;
}

static WinPropertyValue WinSemanticProperty(const WinNodeProperties& node,
                                            PROPERTYID property) {
    const AccessibilityInfo& info = node.info;
    if (property == UIA_ControlTypePropertyId) {
        return AccessibilityControlType(info.role);
    } else if (property == UIA_NamePropertyId) {
        return info.PlatformName();
    } else if (property == UIA_AutomationIdPropertyId) {
        return info.authorId;
    } else if (property == UIA_FullDescriptionPropertyId) {
        // accesskit_windows maps a node's description here, and leaves
        // HelpText to the placeholder.
        return info.description;
    } else if (property == UIA_HelpTextPropertyId) {
        return info.placeholder;
    } else if (property == UIA_AriaRolePropertyId) {
        return AccessibilityRoleName(info.role);
    } else if (property == UIA_LiveSettingPropertyId) {
        return (int)info.live;
    } else if (property == UIA_IsEnabledPropertyId) {
        return !info.disabled;
    } else if (property == UIA_IsControlElementPropertyId ||
               property == UIA_IsContentElementPropertyId) {
        return true;
    } else if (property == UIA_IsKeyboardFocusablePropertyId) {
        return (node.actions & AccessibilityActionFocus) != 0;
    } else if (property == UIA_IsPasswordPropertyId) {
        return info.role == AccessibilityRole::PasswordInput;
    } else if (property == UIA_IsDialogPropertyId) {
        return AccessibilityIsDialog(info.role);
    } else if (property == UIA_WindowIsModalPropertyId &&
               AccessibilityIsDialog(info.role)) {
        return false;
    } else if (property == UIA_OrientationPropertyId &&
               info.orientation != AccessibilityOrientation::Unset) {
        return (int)(info.orientation == AccessibilityOrientation::Vertical
                         ? OrientationType_Vertical
                         : OrientationType_Horizontal);
    } else if (property == UIA_PositionInSetPropertyId &&
               info.hasPositionInSet) {
        return info.positionInSet;
    } else if (property == UIA_SizeOfSetPropertyId && info.hasSizeOfSet) {
        return info.sizeOfSet;
    } else if (property == UIA_LevelPropertyId && info.hasLevel) {
        return info.level;
    } else if (property == UIA_SelectionItemIsSelectedPropertyId &&
               info.hasSelected) {
        return (bool)info.selected;
    } else if (property == UIA_GridRowCountPropertyId && info.hasRowCount) {
        return info.rowCount;
    } else if (property == UIA_GridColumnCountPropertyId &&
               info.hasColumnCount) {
        return info.columnCount;
    } else if (property == UIA_GridItemRowPropertyId && info.hasRowIndex) {
        return std::max(0, info.rowIndex - 1);
    } else if (property == UIA_GridItemColumnPropertyId &&
               info.hasColumnIndex) {
        return std::max(0, info.columnIndex - 1);
    } else if (property == UIA_ToggleToggleStatePropertyId &&
               info.toggled != AccessibilityToggled::Unset) {
        return (int)WinToggleState(info.toggled);
    } else if (property == UIA_ValueValuePropertyId &&
               AccessibilityTextRole(info.role)) {
        return info.value;
    } else if (property == UIA_ValueIsReadOnlyPropertyId &&
               AccessibilityTextRole(info.role)) {
        return !(node.actions & AccessibilityActionSetValue);
    } else if (property == UIA_RangeValueValuePropertyId && node.hasRange) {
        return node.value;
    } else if (property == UIA_RangeValueMinimumPropertyId && node.hasRange) {
        return node.minimum;
    } else if (property == UIA_RangeValueMaximumPropertyId && node.hasRange) {
        return node.maximum;
    } else if (property == UIA_ExpandCollapseExpandCollapseStatePropertyId &&
               info.hasExpanded) {
        return (int)(info.expanded ? ExpandCollapseState_Expanded
                                   : ExpandCollapseState_Collapsed);
    }
    return {};
}

HRESULT WinAccessibilityNode::GetPropertyValue(PROPERTYID property,
                                               VARIANT* out) {
    if (!out) return E_INVALIDARG;
    VariantInit(out);
    const AccessibilityNode* node = Node();
    if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
    if (property == UIA_HasKeyboardFocusPropertyId) {
        VariantBool(out, node->focusId && node->focusId == root->win->focusId);
    } else if (property == UIA_IsOffscreenPropertyId && root->hwnd) {
        RECT client = {};
        GetClientRect(root->hwnd, &client);
        float scale = DipScale(root->win);
        bool off = node->bounds.Right() <= 0 || node->bounds.Bottom() <= 0 ||
                   node->bounds.x * scale >= (float)client.right ||
                   node->bounds.y * scale >= (float)client.bottom;
        VariantBool(out, off);
    } else {
        WinSemanticProperty(WinNodeProperties(*node), property).ToVariant(out);
    }
    return S_OK;
}

HRESULT WinAccessibilityNode::get_HostRawElementProvider(
    IRawElementProviderSimple** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    return S_OK;
}

HRESULT WinAccessibilityNode::Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** out) {
    int index = -1;
    if (HRESULT hr = ClearedIndex(out, &index); FAILED(hr)) {
        return hr;
    }
    const AccessibilityNode& node = root->win->accessibility[index];
    if (direction == NavigateDirection_Parent) {
        if (node.parent < 0) {
            *out = static_cast<IRawElementProviderFragment*>(root);
            root->AddRef();
        } else {
            *out = root->NewNode(node.parent);
        }
        return S_OK;
    }
    if (direction == NavigateDirection_FirstChild ||
        direction == NavigateDirection_LastChild) {
        int child = -1;
        for (int i = 0; i < len(root->win->accessibility); i++) {
            if (root->win->accessibility[i].parent != index) {
                continue;
            }
            child = i;
            if (direction == NavigateDirection_FirstChild) {
                break;
            }
        }
        // a document's parts follow its element children
        int parts = WindowAccessibilityPartCount(node);
        if (parts > 0 &&
            (direction == NavigateDirection_LastChild || child < 0)) {
            int part = direction == NavigateDirection_LastChild ? parts - 1 : 0;
            *out = root->NewPart(index, part);
            return S_OK;
        }
        *out = root->NewNode(child);
        return S_OK;
    }
    int sibling = -1;
    if (direction == NavigateDirection_NextSibling) {
        for (int i = index + 1; i < len(root->win->accessibility); i++) {
            if (root->win->accessibility[i].parent == node.parent) {
                sibling = i;
                break;
            }
        }
        // a document's last element child is followed by its first part
        if (sibling < 0 && node.parent >= 0 &&
            WindowAccessibilityPartCount(
                root->win->accessibility[node.parent]) > 0) {
            *out = root->NewPart(node.parent, 0);
            return S_OK;
        }
    } else if (direction == NavigateDirection_PreviousSibling) {
        for (int i = index - 1; i >= 0; i--) {
            if (root->win->accessibility[i].parent == node.parent) {
                sibling = i;
                break;
            }
        }
    }
    *out = root->NewNode(sibling);
    return S_OK;
}

// {UiaAppendRuntimeId, node id} for a node, {..., node id, part + 1} for a
// part: the lengths keep them apart.
static HRESULT AccessibilityRuntimeId(uint32_t id, int part, SAFEARRAY** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    int n = part < 0 ? 2 : 3;
    *out = SafeArrayCreateVector(VT_I4, 0, n);
    if (!*out) {
        return E_OUTOFMEMORY;
    }
    LONG* values = nullptr;
    HRESULT hr = SafeArrayAccessData(*out, (void**)&values);
    if (FAILED(hr)) {
        SafeArrayDestroy(*out);
        *out = nullptr;
        return hr;
    }
    values[0] = UiaAppendRuntimeId;
    values[1] = (LONG)id;
    if (part >= 0) {
        values[2] = (LONG)part + 1;
    }
    SafeArrayUnaccessData(*out);
    return S_OK;
}

HRESULT WinAccessibilityNode::GetRuntimeId(SAFEARRAY** out) {
    return AccessibilityRuntimeId(id, -1, out);
}

HRESULT WinAccessibilityNode::get_BoundingRectangle(UiaRect* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = {};
    const AccessibilityNode* node = Node();
    if (!node || !root->hwnd) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    AccessibilityScreenRect(root, node->bounds, out);
    return S_OK;
}

HRESULT WinAccessibilityNode::GetEmbeddedFragmentRoots(SAFEARRAY** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    return S_OK;
}

HRESULT WinAccessibilityNode::SetFocus() {
    if (!Node() || !root->hwnd) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    ::SetFocus(root->hwnd);
    return WindowAccessibilityPerform(root->win, id, AccessibilityAction::Focus)
               ? S_OK
               : UIA_E_INVALIDOPERATION;
}

HRESULT WinAccessibilityNode::get_FragmentRoot(
    IRawElementProviderFragmentRoot** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    if (!root->win) {
        *out = nullptr;
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = static_cast<IRawElementProviderFragmentRoot*>(root);
    root->AddRef();
    return S_OK;
}

HRESULT WinAccessibilityNode::Invoke() {
    if (!Node()) return UIA_E_ELEMENTNOTAVAILABLE;
    return WindowAccessibilityPerform(root->win, id,
                                      AccessibilityAction::Default)
               ? S_OK
               : UIA_E_INVALIDOPERATION;
}

HRESULT WinAccessibilityNode::Toggle() {
    const AccessibilityNode* node = Node();
    if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
    return node->info.toggled != AccessibilityToggled::Unset &&
                   WindowAccessibilityPerform(root->win, id,
                                              AccessibilityAction::Default)
               ? S_OK
               : UIA_E_INVALIDOPERATION;
}

HRESULT WinAccessibilityNode::get_ToggleState(ToggleState* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = WinToggleState(node->info.toggled);
    return S_OK;
}

HRESULT WinAccessibilityNode::SetValue(LPCWSTR value) {
    if (!root->win) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    int wn = value ? (int)wcslen(value) : 0;
    int n = wn ? WideCharToMultiByte(CP_UTF8, 0, value, wn, nullptr, 0, nullptr,
                                     nullptr)
               : 0;
    char* text = n ? (char*)Alloc(nullptr, n) : nullptr;
    if (n && !text) {
        return E_OUTOFMEMORY;
    }
    if (n) {
        WideCharToMultiByte(CP_UTF8, 0, value, wn, text, n, nullptr, nullptr);
    }
    bool changed = WindowAccessibilityPerform(
        root->win, id, AccessibilityAction::SetValue, Str(text, n));
    if (text) {
        Free(nullptr, text);
    }
    return changed ? S_OK : UIA_E_INVALIDOPERATION;
}

HRESULT WinAccessibilityNode::get_Value(BSTR* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node) {
        *out = nullptr;
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = AccessibilityBstr(node->info.value);
    return *out ? S_OK : E_OUTOFMEMORY;
}

HRESULT WinAccessibilityNode::get_IsReadOnly(BOOL* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = node->slider
               ? (node->info.disabled ? TRUE : FALSE)
               : ((node->actions & AccessibilityActionSetValue) ? FALSE : TRUE);
    return S_OK;
}

HRESULT WinAccessibilityNode::SetValue(double value) {
    return root->win && WindowAccessibilitySetNumericValue(root->win, id,
                                                           (float)value)
               ? S_OK
               : UIA_E_INVALIDOPERATION;
}

HRESULT WinAccessibilityNode::get_Value(double* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node || !node->slider) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = node->slider->value.End();
    return S_OK;
}

HRESULT WinAccessibilityNode::get_Maximum(double* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node || !node->slider) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = node->slider->max;
    return S_OK;
}

HRESULT WinAccessibilityNode::get_Minimum(double* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node || !node->slider) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = node->slider->min;
    return S_OK;
}

HRESULT WinAccessibilityNode::get_LargeChange(double* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node || !node->slider) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    double step = node->slider->step > 0
                      ? node->slider->step
                      : (node->slider->max - node->slider->min) / 100.0;
    *out = step * 10;
    return S_OK;
}

HRESULT WinAccessibilityNode::get_SmallChange(double* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node || !node->slider) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = node->slider->step > 0
               ? node->slider->step
               : (node->slider->max - node->slider->min) / 100.0;
    return S_OK;
}

HRESULT WinAccessibilityNode::Expand() {
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (node->info.expanded) {
        return S_OK;
    }
    return WindowAccessibilityPerform(root->win, id,
                                      AccessibilityAction::Default)
               ? S_OK
               : UIA_E_INVALIDOPERATION;
}

HRESULT WinAccessibilityNode::Collapse() {
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (!node->info.expanded) {
        return S_OK;
    }
    return WindowAccessibilityPerform(root->win, id,
                                      AccessibilityAction::Default)
               ? S_OK
               : UIA_E_INVALIDOPERATION;
}

HRESULT WinAccessibilityNode::get_ExpandCollapseState(
    ExpandCollapseState* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = node->info.expanded ? ExpandCollapseState_Expanded
                               : ExpandCollapseState_Collapsed;
    return S_OK;
}

static bool AccessibilityDescendsFrom(const Window* win, int index,
                                      int ancestor) {
    if (!win || index < 0 || ancestor < 0) {
        return false;
    }
    int at = win->accessibility[index].parent;
    while (at >= 0 && at < len(win->accessibility)) {
        if (at == ancestor) {
            return true;
        }
        if (AccessibilitySelectionContainerRole(win->accessibility[at]
                                                    .info.role)) {
            return false;
        }
        at = win->accessibility[at].parent;
    }
    return false;
}

HRESULT WinAccessibilityNode::GetSelection(SAFEARRAY** out) {
    const AccessibilityNode* node = nullptr;
    if (HRESULT hr = ClearedNode(out, &node); FAILED(hr)) {
        return hr;
    }
    if (AccessibilityTextPattern(*node)) {
        Str text = WindowAccessibilityTextValue(*node);
        Selection selection;
        if (!WindowAccessibilityTextSelection(*node, &selection)) {
            *out = SafeArrayCreateVector(VT_UNKNOWN, 0, 0);
            return *out ? S_OK : E_OUTOFMEMORY;
        }
        int lo = RopeOffsetToOffsetUtf16(text, selection.start);
        int hi = RopeOffsetToOffsetUtf16(text, selection.end);
        return AccessibilityRangeArray(
            new WinTextRange(root, id, std::min(lo, hi), std::max(lo, hi)),
            out);
    }
    if (!AccessibilitySelectionContainerRole(node->info.role)) {
        return UIA_E_INVALIDOPERATION;
    }
    int ancestor = root->NodeIndex(id);
    int count = 0;
    for (int i = 0; i < len(root->win->accessibility); i++) {
        const AccessibilityNode& candidate = root->win->accessibility[i];
        if (candidate.info.hasSelected && candidate.info.selected &&
            AccessibilityDescendsFrom(root->win, i, ancestor)) {
            count++;
        }
    }
    *out = SafeArrayCreateVector(VT_UNKNOWN, 0, count);
    if (!*out) {
        return E_OUTOFMEMORY;
    }
    LONG at = 0;
    for (int i = 0; i < len(root->win->accessibility); i++) {
        const AccessibilityNode& candidate = root->win->accessibility[i];
        if (!candidate.info.hasSelected || !candidate.info.selected ||
            !AccessibilityDescendsFrom(root->win, i, ancestor)) {
            continue;
        }
        IRawElementProviderSimple* provider = nullptr;
        HRESULT hr = AccessibilitySimpleAt(root, i, &provider);
        if (FAILED(hr) || !provider ||
            FAILED(SafeArrayPutElement(*out, &at, provider))) {
            if (provider) {
                provider->Release();
            }
            SafeArrayDestroy(*out);
            *out = nullptr;
            return FAILED(hr) ? hr : E_OUTOFMEMORY;
        }
        provider->Release();
        at++;
    }
    return S_OK;
}

HRESULT WinAccessibilityNode::get_CanSelectMultiple(BOOL* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (!AccessibilitySelectionContainerRole(node->info.role)) {
        return UIA_E_INVALIDOPERATION;
    }
    // The portable semantic record has no multi-select policy. Every current
    // gpui-kit selection container is single-select.
    *out = FALSE;
    return S_OK;
}

HRESULT WinAccessibilityNode::get_IsSelectionRequired(BOOL* out) {
    // AccessKit does not carry this container policy in the subset GPUI
    // exports, so do not promise that an empty selection is forbidden: the
    // same checks and the same FALSE as CanSelectMultiple.
    return get_CanSelectMultiple(out);
}

HRESULT WinAccessibilityNode::Select() {
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (node->info.selected) {
        return S_OK;
    }
    return WindowAccessibilityPerform(root->win, id,
                                      AccessibilityAction::Default)
               ? S_OK
               : UIA_E_INVALIDOPERATION;
}

HRESULT WinAccessibilityNode::AddToSelection() {
    return Select();
}

HRESULT WinAccessibilityNode::RemoveFromSelection() {
    // The semantic record does not say whether its container allows an empty
    // or multiple selection. Rust keeps that policy in the widget, so asking
    // the item to remove itself cannot safely be translated into a toggle.
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return node->info.selected ? UIA_E_INVALIDOPERATION : S_OK;
}

HRESULT WinAccessibilityNode::get_IsSelected(BOOL* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = node->info.selected ? TRUE : FALSE;
    return S_OK;
}

HRESULT WinAccessibilityNode::get_SelectionContainer(
    IRawElementProviderSimple** out) {
    int index = -1;
    if (HRESULT hr = ClearedIndex(out, &index); FAILED(hr)) {
        return hr;
    }
    int parent = root->win->accessibility[index].parent;
    while (parent >= 0 && !AccessibilitySelectionContainerRole(
                              root->win->accessibility[parent].info.role)) {
        parent = root->win->accessibility[parent].parent;
    }
    if (parent < 0) {
        return root
            ->QueryInterface(__uuidof(IRawElementProviderSimple), (void**)out);
    }
    return AccessibilitySimpleAt(root, parent, out);
}

HRESULT WinAccessibilityNode::GetVisibleRanges(SAFEARRAY** out) {
    const AccessibilityNode* node = nullptr;
    if (HRESULT hr = TextNode(&node); FAILED(hr)) {
        return hr;
    }
    Str text = WindowAccessibilityTextValue(*node);
    int n = RopeOffsetToOffsetUtf16(text, len(text));
    return AccessibilityRangeArray(new WinTextRange(root, id, 0, n), out);
}

HRESULT WinAccessibilityNode::SetVisualState(WindowVisualState) {
    HRESULT hr = DialogNode();
    return FAILED(hr) ? hr : UIA_E_INVALIDOPERATION;
}

HRESULT WinAccessibilityNode::Close() {
    return UnsupportedDialogOperation();
}

HRESULT WinAccessibilityNode::WaitForInputIdle(int, BOOL* out) {
    return out ? UnsupportedDialogOperation() : E_INVALIDARG;
}

HRESULT WinAccessibilityNode::get_CanMaximize(BOOL* out) {
    return out ? UnsupportedDialogOperation() : E_INVALIDARG;
}

HRESULT WinAccessibilityNode::get_CanMinimize(BOOL* out) {
    return out ? UnsupportedDialogOperation() : E_INVALIDARG;
}

HRESULT WinAccessibilityNode::get_IsModal(BOOL* out) {
    if (!out) return E_INVALIDARG;
    if (HRESULT hr = DialogNode(); FAILED(hr)) return hr;
    // The pinned GPUI nodes have no AccessKit modal flag.
    *out = FALSE;
    return S_OK;
}

HRESULT WinAccessibilityNode::get_WindowVisualState(WindowVisualState* out) {
    return out ? UnsupportedDialogOperation() : E_INVALIDARG;
}

HRESULT WinAccessibilityNode::get_WindowInteractionState(
    WindowInteractionState* out) {
    if (!out) return E_INVALIDARG;
    if (HRESULT hr = DialogNode(); FAILED(hr)) return hr;
    *out = WindowInteractionState_ReadyForUserInteraction;
    return S_OK;
}

HRESULT WinAccessibilityNode::get_IsTopmost(BOOL* out) {
    return out ? UnsupportedDialogOperation() : E_INVALIDARG;
}

// Which part of node `id` the provider is, from its runtime id; -1 when it
// is not one.
static int AccessibilityPartOf(IRawElementProviderSimple* child, uint32_t id) {
    IRawElementProviderFragment* fragment = nullptr;
    if (!child ||
        FAILED(child->QueryInterface(__uuidof(IRawElementProviderFragment),
                                     (void**)&fragment)) ||
        !fragment) {
        return -1;
    }
    SAFEARRAY* rid = nullptr;
    HRESULT hr = fragment->GetRuntimeId(&rid);
    fragment->Release();
    if (FAILED(hr) || !rid) {
        return -1;
    }
    int part = -1;
    LONG* values = nullptr;
    if (rid->rgsabound[0].cElements == 3 &&
        SUCCEEDED(SafeArrayAccessData(rid, (void**)&values))) {
        if (values[0] == UiaAppendRuntimeId && values[1] == (LONG)id) {
            part = (int)values[2] - 1;
        }
        SafeArrayUnaccessData(rid);
    }
    SafeArrayDestroy(rid);
    return part;
}

HRESULT WinAccessibilityNode::RangeFromChild(IRawElementProviderSimple* child,
                                             ITextRangeProvider** out) {
    const AccessibilityNode* node = nullptr;
    if (HRESULT hr = ClearedTextNode(out, &node); FAILED(hr)) {
        return hr;
    }
    // Text inputs publish their document as their own value, without embedded
    // child providers, so only a document source's part names a sub-range.
    int part = AccessibilityPartOf(child, id);
    Selection range;
    if (!WindowAccessibilityPartRange(*node, part, &range)) {
        return E_INVALIDARG;
    }
    Str text = WindowAccessibilityTextValue(*node);
    int n = len(text);
    int lo =
        RopeOffsetToOffsetUtf16(text, std::max(0, std::min(range.start, n)));
    int hi = RopeOffsetToOffsetUtf16(text, std::max(0, std::min(range.end, n)));
    *out = new WinTextRange(root, id, lo, std::max(lo, hi));
    return S_OK;
}

HRESULT WinAccessibilityNode::RangeFromPoint(UiaPoint point,
                                             ITextRangeProvider** out) {
    const AccessibilityNode* node = nullptr;
    if (HRESULT hr = ClearedTextNode(out, &node); FAILED(hr)) {
        return hr;
    }
    Str text = WindowAccessibilityTextValue(*node);
    if (!node->input || node->textExcerpt.s) {
        *out = new WinTextRange(root, id, 0, 0);
        return S_OK;
    }
    POINT client = {(LONG)point.x, (LONG)point.y};
    if (!ScreenToClient(root->hwnd, &client)) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    float scale = DipScale(root->win);
    int offset =
        InputIndexForPosition(node->input, &root->win->paint,
                              (float)client.x / scale, (float)client.y / scale);
    int offsetUtf16 = RopeOffsetToOffsetUtf16(text, offset);
    *out = new WinTextRange(root, id, offsetUtf16, offsetUtf16);
    return S_OK;
}

HRESULT WinAccessibilityNode::get_DocumentRange(ITextRangeProvider** out) {
    const AccessibilityNode* node = nullptr;
    if (HRESULT hr = ClearedTextNode(out, &node); FAILED(hr)) {
        return hr;
    }
    Str text = WindowAccessibilityTextValue(*node);
    *out =
        new WinTextRange(root, id, 0, RopeOffsetToOffsetUtf16(text, len(text)));
    return S_OK;
}

HRESULT WinAccessibilityNode::get_SupportedTextSelection(
    SupportedTextSelection* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = nullptr;
    if (HRESULT hr = TextNode(&node); FAILED(hr)) {
        return hr;
    }
    *out = SupportedTextSelection_Single;
    return S_OK;
}

HRESULT WinAccessibilityNode::GetItem(int row, int column,
                                      IRawElementProviderSimple** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    int tableIndex = root->NodeIndex(id);
    const AccessibilityNode* table = Node();
    if (!table || table->info.role != AccessibilityRole::Table ||
        !table->info.hasRowCount || !table->info.hasColumnCount) {
        return UIA_E_INVALIDOPERATION;
    }
    if (row < 0 || column < 0 || row >= table->info.rowCount ||
        column >= table->info.columnCount) {
        return E_INVALIDARG;
    }
    for (int i = 0; i < len(root->win->accessibility); i++) {
        const AccessibilityNode& candidate = root->win->accessibility[i];
        if (candidate.info.role == AccessibilityRole::Cell &&
            AccessibilityAncestor(root, i, AccessibilityRole::Table, false) ==
                tableIndex &&
            AccessibilityGridRow(root, i) == row &&
            AccessibilityGridColumn(root, i) == column) {
            return AccessibilitySimpleAt(root, i, out);
        }
    }
    // A virtualized table can advertise its complete size while only the
    // visible rows exist in the current AccessKit-shaped frame tree.
    return UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT WinAccessibilityNode::get_RowCount(int* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node || node->info.role != AccessibilityRole::Table ||
        !node->info.hasRowCount) {
        return UIA_E_INVALIDOPERATION;
    }
    *out = node->info.rowCount;
    return S_OK;
}

HRESULT WinAccessibilityNode::get_ColumnCount(int* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node || node->info.role != AccessibilityRole::Table ||
        !node->info.hasColumnCount) {
        return UIA_E_INVALIDOPERATION;
    }
    *out = node->info.columnCount;
    return S_OK;
}

HRESULT WinAccessibilityNode::get_Row(int* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    int row = AccessibilityGridRow(root, root->NodeIndex(id));
    if (row < 0) {
        return UIA_E_INVALIDOPERATION;
    }
    *out = row;
    return S_OK;
}

HRESULT WinAccessibilityNode::get_Column(int* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    int column = AccessibilityGridColumn(root, root->NodeIndex(id));
    if (column < 0) {
        return UIA_E_INVALIDOPERATION;
    }
    *out = column;
    return S_OK;
}

HRESULT WinAccessibilityNode::get_RowSpan(int* out) {
    return get_ColumnSpan(out);
}

HRESULT WinAccessibilityNode::get_ColumnSpan(int* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = nullptr;
    if (HRESULT hr = NodeOfRole(AccessibilityRole::Cell, &node); FAILED(hr)) {
        return hr;
    }
    // The pinned semantic table API records one row and column index per cell
    // and no span, so one is the only value the portable tree can promise for
    // either span.
    *out = 1;
    return S_OK;
}

HRESULT WinAccessibilityNode::get_ContainingGrid(
    IRawElementProviderSimple** out) {
    int index = -1;
    int table = -1;
    if (HRESULT hr = CellTable(&index, &table); FAILED(hr)) {
        return hr;
    }
    return AccessibilitySimpleAt(root, table, out);
}

HRESULT WinAccessibilityNode::GetRowHeaders(SAFEARRAY** out) {
    const AccessibilityNode* node = nullptr;
    if (HRESULT hr = NodeOfRole(AccessibilityRole::Table, &node); FAILED(hr)) {
        return hr;
    }
    // AccessKit's role set used by these crates has ColumnHeader but no row
    // header role, so an honest empty provider array is preferable to treating
    // the first data cell as a header.
    return AccessibilityEmptyProviderArray(out);
}

HRESULT WinAccessibilityNode::GetColumnHeaders(SAFEARRAY** out) {
    const AccessibilityNode* node = nullptr;
    if (HRESULT hr = NodeOfRole(AccessibilityRole::Table, &node); FAILED(hr)) {
        return hr;
    }
    return AccessibilityProviderArray(root, root->NodeIndex(id),
                                      AccessibilityRole::ColumnHeader, -1, out);
}

HRESULT WinAccessibilityNode::get_RowOrColumnMajor(RowOrColumnMajor* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node || node->info.role != AccessibilityRole::Table) {
        return UIA_E_INVALIDOPERATION;
    }
    *out = RowOrColumnMajor_RowMajor;
    return S_OK;
}

HRESULT WinAccessibilityNode::GetRowHeaderItems(SAFEARRAY** out) {
    const AccessibilityNode* node = nullptr;
    if (HRESULT hr = NodeOfRole(AccessibilityRole::Cell, &node); FAILED(hr)) {
        return hr;
    }
    return AccessibilityEmptyProviderArray(out);
}

HRESULT WinAccessibilityNode::GetColumnHeaderItems(SAFEARRAY** out) {
    int index = -1;
    int table = -1;
    if (HRESULT hr = CellTable(&index, &table); FAILED(hr)) {
        return hr;
    }
    int column = AccessibilityGridColumn(root, index);
    return AccessibilityProviderArray(
        root, table, AccessibilityRole::ColumnHeader, column, out);
}

HRESULT WinTextRange::Clone(ITextRangeProvider** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    if (!Node()) {
        *out = nullptr;
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    Clamp();
    *out = new WinTextRange(root, id, start, end);
    return S_OK;
}

HRESULT WinTextRange::Compare(ITextRangeProvider* range, BOOL* out) {
    if (!range || !out) {
        return E_INVALIDARG;
    }
    WinTextRange* other = static_cast<WinTextRange*>(range);
    Clamp();
    other->Clamp();
    *out = other->root == root && other->id == id && other->start == start &&
                   other->end == end
               ? TRUE
               : FALSE;
    return S_OK;
}

HRESULT WinTextRange::CompareEndpoints(TextPatternRangeEndpoint endpoint,
                                       ITextRangeProvider* target,
                                       TextPatternRangeEndpoint targetEndpoint,
                                       int* out) {
    if (!target || !out) {
        return E_INVALIDARG;
    }
    WinTextRange* other = static_cast<WinTextRange*>(target);
    if (other->root != root || other->id != id) {
        return E_INVALIDARG;
    }
    Clamp();
    other->Clamp();
    int a = endpoint == TextPatternRangeEndpoint_Start ? start : end;
    int b = targetEndpoint == TextPatternRangeEndpoint_Start ? other->start
                                                             : other->end;
    *out = a - b;
    return S_OK;
}

static bool AccessibilityWideSpace(wchar_t ch) {
    return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
}

static void AccessibilityUnitBounds(Str source, BSTR text, int length, int at,
                                    TextUnit unit, int* lo, int* hi) {
    at = std::max(0, std::min(at, length));
    *lo = at;
    *hi = at;
    if (unit == TextUnit_Word) {
        while (*lo > 0 && !AccessibilityWideSpace(text[*lo - 1])) {
            (*lo)--;
        }
        while (*hi < length && !AccessibilityWideSpace(text[*hi])) {
            (*hi)++;
        }
    } else if (unit == TextUnit_Line) {
        Selection line = AccessibilityTextLineAt(
            source, RopeOffsetUtf16ToOffset(source, at));
        *lo = RopeOffsetToOffsetUtf16(source, line.start);
        *hi = RopeOffsetToOffsetUtf16(source, line.end);
    } else {
        *lo = 0;
        *hi = length;
    }
}

HRESULT WinTextRange::ExpandToEnclosingUnit(TextUnit unit) {
    Str value = Text();
    if (!value.s) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (unit == TextUnit_Character) {
        Clamp();
        int at = RopeOffsetUtf16ToOffset(value, start);
        if (at == len(value)) {
            end = start;
            return S_OK;
        }
        int after = TextNextGraphemeBoundary(value, at);
        int before = TextPreviousGraphemeBoundary(value, after);
        start = RopeOffsetToOffsetUtf16(value, before);
        end = RopeOffsetToOffsetUtf16(value, after);
        return S_OK;
    }
    BSTR text = nullptr;
    if (HRESULT hr = ClampedText(&text); FAILED(hr)) {
        return hr;
    }
    AccessibilityUnitBounds(value, text, (int)SysStringLen(text), start, unit,
                            &start, &end);
    SysFreeString(text);
    return S_OK;
}

HRESULT WinTextRange::FindAttribute(TEXTATTRIBUTEID attribute, VARIANT value,
                                    BOOL backward, ITextRangeProvider** out) {
    (void)attribute;
    (void)value;
    (void)backward;
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    return Node() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT WinTextRange::FindText(BSTR needle, BOOL backward, BOOL ignoreCase,
                               ITextRangeProvider** out) {
    if (!needle || !out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    BSTR doc = nullptr;
    if (HRESULT hr = ClampedText(&doc); FAILED(hr)) {
        return hr;
    }
    int needleLen = (int)SysStringLen(needle);
    int found = -1;
    int first = backward ? end - needleLen : start;
    int last = backward ? start : end - needleLen;
    int step = backward ? -1 : 1;
    for (int at = first; needleLen >= 0 && (backward ? at >= last : at <= last);
         at += step) {
        bool same = ignoreCase
                        ? CompareStringOrdinal(doc + at, needleLen, needle,
                                               needleLen, TRUE) == CSTR_EQUAL
                        : memcmp(doc + at, needle,
                                 (size_t)needleLen * sizeof(wchar_t)) == 0;
        if (same) {
            found = at;
            break;
        }
    }
    SysFreeString(doc);
    if (found >= 0) {
        *out = new WinTextRange(root, id, found, found + needleLen);
    }
    return S_OK;
}

HRESULT WinTextRange::GetAttributeValue(TEXTATTRIBUTEID attribute,
                                        VARIANT* out) {
    (void)attribute;
    if (!out) {
        return E_INVALIDARG;
    }
    VariantInit(out);
    if (!Node()) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    out->vt = VT_UNKNOWN;
    return UiaGetReservedNotSupportedValue(&out->punkVal);
}

HRESULT WinTextRange::GetBoundingRectangles(SAFEARRAY** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    const AccessibilityNode* node = Node();
    if (!node || !root->hwnd || !AccessibilityTextPattern(*node)) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    Clamp();
    bool visible = start != end && node->input;
    Bounds bounds = node->bounds;
    if (visible && InputIsSingleLine(node->input)) {
        Str text = Text();
        Selection range = {RopeOffsetUtf16ToOffset(text, start),
                           RopeOffsetUtf16ToOffset(text, end)};
        visible = WindowAccessibilityTextRangeBounds(root->win, *node, range,
                                                     &bounds) &&
                  bounds.w > 0;
    }
    *out = SafeArrayCreateVector(VT_R8, 0, visible ? 4 : 0);
    if (!*out) {
        return E_OUTOFMEMORY;
    }
    if (visible) {
        POINT origin = {};
        ClientToScreen(root->hwnd, &origin);
        double scale = DipScale(root->win);
        double values[4] = {(double)origin.x + bounds.x * scale,
                            (double)origin.y + bounds.y * scale,
                            bounds.w * scale, bounds.h * scale};
        for (LONG i = 0; i < 4; i++) {
            HRESULT hr = SafeArrayPutElement(*out, &i, &values[i]);
            if (FAILED(hr)) {
                SafeArrayDestroy(*out);
                *out = nullptr;
                return hr;
            }
        }
    }
    return S_OK;
}

HRESULT WinTextRange::GetEnclosingElement(IRawElementProviderSimple** out) {
    return AccessibilitySimpleAt(root, root->NodeIndex(id), out);
}

HRESULT WinTextRange::GetText(int maxLength, BSTR* out) {
    if (!out || maxLength < -1) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    BSTR doc = nullptr;
    if (HRESULT hr = ClampedText(&doc); FAILED(hr)) {
        return hr;
    }
    int n = end - start;
    if (maxLength >= 0) {
        n = std::min(n, maxLength);
    }
    *out = SysAllocStringLen(doc + start, (UINT)n);
    SysFreeString(doc);
    return *out ? S_OK : E_OUTOFMEMORY;
}

HRESULT WinTextRange::Move(TextUnit unit, int count, int* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    Clamp();
    int before = start;
    int moved = 0;
    HRESULT hr =
        MoveEndpointByUnit(TextPatternRangeEndpoint_Start, unit, count, &moved);
    if (FAILED(hr)) {
        return hr;
    }
    int width = end - before;
    end = std::min(Length(), start + std::max(0, width));
    *out = moved;
    return S_OK;
}

HRESULT WinTextRange::MoveEndpointByUnit(TextPatternRangeEndpoint endpoint,
                                         TextUnit unit, int count, int* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    Str valueText = Text();
    if (!valueText.s) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    Clamp();
    int* value = endpoint == TextPatternRangeEndpoint_Start ? &start : &end;
    int direction = count < 0 ? -1 : 1;
    int64_t wanted = (int64_t)direction * count;
    int moved = 0;
    if (unit == TextUnit_Character) {
        int offset = RopeOffsetUtf16ToOffset(valueText, *value);
        while (moved < wanted) {
            int next = direction < 0
                           ? TextPreviousGraphemeBoundary(valueText, offset)
                           : TextNextGraphemeBoundary(valueText, offset);
            if (next == offset) break;
            offset = next;
            moved++;
        }
        if (moved) *value = RopeOffsetToOffsetUtf16(valueText, offset);
    } else {
        BSTR text = AccessibilityBstr(valueText);
        if (!text) {
            return E_OUTOFMEMORY;
        }
        int length = (int)SysStringLen(text);
        while (moved < wanted) {
            int lo = 0;
            int hi = 0;
            int probe = direction < 0 ? std::max(0, *value - 1) : *value;
            AccessibilityUnitBounds(valueText, text, length, probe, unit, &lo,
                                    &hi);
            int next = direction < 0 ? lo : hi;
            if (next == *value) {
                break;
            }
            *value = next;
            moved++;
        }
        SysFreeString(text);
    }
    *out = direction * moved;
    if (start > end) {
        if (endpoint == TextPatternRangeEndpoint_Start) {
            end = start;
        } else {
            start = end;
        }
    }
    return S_OK;
}

HRESULT WinTextRange::MoveEndpointByRange(
    TextPatternRangeEndpoint endpoint, ITextRangeProvider* target,
    TextPatternRangeEndpoint targetEndpoint) {
    if (!target) {
        return E_INVALIDARG;
    }
    WinTextRange* other = static_cast<WinTextRange*>(target);
    if (other->root != root || other->id != id) {
        return E_INVALIDARG;
    }
    other->Clamp();
    int value = targetEndpoint == TextPatternRangeEndpoint_Start ? other->start
                                                                 : other->end;
    if (endpoint == TextPatternRangeEndpoint_Start) {
        start = value;
        if (start > end) {
            end = start;
        }
    } else {
        end = value;
        if (end < start) {
            start = end;
        }
    }
    return S_OK;
}

HRESULT WinTextRange::Select() {
    const AccessibilityNode* node = Node();
    if (!node || !node->input || !root->win || !root->win->app) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    Clamp();
    Str text = WindowAccessibilityTextValue(*node);
    int lo = RopeOffsetUtf16ToOffset(text, start);
    int hi = RopeOffsetUtf16ToOffset(text, end);
    return WindowAccessibilitySetTextSelection(root->win, *node, {lo, hi})
               ? S_OK
               : UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT WinTextRange::AddToSelection() {
    return UIA_E_INVALIDOPERATION;
}

HRESULT WinTextRange::RemoveFromSelection() {
    return UIA_E_INVALIDOPERATION;
}

HRESULT WinTextRange::ScrollIntoView(BOOL alignToTop) {
    (void)alignToTop;
    const AccessibilityNode* node = Node();
    if (!node || !node->input) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    Clamp();
    InputScrollToOffset(node->input,
                        RopeOffsetUtf16ToOffset(InputValue(node->input), start),
                        InputMoveDir::None);
    if (root->win) {
        AppInvalidate(root->win);
    }
    return S_OK;
}

HRESULT WinTextRange::GetChildren(SAFEARRAY** out) {
    return AccessibilityEmptyProviderArray(out);
}

HRESULT WinAccessibilityPart::get_ProviderOptions(ProviderOptions* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = ProviderOptions_ServerSideProvider;
    return S_OK;
}

HRESULT WinAccessibilityPart::GetPatternProvider(PATTERNID pattern,
                                                 IUnknown** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    if (!Node()) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (pattern == UIA_ValuePatternId) {
        return QueryInterface(__uuidof(IValueProvider), (void**)out);
    }
    return S_OK;
}

// orig SumatraPDF's PageProvider: a custom control named "Page N" whose
// read-only value is the page's text.
HRESULT WinAccessibilityPart::GetPropertyValue(PROPERTYID property,
                                               VARIANT* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    VariantInit(out);
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (property == UIA_ControlTypePropertyId) {
        VariantInt(out, UIA_CustomControlTypeId);
    } else if (property == UIA_NamePropertyId) {
        VariantString(out, WindowAccessibilityPartName(*node, part));
    } else if (property == UIA_IsControlElementPropertyId ||
               property == UIA_IsContentElementPropertyId ||
               property == UIA_IsEnabledPropertyId ||
               property == UIA_IsValuePatternAvailablePropertyId ||
               property == UIA_ValueIsReadOnlyPropertyId) {
        VariantBool(out, true);
    } else if (property == UIA_IsKeyboardFocusablePropertyId ||
               property == UIA_HasKeyboardFocusPropertyId) {
        VariantBool(out, false);
    } else if (property == UIA_ValueValuePropertyId) {
        VariantString(out, WindowAccessibilityPartText(*node, part));
    } else if (property == UIA_IsOffscreenPropertyId && root->hwnd) {
        Bounds b;
        RECT client = {};
        GetClientRect(root->hwnd, &client);
        float scale = DipScale(root->win);
        bool off = !WindowAccessibilityPartBounds(*node, part, &b) ||
                   b.Right() <= 0 || b.Bottom() <= 0 ||
                   b.x * scale >= (float)client.right ||
                   b.y * scale >= (float)client.bottom;
        VariantBool(out, off);
    }
    return S_OK;
}

HRESULT WinAccessibilityPart::get_HostRawElementProvider(
    IRawElementProviderSimple** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    return S_OK;
}

HRESULT WinAccessibilityPart::Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    int index = root->NodeIndex(id);
    if (index < 0 || !Node()) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (direction == NavigateDirection_Parent) {
        *out = root->NewNode(index);
    } else if (direction == NavigateDirection_NextSibling) {
        *out = root->NewPart(index, part + 1);
    } else if (direction == NavigateDirection_PreviousSibling) {
        if (part > 0) {
            *out = root->NewPart(index, part - 1);
            return S_OK;
        }
        // the node's last element child comes before its first part
        int child = -1;
        for (int i = index + 1; i < len(root->win->accessibility); i++) {
            if (root->win->accessibility[i].parent == index) {
                child = i;
            }
        }
        *out = root->NewNode(child);
    }
    return S_OK;
}

HRESULT WinAccessibilityPart::GetRuntimeId(SAFEARRAY** out) {
    return AccessibilityRuntimeId(id, part, out);
}

HRESULT WinAccessibilityPart::get_BoundingRectangle(UiaRect* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = {};
    const AccessibilityNode* node = Node();
    if (!node || !root->hwnd) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    Bounds b;
    if (WindowAccessibilityPartBounds(*node, part, &b)) {
        AccessibilityScreenRect(root, b, out);
    }
    return S_OK;
}

HRESULT WinAccessibilityPart::GetEmbeddedFragmentRoots(SAFEARRAY** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    return S_OK;
}

HRESULT WinAccessibilityPart::SetFocus() {
    return Node() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT WinAccessibilityPart::get_FragmentRoot(
    IRawElementProviderFragmentRoot** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    if (!root->win) {
        *out = nullptr;
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = static_cast<IRawElementProviderFragmentRoot*>(root);
    root->AddRef();
    return S_OK;
}

HRESULT WinAccessibilityPart::SetValue(LPCWSTR) {
    return Node() ? UIA_E_INVALIDOPERATION : UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT WinAccessibilityPart::get_Value(BSTR* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = AccessibilityBstr(WindowAccessibilityPartText(*node, part));
    return *out ? S_OK : E_OUTOFMEMORY;
}

HRESULT WinAccessibilityPart::get_IsReadOnly(BOOL* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    if (!Node()) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = TRUE;
    return S_OK;
}

struct WinPropertyChange {
    uint32_t id;
    PROPERTYID property;
    VARIANT before;
    VARIANT after;
    bool allowRemovedNode = false;
};
struct WinNodeEvent {
    uint32_t id;
    EVENTID event;
    bool allowRemovedNode = false;
};
struct WinSelectionDelta {
    uint32_t container;
    uint32_t id;
    bool selected;
};
static bool WinSnapshotChanges(WinAccessibility* root,
                               Vec<WinPropertyChange>* changes,
                               Vec<WinNodeEvent>* events = nullptr);

WinAccessibility* AccessibilityWinNew(Window* win, void* hwnd) {
    if (!win || !hwnd) {
        return nullptr;
    }
    auto* accessibility = new WinAccessibility(win, (HWND)hwnd);
    // The tree may have been collected before there was a provider to tell.
    WinSnapshotChanges(accessibility, nullptr);
    return accessibility;
}

void AccessibilityWinClose(WinAccessibility* accessibility) {
    if (!accessibility) {
        return;
    }
    accessibility->win = nullptr;
    accessibility->hwnd = nullptr;
    accessibility->Release();
}

intptr_t AccessibilityWinGetObject(WinAccessibility* accessibility,
                                   uintptr_t wParam, intptr_t lParam) {
    if (!accessibility || !accessibility->hwnd) {
        return 0;
    }
    return (intptr_t)UiaReturnRawElementProvider(
        accessibility->hwnd, (WPARAM)wParam, (LPARAM)lParam,
        static_cast<IRawElementProviderSimple*>(accessibility));
}

static void AccessibilityRaiseNodeEvent(WinAccessibility* root, int index,
                                        EVENTID event) {
    IRawElementProviderSimple* provider = nullptr;
    if (SUCCEEDED(AccessibilitySimpleAt(root, index, &provider)) && provider) {
        UiaRaiseAutomationEvent(provider, event);
        provider->Release();
    }
}

// A removal can refer to an item that has already left the tree. Preserve
// its runtime ID without reviving the provider's properties or actions.
static IRawElementProviderSimple* WinEventProvider(WinAccessibility* root,
                                                   uint32_t id,
                                                   bool allowRemovedNode) {
    IRawElementProviderSimple* provider = nullptr;
    if (SUCCEEDED(AccessibilitySimpleAt(root, root->NodeIndex(id), &provider)))
        return provider;
    return allowRemovedNode && root->win
               ? static_cast<IRawElementProviderSimple*>(
                     new WinAccessibilityNode(root, id))
               : nullptr;
}

static constexpr PROPERTYID kWinTrackedProperties[] = {
    UIA_ControlTypePropertyId,
    UIA_NamePropertyId,
    UIA_AutomationIdPropertyId,
    UIA_FullDescriptionPropertyId,
    UIA_HelpTextPropertyId,
    UIA_AriaRolePropertyId,
    UIA_LiveSettingPropertyId,
    UIA_IsEnabledPropertyId,
    UIA_IsKeyboardFocusablePropertyId,
    UIA_IsPasswordPropertyId,
    UIA_IsDialogPropertyId,
    UIA_OrientationPropertyId,
    UIA_PositionInSetPropertyId,
    UIA_SizeOfSetPropertyId,
    UIA_LevelPropertyId,
    UIA_SelectionItemIsSelectedPropertyId,
    UIA_GridRowCountPropertyId,
    UIA_GridColumnCountPropertyId,
    UIA_GridItemRowPropertyId,
    UIA_GridItemColumnPropertyId,
    UIA_ToggleToggleStatePropertyId,
    UIA_ValueValuePropertyId,
    UIA_ValueIsReadOnlyPropertyId,
    UIA_RangeValueValuePropertyId,
    UIA_RangeValueMinimumPropertyId,
    UIA_RangeValueMaximumPropertyId,
    UIA_ExpandCollapseExpandCollapseStatePropertyId,
};

static int CompareWinSnapshots(const void* a, const void* b) {
    uint32_t left = static_cast<const WinNodeSnapshot*>(a)->id;
    uint32_t right = static_cast<const WinNodeSnapshot*>(b)->id;
    return left == right ? 0 : left < right ? -1 : 1;
}

static void ClearWinPropertyChange(WinPropertyChange* change) {
    VariantClear(&change->before);
    VariantClear(&change->after);
}

static void WinTextChanges(const WinNodeSnapshot& before,
                           const WinNodeSnapshot& next,
                           Vec<WinNodeEvent>* events) {
    if ((!before.text.s && !before.lazyText) ||
        (!next.text.s && !next.lazyText))
        return;
    if (before.lazyText != next.lazyText ||
        before.textStart != next.textStart ||
        before.textRevision != next.textRevision ||
        !StrEq(before.text, next.text)) {
        VecAppend(*events, WinNodeEvent{next.id, UIA_Text_TextChangedEventId});
    }
    if (before.selectionRevision != next.selectionRevision ||
        before.selection.start != next.selection.start ||
        before.selection.end != next.selection.end ||
        before.caret != next.caret) {
        VecAppend(*events,
                  WinNodeEvent{next.id, UIA_Text_TextSelectionChangedEventId});
    }
}

static void RecordWinSelectionDelta(Vec<WinSelectionDelta>& deltas,
                                    const WinNodeSnapshot& node,
                                    bool selected) {
    uint32_t container =
        node.selectionContainer ? node.selectionContainer : node.id;
    VecAppend(deltas, WinSelectionDelta{container, node.id, selected});
}

static void QueueWinSelectionProperty(Vec<WinPropertyChange>* changes,
                                      uint32_t id, bool selected) {
    if (!changes) return;
    WinPropertyChange change{
        id, UIA_SelectionItemIsSelectedPropertyId, {}, {}, !selected};
    change.before.vt = change.after.vt = VT_BOOL;
    change.before.boolVal = selected ? VARIANT_FALSE : VARIANT_TRUE;
    change.after.boolVal = selected ? VARIANT_TRUE : VARIANT_FALSE;
    VecAppend(*changes, change);
}

static void WinSelectionEvents(Vec<WinSelectionDelta>& deltas,
                               const Vec<WinNodeSnapshot>& next,
                               Vec<WinPropertyChange>* changes,
                               Vec<WinNodeEvent>* events) {
    if (!len(deltas)) return;
    qsort(deltas.els, (size_t)len(deltas), sizeof(deltas[0]),
          [](const void* a, const void* b) {
              uint32_t left = static_cast<const WinSelectionDelta*>(a)
                                  ->container;
              uint32_t right = static_cast<const WinSelectionDelta*>(b)
                                   ->container;
              return left == right ? 0 : left < right ? -1 : 1;
          });
    int nodeIndex = 0;
    for (int end = 0; end < len(deltas);) {
        int start = end++;
        uint32_t id = deltas[start].container;
        while (end < len(deltas) && deltas[end].container == id) end++;
        while (nodeIndex < len(next) && next[nodeIndex].id < id) nodeIndex++;
        if (nodeIndex == len(next) || next[nodeIndex].id != id) continue;
        const auto& node = next[nodeIndex];
        if (node.selectedCount == 1) {
            VecAppend(*events,
                      WinNodeEvent{node.selectedChild,
                                   UIA_SelectionItem_ElementSelectedEventId});
            QueueWinSelectionProperty(changes, node.selectedChild, true);
            for (int i = start; i < end; i++) {
                if (!deltas[i].selected)
                    QueueWinSelectionProperty(changes, deltas[i].id, false);
            }
            continue;
        }
        // AccessKit's Windows adapter coalesces more than 20 item changes.
        constexpr int kInvalidateLimit = 20;
        if (end - start > kInvalidateLimit &&
            AccessibilitySelectionContainerRole(node.properties.info.role)) {
            VecAppend(*events,
                      WinNodeEvent{id, UIA_Selection_InvalidatedEventId});
            continue;
        }
        for (int i = start; i < end; i++) {
            const auto& delta = deltas[i];
            // The current portable containers advertise single selection.
            EVENTID event =
                delta.selected
                    ? UIA_SelectionItem_ElementSelectedEventId
                    : UIA_SelectionItem_ElementRemovedFromSelectionEventId;
            VecAppend(*events, WinNodeEvent{delta.id, event, !delta.selected});
            QueueWinSelectionProperty(changes, delta.id, delta.selected);
        }
    }
}

// Match the same provider across frames even if its siblings moved. Old
// strings belong to this snapshot arena, and queued variants own their
// strings so the snapshot can be published before notifying native clients.
static bool WinSnapshotChanges(WinAccessibility* root,
                               Vec<WinPropertyChange>* changes,
                               Vec<WinNodeEvent>* events) {
    int frame = 1 - root->snapshotFrame;
    const auto& last = root->snapshots[root->snapshotFrame];
    auto& next = root->snapshots[frame];
    Arena*& strings = root->snapshotStrings[frame];
    if (!strings) strings = ArenaNew();
    if (!strings) return true;
    strings->Reset();

    const auto& nodes = root->win->accessibility;
    if (!VecResize(next, len(nodes))) return true;
    for (int i = 0; i < len(nodes); i++) {
        const auto& node = nodes[i];
        uint32_t parent = node.parent < 0 ? 0 : nodes[node.parent].id;
        next[i] = {node.id, parent, i, WinNodeProperties(node)};
        auto& snapshot = next[i];
        auto& info = snapshot.properties.info;
        if (node.parent >= 0) {
            auto& parentSnapshot = next[node.parent];
            snapshot.selectionContainer =
                AccessibilitySelectionContainerRole(parentSnapshot.properties
                                                        .info.role)
                    ? (uint32_t)node.parent + 1
                    : parentSnapshot.selectionContainer;
        }
        if (snapshot.IsSelectedItem() && snapshot.selectionContainer) {
            auto& container = next[snapshot.selectionContainer - 1];
            container.selectedCount++;
            container.selectedChild = node.id;
        }
        info.label = StrDup(strings, info.label);
        info.description = StrDup(strings, info.description);
        info.value = StrDup(strings, info.value);
        info.authorId = StrDup(strings, info.authorId);
        info.placeholder = StrDup(strings, info.placeholder);
        if (node.document.text) {
            snapshot.lazyText = true;
            snapshot.textRevision = node.document.textRevision;
            snapshot.selectionRevision = node.document.selectionRevision;
        } else if (Str text = WindowAccessibilityTextValue(node); text.s) {
            snapshot.text = info.value.s && StrEq(text, info.value)
                                ? info.value
                                : StrDup(strings, text);
            snapshot.textStart = node.textExcerptStart;
            WindowAccessibilityTextSelection(node, &snapshot.selection);
            snapshot.caret = WindowAccessibilityTextCaret(node);
        }
        if (events && node.liveChanged) {
            VecAppend(*events,
                      WinNodeEvent{node.id, UIA_LiveRegionChangedEventId});
        }
    }
    // Resolve container indices to stable IDs before sorting the snapshots.
    for (auto& node : next) {
        if (node.selectionContainer)
            node.selectionContainer = next[node.selectionContainer - 1].id;
    }
    if (len(next) > 1)
        qsort(next.els, (size_t)len(next), sizeof(next[0]),
              CompareWinSnapshots);

    bool structureChanged = len(last) != len(next);
    int beforeIndex = 0;
    Vec<WinSelectionDelta> selectionDeltas;
    auto removedNode = [&](const WinNodeSnapshot& removed) {
        if (!events) return;
        if (AccessibilityIsDialog(removed.properties.info.role)) {
            VecAppend(
                *events,
                WinNodeEvent{removed.id, UIA_Window_WindowClosedEventId, true});
        }
        if (removed.IsSelectedItem())
            RecordWinSelectionDelta(selectionDeltas, removed, false);
    };
    for (const auto& node : next) {
        while (beforeIndex < len(last) && last[beforeIndex].id < node.id) {
            removedNode(last[beforeIndex++]);
        }
        if (beforeIndex == len(last) || last[beforeIndex].id != node.id) {
            structureChanged = true;
            if (events && AccessibilityIsDialog(node.properties.info.role))
                VecAppend(
                    *events,
                    WinNodeEvent{node.id, UIA_Window_WindowOpenedEventId});
            if (events && node.IsSelectedItem())
                RecordWinSelectionDelta(selectionDeltas, node, true);
            continue;
        }
        const auto& before = last[beforeIndex++];
        structureChanged |=
            before.parent != node.parent || before.index != node.index;
        if (events) WinTextChanges(before, node, events);
        bool selectionChanged =
            events && before.IsSelectedItem() != node.IsSelectedItem();
        if (selectionChanged) {
            bool selected = node.IsSelectedItem();
            RecordWinSelectionDelta(selectionDeltas, selected ? node : before,
                                    selected);
        }
        if (!changes) continue;
        for (PROPERTYID property : kWinTrackedProperties) {
            if (selectionChanged &&
                property == UIA_SelectionItemIsSelectedPropertyId)
                continue;
            auto oldValue = WinSemanticProperty(before.properties, property);
            auto newValue = WinSemanticProperty(node.properties, property);
            if (oldValue.SameAs(newValue)) continue;
            WinPropertyChange item{node.id, property, {}, {}};
            oldValue.ToVariant(&item.before);
            newValue.ToVariant(&item.after);
            if (!VecAppend(*changes, item)) ClearWinPropertyChange(&item);
        }
    }
    if (events) {
        for (; beforeIndex < len(last); beforeIndex++) {
            removedNode(last[beforeIndex]);
        }
        WinSelectionEvents(selectionDeltas, next, changes, events);
    }
    root->snapshotFrame = frame;
    return structureChanged;
}

void AccessibilityWinTreeChanged(WinAccessibility* accessibility) {
    if (!accessibility || !accessibility->win) {
        return;
    }
    accessibility->AddRef();
    bool listening = UiaClientsAreListening();
    Vec<WinPropertyChange> properties;
    Vec<WinNodeEvent> events;
    bool structure =
        WinSnapshotChanges(accessibility, listening ? &properties : nullptr,
                           listening ? &events : nullptr);
    if (listening) {
        if (structure) {
            UiaRaiseStructureChangedEvent(
                static_cast<IRawElementProviderSimple*>(accessibility),
                StructureChangeType_ChildrenInvalidated, nullptr, 0);
        }
        for (auto& change : properties) {
            if (auto* provider = WinEventProvider(accessibility, change.id,
                                                  change.allowRemovedNode)) {
                UiaRaiseAutomationPropertyChangedEvent(
                    provider, change.property, change.before, change.after);
                provider->Release();
            }
            ClearWinPropertyChange(&change);
        }
        for (const auto& event : events) {
            if (auto* provider = WinEventProvider(accessibility, event.id,
                                                  event.allowRemovedNode)) {
                UiaRaiseAutomationEvent(provider, event.event);
                provider->Release();
            }
        }
    }
    accessibility->Release();
}

void AccessibilityWinFocusChanged(WinAccessibility* accessibility,
                                  int focusId) {
    if (!accessibility || !accessibility->win || !focusId ||
        !UiaClientsAreListening()) {
        return;
    }
    for (int i = 0; i < len(accessibility->win->accessibility); i++) {
        if (accessibility->win->accessibility[i].focusId != focusId) {
            continue;
        }
        AccessibilityRaiseNodeEvent(accessibility, i,
                                    UIA_AutomationFocusChangedEventId);
        break;
    }
}

// Linked only by the repository test runner (it is deliberately absent from
// accessibility_win.h). This exercises the COM projection itself without
// opening a second native window or making the public API carry test types.
bool AccessibilityWinTextChangesSmokeTest() {
    Window win;
    InputState input;
    InputSetValue(&input, StrL("Before"));
    AccessibilityNode editor;
    editor.id = 1;
    editor.input = &input;
    editor.info.role = AccessibilityRole::TextInput;
    editor.info.value = InputValue(&input);
    AccessibilityNode document;
    document.id = 2;
    document.info.role = AccessibilityRole::Document;
    int reads = 0;
    document.document.data = &reads;
    document.document.text = [](void* data) {
        ++*static_cast<int*>(data);
        return StrL("Lazy text");
    };
    VecAppend(win.accessibility, editor);
    VecAppend(win.accessibility, document);
    WinAccessibility* root = AccessibilityWinNew(&win, GetDesktopWindow());
    if (!root) return false;
    InputSetValue(&input, StrL("After"));
    input.selectedRange = {0, 2};
    win.accessibility[0].info.value = InputValue(&input);
    win.accessibility[1].document.selectionRevision++;

    Vec<WinNodeEvent> events;
    bool ok = !WinSnapshotChanges(root, nullptr, &events) && len(events) == 3 &&
              events[0].id == 1 &&
              events[0].event == UIA_Text_TextChangedEventId &&
              events[1].id == 1 &&
              events[1].event == UIA_Text_TextSelectionChangedEventId &&
              events[2].id == 2 &&
              events[2].event == UIA_Text_TextSelectionChangedEventId;
    VecClear(events);
    input.selectedRange = {2, 2};
    ok &= !WinSnapshotChanges(root, nullptr, &events) && len(events) == 1 &&
          events[0].id == 1 &&
          events[0].event == UIA_Text_TextSelectionChangedEventId;
    VecClear(events);
    win.accessibility[0].info.role = AccessibilityRole::PasswordInput;
    ok &= !WinSnapshotChanges(root, nullptr, &events) && !len(events);
    ok &= reads == 0;
    AccessibilityWinClose(root);
    return ok;
}

bool AccessibilityWinSelectionEventsSmokeTest() {
    Window win;
    AccessibilityNode list;
    list.id = 20;
    list.info.role = AccessibilityRole::ListBox;
    AccessibilityNode group;
    group.id = 10;
    group.parent = 0;
    group.info.role = AccessibilityRole::Group;
    AccessibilityNode option;
    option.id = 3;
    option.parent = 1;
    option.info.role = AccessibilityRole::ListBoxOption;
    option.info.hasSelected = true;
    option.info.selected = true;
    VecAppend(win.accessibility, list);
    VecAppend(win.accessibility, group);
    VecAppend(win.accessibility, option);
    option.id = 5;
    option.info.selected = false;
    VecAppend(win.accessibility, option);
    WinAccessibility* root = AccessibilityWinNew(&win, GetDesktopWindow());
    if (!root) return false;

    Vec<WinPropertyChange> changes;
    Vec<WinNodeEvent> events;
    win.accessibility[2].info.selected = false;
    win.accessibility[3].info.selected = true;
    bool ok = !WinSnapshotChanges(root, &changes, &events) &&
              len(events) == 1 && events[0].id == 5 &&
              events[0].event == UIA_SelectionItem_ElementSelectedEventId &&
              len(changes) == 2 && changes[0].id == 5 && changes[1].id == 3 &&
              changes[0].property == UIA_SelectionItemIsSelectedPropertyId &&
              changes[0].after.vt == VT_BOOL &&
              changes[0].after.boolVal == VARIANT_TRUE &&
              changes[1].after.boolVal == VARIANT_FALSE;
    for (auto& change : changes) ClearWinPropertyChange(&change);
    VecClear(changes);
    VecClear(events);

    VecResize(win.accessibility, 3);
    ok &= WinSnapshotChanges(root, &changes, &events) && len(events) == 1 &&
          events[0].id == 5 && events[0].allowRemovedNode &&
          events[0].event ==
              UIA_SelectionItem_ElementRemovedFromSelectionEventId &&
          len(changes) == 1 && changes[0].id == 5 &&
          changes[0].after.boolVal == VARIANT_FALSE;
    IRawElementProviderSimple* removed = WinEventProvider(root, 5, true);
    VARIANT unavailable = {};
    ok &= removed &&
          removed->GetPropertyValue(UIA_IsEnabledPropertyId, &unavailable) ==
              UIA_E_ELEMENTNOTAVAILABLE;
    if (removed) removed->Release();
    VariantClear(&unavailable);
    for (auto& change : changes) ClearWinPropertyChange(&change);
    VecClear(changes);
    VecClear(events);

    option.info.selected = true;
    for (uint32_t id = 40; id < 61; id++) {
        option.id = id;
        VecAppend(win.accessibility, option);
    }
    ok &= WinSnapshotChanges(root, &changes, &events) && len(events) == 1 &&
          events[0].id == 20 &&
          events[0].event == UIA_Selection_InvalidatedEventId && !len(changes);
    for (auto& change : changes) ClearWinPropertyChange(&change);
    AccessibilityWinClose(root);
    return ok;
}

bool AccessibilityWinDialogEventsSmokeTest() {
    Window win;
    AccessibilityNode node;
    node.id = 10;
    node.info.role = AccessibilityRole::Button;
    VecAppend(win.accessibility, node);
    WinAccessibility* root = AccessibilityWinNew(&win, GetDesktopWindow());
    if (!root) return false;

    node.id = 4;
    node.info.role = AccessibilityRole::Dialog;
    VecAppend(win.accessibility, node);
    node.id = 20;
    node.info.role = AccessibilityRole::AlertDialog;
    VecAppend(win.accessibility, node);
    Vec<WinNodeEvent> events;
    bool ok = WinSnapshotChanges(root, nullptr, &events) && len(events) == 2 &&
              events[0].id == 4 && events[1].id == 20 &&
              events[0].event == UIA_Window_WindowOpenedEventId &&
              events[1].event == UIA_Window_WindowOpenedEventId;
    VecClear(events);
    ok &= !WinSnapshotChanges(root, nullptr, &events) && !len(events);

    VecResize(win.accessibility, 1);
    ok &= WinSnapshotChanges(root, nullptr, &events) && len(events) == 2 &&
          events[0].id == 4 && events[1].id == 20 &&
          events[0].event == UIA_Window_WindowClosedEventId &&
          events[1].event == UIA_Window_WindowClosedEventId &&
          events[0].allowRemovedNode && events[1].allowRemovedNode;
    auto* removed = WinEventProvider(root, 4, true);
    VARIANT value = {};
    ok &=
        removed && removed->GetPropertyValue(UIA_IsDialogPropertyId, &value) ==
                       UIA_E_ELEMENTNOTAVAILABLE;
    if (removed) removed->Release();
    VariantClear(&value);
    AccessibilityWinClose(root);
    return ok;
}

bool AccessibilityWinPropertyChangesSmokeTest() {
    Window win;
    char label[] = "Old";
    SliderState slider;
    slider.value = SliderSingle(10);
    AccessibilityNode box;
    box.id = 10;
    box.info.role = AccessibilityRole::CheckBox;
    box.info.label = Str(label);
    box.info.toggled = AccessibilityToggled::False;
    AccessibilityNode range;
    range.id = 7;
    range.info.role = AccessibilityRole::Slider;
    range.slider = &slider;
    VecAppend(win.accessibility, box);
    VecAppend(win.accessibility, range);
    WinAccessibility* root = AccessibilityWinNew(&win, GetDesktopWindow());
    if (!root) return false;

    memcpy(label, "New", sizeof(label));
    win.accessibility[0].info.disabled = true;
    win.accessibility[0].info.toggled = AccessibilityToggled::Mixed;
    slider.value = SliderSingle(30);
    Vec<WinPropertyChange> changes;
    bool ok = !WinSnapshotChanges(root, &changes) && len(changes) == 4;
    int matched = 0;
    for (auto& change : changes) {
        const VARIANT& old = change.before;
        const VARIANT& now = change.after;
        if (change.property == UIA_NamePropertyId && change.id == 10 &&
            old.vt == VT_BSTR && now.vt == VT_BSTR && old.bstrVal &&
            now.bstrVal && wcscmp(old.bstrVal, L"Old") == 0 &&
            wcscmp(now.bstrVal, L"New") == 0)
            matched |= 1;
        if (change.property == UIA_IsEnabledPropertyId && change.id == 10 &&
            old.vt == VT_BOOL && now.vt == VT_BOOL &&
            old.boolVal == VARIANT_TRUE && now.boolVal == VARIANT_FALSE)
            matched |= 2;
        if (change.property == UIA_ToggleToggleStatePropertyId &&
            change.id == 10 && old.vt == VT_I4 && now.vt == VT_I4 &&
            old.lVal == ToggleState_Off &&
            now.lVal == ToggleState_Indeterminate)
            matched |= 4;
        if (change.property == UIA_RangeValueValuePropertyId &&
            change.id == 7 && old.vt == VT_R8 && now.vt == VT_R8 &&
            old.dblVal == 10 && now.dblVal == 30)
            matched |= 8;
        ClearWinPropertyChange(&change);
    }
    ok &= matched == 15;
    VecClear(changes);
    ok &= !WinSnapshotChanges(root, &changes) && !len(changes);
    box = win.accessibility[0];
    win.accessibility[0] = win.accessibility[1];
    win.accessibility[1] = box;
    ok &= WinSnapshotChanges(root, &changes) && !len(changes);
    for (auto& change : changes) ClearWinPropertyChange(&change);
    AccessibilityWinClose(root);
    return ok;
}

bool AccessibilityWinSmokeTest(Window* win, uint32_t nodeId) {
    const AccessibilityNode* expected = WindowAccessibilityNode(win, nodeId);
    if (!expected) {
        return false;
    }
    WinAccessibility* root =
        AccessibilityWinNew(win, (void*)GetDesktopWindow());
    int index = root ? root->NodeIndex(nodeId) : -1;
    IRawElementProviderFragment* fragment =
        root ? root->NewNode(index) : nullptr;
    IRawElementProviderSimple* simple = nullptr;
    bool ok = fragment &&
              SUCCEEDED(fragment->QueryInterface(
                  __uuidof(IRawElementProviderSimple), (void**)&simple)) &&
              simple;
    VARIANT property = {};
    if (ok) {
        ok = SUCCEEDED(simple->GetPropertyValue(UIA_ControlTypePropertyId,
                                                &property)) &&
             property.vt == VT_I4 && property.lVal != 0;
        VariantClear(&property);
    }
    if (ok) {
        ok = SUCCEEDED(simple
                           ->GetPropertyValue(UIA_NamePropertyId, &property)) &&
             property.vt == VT_BSTR;
        BSTR wanted = AccessibilityBstr(expected->info.PlatformName());
        ok = ok && wanted && property.bstrVal &&
             wcscmp(wanted, property.bstrVal) == 0;
        SysFreeString(wanted);
        VariantClear(&property);
    }
    if (ok && expected->info.description.s) {
        ok = SUCCEEDED(simple->GetPropertyValue(UIA_FullDescriptionPropertyId,
                                                &property)) &&
             property.vt == VT_BSTR;
        BSTR wanted = AccessibilityBstr(expected->info.description);
        ok = ok && wanted && property.bstrVal &&
             wcscmp(wanted, property.bstrVal) == 0;
        SysFreeString(wanted);
        VariantClear(&property);
    }
    bool dialog = AccessibilityIsDialog(expected->info.role);
    if (ok) {
        ok = SUCCEEDED(
                 simple->GetPropertyValue(UIA_IsDialogPropertyId, &property)) &&
             property.vt == VT_BOOL &&
             (property.boolVal != VARIANT_FALSE) == dialog;
        VariantClear(&property);
    }
    struct PatternExpectation {
        PATTERNID id;
        bool wanted;
    } patterns[] = {
        {UIA_InvokePatternId, AccessibilityInvokePattern(*expected)},
        {UIA_TogglePatternId, expected->info
                                      .toggled != AccessibilityToggled::Unset},
        {UIA_ValuePatternId, AccessibilityTextRole(expected->info.role)},
        {UIA_RangeValuePatternId, expected->slider && expected->info
                                                          .hasNumericValue},
        {UIA_ExpandCollapsePatternId, expected->info.hasExpanded != 0},
        {UIA_SelectionPatternId,
         AccessibilitySelectionContainerRole(expected->info.role)},
        {UIA_SelectionItemPatternId,
         expected->info.hasSelected &&
             AccessibilitySelectionItemRole(expected->info.role)},
        {UIA_TextPatternId, AccessibilityTextPattern(*expected)},
        {UIA_WindowPatternId, dialog},
        {UIA_GridPatternId, expected->info.role == AccessibilityRole::Table &&
                                expected->info.hasRowCount &&
                                expected->info.hasColumnCount},
        {UIA_TablePatternId, expected->info.role == AccessibilityRole::Table &&
                                 expected->info.hasRowCount &&
                                 expected->info.hasColumnCount},
        {UIA_GridItemPatternId, expected->info
                                            .role == AccessibilityRole::Cell &&
                                    expected->info.hasColumnIndex &&
                                    AccessibilityGridRow(root, index) >= 0},
        {UIA_TableItemPatternId, expected->info
                                             .role == AccessibilityRole::Cell &&
                                     expected->info.hasColumnIndex &&
                                     AccessibilityGridRow(root, index) >= 0},
    };
    for (const PatternExpectation& pattern : patterns) {
        IUnknown* provider = nullptr;
        bool got =
            simple &&
            SUCCEEDED(simple->GetPatternProvider(pattern.id, &provider)) &&
            provider;
        ok = ok && got == pattern.wanted;
        if (provider) {
            provider->Release();
        }
    }
    if (ok && dialog) {
        IWindowProvider* window = nullptr;
        BOOL modal = TRUE;
        WindowInteractionState state = WindowInteractionState_NotResponding;
        ok = SUCCEEDED(simple->QueryInterface(__uuidof(IWindowProvider),
                                              (void**)&window)) &&
             window && SUCCEEDED(window->get_IsModal(&modal)) && !modal &&
             SUCCEEDED(window->get_WindowInteractionState(&state)) &&
             state == WindowInteractionState_ReadyForUserInteraction &&
             window->Close() == (HRESULT)UIA_E_NOTSUPPORTED;
        if (window) window->Release();
    }
    if (ok && AccessibilityTextPattern(*expected)) {
        ITextProvider* text = nullptr;
        ITextRangeProvider* document = nullptr;
        BSTR value = nullptr;
        ok = SUCCEEDED(simple->QueryInterface(__uuidof(ITextProvider),
                                              (void**)&text)) &&
             text && SUCCEEDED(text->get_DocumentRange(&document)) &&
             document && SUCCEEDED(document->GetText(-1, &value)) && value;
        BSTR wanted = AccessibilityBstr(InputValue(expected->input));
        ok = ok && wanted && value && wcscmp(wanted, value) == 0;
        SysFreeString(wanted);
        SysFreeString(value);
        if (ok) {
            Str source = WindowAccessibilityTextValue(*expected);
            int first = TextNextGraphemeBoundary(source, 0);
            BSTR firstText = AccessibilityBstr(Str(source.s, first));
            BSTR fromRange = nullptr;
            ok = firstText &&
                 SUCCEEDED(document
                               ->ExpandToEnclosingUnit(TextUnit_Character)) &&
                 SUCCEEDED(document->GetText(-1, &fromRange)) && fromRange &&
                 SysStringLen(firstText) == SysStringLen(fromRange) &&
                 wcscmp(firstText, fromRange) == 0;
            SysFreeString(firstText);
            SysFreeString(fromRange);
        }
        if (document) {
            document->Release();
        }
        if (text) {
            text->Release();
        }
    }
    if (ok && AccessibilitySelectionContainerRole(expected->info.role)) {
        ISelectionProvider* selection = nullptr;
        SAFEARRAY* selected = nullptr;
        ok = SUCCEEDED(simple->QueryInterface(__uuidof(ISelectionProvider),
                                              (void**)&selection)) &&
             selection && SUCCEEDED(selection->GetSelection(&selected)) &&
             selected && SafeArrayGetDim(selected) == 1;
        if (selected) {
            SafeArrayDestroy(selected);
        }
        if (selection) {
            selection->Release();
        }
    }
    if (ok && expected->info.role == AccessibilityRole::Table) {
        IGridProvider* grid = nullptr;
        ITableProvider* table = nullptr;
        int rows = -1;
        int columns = -1;
        ok = SUCCEEDED(simple->QueryInterface(__uuidof(IGridProvider),
                                              (void**)&grid)) &&
             grid && SUCCEEDED(grid->get_RowCount(&rows)) &&
             SUCCEEDED(grid->get_ColumnCount(&columns)) &&
             rows == expected->info.rowCount &&
             columns == expected->info.columnCount;
        int cellIndex = -1;
        for (int i = 0; ok && i < len(root->win->accessibility); i++) {
            if (root->win->accessibility[i]
                        .info.role == AccessibilityRole::Cell &&
                AccessibilityAncestor(root, i, AccessibilityRole::Table,
                                      false) == index) {
                cellIndex = i;
                break;
            }
        }
        IRawElementProviderSimple* item = nullptr;
        ok = ok && cellIndex >= 0 &&
             SUCCEEDED(grid->GetItem(AccessibilityGridRow(root, cellIndex),
                                     AccessibilityGridColumn(root, cellIndex),
                                     &item)) &&
             item;
        if (item) {
            item->Release();
        }
        if (grid) {
            grid->Release();
        }
        SAFEARRAY* headers = nullptr;
        ok = ok &&
             SUCCEEDED(simple->QueryInterface(__uuidof(ITableProvider),
                                              (void**)&table)) &&
             table && SUCCEEDED(table->GetColumnHeaders(&headers)) && headers;
        if (headers) {
            LONG lo = -1;
            LONG hi = -1;
            ok = ok && SafeArrayGetDim(headers) == 1 &&
                 SUCCEEDED(SafeArrayGetLBound(headers, 1, &lo)) &&
                 SUCCEEDED(SafeArrayGetUBound(headers, 1, &hi)) && lo == 0 &&
                 hi == 0;
            SafeArrayDestroy(headers);
        }
        if (table) {
            table->Release();
        }
    }
    if (ok && expected->info.role == AccessibilityRole::Cell) {
        IGridItemProvider* gridItem = nullptr;
        ITableItemProvider* tableItem = nullptr;
        int row = -1;
        int column = -1;
        IRawElementProviderSimple* containing = nullptr;
        ok = SUCCEEDED(simple->QueryInterface(__uuidof(IGridItemProvider),
                                              (void**)&gridItem)) &&
             gridItem && SUCCEEDED(gridItem->get_Row(&row)) &&
             SUCCEEDED(gridItem->get_Column(&column)) &&
             SUCCEEDED(gridItem->get_ContainingGrid(&containing)) &&
             containing && row == AccessibilityGridRow(root, index) &&
             column == AccessibilityGridColumn(root, index);
        if (containing) {
            containing->Release();
        }
        if (gridItem) {
            gridItem->Release();
        }
        SAFEARRAY* headers = nullptr;
        ok = ok &&
             SUCCEEDED(simple->QueryInterface(__uuidof(ITableItemProvider),
                                              (void**)&tableItem)) &&
             tableItem &&
             SUCCEEDED(tableItem->GetColumnHeaderItems(&headers)) && headers &&
             SafeArrayGetDim(headers) == 1;
        if (headers) {
            LONG lo = -1;
            LONG hi = -1;
            ok = ok && SUCCEEDED(SafeArrayGetLBound(headers, 1, &lo)) &&
                 SUCCEEDED(SafeArrayGetUBound(headers, 1, &hi)) && lo == 0 &&
                 hi == 0;
            SafeArrayDestroy(headers);
        }
        if (tableItem) {
            tableItem->Release();
        }
    }
    SAFEARRAY* runtime = nullptr;
    if (fragment) {
        ok = ok && SUCCEEDED(fragment->GetRuntimeId(&runtime)) && runtime &&
             SafeArrayGetDim(runtime) == 1;
        if (runtime) {
            SafeArrayDestroy(runtime);
        }
        IRawElementProviderFragmentRoot* gotRoot = nullptr;
        ok = ok && SUCCEEDED(fragment->get_FragmentRoot(&gotRoot)) && gotRoot;
        if (gotRoot) {
            gotRoot->Release();
        }
    }
    AccessibilityWinClose(root);
    if (simple) {
        VariantInit(&property);
        ok = ok && simple->GetPropertyValue(UIA_NamePropertyId, &property) ==
                       (HRESULT)UIA_E_ELEMENTNOTAVAILABLE;
        VariantClear(&property);
        simple->Release();
    }
    if (fragment) {
        fragment->Release();
    }
    return ok;
}

// Whether a fragment's string property is `want`.
static bool AccessibilityPropertyIs(IRawElementProviderFragment* fragment,
                                    PROPERTYID property, Str want) {
    IRawElementProviderSimple* simple = nullptr;
    if (!fragment ||
        FAILED(fragment->QueryInterface(__uuidof(IRawElementProviderSimple),
                                        (void**)&simple)) ||
        !simple) {
        return false;
    }
    VARIANT value = {};
    BSTR wanted = AccessibilityBstr(want);
    bool ok = SUCCEEDED(simple->GetPropertyValue(property, &value)) &&
              value.vt == VT_BSTR && wanted && value.bstrVal &&
              wcscmp(wanted, value.bstrVal) == 0;
    SysFreeString(wanted);
    VariantClear(&value);
    simple->Release();
    return ok;
}

// The part a fragment is, -1 for an element.
static int AccessibilityFragmentPart(IRawElementProviderFragment* fragment,
                                     uint32_t id) {
    IRawElementProviderSimple* simple = nullptr;
    if (!fragment ||
        FAILED(fragment->QueryInterface(__uuidof(IRawElementProviderSimple),
                                        (void**)&simple))) {
        return -1;
    }
    int part = AccessibilityPartOf(simple, id);
    simple->Release();
    return part;
}

// Linked only by the repository test runner, like AccessibilityWinSmokeTest:
// walks node `nodeId`'s children and checks each document part's name, value,
// parent, text range and hit test against the source.
bool AccessibilityWinPartsSmokeTest(Window* win, uint32_t nodeId) {
    const AccessibilityNode* node = WindowAccessibilityNode(win, nodeId);
    if (!node) {
        return false;
    }
    int parts = WindowAccessibilityPartCount(*node);
    WinAccessibility* root =
        AccessibilityWinNew(win, (void*)GetDesktopWindow());
    int index = root->NodeIndex(nodeId);
    IRawElementProviderFragment* doc = root->NewNode(index);
    ITextProvider* text = nullptr;
    bool ok = doc &&
              SUCCEEDED(doc->QueryInterface(__uuidof(ITextProvider),
                                            (void**)&text)) &&
              text;
    IRawElementProviderFragment* at = nullptr;
    ok = ok && SUCCEEDED(doc->Navigate(NavigateDirection_FirstChild, &at));
    int seen = 0;
    while (ok && at) {
        int part = AccessibilityFragmentPart(at, nodeId);
        if (part >= 0) {
            ok = part == seen;
            ok = ok && AccessibilityPropertyIs(
                           at, UIA_NamePropertyId,
                           WindowAccessibilityPartName(*node, part));
            ok = ok && AccessibilityPropertyIs(
                           at, UIA_ValueValuePropertyId,
                           WindowAccessibilityPartText(*node, part));
            IRawElementProviderFragment* parent = nullptr;
            ok = ok &&
                 SUCCEEDED(at->Navigate(NavigateDirection_Parent, &parent)) &&
                 parent && AccessibilityFragmentPart(parent, nodeId) < 0;
            if (parent) {
                parent->Release();
            }
            // RangeFromChild: the part's text within the document's
            IRawElementProviderSimple* simple = nullptr;
            ITextRangeProvider* range = nullptr;
            BSTR got = nullptr;
            at->QueryInterface(__uuidof(IRawElementProviderSimple),
                               (void**)&simple);
            ok = ok && simple &&
                 SUCCEEDED(text->RangeFromChild(simple, &range)) && range &&
                 SUCCEEDED(range->GetText(-1, &got));
            BSTR wanted =
                AccessibilityBstr(WindowAccessibilityPartText(*node, part));
            ok = ok && got && wanted && wcscmp(got, wanted) == 0;
            SysFreeString(wanted);
            SysFreeString(got);
            if (range) {
                range->Release();
            }
            if (simple) {
                simple->Release();
            }
            // hit test at the part's center
            Bounds b;
            if (ok && WindowAccessibilityPartBounds(*node, part, &b)) {
                float scale = DipScale(win);
                IRawElementProviderFragment* hit = nullptr;
                ok = SUCCEEDED(root->ElementProviderFromPoint(
                         (b.x + b.w / 2) * scale, (b.y + b.h / 2) * scale,
                         &hit)) &&
                     AccessibilityFragmentPart(hit, nodeId) == part;
                if (hit) {
                    hit->Release();
                }
            }
            seen++;
        } else {
            ok = seen == 0;
        }
        IRawElementProviderFragment* next = nullptr;
        ok =
            ok && SUCCEEDED(at->Navigate(NavigateDirection_NextSibling, &next));
        at->Release();
        at = next;
    }
    if (at) {
        at->Release();
    }
    ok = ok && seen == parts;
    IRawElementProviderFragment* last = nullptr;
    ok = ok && SUCCEEDED(doc->Navigate(NavigateDirection_LastChild, &last));
    ok = ok &&
         (parts == 0 || AccessibilityFragmentPart(last, nodeId) == parts - 1);
    IRawElementProviderFragment* before = nullptr;
    if (ok && last && parts > 0) {
        // back across from the first part to the last element child
        IRawElementProviderFragment* first = root->NewPart(index, 0);
        ok = first && SUCCEEDED(first->Navigate(
                          NavigateDirection_PreviousSibling, &before));
        bool hasChild = false;
        for (int i = 0; i < len(win->accessibility); i++) {
            hasChild = hasChild || win->accessibility[i].parent == index;
        }
        ok = ok && (before != nullptr) == hasChild;
        if (first) {
            first->Release();
        }
    }
    if (before) {
        before->Release();
    }
    if (last) {
        last->Release();
    }
    if (text) {
        text->Release();
    }
    if (doc) {
        doc->Release();
    }
    AccessibilityWinClose(root);
    return ok;
}

} // namespace gpui
