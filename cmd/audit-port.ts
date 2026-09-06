// Structural fidelity gate for crates/base -> src/base and crates/component -> src/ui.
// The Rust checkout is optional in CI; when present, its module declarations
// are checked against the pinned ledger as well as the C++ destinations.

import { createHash } from "node:crypto";
import { existsSync, readFileSync, readdirSync, statSync } from "node:fs";
import { join, resolve } from "node:path";

type CrateName = "base" | "ui";
type Status = "full" | "partial" | "adapter" | "excluded";
type Entry = {
  crate: CrateName;
  module: string;
  status: Status;
  targets: string[];
  reason?: string;
};

const root = resolve(import.meta.dir, "..");
const pinnedGpuiComponent = "4475eeb56ae78a5bf40ef23c2fb18396bdfff52b";

const baseModules = `
accordion actions alert_dialog animation async_util auto_scroll avatar button
calendar checkbox collapsible color_picker combobox component_traits
date_picker dialog dock element_ext event focus_trap geometry global_state
history hover_card index_path input link list_settings macos_accessibility
measure motion nav_stack number_input otp_input pagination popover popup positioner
progress radio radio_group resizable scrollbar scrollable_mask select selectable_text
sheet slider state_style styled switch table tabs text text_boundary text_selection
theme theme_tokens toast
toggle toggle_group tooltip tree undo_history virtual_list
`
  .trim()
  .split(/\s+/);

const uiModules = `
component_traits element_ext global_state icon index_path inspector
root sizing styled time title_bar virtual_list window_border window_ext
accordion alert attachment avatar badge breadcrumb bubble button chart checkbox
clipboard collapsible color_picker combobox command description_list dialog dock
form group_box highlighter history hover_card input kbd label link list marker
menu message message_scroller native_menu notification pagination plot popover
progress radio rating resizable scroll searchable_list select separator setting
sheet shimmer sidebar skeleton slider spinner status_bar stepper switch tab
table tag text theme tooltip tree
`
  .trim()
  .split(/\s+/);

const partialBase = new Set<string>();
const adapterBase = new Set(["component_traits", "element_ext", "event", "measure"]);
const partialUi = new Set<string>();
const adapterUi = new Set(["component_traits", "element_ext", "highlighter", "styled"]);

const partialReasons: Record<string, string> = {
  "base/global_state": "the App global carries selection/popover state; entity-stack coverage remains partial",
};

const adapterReasons: Record<string, string> = {
  "base/component_traits": "Rust traits are C++ builder conventions and function tables",
  "base/element_ext": "extension traits are methods on El plus forwarding helpers",
  "base/event": "typed GPUI closures are generational Listener records",
  "base/measure": "measurement is routed through the synchronous runtime layout seam",
  "ui/component_traits": "the UI faÃƒÂ§ade re-exports Base's C++ trait conventions",
  "ui/element_ext": "extension traits are methods on El plus forwarding helpers",
  "ui/highlighter": "tree-sitter/syntect are excluded; a dependency-free scanner is used",
  "ui/styled": "fluent traits are C++ builders and the shared sizing vocabulary",
};

const declarationReviewReason =
  "public-declaration spelling review has unresolved items; each needs a C++ mapping or deliberate-collapse record";

const baseOverrides: Record<string, string[]> = {
  async_util: [],
  dock: [
    "src/base/dock.h",
    "src/base/dock.cpp",
    "src/base/dock_layout.h",
    "src/base/dock_layout.cpp",
    "src/base/dock_registry.h",
    "src/base/dock_registry.cpp",
    "src/base/dock_area.cpp",
    "src/base/dock_state.h",
    "src/base/dock_state.cpp",
    "src/base/tiles.h",
    "src/base/tiles.cpp",
  ],
  input: [
    "src/base/input.h",
    "src/base/input_core.h",
    "src/base/input_editor.h",
    "src/base/input_lsp.h",
    "src/base/input_rope.h",
    "src/gpui/gpui.h",
    "src/base/input.cpp",
    "src/base/input_core.cpp",
    "src/base/input_editor.cpp",
    "src/base/input_lsp.cpp",
    "src/base/input_rope.cpp",
    "src/base/input_keys.h",
    "src/base/input_keys.cpp",
  ],
  macos_accessibility: ["src/gpui/window_mac.cpp"],
  // crates/base/src/text/ is one C++ file per Rust module directory, and the
  // HTML half of `text/format/` is beside it under the name the tree gives a
  // second file of one module.
  text: ["src/base/text.h", "src/base/text.cpp", "src/base/text_format.h", "src/base/text_format.cpp"],
};

const uiOverrides: Record<string, string[]> = {
  component_traits: ["src/ui/component_traits.h", "src/base/component_traits.h"],
  element_ext: ["src/ui/element_ext.h", "src/base/element_ext.h"],
  global_state: ["src/ui/global_state.h", "src/ui/global_state.cpp"],
  history: ["src/ui/history.h", "src/base/history.h", "src/base/history.cpp", "src/base/undo_history.h"],
  index_path: ["src/ui/index_path.h", "src/base/index_path.h", "src/base/index_path.cpp"],
  styled: ["src/ui/styled.h", "src/base/styled.h", "src/ui/sizing.h"],
  dock: ["src/ui/dock.h", "src/ui/dock.cpp", "src/ui/tiles.h", "src/ui/tiles.cpp"],
  highlighter: ["src/ui/highlighter.h", "src/ui/highlighter.cpp", "src/ui/syntax.h", "src/ui/syntax.cpp"],
  list: ["src/ui/list.h", "src/ui/list.cpp", "src/base/list.h", "src/base/list.cpp"],
  menu: ["src/ui/menu.h", "src/ui/menu.cpp", "src/ui/popup_menu.h", "src/base/popup_menu.h", "src/base/popup_menu.cpp"],
  plot: ["src/ui/plot.h", "src/ui/plot.cpp", "src/ui/sankey.h", "src/base/sankey.h", "src/base/sankey.cpp"],
  resizable: ["src/ui/resizable.h", "src/ui/resizable.cpp", "src/base/resizable.h", "src/base/resizable.cpp"],
  sheet: ["src/ui/sheet_settings.h", "src/ui/sheet.h", "src/ui/sheet.cpp"],
  // The UI side of text is the faÃƒÂ§ade over Base's: text/mod.rs, compat.rs,
  // style.rs and window_selection.rs.
  text: ["src/ui/text.h", "src/ui/text.cpp"],
  scroll: ["src/ui/scroll.h", "src/ui/scroll.cpp", "src/base/scrollable_mask.h"],
  table: [
    "src/ui/table.h",
    "src/ui/table.cpp",
    "src/ui/data_table.h",
    "src/base/data_table.h",
    "src/base/data_table.cpp",
  ],
  theme: ["src/ui/theme.h", "src/ui/theme.cpp", "src/ui/theme_data.cpp"],
};

function defaultTargets(crate: CrateName, module: string): string[] {
  const stem = `src/${crate}/${module}`;
  const targets: string[] = [];
  if (existsSync(join(root, `${stem}.h`))) targets.push(`${stem}.h`);
  if (existsSync(join(root, `${stem}.cpp`))) targets.push(`${stem}.cpp`);
  return targets;
}

function entriesFor(crate: CrateName, modules: string[]): Entry[] {
  return modules.map((module) => {
    const overrides = crate === "base" ? baseOverrides : uiOverrides;
    const partial = crate === "base" ? partialBase : partialUi;
    const adapter = crate === "base" ? adapterBase : adapterUi;
    let status: Status = partial.has(module) ? "partial" : adapter.has(module) ? "adapter" : "full";
    if (module === "async_util") status = "excluded";
    const key = `${crate}/${module}`;
    return {
      crate,
      module,
      status,
      targets: module in overrides ? overrides[module] : defaultTargets(crate, module),
      reason:
        module === "async_util"
          ? "async is a standing repository non-goal"
          : (partialReasons[key] ??
            adapterReasons[key] ??
            (status === "partial" ? declarationReviewReason : undefined)),
    };
  });
}

const entries = [...entriesFor("base", baseModules), ...entriesFor("ui", uiModules)];
const errors: string[] = [];

// A Rust test does not need a line-for-line C++ twin, but it does need a
// named suite that owns the same behavior. Every discovered test below is
// projected through this table individually; the digest then makes additions,
// removals and renames at the pinned checkout deliberate review events.
const testTargets: Record<string, string[]> = {
  "base/accordion": ["tests/AccessibilityTests.cpp", "tests/BuilderCapacityTests.cpp"],
  "base/actions": ["tests/ClickTests.cpp", "tests/FocusTrapTests.cpp"],
  "base/alert_dialog": ["tests/DialogTests.cpp", "tests/AccessibilityTests.cpp"],
  "base/animation": ["tests/MotionTests.cpp"],
  "base/auto_scroll": ["tests/AutoScrollTests.cpp"],
  "base/avatar": ["tests/AvatarTests.cpp", "tests/AccessibilityTests.cpp"],
  "base/button": ["tests/ButtonGroupTests.cpp", "tests/ClickTests.cpp", "tests/AccessibilityTests.cpp"],
  "base/calendar": ["tests/CalendarTests.cpp"],
  "base/checkbox": ["tests/ClickTests.cpp", "tests/AccessibilityTests.cpp"],
  "base/collapsible": ["tests/AccessibilityTests.cpp"],
  "base/color_picker": ["tests/ColorPickerTests.cpp", "tests/AccessibilityTests.cpp"],
  "base/combobox": ["tests/SelectTests.cpp"],
  "base/component_traits": ["tests/StyleEqTests.cpp"],
  "base/dialog": ["tests/DialogTests.cpp", "tests/AccessibilityTests.cpp"],
  "base/dock": ["tests/DockTests.cpp", "tests/DockStateTests.cpp", "tests/TilesTests.cpp"],
  "base/global_state": ["tests/AppGlobalTests.cpp"],
  "base/history": ["tests/HistoryTests.cpp"],
  "base/undo_history": ["tests/HistoryTests.cpp"],
  "base/hover_card": ["tests/HoverCardTests.cpp"],
  "base/index_path": ["tests/IndexPathTests.cpp"],
  "base/input": [
    "tests/InputStateTests.cpp",
    "tests/TextBoundaryTests.cpp",
    "tests/TextSelectionTests.cpp",
    "tests/FoldMapTests.cpp",
    "tests/SearchMatcherTests.cpp",
    "tests/RopeTests.cpp",
    "tests/MaskPatternTests.cpp",
    "tests/UndoManagerTests.cpp",
  ],
  "base/link": ["tests/ClickTests.cpp", "tests/AccessibilityTests.cpp"],
  "base/motion": ["tests/MotionTests.cpp"],
  "base/nav_stack": ["tests/NavStackTests.cpp"],
  "base/number_input": ["tests/NumberInputTests.cpp"],
  "base/otp_input": ["tests/OtpInputTests.cpp"],
  "base/pagination": ["tests/PaginationTests.cpp"],
  "base/popover": ["tests/PopupTests.cpp"],
  "base/popup": ["tests/PopupTests.cpp"],
  "base/positioner": ["tests/PositionerTests.cpp", "tests/AnchorTests.cpp"],
  "base/progress": ["tests/AccessibilityTests.cpp"],
  "base/radio": ["tests/ClickTests.cpp", "tests/AccessibilityTests.cpp"],
  "base/resizable": ["tests/ResizableTests.cpp"],
  "base/scrollbar": ["tests/ScrollbarTests.cpp"],
  "base/select": ["tests/SelectTests.cpp"],
  "base/sheet": ["tests/SheetTests.cpp"],
  "base/slider": ["tests/SliderTests.cpp", "tests/AccessibilityTests.cpp"],
  "base/state_style": ["tests/StateStyleTests.cpp"],
  "base/styled": ["tests/SizingTests.cpp", "tests/StyleEqTests.cpp"],
  "base/switch": ["tests/ClickTests.cpp", "tests/AccessibilityTests.cpp", "tests/StateStyleTests.cpp"],
  "base/table": ["tests/DataTableTests.cpp", "tests/AccessibilityTests.cpp"],
  "base/tabs": ["tests/TabTests.cpp", "tests/AccessibilityTests.cpp"],
  "base/scrollable_mask": ["tests/ScrollbarTests.cpp"],
  "base/selectable_text": ["tests/TextSelectionTests.cpp"],
  "base/text": ["tests/TextViewTests.cpp", "tests/MarkdownTests.cpp"],
  "base/text_selection": ["tests/TextSelectionTests.cpp"],
  "base/theme_tokens": ["tests/ThemeColorTests.cpp"],
  "base/theme": ["tests/ThemeSettingsTests.cpp"],
  "base/toast": ["tests/ToastTests.cpp"],
  "base/toggle": ["tests/ClickTests.cpp", "tests/AccessibilityTests.cpp"],
  "base/tooltip": ["tests/PopupTests.cpp"],
  "base/tree": ["tests/TreeTests.cpp"],
  "base/virtual_list": ["tests/VirtualListTests.cpp"],
  "ui/accordion": ["tests/AccessibilityTests.cpp"],
  "ui/attachment": ["tests/AttachmentTests.cpp"],
  "ui/avatar": ["tests/AvatarTests.cpp", "tests/AccessibilityTests.cpp"],
  "ui/bubble": ["tests/BubbleTests.cpp"],
  "ui/button": ["tests/ButtonGroupTests.cpp", "tests/ClickTests.cpp", "tests/AccessibilityTests.cpp"],
  "ui/chart": [
    "tests/ChartTests.cpp",
    "tests/BuilderCapacityTests.cpp",
    "tests/ScaleTests.cpp",
    "tests/SankeyTests.cpp",
  ],
  "ui/checkbox": ["tests/ClickTests.cpp", "tests/AccessibilityTests.cpp"],
  "ui/icon": ["tests/IconTests.cpp"],
  "ui/collapsible": ["tests/AccessibilityTests.cpp"],
  "ui/color_picker": ["tests/ColorPickerTests.cpp", "tests/AccessibilityTests.cpp"],
  "ui/combobox": ["tests/SelectTests.cpp", "tests/SearchableListTests.cpp"],
  "ui/command": ["tests/CommandTests.cpp"],
  "ui/description_list": [
    "tests/DescriptionListTests.cpp",
    "tests/BuilderCapacityTests.cpp",
    "tests/AccessibilityTests.cpp",
  ],
  "ui/dock": ["tests/DockTests.cpp", "tests/DockStateTests.cpp", "tests/TilesTests.cpp"],
  "ui/group_box": ["tests/GroupBoxTests.cpp", "tests/AccessibilityTests.cpp"],
  "ui/highlighter": ["tests/SyntaxTests.cpp"],
  "ui/index_path": ["tests/IndexPathTests.cpp"],
  "ui/input": [
    "tests/InputStateTests.cpp",
    "tests/TextBoundaryTests.cpp",
    "tests/TextSelectionTests.cpp",
    "tests/MaskPatternTests.cpp",
  ],
  "ui/inspector": ["tests/InspectorTests.cpp"],
  "ui/kbd": ["tests/KbdTests.cpp", "tests/AccessibilityTests.cpp"],
  "ui/label": ["tests/LabelTests.cpp", "tests/AccessibilityTests.cpp"],
  "ui/link": ["tests/ClickTests.cpp", "tests/AccessibilityTests.cpp"],
  "ui/list": ["tests/ListTests.cpp"],
  "ui/marker": ["tests/MarkerTests.cpp"],
  "ui/menu": ["tests/PopupMenuTests.cpp", "tests/AppMenuTests.cpp"],
  "ui/message": ["tests/MessageTests.cpp"],
  "ui/message_scroller": ["tests/MessageScrollerTests.cpp"],
  "ui/native_menu": ["tests/NativeMenuTests.cpp"],
  "ui/notification": ["tests/NotificationTests.cpp"],
  "ui/plot": ["tests/ScaleTests.cpp", "tests/SankeyTests.cpp"],
  "ui/popover": ["tests/PopupTests.cpp"],
  "ui/progress": ["tests/AccessibilityTests.cpp"],
  "ui/radio": ["tests/ClickTests.cpp", "tests/AccessibilityTests.cpp"],
  "ui/root": ["tests/RootTests.cpp"],
  "ui/scroll": ["tests/ScrollbarTests.cpp", "tests/AutoScrollTests.cpp"],
  "ui/select": ["tests/SelectTests.cpp"],
  "ui/shimmer": ["tests/ShimmerTests.cpp"],
  "ui/sidebar": ["tests/SidebarTests.cpp", "tests/BuilderCapacityTests.cpp"],
  "ui/sizing": ["tests/SizingTests.cpp", "tests/StyleEqTests.cpp"],
  "ui/slider": ["tests/SliderTests.cpp", "tests/AccessibilityTests.cpp"],
  "ui/spinner": ["tests/MotionTests.cpp"],
  "ui/switch": ["tests/ClickTests.cpp", "tests/AccessibilityTests.cpp"],
  "ui/tab": ["tests/TabTests.cpp", "tests/AccessibilityTests.cpp"],
  "ui/table": ["tests/DataTableTests.cpp", "tests/AccessibilityTests.cpp"],
  "ui/text": ["tests/TextViewTests.cpp", "tests/MarkdownTests.cpp"],
  "ui/theme": [
    "tests/ThemeColorTests.cpp",
    "tests/ThemeRegistryTests.cpp",
    "tests/ThemeSettingsTests.cpp",
    "tests/MotionTests.cpp",
  ],
  "ui/time": ["tests/CalendarTests.cpp", "tests/DatePickerTests.cpp"],
  "ui/title_bar": ["tests/TitleBarTests.cpp"],
  "ui/tree": ["tests/TreeTests.cpp"],
  "ui/virtual_list": ["tests/VirtualListTests.cpp"],
};

type DeclarationMapping = {
  spellings?: string[];
  targets?: string[];
  collapse?: string;
};

// Rust's crate faÃƒÂ§ade uses free snake_case functions. The C++ faÃƒÂ§ade keeps
// the same operation but follows this tree's subsystem-prefix convention.
// This table grows as -missing-declarations is reviewed; putting a spelling
// here is an explicit structural decision, not a fuzzy match.
const declarationMappings: Record<string, DeclarationMapping> = {
  "base/auto_scroll.rs::struct AutoScroll": {
    spellings: ["AutoScroll"],
    targets: ["src/gpui/gpui.h"],
  },
  "base/lib.rs::fn init": { spellings: ["BaseInit"] },
  "base/dialog.rs::fn init": { spellings: ["DialogInitKeys"] },
  "base/number_input.rs::fn step_value": {
    spellings: ["NumberStepValueTemp"],
  },
  "base/macos_accessibility.rs::fn install_window_hit_test_forwarder": {
    spellings: ["PlatInstallAccessibilityHitTest"],
    targets: ["src/gpui/platform.h", "src/gpui/window_mac.cpp"],
  },
  "base/geometry.rs::struct Edges": {
    spellings: ["Edges"],
    targets: ["src/gpui/gpui.h", "src/base/geometry.h"],
  },
  "base/geometry.rs::trait AxisExt": {
    spellings: ["AxisIsHorizontal", "AxisIsVertical"],
  },
  "base/geometry.rs::trait LengthExt": {
    collapse:
      "C++ builders accept resolved DIP floats and pass relative/auto lengths directly to taffy, so no retained gpui::Length exists to extend",
  },
  "base/focus_trap.rs::fn active_focus_trap": {
    spellings: ["FocusTrapActive"],
  },
  "base/focus_trap.rs::fn init": {
    spellings: ["FocusTrapInit"],
  },
  "base/focus_trap.rs::trait FocusTrapElement": {
    collapse:
      "C++ El::TrapId plus FocusTrapContainer::New is the extension surface; the POD tree has no inheritance trait wrapper",
  },
  "base/popup.rs::const POPUP_PRIORITY": {
    spellings: ["kPopupPriority"],
  },
  "base/dock/dock_area.rs::const CLOSED_BOTTOM_STRIP": {
    spellings: ["kClosedBottomStrip"],
  },
  "base/select.rs::fn init": { spellings: ["SelectInitKeys"] },
  "base/sheet.rs::fn init": { spellings: ["SheetInitKeys"] },
  "base/slider.rs::enum SliderEvent": {
    spellings: ["SliderEvent"],
    targets: ["src/gpui/gpui.h", "src/gpui/window_common.cpp"],
  },
  "ui/lib.rs::fn init": { spellings: ["Init"] },
  "ui/lib.rs::fn locale": {
    spellings: ["LocaleNow"],
    targets: ["src/ui/i18n.h", "src/ui/i18n.cpp"],
  },
  "ui/lib.rs::fn set_locale": {
    spellings: ["LocaleSet"],
    targets: ["src/ui/i18n.h", "src/ui/i18n.cpp"],
  },
  "ui/title_bar.rs::const TITLE_BAR_HEIGHT": {
    spellings: ["kTitleBarHeight"],
  },
  "ui/group_box.rs::trait GroupBoxVariants": {
    spellings: ["WithVariant", "Normal", "Fill", "Outline"],
  },
  "ui/plot/axis.rs::const AXIS_GAP": {
    spellings: ["kPlotAxisGap"],
  },
  "ui/plot/label.rs::const TEXT_GAP": {
    spellings: ["kPlotTextGap"],
  },
  "ui/plot/label.rs::const TEXT_HEIGHT": {
    spellings: ["kPlotTextHeight"],
  },
  "ui/plot/label.rs::const TEXT_SIZE": {
    spellings: ["kPlotTextSize"],
  },
  "ui/plot/mod.rs::trait Plot": {
    collapse:
      "C++ uses ChartSeries for the common plot contract and El::customPaint for source-shaped immediate Plot implementations; prepaint children are ordinary El children",
  },
  "ui/plot/scale.rs::trait Scale": {
    collapse:
      "C++ ScaleLinear, ScalePoint, ScaleBand and ScaleOrdinal expose Tick and LeastIndex directly; templates need no runtime scale trait",
  },
  "ui/plot/scale/sealed.rs::trait Sealed": {
    collapse:
      "Rust's private sealed-trait gate has no C++ runtime representation; only the four concrete scale types expose the convention",
  },
  "base/text/text_view.rs::struct TextViewPrepaintState": {
    collapse:
      "C++ fuses element prepaint into El painting: the ordinary hit-test entry and line-clamp clip bottom are resolved by the shared paint walk rather than retained in a TextView-specific state",
  },
  "ui/text/compat.rs::struct TextViewPrepaintState": {
    collapse:
      "the C++ faÃƒÂ§ade re-exports Base's TextView rather than wrapping it in a second element, so the compatibility element's prepaint state has no separate representation",
  },
  "ui/text/compat.rs::struct TextViewLayoutState": {
    collapse:
      "same faÃƒÂ§ade: TextViewLayoutState is Base's, re-exported, because there is no wrapper element holding an AnyElement of its own",
  },
  "ui/theme/color.rs::fn black": { spellings: ["ThemeBlack"] },
  "ui/theme/color.rs::fn hsl": { spellings: ["ThemeHsl"] },
  "ui/theme/color.rs::fn try_parse_background": {
    spellings: ["ThemeParseBackground"],
  },
  "ui/theme/color.rs::fn try_parse_color": {
    spellings: ["ThemeParseColor"],
  },
  "ui/theme/color.rs::fn white": { spellings: ["ThemeWhite"] },
  "ui/theme/color.rs::trait Colorize": {
    collapse:
      "C++ exposes the complete Colorize operation set as Rgba-prefixed value functions in the GPUI runtime color vocabulary",
  },
  "ui/theme/schema.rs::struct ThemeSet": {
    spellings: ["ThemeSetConfig"],
  },
  "ui/sizing.rs::enum Size": {
    spellings: ["UiSize"],
  },
  "ui/sizing.rs::trait Sizable": {
    collapse: "C++ components expose WithSize(UiSize) directly; fluent trait extension needs no inheritance wrapper",
  },
  "ui/sizing.rs::trait StyleSized": {
    spellings: ["UiInputTextSize", "UiInputSize", "UiListSize", "UiSizeWith", "UiTableCellSize", "UiButtonTextSize"],
  },
  "ui/window_ext.rs::trait WindowExt": {
    collapse:
      "C++ cannot extend Window with a trait; the complete surface is exposed as Window-prefixed free functions, with retained Rust build closures represented by owned Entity views",
  },
};

type SurfaceKind = "declaration" | "pub-use" | "test";
type SurfaceItem = {
  crate: CrateName;
  kind: SurfaceKind;
  path: string;
  name: string;
};

function rustFiles(dir: string): string[] {
  const files: string[] = [];
  for (const name of readdirSync(dir).sort()) {
    const path = join(dir, name);
    if (statSync(path).isDirectory()) files.push(...rustFiles(path));
    else if (name.endsWith(".rs")) files.push(path);
  }
  return files;
}

function sourceModule(path: string): string | null {
  const first = path.split("/")[0]!;
  const module = first.endsWith(".rs") ? first.slice(0, -3) : first;
  return module === "lib" ? null : module;
}

function surfaceItems(crate: CrateName, src: string): SurfaceItem[] {
  const items: SurfaceItem[] = [];
  for (const path of rustFiles(src)) {
    const rel = path.slice(src.length + 1).replaceAll("\\", "/");
    const lines = readFileSync(path, "utf8").split(/\r?\n/);
    for (let i = 0; i < lines.length; i++) {
      if (/^\s*pub\s+use\b/.test(lines[i]!)) {
        let statement = "";
        for (; i < lines.length; i++) {
          const part = lines[i]!.replace(/\/\/.*$/, "").trim();
          statement += (statement ? " " : "") + part;
          if (part.includes(";")) break;
        }
        items.push({ crate, kind: "pub-use", path: rel, name: statement.replace(/\s+/g, " ") });
        continue;
      }
      // rustfmt leaves module-level declarations at column zero. Restricting
      // this inventory to that column deliberately excludes public fields and
      // impl methods: this gate tracks the crate/module surface, while method
      // spelling is often a C++ builder convention rather than a free symbol.
      const declaration = lines[i]!.match(
        /^pub\s+(?:(?:unsafe|async|const)\s+|extern\s+"[^"]+"\s+)*(struct|enum|trait|fn|type|const|static|mod)\s+(?:r#)?([A-Za-z_][A-Za-z0-9_]*)/,
      );
      if (declaration) {
        items.push({
          crate,
          kind: "declaration",
          path: rel,
          name: `${declaration[1]} ${declaration[2]}`,
        });
        continue;
      }
      if (!/^\s*#\[(?:gpui::)?test(?:\([^\]]*\))?\]\s*$/.test(lines[i]!)) continue;
      let name = "";
      for (let j = i + 1; j < lines.length && j <= i + 20; j++) {
        const match = lines[j]!.match(/^\s*(?:async\s+)?fn\s+([A-Za-z0-9_]+)/);
        if (match) {
          name = match[1]!;
          break;
        }
      }
      if (!name) errors.push(`${crate}/${rel}:${i + 1}: test has no following function`);
      else items.push({ crate, kind: "test", path: rel, name });
    }
  }
  return items.sort((a, b) => `${a.kind}\t${a.path}\t${a.name}`.localeCompare(`${b.kind}\t${b.path}\t${b.name}`));
}

function surfaceDigest(items: SurfaceItem[], kind: SurfaceKind): string {
  const text = items
    .filter((item) => item.kind === kind)
    .map((item) => `${item.path}\t${item.name}`)
    .join("\n");
  return createHash("sha256").update(text).digest("hex");
}

function declarationSourceText(targets: string[]): string {
  return targets
    .map((target) => readFileSync(join(root, target), "utf8"))
    .join("\n")
    .replace(/\/\*[\s\S]*?\*\//g, " ")
    .replace(/\/\/.*$/gm, " ")
    .replace(/"(?:\\.|[^"\\])*"/g, " ");
}

// Counts and content hashes for the exact pinned tree. Unlike line numbers,
// these survive unrelated edits; a changed export or renamed test changes the
// hash and forces this ledger to be reviewed with the pin update.
const surfacePins: Record<CrateName, Record<SurfaceKind, { count: number; sha256: string }>> = {
  base: {
    declaration: { count: 424, sha256: "08da4d596474aa84b0ff4c6e6a410615ee4925f83849ffbaf38f850e33c6c3b3" },
    "pub-use": { count: 130, sha256: "54481e7c3ffecdb5609b14191efcb779f20bfb627e32fd0e73557670ca7bf346" },
    test: { count: 776, sha256: "8e4dfbb125b61d22dd014e781a4064a380c57052e14b7ca334db95a43599454c" },
  },
  ui: {
    declaration: { count: 426, sha256: "e5138aa5e62871e1daf3759d46e05a45f47cd2e2038deeed50a857a066f00634" },
    "pub-use": { count: 156, sha256: "3621ab05b7cbddc92fb38bb3b9ab28f48a28194e5a1bfb34dd6ff640d2941e69" },
    test: { count: 447, sha256: "54ef9c50e89bcd36022578daab63c8450ab287acfc3b328ac03ab3173002fc4b" },
  },
};

for (const [key, targets] of Object.entries(testTargets)) {
  const entry = entries.find((candidate) => `${candidate.crate}/${candidate.module}` === key);
  if (!entry) errors.push(`${key}: test destination belongs to no module ledger entry`);
  for (const target of targets) {
    if (!existsSync(join(root, target))) errors.push(`${key}: missing test destination ${target}`);
  }
}

for (const entry of entries) {
  if (entry.status !== "excluded" && entry.targets.length === 0) {
    errors.push(`${entry.crate}/${entry.module}: no C++ destination`);
  }
  for (const target of entry.targets) {
    if (!existsSync(join(root, target))) {
      errors.push(`${entry.crate}/${entry.module}: missing ${target}`);
    }
  }
  if (entry.status !== "full" && !entry.reason) {
    errors.push(`${entry.crate}/${entry.module}: ${entry.status} entry has no reason`);
  }
}

function modulesIn(path: string): string[] {
  const text = readFileSync(path, "utf8");
  const found = [...text.matchAll(/^\s*(?:pub\s+)?mod\s+([a-zA-Z0-9_]+)/gm)].map((m) => m[1]);
  return [...new Set(found)].sort();
}

function sameMembers(label: string, actual: string[], expected: string[]) {
  const a = [...actual].sort();
  const e = [...expected].sort();
  for (const name of e) if (!a.includes(name)) errors.push(`${label}: unclassified upstream module ${name}`);
  for (const name of a) if (!e.includes(name)) errors.push(`${label}: ledger names missing upstream module ${name}`);
}

const rustRoot = join(root, ".work", "gpui-component", "crates");
if (existsSync(rustRoot)) {
  sameMembers("base", modulesIn(join(rustRoot, "base", "src", "lib.rs")), baseModules);
  sameMembers("ui", modulesIn(join(rustRoot, "component", "src", "lib.rs")), uiModules);

  const byModule = new Map(entries.map((entry) => [`${entry.crate}/${entry.module}`, entry]));
  const verboseSurface = Bun.argv.includes("-surface");
  const missingDeclarations = Bun.argv.includes("-missing-declarations");
  const seenDeclarations = new Set<string>();
  for (const crate of ["base", "ui"] as const) {
    const items = surfaceItems(crate, join(rustRoot, crate === "ui" ? "component" : crate, "src"));
    for (const kind of ["declaration", "pub-use", "test"] as const) {
      const selected = items.filter((item) => item.kind === kind);
      const expected = surfacePins[crate][kind];
      const digest = surfaceDigest(items, kind);
      if (selected.length !== expected.count || digest !== expected.sha256) {
        errors.push(
          `${crate} ${kind} inventory differs: ` +
            `count ${selected.length}, sha256 ${digest}; ` +
            `ledger has ${expected.count}, ${expected.sha256}`,
        );
      }
    }

    const testedModules = new Set<string>();
    for (const item of items) {
      const module = sourceModule(item.path);
      let status: Status | "facade" = "facade";
      let targets = [`src/${crate}/lib.h`];
      if (module) {
        const entry = byModule.get(`${crate}/${module}`);
        if (!entry) {
          errors.push(`${crate}/${item.path}: ${item.kind} belongs to unclassified module ${module}`);
          continue;
        }
        status = entry.status;
        targets = entry.targets;
      }

      if (item.kind === "test") {
        if (!module) {
          errors.push(`${crate}/${item.path}: root test ${item.name} has no module owner`);
          continue;
        }
        const key = `${crate}/${module}`;
        testedModules.add(key);
        targets = testTargets[key] ?? [];
        if (targets.length === 0) {
          errors.push(`${key}: upstream test ${item.name} has no C++ test destination`);
        }
      }
      const declarationKey = `${crate}/${item.path}::${item.name}`;
      if (item.kind === "declaration") seenDeclarations.add(declarationKey);
      if (
        item.kind === "declaration" &&
        status !== "excluded" &&
        status !== "adapter" &&
        (missingDeclarations || status === "full" || status === "facade")
      ) {
        const [kind, name] = item.name.split(" ");
        if (kind !== "mod") {
          const mapping = declarationMappings[declarationKey];
          if (!mapping?.collapse) {
            const searchTargets = mapping?.targets ?? targets;
            const cpp = declarationSourceText(searchTargets);
            const pascal = name!
              .split("_")
              .map((part) => (part.length ? part[0]!.toUpperCase() + part.slice(1) : ""))
              .join("");
            const spellings = mapping?.spellings ?? (kind === "fn" ? [name!, pascal] : [name!]);
            if (!spellings.some((spelling) => new RegExp(`\\b${spelling}\\b`).test(cpp))) {
              const message = `${crate} ${status} missing ${item.path} :: ${item.name} -> ${searchTargets.join(", ")}`;
              if (status === "full" || status === "facade") errors.push(message);
              else if (missingDeclarations) console.log(message);
            }
          }
        }
      }
      if (verboseSurface) {
        const mapping = item.kind === "declaration" ? declarationMappings[declarationKey] : undefined;
        const mappingNote = mapping?.collapse
          ? ` [collapsed: ${mapping.collapse}]`
          : mapping?.spellings
            ? ` [C++ spelling: ${mapping.spellings.join(" | ")}` +
              `${mapping.targets ? ` in ${mapping.targets.join(", ")}` : ""}]`
            : "";
        console.log(
          `${crate} ${item.kind} ${item.path} :: ${item.name} -> ` +
            `${status} ${targets.length ? targets.join(", ") : "(standing exclusion)"}${mappingNote}`,
        );
      }
    }

    for (const key of Object.keys(testTargets).filter((key) => key.startsWith(`${crate}/`))) {
      if (!testedModules.has(key)) errors.push(`${key}: test destination ledger has no upstream tests`);
    }
    const declarations = items.filter((item) => item.kind === "declaration").length;
    const uses = items.filter((item) => item.kind === "pub-use").length;
    const tests = items.filter((item) => item.kind === "test").length;
    console.log(
      `port surface: ${crate} ${declarations} declarations, ${uses} pub-use statements and ${tests} tests ` +
        `across ${testedModules.size} tested modules`,
    );
  }
  for (const key of Object.keys(declarationMappings)) {
    if (!seenDeclarations.has(key)) {
      errors.push(`public declaration mapping has no pinned Rust declaration: ${key}`);
    }
    const mapping = declarationMappings[key]!;
    const hasSpellings = !!mapping.spellings?.length;
    const hasCollapse = !!mapping.collapse?.trim();
    if (hasSpellings === hasCollapse) {
      errors.push(`public declaration mapping needs exactly one spelling list or collapse reason: ${key}`);
    }
  }
}

const runText = readFileSync(join(root, "cmd", "run.ts"), "utf8");
const pinAt = runText.indexOf("export const gpuiComponent");
const pinText = pinAt >= 0 ? runText.slice(pinAt, pinAt + 500) : "";
if (!pinText.includes(pinnedGpuiComponent)) {
  errors.push(`ledger pin ${pinnedGpuiComponent} differs from cmd/run.ts`);
}

// Layering invariant: GPUI owns only the small style record its renderer
// consumes; the component palette and its Base conversion live under ui.
const gpuiHeader = readFileSync(join(root, "src/gpui/gpui.h"), "utf8");
const gpuiSources = ["src/gpui/gpui.cpp", "src/gpui/entity.cpp", "src/gpui/window_common.cpp"]
  .map((path) => readFileSync(join(root, path), "utf8"))
  .join("\n");
const uiThemeHeader = readFileSync(join(root, "src/ui/theme.h"), "utf8");
const baseThemeTokens = readFileSync(join(root, "src/base/theme_tokens.h"), "utf8");
const windowCommon = readFileSync(join(root, "src/gpui/window_common.cpp"), "utf8");
const accessibilityWin = readFileSync(join(root, "src/gpui/accessibility_win.cpp"), "utf8");
const accessibilityLinux = readFileSync(join(root, "src/gpui/accessibility_linux.cpp"), "utf8");
const windowLinux = readFileSync(join(root, "src/gpui/window_linux.cpp"), "utf8");
const windowMac = readFileSync(join(root, "src/gpui/window_mac.cpp"), "utf8");
const windowWin = readFileSync(join(root, "src/gpui/window_win.cpp"), "utf8");
if (/\bstruct\s+Theme(?:Tokens)?\b/.test(gpuiHeader)) {
  errors.push("theme layering: component Theme type leaked into gpui/gpui.h");
}
if (/\bTheme(?:Now|Light|Dark|Install|SemanticTokens)\s*\(/.test(gpuiSources)) {
  errors.push("theme layering: GPUI runtime reads the component palette directly");
}
if (!/\bstruct\s+Theme\b/.test(uiThemeHeader) || !/\bstruct\s+ThemeTokens\b/.test(uiThemeHeader)) {
  errors.push("theme layering: ui/theme.h does not own both component theme types");
}
if (/\bThemeSemanticTokens\s*\(/.test(baseThemeTokens)) {
  errors.push("theme layering: Base declares a conversion from the component palette");
}

// Accessibility invariant: Base/UI roles are frame data owned by GPUI, then
// projected after layout beside focus/hit state. Actions must route back
// through the same listener/state seams as pointer and keyboard input.
for (const marker of [
  "struct AccessibilityInfo",
  "struct AccessibilityNode",
  "Vec<AccessibilityNode> accessibility",
  "AccessibilityCollect(El* root",
  "OnAccessibilityIncrement",
  "AccessibilityActionSetValue",
]) {
  if (!gpuiHeader.includes(marker)) {
    errors.push(`accessibility runtime: missing ${marker}`);
  }
}
if (
  !windowCommon.includes("AccessibilityCollect(root, &win->accessibility)") ||
  !windowCommon.includes("WindowAccessibilityPerform(")
) {
  errors.push("accessibility runtime: frame projection or action dispatch is not wired");
}
for (const marker of [
  "IRawElementProviderFragmentRoot",
  "UiaReturnRawElementProvider",
  "IInvokeProvider",
  "IToggleProvider",
  "IValueProvider",
  "IRangeValueProvider",
  "IExpandCollapseProvider",
  "ISelectionProvider",
  "ISelectionItemProvider",
  "ITextProvider",
  "ITextRangeProvider",
  "IGridProvider",
  "IGridItemProvider",
  "ITableProvider",
  "ITableItemProvider",
  "UiaRaiseStructureChangedEvent",
  "UIA_AutomationFocusChangedEventId",
]) {
  if (!accessibilityWin.includes(marker)) {
    errors.push(`Windows accessibility adapter: missing ${marker}`);
  }
}
if (!windowWin.includes("case WM_GETOBJECT") || !windowWin.includes("AccessibilityWinGetObject(")) {
  errors.push("Windows accessibility adapter: WM_GETOBJECT is not wired");
}
for (const marker of [
  "GpuiAccessibilityElement",
  "accessibilityChildren",
  "accessibilityHitTest:",
  "accessibilitySelectedTextRange",
  "NSAccessibilityPostNotification",
]) {
  if (!windowMac.includes(marker)) {
    errors.push(`macOS accessibility adapter: missing ${marker}`);
  }
}
for (const marker of [
  "org.a11y.atspi.Accessible",
  "org.a11y.atspi.Component",
  "org.a11y.atspi.Action",
  "org.a11y.atspi.Value",
  "org.a11y.atspi.Selection",
  "org.a11y.atspi.Text",
  "GetApplicationBusAddress",
  "GetActions",
  "GetStringAtOffset",
  "GetAll",
  "AccessibilityLinuxPump",
]) {
  if (!accessibilityLinux.includes(marker)) {
    errors.push(`Linux accessibility adapter: missing ${marker}`);
  }
}
if (!windowLinux.includes("AccessibilityLinuxFd()") || !windowLinux.includes("AccessibilityLinuxPump()")) {
  errors.push("Linux accessibility adapter: AT-SPI socket is not wired into poll");
}

const counts = (status: Status) => entries.filter((e) => e.status === status).length;
console.log(
  `port audit: ${entries.length} modules at ${pinnedGpuiComponent.slice(0, 12)} ` +
    `(${counts("full")} full, ${counts("partial")} partial, ` +
    `${counts("adapter")} adapters, ${counts("excluded")} excluded)`,
);
if (!existsSync(rustRoot)) console.log("port audit: Rust checkout absent; checked pinned ledger and C++ destinations");

if (errors.length) {
  for (const error of errors) console.error(`error: ${error}`);
  process.exit(1);
}
