# Base/UI structural fidelity map

`cmd/audit-port.ts` is the machine-readable source of truth for the mapping
from the pinned `crates/base` and `crates/component` module trees into `src/base` and
`src/ui`. This file only explains it.

```
bun cmd/audit-port.ts
bun cmd/audit-port.ts -surface                # every declaration/re-export/test mapping
bun cmd/audit-port.ts -missing-declarations   # heuristic C++ spelling report
```

The audit checks the pinned SHA and every declared C++ destination. When
`.work/gpui-component` exists it reads the complete Rust source trees, not just
`lib.rs`: every module-level public declaration, every `pub use`, and every
real test (`#[test]` inside a doc comment deliberately does not count). Each
item must belong to a classified module, each tested module must name existing
C++ suites, and stable content hashes fail when anything is added, removed or
renamed at the pinned checkout — so a pin bump produces an explicit decision
list instead of a silent gap. CI runs it, and validates test destinations even
when the Rust checkout is deliberately absent.

For a module marked `full`, the audit also requires each declaration's Rust
spelling (or the direct PascalCase form of a free function) in its C++ targets.
The `declarationMappings` table records intentional placements and spellings
(`init` → `BaseInit`, `locale` → `LocaleNow`, the runtime-owned `AutoScroll`)
and deliberate collapses, each with a required reason — Rust permits a public
item under a private submodule, and traits and functions frequently project
into a C++ builder or function table.

Statuses:

- `full` — no known structural or behavioural gap in the surface upstream
  examples and stories use; every public declaration has a C++ spelling or an
  explicit mapping.
- `partial` — a destination exists but its surface, ownership, runtime
  placement, accessibility or tests still differ.
- `adapter` — the C++ runtime or the hard dependency rules require a
  deliberately different shape.
- `excluded` — a standing non-goal in AGENTS.md (currently only async utility
  plumbing).

Every non-`full` entry carries a reason in the ledger.

## Non-mechanical mappings

Everything else follows the naming rule in `port-upstream.md`. These do not:

| Rust module family | C++ surface | Decision |
| --- | --- | --- |
| `base/calendar`, `base/date_picker` | `base/calendar*`, `base/date_picker*` | payload enums are tagged POD values; `CalendarState` is an emitting entity; label/item closures become function tables |
| `base/dock/*` | `base/dock*`, `base/tiles*` | one Base dock family |
| `base/input/*` | `base/input*`, `base/input_keys*` | one Base input family |
| `ui/table/data_table` | `ui/data_table.h`, `ui/table*` | canonical UI include; behaviour delegates to `base/data_table*` |
| `ui/list/*` | `ui/list*` | themed surface over shared `base/list*` |
| `ui/menu/popup_menu` | `ui/popup_menu.h`, `ui/menu*` | canonical UI include over `base/popup_menu*` |
| `ui/plot/shape/sankey` | `ui/sankey.h`, `ui/plot*` | canonical UI include over `base/sankey*` |
| `ui/scroll/*` | `ui/scroll*`, `gpui::ScrollRect` | transparent mask/handle siblings collapse onto the renderer-owned viewport; source-shaped builders keep the distinction |
| `ui/sidebar/*` | `ui/sidebar*` | `SidebarItem` is a POD render function table; menus, groups and the sidebar compose through it |
| `ui/time/*` | `ui/time*`, `base/calendar*` | themed `Calendar`/`DatePicker` are façades over retained Base state |
| `ui/highlighter/*` | `ui/highlighter*`, `ui/syntax*` | one scanner-backed adapter |
| `ui/text/format/html` | `ui/text*`, `ui/html*` | one handwritten-parser adapter |
| serde-backed state | `base/json*` | private support, not a Base public module |

Recurring projections, so they need no per-module note: a Rust trait becomes a
POD function table; a retained closure becomes an entity or a function table; a
trait's methods become `Window*`-prefixed free functions; `Vec`-backed public
builders must not silently truncate at a fixed C++ capacity.

## Next fidelity order

The audit currently reports no partial modules and no missing declaration
spellings. The portable accessibility tree now reaches all three desktop
APIs: Windows UIA (including Text, selection, grid and table patterns), macOS
NSAccessibility, and Linux AT-SPI without adding a D-Bus library dependency.
The remaining fidelity work is behavioural depth and native assistive-
technology testing rather than another Base/UI surface port.

Behavioural gaps live in `port-status.md`.
