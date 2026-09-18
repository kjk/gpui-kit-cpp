/* Project the portable frame accessibility tree through Windows UI
   Automation. This is a raw fragment provider: nodes are light COM handles
   containing only the stable frame id, and every query resolves that id in
   the latest Window::accessibility rather than retaining arena strings. */

#include "gpui/accessibility_win.h"

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

static int AccessibilityControlType(AccessibilityRole role) {
    switch (role) {
        case AccessibilityRole::Alert:
        case AccessibilityRole::Heading:
            return UIA_TextControlTypeId;
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
        case AccessibilityRole::DateInput:
        case AccessibilityRole::DateTimeInput:
        case AccessibilityRole::EmailInput:
        case AccessibilityRole::MultilineTextInput:
        case AccessibilityRole::PasswordInput:
        case AccessibilityRole::PhoneNumberInput:
        case AccessibilityRole::TextInput:
        case AccessibilityRole::UrlInput:
            return UIA_EditControlTypeId;
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
        case AccessibilityRole::Tab:
            return UIA_TabItemControlTypeId;
        case AccessibilityRole::Table:
            return UIA_DataGridControlTypeId;
        case AccessibilityRole::TabList:
            return UIA_TabControlTypeId;
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

static const wchar_t* AccessibilityRoleName(AccessibilityRole role) {
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
        case AccessibilityRole::Switch:
            return L"switch";
        case AccessibilityRole::Tab:
            return L"tab";
        case AccessibilityRole::Table:
            return L"table";
        case AccessibilityRole::TabList:
            return L"tablist";
        case AccessibilityRole::TextInput:
        case AccessibilityRole::MultilineTextInput:
        case AccessibilityRole::EmailInput:
        case AccessibilityRole::PasswordInput:
        case AccessibilityRole::PhoneNumberInput:
        case AccessibilityRole::UrlInput:
        case AccessibilityRole::DateInput:
        case AccessibilityRole::DateTimeInput:
            return L"textbox";
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

static bool AccessibilityTextRole(AccessibilityRole role) {
    switch (role) {
        case AccessibilityRole::DateInput:
        case AccessibilityRole::DateTimeInput:
        case AccessibilityRole::EmailInput:
        case AccessibilityRole::MultilineTextInput:
        case AccessibilityRole::PasswordInput:
        case AccessibilityRole::PhoneNumberInput:
        case AccessibilityRole::TextInput:
        case AccessibilityRole::UrlInput:
            return true;
        default:
            return false;
    }
}

static bool AccessibilitySelectionItemRole(AccessibilityRole role) {
    return role == AccessibilityRole::ListBoxOption ||
           role == AccessibilityRole::RadioButton ||
           role == AccessibilityRole::Tab;
}

static bool AccessibilitySelectionContainerRole(AccessibilityRole role) {
    return role == AccessibilityRole::ListBox ||
           role == AccessibilityRole::RadioGroup ||
           role == AccessibilityRole::TabList ||
           role == AccessibilityRole::Tree;
}

static bool AccessibilityTextPattern(const AccessibilityNode& node) {
    return AccessibilityTextRole(node.info.role) && node.input &&
           node.info.role != AccessibilityRole::PasswordInput;
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

struct WinAccessibility : IRawElementProviderSimple,
                          IRawElementProviderFragment,
                          IRawElementProviderFragmentRoot {
    LONG refs = 1;
    Window* win = nullptr;
    HWND hwnd = nullptr;

    WinAccessibility(Window* window, HWND handle) : win(window), hwnd(handle) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE
    get_ProviderOptions(ProviderOptions* out) override;
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id,
                                                 IUnknown** out) override;
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id,
                                               VARIANT* out) override;
    HRESULT STDMETHODCALLTYPE
    get_HostRawElementProvider(IRawElementProviderSimple** out) override;
    HRESULT STDMETHODCALLTYPE
    Navigate(NavigateDirection direction,
             IRawElementProviderFragment** out) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override;
    HRESULT STDMETHODCALLTYPE
    GetEmbeddedFragmentRoots(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE SetFocus() override;
    HRESULT STDMETHODCALLTYPE
    get_FragmentRoot(IRawElementProviderFragmentRoot** out) override;
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
        for (int i = 0; i < win->accessibility.len; i++) {
            if (win->accessibility[i].id == id) {
                return i;
            }
        }
        return -1;
    }
    IRawElementProviderFragment* NewNode(int index);
};

struct WinAccessibilityNode : IRawElementProviderSimple,
                              IRawElementProviderFragment,
                              IInvokeProvider,
                              IToggleProvider,
                              IValueProvider,
                              IRangeValueProvider,
                              IExpandCollapseProvider,
                              ISelectionProvider,
                              ISelectionItemProvider,
                              ITextProvider,
                              IGridProvider,
                              IGridItemProvider,
                              ITableProvider,
                              ITableItemProvider {
    LONG refs = 1;
    WinAccessibility* root = nullptr;
    uint32_t id = 0;

    WinAccessibilityNode(WinAccessibility* owner, uint32_t nodeId)
        : root(owner), id(nodeId) {
        root->AddRef();
    }
    ~WinAccessibilityNode() { root->Release(); }

    const AccessibilityNode* Node() const { return root->Node(id); }
    HRESULT Missing() const {
        return Node() ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE
    get_ProviderOptions(ProviderOptions* out) override;
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern,
                                                 IUnknown** out) override;
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property,
                                               VARIANT* out) override;
    HRESULT STDMETHODCALLTYPE
    get_HostRawElementProvider(IRawElementProviderSimple** out) override;
    HRESULT STDMETHODCALLTYPE
    Navigate(NavigateDirection direction,
             IRawElementProviderFragment** out) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override;
    HRESULT STDMETHODCALLTYPE
    GetEmbeddedFragmentRoots(SAFEARRAY** out) override;
    HRESULT STDMETHODCALLTYPE SetFocus() override;
    HRESULT STDMETHODCALLTYPE
    get_FragmentRoot(IRawElementProviderFragmentRoot** out) override;
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
};

// A text range stores UTF-16 offsets, which is what UI Automation speaks.
// The owner and node id are stable; every operation resolves the current
// InputState and clamps the offsets after an edit instead of retaining text.
struct WinTextRange : ITextRangeProvider {
    LONG refs = 1;
    WinAccessibility* root = nullptr;
    uint32_t id = 0;
    int start = 0;
    int end = 0;

    WinTextRange(WinAccessibility* owner, uint32_t nodeId, int lo, int hi)
        : root(owner), id(nodeId), start(lo), end(hi) {
        root->AddRef();
    }
    ~WinTextRange() { root->Release(); }

    const AccessibilityNode* Node() const { return root->Node(id); }
    Str Text() const {
        const AccessibilityNode* node = Node();
        return node && node->input ? InputValue(node->input) : Str{};
    }
    int Length() const { return RopeOffsetToOffsetUtf16(Text(), len(Text())); }
    void Clamp() {
        int n = Length();
        start = std::max(0, std::min(start, n));
        end = std::max(start, std::min(end, n));
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
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

IRawElementProviderFragment* WinAccessibility::NewNode(int index) {
    if (!win || index < 0 || index >= win->accessibility.len) {
        return nullptr;
    }
    return static_cast<IRawElementProviderFragment*>(
        new WinAccessibilityNode(this, win->accessibility[index].id));
}

static int AccessibilityAncestor(const WinAccessibility* root, int index,
                                 AccessibilityRole role, bool includeSelf) {
    if (!root || !root->win || index < 0 ||
        index >= root->win->accessibility.len) {
        return -1;
    }
    int at = includeSelf ? index : root->win->accessibility[index].parent;
    while (at >= 0 && at < root->win->accessibility.len) {
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
    while (at >= 0 && at < root->win->accessibility.len) {
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
        index >= root->win->accessibility.len) {
        return -1;
    }
    const AccessibilityInfo& info = root->win->accessibility[index].info;
    return info.hasColumnIndex ? std::max(0, info.columnIndex - 1) : -1;
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
    for (int i = 0; i < root->win->accessibility.len; i++) {
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
    for (int i = 0; i < root->win->accessibility.len; i++) {
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

HRESULT WinAccessibility::QueryInterface(REFIID iid, void** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    if (iid == __uuidof(IUnknown) ||
        iid == __uuidof(IRawElementProviderSimple)) {
        *out = static_cast<IRawElementProviderSimple*>(this);
    } else if (iid == __uuidof(IRawElementProviderFragment)) {
        *out = static_cast<IRawElementProviderFragment*>(this);
    } else if (iid == __uuidof(IRawElementProviderFragmentRoot)) {
        *out = static_cast<IRawElementProviderFragmentRoot*>(this);
    }
    if (!*out) {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG WinAccessibility::AddRef() {
    return (ULONG)InterlockedIncrement(&refs);
}

ULONG WinAccessibility::Release() {
    ULONG left = (ULONG)InterlockedDecrement(&refs);
    if (!left) {
        delete this;
    }
    return left;
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
    for (int i = 0; i < win->accessibility.len; i++) {
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
    int found = -1;
    for (int i = 0; i < win->accessibility.len; i++) {
        const Bounds& b = win->accessibility[i].bounds;
        if ((float)point.x >= b.x && (float)point.x <= b.Right() &&
            (float)point.y >= b.y && (float)point.y <= b.Bottom()) {
            // The tree is preorder; a later containing node is a deeper child
            // or a later painted sibling and is the one hit-testing should use.
            found = i;
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
    for (int i = 0; i < win->accessibility.len; i++) {
        const AccessibilityNode& node = win->accessibility[i];
        if (node.focusId && node.focusId == win->focusId) {
            *out = NewNode(i);
            break;
        }
    }
    return S_OK;
}

HRESULT WinAccessibilityNode::QueryInterface(REFIID iid, void** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    if (iid == __uuidof(IUnknown) ||
        iid == __uuidof(IRawElementProviderSimple)) {
        *out = static_cast<IRawElementProviderSimple*>(this);
    } else if (iid == __uuidof(IRawElementProviderFragment)) {
        *out = static_cast<IRawElementProviderFragment*>(this);
    } else if (iid == __uuidof(IInvokeProvider)) {
        *out = static_cast<IInvokeProvider*>(this);
    } else if (iid == __uuidof(IToggleProvider)) {
        *out = static_cast<IToggleProvider*>(this);
    } else if (iid == __uuidof(IValueProvider)) {
        *out = static_cast<IValueProvider*>(this);
    } else if (iid == __uuidof(IRangeValueProvider)) {
        *out = static_cast<IRangeValueProvider*>(this);
    } else if (iid == __uuidof(IExpandCollapseProvider)) {
        *out = static_cast<IExpandCollapseProvider*>(this);
    } else if (iid == __uuidof(ISelectionProvider)) {
        *out = static_cast<ISelectionProvider*>(this);
    } else if (iid == __uuidof(ISelectionItemProvider)) {
        *out = static_cast<ISelectionItemProvider*>(this);
    } else if (iid == __uuidof(ITextProvider)) {
        *out = static_cast<ITextProvider*>(this);
    } else if (iid == __uuidof(IGridProvider)) {
        *out = static_cast<IGridProvider*>(this);
    } else if (iid == __uuidof(IGridItemProvider)) {
        *out = static_cast<IGridItemProvider*>(this);
    } else if (iid == __uuidof(ITableProvider)) {
        *out = static_cast<ITableProvider*>(this);
    } else if (iid == __uuidof(ITableItemProvider)) {
        *out = static_cast<ITableItemProvider*>(this);
    }
    if (!*out) {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG WinAccessibilityNode::AddRef() {
    return (ULONG)InterlockedIncrement(&refs);
}

ULONG WinAccessibilityNode::Release() {
    ULONG left = (ULONG)InterlockedDecrement(&refs);
    if (!left) {
        delete this;
    }
    return left;
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
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
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

HRESULT WinAccessibilityNode::GetPropertyValue(PROPERTYID property,
                                               VARIANT* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    VariantInit(out);
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    const AccessibilityInfo& info = node->info;
    if (property == UIA_ControlTypePropertyId) {
        VariantInt(out, AccessibilityControlType(info.role));
    } else if (property == UIA_NamePropertyId) {
        VariantString(out, info.label);
    } else if (property == UIA_AutomationIdPropertyId) {
        VariantString(out, info.authorId);
    } else if (property == UIA_HelpTextPropertyId) {
        VariantString(out, info.placeholder);
    } else if (property == UIA_AriaRolePropertyId) {
        VariantInit(out);
        out->vt = VT_BSTR;
        out->bstrVal = SysAllocString(AccessibilityRoleName(info.role));
    } else if (property == UIA_IsEnabledPropertyId) {
        VariantBool(out, !info.disabled);
    } else if (property == UIA_IsControlElementPropertyId ||
               property == UIA_IsContentElementPropertyId) {
        VariantBool(out, true);
    } else if (property == UIA_IsKeyboardFocusablePropertyId) {
        VariantBool(out, (node->actions & AccessibilityActionFocus) != 0);
    } else if (property == UIA_HasKeyboardFocusPropertyId) {
        VariantBool(out, node->focusId && root->win &&
                             node->focusId == root->win->focusId);
    } else if (property == UIA_IsPasswordPropertyId) {
        VariantBool(out, info.role == AccessibilityRole::PasswordInput);
    } else if (property == UIA_OrientationPropertyId &&
               info.orientation != AccessibilityOrientation::Unset) {
        VariantInt(out, info.orientation == AccessibilityOrientation::Vertical
                            ? OrientationType_Vertical
                            : OrientationType_Horizontal);
    } else if (property == UIA_PositionInSetPropertyId &&
               info.hasPositionInSet) {
        VariantInt(out, info.positionInSet);
    } else if (property == UIA_SizeOfSetPropertyId && info.hasSizeOfSet) {
        VariantInt(out, info.sizeOfSet);
    } else if (property == UIA_LevelPropertyId && info.hasLevel) {
        VariantInt(out, info.level);
    } else if (property == UIA_SelectionItemIsSelectedPropertyId &&
               info.hasSelected) {
        VariantBool(out, info.selected);
    } else if (property == UIA_GridRowCountPropertyId && info.hasRowCount) {
        VariantInt(out, info.rowCount);
    } else if (property == UIA_GridColumnCountPropertyId &&
               info.hasColumnCount) {
        VariantInt(out, info.columnCount);
    } else if (property == UIA_GridItemRowPropertyId && info.hasRowIndex) {
        VariantInt(out, std::max(0, info.rowIndex - 1));
    } else if (property == UIA_GridItemColumnPropertyId &&
               info.hasColumnIndex) {
        VariantInt(out, std::max(0, info.columnIndex - 1));
    } else if (property == UIA_ToggleToggleStatePropertyId &&
               info.toggled != AccessibilityToggled::Unset) {
        ToggleState state = ToggleState_Off;
        get_ToggleState(&state);
        VariantInt(out, state);
    } else if (property == UIA_ValueValuePropertyId &&
               AccessibilityTextRole(info.role)) {
        VariantString(out, info.value);
    } else if (property == UIA_ValueIsReadOnlyPropertyId &&
               AccessibilityTextRole(info.role)) {
        VariantBool(out, !(node->actions & AccessibilityActionSetValue));
    } else if (property == UIA_RangeValueValuePropertyId && node->slider) {
        VariantDouble(out, node->slider->value.End());
    } else if (property == UIA_RangeValueMinimumPropertyId && node->slider) {
        VariantDouble(out, node->slider->min);
    } else if (property == UIA_RangeValueMaximumPropertyId && node->slider) {
        VariantDouble(out, node->slider->max);
    } else if (property == UIA_ExpandCollapseExpandCollapseStatePropertyId &&
               info.hasExpanded) {
        VariantInt(out, info.expanded ? ExpandCollapseState_Expanded
                                      : ExpandCollapseState_Collapsed);
    } else if (property == UIA_IsOffscreenPropertyId && root->hwnd) {
        RECT client = {};
        GetClientRect(root->hwnd, &client);
        bool off = node->bounds.Right() <= 0 || node->bounds.Bottom() <= 0 ||
                   node->bounds.x >= (float)client.right ||
                   node->bounds.y >= (float)client.bottom;
        VariantBool(out, off);
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
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    int index = root->NodeIndex(id);
    if (!root->win || index < 0) {
        return UIA_E_ELEMENTNOTAVAILABLE;
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
        for (int i = 0; i < root->win->accessibility.len; i++) {
            if (root->win->accessibility[i].parent != index) {
                continue;
            }
            child = i;
            if (direction == NavigateDirection_FirstChild) {
                break;
            }
        }
        *out = root->NewNode(child);
        return S_OK;
    }
    int sibling = -1;
    if (direction == NavigateDirection_NextSibling) {
        for (int i = index + 1; i < root->win->accessibility.len; i++) {
            if (root->win->accessibility[i].parent == node.parent) {
                sibling = i;
                break;
            }
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

HRESULT WinAccessibilityNode::GetRuntimeId(SAFEARRAY** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = SafeArrayCreateVector(VT_I4, 0, 2);
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
    SafeArrayUnaccessData(*out);
    return S_OK;
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
    POINT origin = {};
    ClientToScreen(root->hwnd, &origin);
    out->left = (float)origin.x + node->bounds.x;
    out->top = (float)origin.y + node->bounds.y;
    out->width = node->bounds.w;
    out->height = node->bounds.h;
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
    if (!root->win || !root->hwnd) {
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
    return root->win && WindowAccessibilityPerform(root->win, id,
                                                   AccessibilityAction::Default)
               ? S_OK
               : UIA_E_INVALIDOPERATION;
}

HRESULT WinAccessibilityNode::Toggle() {
    const AccessibilityNode* node = Node();
    return node && node->info.toggled != AccessibilityToggled::Unset &&
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
    *out = node->info.toggled == AccessibilityToggled::Mixed
               ? ToggleState_Indeterminate
           : node->info.toggled == AccessibilityToggled::True ? ToggleState_On
                                                              : ToggleState_Off;
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
    while (at >= 0 && at < win->accessibility.len) {
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
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (AccessibilityTextPattern(*node)) {
        Str text = InputValue(node->input);
        Selection selection = node->input->selectedRange;
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
    for (int i = 0; i < root->win->accessibility.len; i++) {
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
    for (int i = 0; i < root->win->accessibility.len; i++) {
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
    // AccessKit does not carry this container policy in the subset GPUI
    // exports, so do not promise that an empty selection is forbidden.
    *out = FALSE;
    return S_OK;
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
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    int index = root->NodeIndex(id);
    if (!root->win || index < 0) {
        return UIA_E_ELEMENTNOTAVAILABLE;
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
    IRawElementProviderFragment* fragment = root->NewNode(parent);
    if (!fragment) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT hr = fragment->QueryInterface(__uuidof(IRawElementProviderSimple),
                                          (void**)out);
    fragment->Release();
    return hr;
}

HRESULT WinAccessibilityNode::GetVisibleRanges(SAFEARRAY** out) {
    const AccessibilityNode* node = Node();
    if (!node || !AccessibilityTextPattern(*node)) {
        return node ? UIA_E_INVALIDOPERATION : UIA_E_ELEMENTNOTAVAILABLE;
    }
    int n = RopeOffsetToOffsetUtf16(InputValue(node->input),
                                    len(InputValue(node->input)));
    return AccessibilityRangeArray(new WinTextRange(root, id, 0, n), out);
}

HRESULT WinAccessibilityNode::RangeFromChild(IRawElementProviderSimple* child,
                                             ITextRangeProvider** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    const AccessibilityNode* node = Node();
    if (!node || !AccessibilityTextPattern(*node)) {
        return node ? UIA_E_INVALIDOPERATION : UIA_E_ELEMENTNOTAVAILABLE;
    }
    // Text inputs publish their document as their own value, without embedded
    // child providers, so no child can name a sub-range.
    (void)child;
    return E_INVALIDARG;
}

HRESULT WinAccessibilityNode::RangeFromPoint(UiaPoint point,
                                             ITextRangeProvider** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    const AccessibilityNode* node = Node();
    if (!node || !AccessibilityTextPattern(*node)) {
        return node ? UIA_E_INVALIDOPERATION : UIA_E_ELEMENTNOTAVAILABLE;
    }
    Str text = InputValue(node->input);
    POINT client = {(LONG)point.x, (LONG)point.y};
    ScreenToClient(root->hwnd, &client);
    int offset = InputIndexForPosition(node->input, &root->win->paint,
                                       (float)client.x, (float)client.y);
    int offsetUtf16 = RopeOffsetToOffsetUtf16(text, offset);
    *out = new WinTextRange(root, id, offsetUtf16, offsetUtf16);
    return S_OK;
}

HRESULT WinAccessibilityNode::get_DocumentRange(ITextRangeProvider** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    const AccessibilityNode* node = Node();
    if (!node || !AccessibilityTextPattern(*node)) {
        return node ? UIA_E_INVALIDOPERATION : UIA_E_ELEMENTNOTAVAILABLE;
    }
    Str text = InputValue(node->input);
    *out =
        new WinTextRange(root, id, 0, RopeOffsetToOffsetUtf16(text, len(text)));
    return S_OK;
}

HRESULT WinAccessibilityNode::get_SupportedTextSelection(
    SupportedTextSelection* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node || !AccessibilityTextPattern(*node)) {
        return node ? UIA_E_INVALIDOPERATION : UIA_E_ELEMENTNOTAVAILABLE;
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
    for (int i = 0; i < root->win->accessibility.len; i++) {
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
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (node->info.role != AccessibilityRole::Cell) {
        return UIA_E_INVALIDOPERATION;
    }
    *out = 1;
    return S_OK;
}

HRESULT WinAccessibilityNode::get_ColumnSpan(int* out) {
    if (!out) {
        return E_INVALIDARG;
    }
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (node->info.role != AccessibilityRole::Cell) {
        return UIA_E_INVALIDOPERATION;
    }
    // The pinned semantic table API records one column index per cell and no
    // span, so one is the only value the portable tree can promise.
    *out = 1;
    return S_OK;
}

HRESULT WinAccessibilityNode::get_ContainingGrid(
    IRawElementProviderSimple** out) {
    int index = root->NodeIndex(id);
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (node->info.role != AccessibilityRole::Cell) {
        return UIA_E_INVALIDOPERATION;
    }
    int table =
        AccessibilityAncestor(root, index, AccessibilityRole::Table, false);
    return AccessibilitySimpleAt(root, table, out);
}

HRESULT WinAccessibilityNode::GetRowHeaders(SAFEARRAY** out) {
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (node->info.role != AccessibilityRole::Table) {
        return UIA_E_INVALIDOPERATION;
    }
    // AccessKit's role set used by these crates has ColumnHeader but no row
    // header role, so an honest empty provider array is preferable to treating
    // the first data cell as a header.
    return AccessibilityEmptyProviderArray(out);
}

HRESULT WinAccessibilityNode::GetColumnHeaders(SAFEARRAY** out) {
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (node->info.role != AccessibilityRole::Table) {
        return UIA_E_INVALIDOPERATION;
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
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (node->info.role != AccessibilityRole::Cell) {
        return UIA_E_INVALIDOPERATION;
    }
    return AccessibilityEmptyProviderArray(out);
}

HRESULT WinAccessibilityNode::GetColumnHeaderItems(SAFEARRAY** out) {
    int index = root->NodeIndex(id);
    const AccessibilityNode* node = Node();
    if (!node) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (node->info.role != AccessibilityRole::Cell) {
        return UIA_E_INVALIDOPERATION;
    }
    int table =
        AccessibilityAncestor(root, index, AccessibilityRole::Table, false);
    int column = AccessibilityGridColumn(root, index);
    return AccessibilityProviderArray(
        root, table, AccessibilityRole::ColumnHeader, column, out);
}

HRESULT WinTextRange::QueryInterface(REFIID iid, void** out) {
    if (!out) {
        return E_INVALIDARG;
    }
    *out = nullptr;
    if (iid == __uuidof(IUnknown) || iid == __uuidof(ITextRangeProvider)) {
        *out = static_cast<ITextRangeProvider*>(this);
    }
    if (!*out) {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG WinTextRange::AddRef() {
    return (ULONG)InterlockedIncrement(&refs);
}

ULONG WinTextRange::Release() {
    ULONG left = (ULONG)InterlockedDecrement(&refs);
    if (!left) {
        delete this;
    }
    return left;
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

static void AccessibilityUnitBounds(BSTR text, int length, int at,
                                    TextUnit unit, int* lo, int* hi) {
    at = std::max(0, std::min(at, length));
    *lo = at;
    *hi = at;
    if (unit == TextUnit_Character) {
        *hi = std::min(length, at + 1);
    } else if (unit == TextUnit_Word) {
        while (*lo > 0 && !AccessibilityWideSpace(text[*lo - 1])) {
            (*lo)--;
        }
        while (*hi < length && !AccessibilityWideSpace(text[*hi])) {
            (*hi)++;
        }
    } else if (unit == TextUnit_Line) {
        while (*lo > 0 && text[*lo - 1] != L'\n') {
            (*lo)--;
        }
        while (*hi < length && text[*hi] != L'\n') {
            (*hi)++;
        }
        if (*hi < length) {
            (*hi)++;
        }
    } else {
        *lo = 0;
        *hi = length;
    }
}

HRESULT WinTextRange::ExpandToEnclosingUnit(TextUnit unit) {
    const AccessibilityNode* node = Node();
    if (!node || !AccessibilityTextPattern(*node)) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    Clamp();
    BSTR text = AccessibilityBstr(Text());
    if (!text) {
        return E_OUTOFMEMORY;
    }
    AccessibilityUnitBounds(text, (int)SysStringLen(text), start, unit, &start,
                            &end);
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
    if (!Node()) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    Clamp();
    BSTR doc = AccessibilityBstr(Text());
    if (!doc) {
        return E_OUTOFMEMORY;
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
    if (!node || !root->hwnd) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    Clamp();
    *out = SafeArrayCreateVector(VT_R8, 0, start == end ? 0 : 4);
    if (!*out) {
        return E_OUTOFMEMORY;
    }
    if (start != end) {
        POINT origin = {};
        ClientToScreen(root->hwnd, &origin);
        double values[4] = {(double)origin.x + node->bounds.x,
                            (double)origin.y + node->bounds.y, node->bounds.w,
                            node->bounds.h};
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
    if (!Node()) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    Clamp();
    BSTR doc = AccessibilityBstr(Text());
    if (!doc) {
        return E_OUTOFMEMORY;
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
    if (!Node()) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    Clamp();
    int* value = endpoint == TextPatternRangeEndpoint_Start ? &start : &end;
    int old = *value;
    int length = Length();
    if (unit == TextUnit_Character) {
        *value = std::max(0, std::min(length, *value + count));
        *out = *value - old;
    } else {
        BSTR text = AccessibilityBstr(Text());
        if (!text) {
            return E_OUTOFMEMORY;
        }
        int direction = count < 0 ? -1 : 1;
        int wanted = count < 0 ? -count : count;
        int moved = 0;
        while (moved < wanted) {
            int lo = 0;
            int hi = 0;
            int probe = direction < 0 ? std::max(0, *value - 1) : *value;
            AccessibilityUnitBounds(text, length, probe, unit, &lo, &hi);
            int next = direction < 0 ? lo : hi;
            if (next == *value) {
                break;
            }
            *value = next;
            moved++;
        }
        SysFreeString(text);
        *out = direction * moved;
    }
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
    Str text = InputValue(node->input);
    int lo = RopeOffsetUtf16ToOffset(text, start);
    int hi = RopeOffsetUtf16ToOffset(text, end);
    InputSetSelectedRange(node->input, root->win->app, root->win, lo, hi);
    AppInvalidate(root->win);
    return S_OK;
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

WinAccessibility* AccessibilityWinNew(Window* win, void* hwnd) {
    if (!win || !hwnd) {
        return nullptr;
    }
    return new WinAccessibility(win, (HWND)hwnd);
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

void AccessibilityWinTreeChanged(WinAccessibility* accessibility) {
    if (!accessibility || !accessibility->win || !UiaClientsAreListening()) {
        return;
    }
    UiaRaiseStructureChangedEvent(
        static_cast<IRawElementProviderSimple*>(accessibility),
        StructureChangeType_ChildrenInvalidated, nullptr, 0);
}

void AccessibilityWinFocusChanged(WinAccessibility* accessibility,
                                  int focusId) {
    if (!accessibility || !accessibility->win || !focusId ||
        !UiaClientsAreListening()) {
        return;
    }
    for (int i = 0; i < accessibility->win->accessibility.len; i++) {
        if (accessibility->win->accessibility[i].focusId != focusId) {
            continue;
        }
        IRawElementProviderFragment* fragment = accessibility->NewNode(i);
        IRawElementProviderSimple* provider = nullptr;
        if (fragment) {
            fragment->QueryInterface(__uuidof(IRawElementProviderSimple),
                                     (void**)&provider);
            fragment->Release();
        }
        if (provider) {
            UiaRaiseAutomationEvent(provider,
                                    UIA_AutomationFocusChangedEventId);
            provider->Release();
        }
        break;
    }
}

// Linked only by the repository test runner (it is deliberately absent from
// accessibility_win.h). This exercises the COM projection itself without
// opening a second native window or making the public API carry test types.
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
        BSTR wanted = AccessibilityBstr(expected->info.label);
        ok = ok && wanted && property.bstrVal &&
             wcscmp(wanted, property.bstrVal) == 0;
        SysFreeString(wanted);
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
        for (int i = 0; ok && i < root->win->accessibility.len; i++) {
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

} // namespace gpui
