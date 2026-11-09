> 질문: 에디터 UI(ImGui 패널)를 고치거나 검토할 때 무엇을 지키고, 결과 화면을 어떻게 판정하는가?
> 독자: 에디터 패널 · 공용 위젯 · 테마 토큰 · 에디터 입력을 고치는 에이전트와 사람.
> 결론: 패널은 MVVM(View 는 ImGui 와 ViewState 만, 모델 접근은 ViewModel 하나)으로 짓는다. 색 · 간격은 토큰 키로만, 결함은 공용 위젯 · 토큰에서 고치고, 화면은 `uiaudit` · `uicapture` 캡처를 아래 체크리스트로 직접 본다. 입력 · 맥락 메뉴는 입력 맵과 `EditorInputGUI` 하나가 정한다.

## MVVM: View, ViewModel, model

A panel is split so that the ImGui frame can later leave the game (main) thread (`docs/DesignBacklog.md` "에디터 UI 스레드 분리 검토"). Immediate-mode UI redraws every frame, so there is no data binding or change notification (Unreal UMG MVVM and Unity UI Toolkit bindings exist for retained UI).

| Layer | Where | Does | Must not |
| --- | --- | --- | --- |
| Model | `Common/Workspace` · `Common/Commands` · `Engine/*` | Scene, assets, editor commands, workspace — unchanged | — |
| ViewModel | `Panels/<Panel>/Model/<Name>ViewModel.{h,cpp}` (ImGui-free part: `imgui.h` hits the generated `EditorModelForbidsImGui.h` `#error`) | The only code of the panel that reads or writes the model. `gather( <Name>ViewState& )` fills a value-only state; intent arrives as command methods | Include model headers in its **header** (forward-declare; the `.cpp` includes them) |
| ViewState | next to the ViewModel | Plain values: strings, numbers, enums, ids, vectors of row structs | Hold a pointer or reference (`GameObject*`, `Scene&`, a span into the scene) |
| View | `Panels/<Panel>/<Name>Panel.{h,cpp}` | ImGui calls, reading the ViewState, calling ViewModel commands | Include a model or scene header (`Config/Rules/Lint/EditorViewIncludes.json` lists the banned folders and the value headers that stay allowed) |

- **One `gather` per frame, at the top of `drawContent`.** It is the synchronisation point: today it runs on the main thread inside the frame; when the UI leaves the main thread the main thread calls it and hands the state over. Keep the state in a panel member so its vectors reuse their storage.
- **Commands are methods named for the intent** (`setLayerInitiallyActive( name, bOn )`), taking ids and values, never pointers. They are the only mutation path from the View; later they become queued and run at frame start.
- **A command that changes document data goes through the undo stack**: an editor command that records a data undo step (`SW_UNDO_STEP`) and marks the scene dirty. Scene settings go through `EditorSceneSettingsCommands::apply` (one path for every `SceneSettings` field); objects through `EditorSceneCommands` · `EditorTransaction`. A command that changes only the view (hide in editor, expand a row, a filter) records nothing and says so in a comment.
- **Headless test** for every ViewModel command that edits data: `Panels/<Panel>/Model/Tests/Test<Name>ViewModel.cpp` binds a local `CommandStack` in a `SubsystemScope` (`ScopedEditorScope`), runs the command, checks one undo step, undoes and redoes. Editor unit tests run through `App -EnableEditor -scenario=engine/automation/editor/selftests.scenario.xml`, not `App --test`.
- **Name**: a class that gathers view values and takes commands for one panel is a `…ViewModel`; a class of pure rules shared by several ViewModels keeps its own name (`…Util`, `…Rules`).
- **Shared windows a View may use** (instead of `EditorService.h` · `EditorContext.h`): find a panel, a command or the shortcut table with `EditorPanelManager` · `EditorCommandRegistry` · `EditorInputActions::findInEditorContext` / `getInEditorContext`; ask for the scene-edit lock with `IEditorPanel::areSceneEditsAllowed`; resolve resource paths with `EditorPaths::getAssetPaths`; a viewport image (texture, size, drawn position, game-view focus, screenshot, ImGui image registration) goes through `EditorViewSurface`. An engine service a single panel needs (audio, input, UI system, profiler, …) is found by a static on that panel's Model (`AudioMixerModel::getAudioService`), never in the View.
- **Model headers stay light**: a Model that holds a background job or a document value by value includes only value headers (`EditorBackgroundIO.h` sees `Asset/*Entry.h` · `EditorLocalizationSheet.h`; the job bodies' commands are in the `.cpp`). A value type that lived in a command header moves to its own value header and goes on `_allowedHeaders` with its reason. An owner object from the undo plumbing is held by `unique_ptr` with a forward declaration (`UIPreviewModel::_pUndoOwner`).
- **Inspector extensions** (`IInspectorComponent`) do not include component headers: values come from an edit model (`InspectorComponentEdit::gatherCamera` · `applyCamera`, write only changed fields) and the registration takes the type function from it (`SW_EDITOR_INSPECTOR_FOR_TYPE`). Undo for the extension area stays with the inspector frame (`InspectorPropertyUndo::trackLastItem`).
- **Property grid edit model** (inspector core — `EditorPropertyGrid`, `InspectorPanel`, `InspectorPropertyManager`, the reference drawer; Unreal `IPropertyHandle` / Unity `SerializedProperty` are the same layer). The grid today walks reflection with raw instance pointers (`EditorPropertyGridTarget::_pInstance` · `_pComponent` · `_pObject`) and hands member addresses to ImGui. The edit model replaces that in steps, each step one commit that keeps the inspector working:
  1. **Edit recorder** (done): `Model/InspectorEditRecorder` owns the before-snapshots and the end-of-frame commit, keyed by widget id; the View (`InspectorPropertyUndo`) only reads ImGui item activation and forwards.
  2. **Target ids**: the target becomes a value — selection scope (main or preview selection) + object ids + component type name; `PropertyGridModel` (Model/) resolves it each frame and owns the per-target layout cache and favourites.
  3. **Rows**: `gather` emits one value row per drawn property — address path (`TextAddress` chain), label, category, widget kind (bool · int · float · vector · enum · string · asset path · handle · scene reference · struct · container), current value (typed scalar or `SerializerUtil` text), mixed / read-only / differs-from-default flags, display unit. Scalars first, then structs and containers (child rows carry the parent path).
  4. **Commands**: one write path `setPropertyText( path, text )` (`applyPropertyTextAsEdit` today — notify `onPropertyChanged` / `_onEdited`, then multi-select fan-out), typed live setters for drags (`setPropertyFloat` …, recorder begin/finish around them), `resetToDefault`, container `addElement` · `removeElement` · `replaceElement`, `invokeMethod( name, argument texts )`, favourite toggle.
  5. **Drawers**: `IInspectorProperty` receives the row and a command sink instead of `MutableObjectAddress`; the reference drawer's scene lookup moves to the model.
  6. **Panel**: `InspectorViewModel` gathers the header (name · active · tag · prefab state), component cards and the add-component list; object edits (`Model/InspectorObjectEdit::applyObjectEdit` — model code, already in Model/) become commands.
- **While migrating** the lint `EditorViewIncludes` (`py -3 -m Scripts lint editor-view-includes`, run by the commit hook) checks the views listed in `_migratedViews`, directly and through the panel `Model` headers they include. Add a view to the list in the commit that migrates it. When every panel is migrated, the View parts in `Source/Editor/CMakeLists.txt` get `#error` stubs for the banned headers (the same device as the tier stubs) and the list and lint are deleted. The shared GUI headers (`IEditorPanel.h` · `EditorPanelManager.h` · `EditorPreviewPanel.h` · `EditorDocumentPanel.h`) already reach no banned folder (a stub blocks transitive includes too): the registration base lives in `Common/EditorExtensionRegistry.h`, workspace members are held by `unique_ptr` behind forward declarations, and a panel asks its base for workspace services it only triggers (`IEditorPanel::requestAnimationFrame`, `EditorPanelManager::findInEditorContext`). Keep them that way.

## Tokens, not numbers

- Colours, spacing, radii, borders, font and icon sizes come from `Content/editor/theme/<name>.tokens.json` only (dark · charcoal · midnight · light · highcontrast). Code names a key: `SW_THEME_COLOR( "Base.TextMuted" )`, `SW_THEME_METRIC( "Spacing.ItemSpacingY" )`. The ImGui style is built from the tokens in `EditorThemeUtil::applyTheme`. A set writes `_base` (another set's name) and only the keys that differ; `dark` is the parent of the others.
- Every set has the same key set (checked on load); the unit test `EditorThemeKeys` scans the sources for keys the files lack. Adding a key means adding it to all five files in one commit.
- A defect seen in many panels is fixed once: in a token value, in `EditorThemeUtil`, or in a shared widget (`EditorWidgets` · `EditorChrome`). A per-panel `PushStyleColor` / size literal that patches one screen is the wrong fix.
- Shared shapes already exist — use them instead of re-drawing (every panel the same, like Unreal · Unity · Godot):
  - panel toolbar: a command as a button `EditorCommandGUI::drawToolbarButton` (empty state / dialog: `drawCommandButton`, icon + label) (icon, greyed when disabled, tooltip "name shortcut + description" from the command — no hand-written shortcut), groups split by `EditorWidgets::drawToolbarSeparator`, toggles `drawToggleButton` · `drawIconToggle`;
  - search / filter row: `drawFilterBar` (search field filling the row, count at the right end); a bare field `drawSearchField`;
  - empty state: `drawEmptyState` (one sentence + optional next-step button), `drawEmptyHint`, and for a filter with no match `drawNoSearchResultHint`;
  - verbosity: short labels with the long text in a tooltip (`drawPropertyRowLabel( label, tooltip )` · `drawInlineLabel( label, tooltip )` · `drawTooltip` · `drawHelpMarker`), rarely used settings under `beginAdvancedSection` (collapsed, like Unreal Details' Advanced);
  - status line at the panel foot: `drawPanelStatus`; counts `drawCountLabel`;
  - context menus: commands for a target through `EditorInputGUI::drawContextMenuFor` (R1 – R9 below); clamped names `drawClampedLabel`; widths in text columns `EditorChrome::setNextItemWidthColumns`.
  - labels go left: `drawTextField` · `drawTextFieldMultiline` (with `pHint` placeholder) and `beginCombo` move a visible `"Name###id"` label in front of the field; never `ImGui::BeginCombo` with a visible label.
  Lint `EditorWidgetLabels` bans ImGui's right-hand labels on inputs and combos (string literal or `makeStableLabel`). A row label longer than the label column is cut with an ellipsis and shown whole on hover.

## App-screen rules for the editor

The editor is an app screen (taste-skill `references/app-screens.md`). The parts that apply to an ImGui editor:

- **Accent on data and selection only**: the selected row or tab, the primary action of a dialog, a plotted series, a status that needs action. Not on chrome, headers, every button or a whole toolbar (Unreal 5 and Godot 4 keep chrome grey and spend blue on selection; Unity's Pro skin does the same).
- **A toggle that is on is a neutral step** (`Base.ToggleActive`, one tone lighter than `Base.ToggleInactive`), never the accent fill.
- **One grotesk** for all text; monospace only for log lines, IDs and code. Sizes come from `Typography.*` tokens; no extra heading sizes per panel.
- **Status colours are for status** (`Signal.*`): one red mark per object at most; a zero is printed as a zero, a missing value as a grey dash.
- **Tables are rules and alignment**: no zebra (`Base.TableRowBgAlt` is transparent), sentence-case grey headers, numbers right-aligned.
- **No glows, gradients or shadows** on resting panels; a shadow only on what floats (popup, tooltip, menu).
- **An empty area keeps its frame** and says why it is empty and what to do next, in one sentence (`drawEmptyHint`).
- Words carry ≥ 4.5:1 contrast on the surface they sit on (`Base.TextMuted` included); icons and focus rings ≥ 3:1; the highcontrast set ≥ 7:1 for text.

## Capture

- `<CaptureEditorUI file="…" maxMagentaFraction="0" maxClippedWindows="N"/>` re-draws the final ImGui draw lists of every viewport into an off-screen target (`EditorUICapture`) — floating windows included, independent of OS window order. It waits until the editor is settled and window layout is stable.
- Metrics: magenta fraction (missing texture or debug colour), and clipped windows (content larger than the window with no scrollbar in that direction; the names go to the log line `Editor UI capture`).
- `editor/uiaudit.scenario.xml` captures one panel at a time (focused, then closed so it does not hide the next); `editor/uicapture.scenario.xml` captures the default layout in the dark, light and highcontrast sets on every backend. PNGs land in the scenario output folder (`build/<preset>/Testing/Scenario/…`).
- No pixel goldens for editor UI: font rasterisation and DPI differ per machine, so the PNG is read by a person or agent on the same machine.

## Review checklist

Read each PNG at full size (Read tool on the image). One line per defect: `priority | panel | file | defect | where fixed`. P0 unusable, P1 hard to read or misleading, P2 consistency, P3 polish. Fix P0 · P1 at the root, capture again.

| Item | Defect when |
| --- | --- |
| Clipping | Text or an icon cut by its container with no ellipsis or tooltip; a window counted by `maxClippedWindows`; a column or combo too narrow for its value |
| Overlap | Two widgets' rectangles overlap by 1px or more; a value drawn over its slider or label |
| Alignment | Labels and fields in one column start more than 1px apart; widgets on one line off their vertical centre; different label-column widths inside one panel |
| Spacing | Gaps that are not the `Spacing.*` tokens (density applied); two gap sizes for the same relation in one panel |
| Contrast | Body or muted text under 4.5:1; icons and focus under 3:1; highcontrast text under 7:1 |
| Accent overuse | Accent fill on chrome, headers or an on-toggle; more than one hue family outside data and status |
| Empty state | An empty list or panel with no reason and next step |
| Error state | Told by colour alone; no cause or fix in the message; not a `Signal.Error` colour |
| Consistency | Same-role buttons of different heights; one action named differently across panels (`Reset` · `Revert`); icon sizes differ for one purpose; internal variable names shown as labels |
| Type | A second text face outside log lines; a per-panel heading size |
| Missing art | Magenta, a missing icon glyph (tofu box), a localisation key shown instead of text |

Not judgeable from one picture: tab order, focus visibility, 24×24 hit targets — write "not judged".

## Input and context menus

Panels declare *what* (commands for a target, payload kinds a widget drags or accepts); *when* is decided in one place. Keys, buttons and gestures belong to the engine input map (`Content/editor/input/editor.input.xml`, layer `Editor.Global` plus `Editor.Panel.<id>` turned on for the focused panel); `EditorInputGUI` holds only what the map cannot: context menus, drag-and-drop payloads and edit keys while typing.

| Rule | |
| --- | --- |
| R1 | A context menu opens on **release**, and not if the press passed the drag threshold (`dragThreshold`, one constant) or flew/panned the view |
| R2 | When the menu opens on an item outside the selection, that item becomes the selection; inside, the multi-selection stays; empty space keeps the selection |
| R3 | The menu acts on the whole selection; single-target commands (Rename) are disabled for a multi-selection |
| R4 | Menu items point at a command id; label, enabled state and shortcut text come from the command and its input action — no hand-written `"Ctrl+D"` |
| R5 | One target kind, one menu, whichever panel opens it; panel-only items go into a section of that kind |
| R6 | Core items open without extensions; a menu with no enabled item does not open |
| R7 | Viewport and 2D canvas: right-drag flies (2D pans), Alt+right-drag orbits/zooms, a release under the threshold opens the menu |
| R8 | No hidden right-click action that is not a menu item |
| R9 | The game view passes the right button to the game |

Enforced by type: `Common/Input/EditorInputBan.h` is attached to every editor target as ImGui's user header (`sw_applyEditorInputBan`) and re-declares ImGui input, popup and drag-drop functions `[[deprecated]]` with the warning raised to an error; only the two adapter files that define `SW_EDITOR_INPUT_ADAPTER` are exempt. Drag payloads are one POD `{kind, uint64 id}` (`EditorDragPayload`), accepted by kind set, never by name string. A command takes its target (`EditorCommandTarget`) and runs at the start of the next frame (`queueCommand`).
