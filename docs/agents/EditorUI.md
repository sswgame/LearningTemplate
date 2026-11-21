# EditorUI

> 결론: 패널은 MVVM(View 는 ImGui 와 ViewState 만, 모델 접근은 ViewModel 하나)으로 짓는다. 색 · 간격은 토큰 키로만, 결함은 공용 위젯 · 토큰에서 고치고, 화면은 `uiaudit` · `uicapture` 캡처를 아래 체크리스트로 직접 본다. 입력 · 맥락 메뉴는 입력 맵과 `EditorInputGUI` 하나가 정한다.

## MVVM: View, ViewModel, model

| Layer | Where | Does | Must not |
| --- | --- | --- | --- |
| Model | `Common/Workspace` · `Common/Commands` · `Engine/*` | Scene, assets, editor commands, workspace | — |
| ViewModel | `Panels/<Panel>/Model/<Name>ViewModel.{h,cpp}` | The only panel code that reads or writes the model. `gather( <Name>ViewState& )` fills a value-only state; intent arrives as command methods | Include `imgui.h` (generated `#error`) or model headers in its **header** (forward-declare) |
| ViewState | next to the ViewModel | Plain values: strings, numbers, enums, ids, vectors of row structs | Hold a pointer or reference into the scene |
| View | `Panels/<Panel>/<Name>Panel.{h,cpp}` | ImGui calls, reading the ViewState, calling ViewModel commands | Include a model, scene or service header |

**Build-enforced**: view files (`[panelViewBan] views` in `Config/Rules/Layers/EditorLayers.toml`, extension modules via `sw_add_editor_extension( <name> PANEL_VIEW_SOURCES … )`) compile with `#error` stubs for banned headers and for `Engine/` · `DevTools/` · `GameFramework/` outside `[panelViewAllowedFolders]`. `[panelViewAllowed]` lists allowed value headers, each with a reason. A file joins `views` once it includes no banned header.

- **One `gather` per frame, at the top of `drawContent`, through `EditorPanel::gatherViewState( viewModel, args… )`.** Keep the state in a panel member so vectors reuse storage. A ViewModel whose state comes only from the model exposes `uint64 getGatherGeneration() const` so an unchanged frame skips the gather; one that takes per-frame view input (scroll, clip rect) does not.
- **Commands are methods named for the intent** (`setLayerInitiallyActive( name, bOn )`), taking ids and values, never pointers — the only mutation path from the View. A command changing engine state the game thread owns (render target size, play session) posts an `EditorUICommand` to `EditorUICommandQueue` instead of calling the engine.
- **Registered commands live outside the View**: `SW_EDITOR_COMMAND` · `SW_EDITOR_CONTEXT_COMMAND` · `SW_DEV_COMMAND` do not compile in a view TU — put them in `Model/` or `Common/Commands`. A view registers only `SW_EDITOR_SUBMENU_COMMAND` and `SW_EDITOR_PANEL_OPEN_COMMAND`. A command whose result the window must show posts a request value to the ViewModel (`postRequest`).
- **Document edits go through the undo stack** (`SW_UNDO_STEP`, mark the scene dirty). Scene settings through `EditorSceneSettingsCommands::apply`; objects through `EditorSceneCommands` · `EditorTransaction`. A view-only command records nothing and says so in a comment.
- **Headless test** for every data-editing ViewModel command: `Panels/<Panel>/Model/Tests/Test<Name>ViewModel.cpp` with a local `CommandStack` (`ScopedEditorScope`) — run, check one undo step, undo, redo. Run via `App -EnableEditor -scenario=engine/automation/editor/selftests.scenario.xml`, not `App --test`.
- **Document panels**: a panel editing one asset file derives from `EditorDocumentPanel`; one without a document path from `EditorStateDocumentPanel`.
- **Names**: a window's state-and-commands class is `…ViewModel`, never a bare `…Model`. The editing copy of one document owned by a ViewModel is `…EditModel`; a data structure is named for what it is; shared pure rules keep their own name (`…Util`, `…Rules`).
- **Shared windows a View may use**: `EditorPanelManager` · `EditorCommandRegistry` · `EditorInputActions::findInEditorContext`, `EditorPanel::areSceneEditsAllowed`, `EditorPaths::getAssetPaths`, `EditorViewSurface` for viewport images. An engine service one panel needs is found by a **private** static on its ViewModel; the View never sees the service.
- **ViewModel headers stay light**: value headers only; an owner object from undo plumbing is a forward-declared `unique_ptr`. The shared GUI headers (`EditorPanel.h` · `EditorPanelManager.h` · `EditorPreviewPanel.h` · `EditorDocumentPanel.h`) reach no banned folder — keep them that way.
- **Trees and long lists**: the ViewModel owns expansion state (`SetNextItemOpen` in, `TreeNodeEx` result back as a command), caches flattened rows until the structure changes, and fills details only for on-screen rows (`HierarchyViewModel`). An unbounded flat list draws through `EditorVirtualList` — never a bare `ImGuiListClipper`, never every row.
- **Inspector extensions** (`IInspectorComponent`) take `EditorPropertyGridTarget` by value, never a component pointer, and include no component headers; values come from an edit model, registration via `SW_EDITOR_INSPECTOR_FOR_TYPE`.
- **Property grid** (`Common/Widgets/PropertyGrid`, ViewModel `Model/PropertyGridViewModel`): the View holds only row ids; commands take a row id (`setRowValue` · `setPropertyText` · `setAssetPath` · container and map commands …). A widget calls `InspectorPropertyUndo::trackRowEdit` right after it is drawn and **before** its write command. Edits that restructure a value run after all rows are drawn. Drawers (`IInspectorProperty::draw( const PropertyValueRow&, PropertyGridViewModel& )`) return edits as commands; scene reads they need are value gatherers in `Model/`.
- **AssetActions and viewport visualizers split the same way**: registration and placement in `Model/`; visualizers gather into `EditorVisualizerDrawList` (`SW_EDITOR_VISUALIZER_GATHER`) and draw only values. Interactive tools register with `SW_EDITOR_VISUALIZER_TOOL` (view TU only), draw from their Model and send commands back; picking takes ids from `EditorViewportPick`.

## Tokens, not numbers

- Colours, spacing, radii, borders, font and icon sizes come only from `Content/editor/theme/<name>.tokens.json`. Code names a key: `SW_THEME_COLOR( "Base.TextMuted" )`, `SW_THEME_METRIC( "Spacing.ItemSpacingY" )`. A set writes `_base` and only the keys that differ.
- Every set has the same keys (checked on load; unit test `EditorThemeKeys`). A new key goes into all five files in one commit.
- A defect seen in many panels is fixed once — in a token, `EditorThemeUtil`, or a shared widget (`EditorWidgets` · `EditorChrome`). A per-panel `PushStyleColor` or size literal is the wrong fix.
- Use the shared shapes:
  - toolbar: `EditorCommandGUI::drawToolbarButton` (dialog: `drawCommandButton`) — tooltip and shortcut come from the command; groups `drawToolbarSeparator`; toggles `drawToggleButton` · `drawIconToggle`;
  - search row `drawFilterBar` (bare field `drawSearchField`);
  - empty state `drawEmptyState` · `drawEmptyHint` · `drawNoSearchResultHint`;
  - short labels with long text in a tooltip (`drawPropertyRowLabel` · `drawInlineLabel` · `drawTooltip` · `drawHelpMarker`); rare settings under `beginAdvancedSection`;
  - status `drawPanelStatus`, counts `drawCountLabel`, clamped names `drawClampedLabel`, widths `EditorChrome::setNextItemWidthColumns`;
  - labels go left: `drawTextField` · `drawTextFieldMultiline` · `beginCombo`; never `ImGui::BeginCombo` with a visible label.
- Type-enforced: editor code cannot call ImGui input widgets (`EditorInputBan.h`); `EditorWidgets::inputInt` · `dragFloat` · `combo` … take an `EditorWidgetID` (`SW_WIDGET_ID( "##…" )`, checked at compile time, or `EditorWidgetID::fromString`).

## App-screen rules for the editor

From taste-skill `references/app-screens.md`:

- **Accent on data and selection only** (selected row or tab, a dialog's primary action, a plotted series, a status needing action) — not on chrome, headers or toolbars.
- **A toggle that is on is a neutral step** (`Base.ToggleActive`), never the accent fill.
- **One grotesk** for all text; monospace only for logs, IDs and code. Sizes from `Typography.*`; no per-panel heading sizes.
- **Status colours only for status** (`Signal.*`): at most one red mark per object; zero prints as zero, missing as a grey dash.
- **Tables**: no zebra, sentence-case grey headers, numbers right-aligned.
- **No glows, gradients or shadows** on resting panels; shadows only on what floats.
- **An empty area keeps its frame** and says why and what to do next in one sentence.
- Contrast: words ≥ 4.5:1 (`Base.TextMuted` included), icons and focus ≥ 3:1, highcontrast text ≥ 7:1.

## Capture

- `<CaptureEditorUI file="…" maxMagentaFraction="0" maxClippedWindows="N"/>` re-draws every viewport's ImGui draw lists off-screen once the editor settles; it counts magenta and clipped windows (names in the `Editor UI capture` log line).
- `editor/uiaudit.scenario.xml` captures one panel at a time; `editor/uicapture.scenario.xml` the default layout in dark, light and highcontrast on every backend. PNGs are in `build/<preset>/Testing/Scenario/…`.
- No pixel goldens for editor UI (fonts and DPI differ per machine) — read the PNG on the same machine.

## Review checklist

Read each PNG at full size. One line per defect: `priority | panel | file | defect | where fixed`. P0 unusable, P1 hard to read or misleading, P2 consistency, P3 polish. Fix P0 · P1 at the root, capture again.

| Item | Defect when |
| --- | --- |
| Clipping | Text or icon cut with no ellipsis or tooltip; a window counted by `maxClippedWindows`; a column or combo too narrow for its value |
| Overlap | Two widgets overlap by 1px or more; a value drawn over its slider or label |
| Alignment | Labels and fields in one column start more than 1px apart; off vertical centre; different label-column widths in one panel |
| Spacing | Gaps that are not `Spacing.*` tokens; two gap sizes for the same relation in one panel |
| Contrast | Body or muted text under 4.5:1; icons and focus under 3:1; highcontrast text under 7:1 |
| Accent overuse | Accent fill on chrome, headers or an on-toggle; more than one hue family outside data and status |
| Empty state | An empty list or panel with no reason and next step |
| Error state | Told by colour alone; no cause or fix; not a `Signal.Error` colour |
| Consistency | Same-role buttons of different heights; one action named differently across panels; icon sizes differ for one purpose; internal variable names as labels |
| Type | A second text face outside logs; a per-panel heading size |
| Missing art | Magenta, a tofu glyph, a localisation key shown instead of text |

Not judgeable from one picture: tab order, focus visibility, 24×24 hit targets — write "not judged".

## Input and context menus

Panels declare *what* (commands for a target, payload kinds); *when* is decided in one place. Keys, buttons and gestures belong to the input map (`Content/editor/input/editor.input.xml`, layer `Editor.Global` plus `Editor.Panel.<id>` for the focused panel); `EditorInputGUI` holds only context menus, drag-and-drop payloads and edit keys while typing.

| Rule | |
| --- | --- |
| R1 | A context menu opens on **release**, and not if the press passed `dragThreshold` or flew/panned the view |
| R2 | Opening on an item outside the selection selects it; inside, the multi-selection stays; empty space keeps the selection |
| R3 | The menu acts on the whole selection; single-target commands (Rename) are disabled for a multi-selection |
| R4 | Menu items point at a command id; label, enabled state and shortcut text come from the command — no hand-written `"Ctrl+D"` |
| R5 | One target kind, one menu, whichever panel opens it; panel-only items go into a section of that kind |
| R6 | Core items open without extensions; a menu with no enabled item does not open |
| R7 | Viewport and 2D canvas: right-drag flies (2D pans), Alt+right-drag orbits/zooms, a release under the threshold opens the menu |
| R8 | No hidden right-click action that is not a menu item |
| R9 | The game view passes the right button to the game |
| R10 | Middle click (under the drag threshold) on a property field resets it to its default, only where it differs; one undo step (`EditorInputGUI::kResetToDefaultButton`, `EditorWidgets::beginResetToDefaultGroup` · `endResetToDefaultGroup`). In inheriting data the default is the inherited value |
| R11 | A drop target filters by payload kind, then by content, **before** it highlights (`EditorInputGUI::peekPayload` · `EditorWidgets::peekAssetDrag`); with no accepting target the preview dims. A drop is one model command = one undo step. OS file drops come through `GenericWindow::bindFileDropHandler` into `EditorFileDrop`; paths are untrusted (absolute, no `..`, regular file, importable extension, size · count limits) |
| R12 | A draggable list row selects on release under the drag threshold (`wasItemClickedWithoutDrag`), not on press |
| R13 | Property Paste is enabled only when the clipboard parses as the field's type (`canPasteText`); Ctrl+C · Ctrl+V over a field are the row menu's Copy · Paste (`property.copyValue` · `property.pasteValue`); an asset field's menu adds Browse to Asset and Find References |

Type-enforced: `Common/Input/EditorInputBan.h` re-declares ImGui input, popup, drag-drop and input-widget functions `[[deprecated]]` as errors in every editor target. Only adapter files marked by CMake source properties (`SW_EDITOR_INPUT_ADAPTER` · `SW_EDITOR_NODE_GRAPH_ADAPTER` · `SW_EDITOR_WIDGET_ADAPTER`) are exempt — an in-source `#define` is a redefinition error. Drag payloads are one POD `{kind, uint64 id}` (`EditorDragPayload`), accepted by kind, never by name. A command takes its target (`EditorCommandTarget`) and runs at the start of the next frame (`queueCommand`).
