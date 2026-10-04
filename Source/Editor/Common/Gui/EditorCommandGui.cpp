/**
 * @file EditorCommandGui.cpp
 * @brief 기본 커맨드 표와, 그것을 ImGui 에 드러내는 곳(메뉴 항목 · 전역 단축키)입니다.
 */
#include "pch.h"

#include "Editor/Common/Gui/EditorCommandGui.h"

#include "Core/File/FileUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorAssetCommands.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/EditorGlobalVariableCommands.h"
#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/EditorTransformCommands.h"
#include "Editor/Common/Gui/EditorMenuBar.h"
#include "Editor/Common/Gui/EditorNotificationManager.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Popups/CommandPalettePopup.h"
#include "Editor/Popups/QuickLauncherPopup.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/CommandStack.h"

#include "RuntimeAPI/Service/IModuleCompiler.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorCommandGuiInternal
        {
            // ------------------------------------------------------------------------------
            // 1) 커맨드 동작. 표가 함수 포인터로 가리킨다
            //
            // **여기 있는 함수는 모두 "표가 요구하는 모양으로 바꾸는 일" 을 한다.** 대상이 이미 `void()` / `bool()` 이라면 표가
            // 그 함수를 **직접** 가리키면 되고, 감싸는 것은 이름만 하나 늘리는 일이다(`saveFocusedOrScene` · `requestExit` ·
            // `QuickLauncherPopup::toggle` 등은 표가 직접 가리킨다).
            //
            // 그러니 여기에 새 함수를 만들기 전에 **왜 직접 가리킬 수 없는지** 이유가 있어야 한다. 남아 있는 것들의 이유는 셋 중
            // 하나다.
            //   - 반환형을 맞춘다   : `commandNewScene` (대상이 `bool` 인데 표는 `void()` 다)
            //   - 인자를 고정한다   : `commandAlign<TAxis>` (대상이 축 · 정렬 두 인자를 받는다)
            //   - 대상을 찾아온다   : `commandUndo` (`getService<CommandStack>()`)
            //
            // 그리고 인자를 고정하는 경우에도 **값마다 함수를 하나씩 적지는 않는다** — 축처럼 값만 다른 것은 템플릿 인자로 받는다.
            // ------------------------------------------------------------------------------
            static void onOpenSceneDialogResult( const vector<string>& listPath )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return;

                if ( listPath.empty() == false )
                    pContext->getWorkspace().requestLoadScene( listPath[0] );
            }

            /// @brief 표는 `void()` 를 요구하는데 `tryCreateNewScene` 은 `bool` 을 반환합니다. 그 차이를 메우려고 남겨 둡니다.
            static void commandNewScene()
            {
                // 실패 사유(플레이 중 · SceneManager 없음)는 tryCreateNewScene 이 자체 처리한다.
                (void)EditorAssetCommands::tryCreateNewScene();
            }

            static void commandOpenScene()
            {
                FileDialogParams params{};
                params._type               = FileDialogParams::Type::Open;
                params._title              = "Open Scene";
                params._description        = "Scene";
                params._bEnableMultiselect = false;
                // 씬 이름은 쿠커의 규칙 하나다(`EditorAssetTypeRegistry` → `AssetCookPath`). 맨 `.xml` 은 쿠커가 굽지 않는 이름이다.
                EditorAssetTypeRegistry::appendSuffixes( EditorAssetType::Scene, params._listFilterExtension );

                const string activePack = GameConfig::getActive()._packRoot;
                const string mapsDir    = ResourceUtil::getDomainFolderPath( activePack, path::kMapsFolder );
                if ( FileUtil::directoryExists( mapsDir ) )
                    params._initialDirectory = mapsDir;
                else if ( ResourceUtil::getDomainFolderPath( activePack ).empty() == false )
                    params._initialDirectory = ResourceUtil::getDomainFolderPath( activePack );
                else if ( ResourceUtil::getDomainFolderPath( path::kGamePack ).empty() == false )
                    params._initialDirectory = ResourceUtil::getDomainFolderPath( path::kGamePack );

                FileUtil::openFileDialog( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, onOpenSceneDialogResult ) );
            }

            static void commandUndo()
            {
                CommandStack* pStack = getService<CommandStack>();
                if ( pStack != nullptr )
                    pStack->undo();
            }

            static void commandRedo()
            {
                CommandStack* pStack = getService<CommandStack>();
                if ( pStack != nullptr )
                    pStack->redo();
            }

            static void commandCompileGame()
            {
                IModuleCompiler* pCompiler = getService<IModuleCompiler>();
                if ( pCompiler != nullptr )
                    pCompiler->compileModule( "SWGame" );
            }

            static void commandCompileEditor()
            {
                IModuleCompiler* pCompiler = getService<IModuleCompiler>();
                if ( pCompiler != nullptr )
                    pCompiler->compileModule( "EditorModule" );
            }

            static void commandCompileAll()
            {
                IModuleCompiler* pCompiler = getService<IModuleCompiler>();
                if ( pCompiler != nullptr )
                    pCompiler->compileAll();
            }

            static void commandCancelBuild()
            {
                IModuleCompiler* pCompiler = getService<IModuleCompiler>();
                if ( pCompiler != nullptr )
                    pCompiler->cancel();
            }

            static void commandPlay()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr && pContext->getWorkspace().isSceneDirty() && EditorPlaySession::isStopped() )
                {
                    pContext->getNotificationManager().push( "Play", "Scene has unsaved changes. Use Game View Play to confirm.",
                                                             NotificationType::Warning );
                    return;
                }
                EditorPlaySession::play();
            }

            static GameObject* primaryObject()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return nullptr;
                return pContext->getEditorSelection().getPrimaryObject();
            }

            static Component* selectedComponent()
            {
                EditorContext* pContext = EditorContext::get();
                GameObject*    pObj     = primaryObject();
                if ( pContext == nullptr || pObj == nullptr )
                    return nullptr;
                const uint64 componentId = pContext->getWorkspace().getSelectedComponentId();
                if ( componentId == 0 )
                    return nullptr;
                return pObj->findComponentById( componentId );
            }

            static void warn( const utf8* pTitle, const utf8* pDetail )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                    pContext->getNotificationManager().push( pTitle, pDetail, NotificationType::Warning );
            }

            static void commandPasteComponentValues()
            {
                EditorContext* pContext = EditorContext::get();
                Component*     pComp    = selectedComponent();
                if ( pContext == nullptr || pComp == nullptr )
                {
                    warn( "Paste", "Select a component first" );
                    return;
                }
                if ( pContext->getWorkspace().hasCopiedComponent() == false )
                {
                    warn( "Paste", "Clipboard is empty" );
                    return;
                }
                pContext->getWorkspace().pasteComponentValues( pComp );
            }

            static void commandPasteComponentAsNew()
            {
                EditorContext* pContext = EditorContext::get();
                GameObject*    pObj     = primaryObject();
                if ( pContext == nullptr || pObj == nullptr )
                {
                    warn( "Paste", "Select an object first" );
                    return;
                }
                if ( pContext->getWorkspace().hasCopiedComponent() == false )
                {
                    warn( "Paste", "Clipboard is empty" );
                    return;
                }
                pContext->getWorkspace().pasteComponentAsNew( pObj );
            }

            static void onLoadPresetDialogResult( const vector<string>& listPath )
            {
                if ( listPath.empty() )
                    return;
                Component* pComp = selectedComponent();
                if ( pComp == nullptr )
                {
                    warn( "Preset", "Select a component first" );
                    return;
                }
                // 반환값을 버리면 "프리셋을 골랐는데 아무 일도 없다" 가 된다 (파일이 없거나
                // XML 이 그 컴포넌트 타입과 맞지 않으면 실패한다).
                if ( EditorTransformCommands::loadComponentPreset( pComp, listPath[0] ) == false )
                    warn( "Preset", "Failed to apply the preset to this component" );
            }

            static void onSavePresetDialogResult( const vector<string>& listPath )
            {
                if ( listPath.empty() )
                    return;
                Component* pComp = selectedComponent();
                if ( pComp == nullptr )
                {
                    warn( "Preset", "Select a component first" );
                    return;
                }
                if ( EditorTransformCommands::saveComponentPresetTo( pComp, listPath[0] ) == false )
                    warn( "Preset", "Failed to write the preset file" );
            }

            static void commandLoadComponentPreset()
            {
                if ( selectedComponent() == nullptr )
                {
                    warn( "Preset", "Select a component first" );
                    return;
                }
                FileDialogParams params{};
                params._type                = FileDialogParams::Type::Open;
                params._title               = "Load Component Preset";
                params._description         = "Component Preset";
                params._bEnableMultiselect  = false;
                params._listFilterExtension = { ".preset.xml", ".xml" };
                params._initialDirectory    = EditorGlobalVariableCommands::getComponentPresetFolderPath();
                FileUtil::openFileDialog( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, onLoadPresetDialogResult ) );
            }

            static void commandSaveComponentPreset()
            {
                if ( selectedComponent() == nullptr )
                {
                    warn( "Preset", "Select a component first" );
                    return;
                }
                FileDialogParams params{};
                params._type                = FileDialogParams::Type::Save;
                params._title               = "Save Component Preset";
                params._description         = "Component Preset";
                params._bEnableMultiselect  = false;
                params._listFilterExtension = { ".preset.xml" };
                params._initialDirectory    = EditorGlobalVariableCommands::getComponentPresetFolderPath();
                FileUtil::openFileDialog( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, onSavePresetDialogResult ) );
            }

            // 표가 `void()` 를 요구하므로 인자를 고정하는 함수 자체는 있어야 하지만, **축마다 하나씩 적을 이유는 없다.** 축을 템플릿 인자로 받으면 표는 그대로 `&commandAlign<AlignAxis::X>` 로 가리킨다.
            // 축이 하나 늘어도 여기서 고칠 것은 없다.
            template <AlignAxis TAxis>
            static void commandAlign()
            {
                EditorTransformCommands::alignSelectedObjects( TAxis, AlignType::Center );
            }

            template <AlignAxis TAxis>
            static void commandDistribute()
            {
                EditorTransformCommands::distributeSelectedObjects( TAxis );
            }

            /** @brief 선택한 오브젝트가 스폰된 프리팹의 경로입니다. 프리팹 인스턴스가 아니면 비어 있습니다. */
            static string findSelectedPrefabPath( GameObject*& pOutObj )
            {
                EditorContext* pContext = EditorContext::get();
                pOutObj                 = ( pContext != nullptr ) ? pContext->getEditorSelection().getPrimaryObject() : nullptr;
                if ( pOutObj == nullptr )
                    return {};
                return pContext->getWorkspace().getGameObjectPrefabPath( pOutObj->getObjectId() );
            }

            static void commandApplyPrefabOverrides()
            {
                // **선택한 인스턴스의 프리팹에만** 쓴다. 주의: 포커스된 에셋 경로(콘텐츠 브라우저에서 마지막에 클릭한 것 — 씬 · 머티리얼 · 텍스처)를
                // 쓰면 그 파일을 프리팹으로 덮어쓴다.
                GameObject*  pObj = nullptr;
                const string path = findSelectedPrefabPath( pObj );
                if ( path.empty() )
                {
                    SW_LOG_WARNING( "Apply Overrides needs a selected prefab instance - nothing was written" );
                    return;
                }
                if ( EditorToolAssetCommands::applyPrefabOverridesToTemplate( pObj, path ) == false )
                    SW_LOG_ERROR( "Apply Overrides could not write '%#'", path.c_str() );
            }

            // ------------------------------------------------------------------------------
            // 2) 활성 조건. 표가 함수 포인터로 가리킨다
            // ------------------------------------------------------------------------------
            static bool isCompilerIdle()
            {
                IModuleCompiler* pCompiler = getService<IModuleCompiler>();
                return pCompiler != nullptr && pCompiler->isCompiling() == false;
            }

            static bool isCompilerBusy()
            {
                IModuleCompiler* pCompiler = getService<IModuleCompiler>();
                return pCompiler != nullptr && pCompiler->isCompiling();
            }

            static size_t selectedObjectCount()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return 0;
                return pContext->getEditorSelection().getSelectedObjectCount();
            }

            static bool hasSelection()
            {
                return selectedObjectCount() >= 1;
            }

            static bool hasMultiSelection()
            {
                return selectedObjectCount() >= 2;
            }

            static bool hasPrefabInstanceSelected()
            {
                GameObject* pObj = nullptr;
                return findSelectedPrefabPath( pObj ).empty() == false;
            }

            // ------------------------------------------------------------------------------
            // 3) 기본 커맨드 표. 커맨드를 더하려면 여기에 한 줄
            // ------------------------------------------------------------------------------
            /** @brief 표의 한 줄입니다. 문자열은 모두 리터럴이라 수명 걱정이 없습니다. */
            struct CommandRow
            {
                const utf8*           _pId;
                const utf8*           _pLabel;
                const utf8*           _pIcon;
                const utf8*           _pCategory;
                const utf8*           _pTooltip;
                const utf8*           _pDetail;
                EditorCommandShortcut _shortcut;
                EditorCommandShortcut _altShortcut;
                void ( *_pAction )();
                bool ( *_pEnabled )();
                bool        _bPaletteVisible;
                const utf8* _pMenuPath; ///< nullptr 이면 어느 메뉴에도 없습니다(`EditorCommandDesc::_menuPath`)
                int32       _menuOrder; ///< 메뉴 안 순서 — 백의 자리가 바뀌면 구분선, 메뉴끼리도 이 값으로 줄 섭니다
            };

            /**
             * @brief 에디터 커맨드의 정본입니다.
             * @details 툴팁에 단축키를 손으로 적지 않습니다. `drawCommandItem` 이 이 표의 조합으로 만들어 붙이므로 라벨과 실제 처리가
             *          어긋날 수 없습니다.
             */
            inline static const CommandRow _s_arrCommandRow[] = {
                {                     "scene.new",                     "New Scene",               ICON_FA_FILE,     "Scene",                                     "새로운 빈 씬을 생성합니다",           "Replace the active scene with an empty one",                                                                              {},                                                                        {},                               &commandNewScene,                       nullptr,  true,             "MainMenu/File", 1100},
                {                    "scene.open",                 "Open Scene...",        ICON_FA_FOLDER_OPEN,     "Scene",                  "디스크에서 기존 씬 파일(.scene.xml)을 엽니다",                          "Open a .scene.xml from disk",                                 { EditorCommandKey::O, commandmodifier::kCtrl },                                                                        {},                              &commandOpenScene,                       nullptr,  true,             "MainMenu/File", 1110},
                {                    "asset.save",                          "Save",        ICON_FA_FLOPPY_DISK,     "Scene",                  "현재 포커스된 에셋 또는 활성 씬을 저장합니다",          "Save the focused asset, or the active scene",                                 { EditorCommandKey::S, commandmodifier::kCtrl },                                                                        {},       &EditorAssetCommands::saveFocusedOrScene,                       nullptr,  true,             "MainMenu/File", 1120},
                {               "scene.saveScene",                    "Save Scene",        ICON_FA_FLOPPY_DISK,     "Scene",                        "현재 활성화된 씬을 디스크에 저장합니다", "Write the active scene, or prompt Save As if unsaved",                                                                              {},                                                                        {},  &EditorAssetCommands::saveActiveSceneOrPrompt,                       nullptr,  true,             "MainMenu/File", 1130},
                {              "editor.quickOpen",                 "Quick Open...",   ICON_FA_MAGNIFYING_GLASS,    "Editor",                   "에셋, 씬, 스크립트를 빠르게 검색하여 엽니다",              "Fuzzy-search assets, scenes and scripts",                                 { EditorCommandKey::P, commandmodifier::kCtrl },                                                                        {},                    &QuickLauncherPopup::toggle,                       nullptr, false,             "MainMenu/File", 1200},
                {         "editor.commandPalette",            "Command Palette...",           ICON_FA_TERMINAL,    "Editor",                     "에디터 명령 및 액션을 검색하여 실행합니다",                       "Search and run editor commands",       { EditorCommandKey::P, commandmodifier::kCtrl | commandmodifier::kShift },                       { EditorCommandKey::Space, commandmodifier::kCtrl },                   &CommandPalettePopup::toggle,                       nullptr, false,             "MainMenu/File", 1210},
                {                   "editor.exit",                          "Exit", ICON_FA_RIGHT_FROM_BRACKET,      "File",                                           "에디터를 종료합니다",   "Close the editor after unsaved-change confirmation", { EditorCommandKey::F4, commandmodifier::kAlt | commandmodifier::kDisplayOnly },                                                                        {},              &EditorAssetCommands::requestExit,                       nullptr,  true,             "MainMenu/File", 1300},

                {                     "edit.undo",                          "Undo",        ICON_FA_ROTATE_LEFT,      "Edit",                                 "마지막 편집 작업을 되돌립니다",                                   "Undo the last edit",                                 { EditorCommandKey::Z, commandmodifier::kCtrl },                                                                        {},                                   &commandUndo, &EditorPlaySession::isStopped,  true,             "MainMenu/Edit", 2100},
                {                     "edit.redo",                          "Redo",       ICON_FA_ROTATE_RIGHT,      "Edit",                            "되돌린 편집 작업을 다시 실행합니다",                            "Redo the last undone edit",                                 { EditorCommandKey::Y, commandmodifier::kCtrl }, { EditorCommandKey::Z, commandmodifier::kCtrl | commandmodifier::kShift },                                   &commandRedo, &EditorPlaySession::isStopped,  true,             "MainMenu/Edit", 2110},
                {          "editor.themeSettings",      "Theme & Look and Feel...",            ICON_FA_PALETTE,    "Editor", "에디터 테마 프리셋, 액센트 색상 및 모서리 라운딩을 설정합니다",                       "Open the theme settings dialog",                                                                              {},                                                                        {},                &EditorMenuBar::openThemeDialog,                       nullptr,  true,             "MainMenu/Edit", 2200},

                {             "build.compileGame",         "Compile Game (SWGame)",             ICON_FA_HAMMER,     "Build",       "게임 모듈(SWGame)을 라이브 코딩으로 즉시 재컴파일합니다",                      "Recompile and hot-reload SWGame",       { EditorCommandKey::F11, commandmodifier::kCtrl | commandmodifier::kAlt },                          { EditorCommandKey::F7, commandmodifier::kNone },                            &commandCompileGame,               &isCompilerIdle,  true,            "MainMenu/Build", 3100},
                {           "build.compileEditor", "Compile Editor (EditorModule)",             ICON_FA_WRENCH,     "Build",    "에디터 모듈(EditorModule)을 라이브 코딩으로 재컴파일합니다",                               "Recompile EditorModule",                                                                              {},                                                                        {},                          &commandCompileEditor,               &isCompilerIdle,  true,            "MainMenu/Build", 3110},
                {              "build.compileAll",           "Compile All Modules",      ICON_FA_BOXES_STACKED,     "Build",               "엔진 및 모든 게임/에디터 모듈을 전체 빌드합니다",                    "Build the engine and every module",       { EditorCommandKey::B, commandmodifier::kCtrl | commandmodifier::kShift },                                                                        {},                             &commandCompileAll,               &isCompilerIdle,  true,            "MainMenu/Build", 3120},
                {                  "build.cancel",                  "Cancel Build",                ICON_FA_BAN,     "Build",                       "현재 진행 중인 컴파일 작업을 취소합니다",                           "Cancel the running compile",                                                                              {},                                                                        {},                            &commandCancelBuild,               &isCompilerBusy,  true,            "MainMenu/Build", 3200},

                {                    "play.start",                          "Play",                         "",      "Play",                                                              "",                                 "Start play-in-editor",                                                                              {},                                                                        {},                                   &commandPlay,                       nullptr,  true,                     nullptr,    0},

                {"clipboard.pasteComponentValues",        "Paste Component Values",                         "", "Clipboard",                                                              "",  "Overwrite the selected component from the clipboard",                                                                              {},                                                                        {},                   &commandPasteComponentValues,                       nullptr,  true,                     nullptr,    0},
                { "clipboard.pasteComponentAsNew",        "Paste Component As New",                         "", "Clipboard",                                                              "",      "Add the copied component to the selected object",                                                                              {},                                                                        {},                    &commandPasteComponentAsNew,                       nullptr,  true,                     nullptr,    0},
                {          "preset.loadComponent",         "Load Component Preset",                         "",    "Preset",                                                              "",        "Apply a .preset.xml to the selected component",                                                                              {},                                                                        {},                    &commandLoadComponentPreset,                       nullptr,  true,                     nullptr,    0},
                {          "preset.saveComponent",         "Save Component Preset",                         "",    "Preset",                                                              "",        "Write the selected component to a preset file",                                                                              {},                                                                        {},                    &commandSaveComponentPreset,                       nullptr,  true,                     nullptr,    0},

                {        "transform.snapToGround",          "Snap to Ground (Y=0)",                         "", "Transform",                                                              "",          "Snap selected objects onto the ground plane",                                                                              {},                                                                        {}, &EditorTransformCommands::snapSelectedToGround,                 &hasSelection,  true, commandmenu::kViewportAlign,  100},
                {              "transform.alignX",              "Align X (Center)",                         "", "Transform",                                                              "",                          "Align selected objects on X",                                                                              {},                                                                        {},                    &commandAlign<AlignAxis::X>,            &hasMultiSelection,  true, commandmenu::kViewportAlign,  200},
                {              "transform.alignY",              "Align Y (Center)",                         "", "Transform",                                                              "",                          "Align selected objects on Y",                                                                              {},                                                                        {},                    &commandAlign<AlignAxis::Y>,            &hasMultiSelection,  true, commandmenu::kViewportAlign,  210},
                {              "transform.alignZ",              "Align Z (Center)",                         "", "Transform",                                                              "",                          "Align selected objects on Z",                                                                              {},                                                                        {},                    &commandAlign<AlignAxis::Z>,            &hasMultiSelection,  true, commandmenu::kViewportAlign,  220},
                {         "transform.distributeX",           "Distribute X Evenly",                         "", "Transform",                                                              "",                   "Evenly space selected objects on X",                                                                              {},                                                                        {},               &commandDistribute<AlignAxis::X>,            &hasMultiSelection,  true, commandmenu::kViewportAlign,  300},
                {         "transform.distributeY",           "Distribute Y Evenly",                         "", "Transform",                                                              "",                   "Evenly space selected objects on Y",                                                                              {},                                                                        {},               &commandDistribute<AlignAxis::Y>,            &hasMultiSelection,  true, commandmenu::kViewportAlign,  310},
                {         "transform.distributeZ",           "Distribute Z Evenly",                         "", "Transform",                                                              "",                   "Evenly space selected objects on Z",                                                                              {},                                                                        {},               &commandDistribute<AlignAxis::Z>,            &hasMultiSelection,  true, commandmenu::kViewportAlign,  320},

                {         "prefab.applyOverrides",               "Apply Overrides",                         "",    "Prefab",                                                              "", "Write instance overrides back to the prefab template",                                                                              {},                                                                        {},                   &commandApplyPrefabOverrides,    &hasPrefabInstanceSelected,  true,                     nullptr,    0}
            };

            // ------------------------------------------------------------------------------
            // 4) ImGui 연결
            // ------------------------------------------------------------------------------
            static_assert( ImGuiKey_Z - ImGuiKey_A == 25, "ImGuiKey 의 문자 키가 연속이 아닙니다" );
            static_assert( ImGuiKey_F12 - ImGuiKey_F1 == 11, "ImGuiKey 의 F 키가 연속이 아닙니다" );

            /**
             * @brief 자체 키 열거형을 ImGuiKey 로 옮깁니다. 모르는 키는 ImGuiKey_None 입니다.
             * @details 표가 아니라 **뺄셈**으로 옮깁니다. 그래서 양쪽 열거형이 A..Z · F1..F12 구간에서 연속이라는 가정에
             *          기댑니다. ImGui 쪽은 바로 위의 static_assert 가, 우리 쪽은 `EditorCommandKey` 선언 아래의 static_assert 가
             *          이 가정을 지킵니다.
             */
            static ImGuiKey toImGuiKey( EditorCommandKey key )
            {
                const int32 keyValue = static_cast<int32>( key );
                if ( static_cast<int32>( EditorCommandKey::A ) <= keyValue && keyValue <= static_cast<int32>( EditorCommandKey::Z ) )
                    return static_cast<ImGuiKey>( ImGuiKey_A + ( keyValue - static_cast<int32>( EditorCommandKey::A ) ) );
                if ( static_cast<int32>( EditorCommandKey::F1 ) <= keyValue && keyValue <= static_cast<int32>( EditorCommandKey::F12 ) )
                    return static_cast<ImGuiKey>( ImGuiKey_F1 + ( keyValue - static_cast<int32>( EditorCommandKey::F1 ) ) );
                if ( key == EditorCommandKey::Space )
                    return ImGuiKey_Space;
                return ImGuiKey_None;
            }

            /** @brief 이 조합이 이번 프레임에 정확히 눌렸으면 true입니다. 수정자 비교는 `EditorCommandRegistry::matchesPressedModifiers` 입니다. */
            static bool isShortcutPressed( const EditorCommandShortcut& shortcut )
            {
                if ( EditorCommandRegistry::isHandledShortcut( shortcut ) == false )
                    return false;

                const ImGuiKey imKey = toImGuiKey( shortcut._key );
                if ( imKey == ImGuiKey_None )
                    return false;

                const ImGuiIO& io              = ImGui::GetIO();
                uint8          pressedModifier = commandmodifier::kNone;
                if ( io.KeyCtrl )
                    pressedModifier |= commandmodifier::kCtrl;
                if ( io.KeyShift )
                    pressedModifier |= commandmodifier::kShift;
                if ( io.KeyAlt )
                    pressedModifier |= commandmodifier::kAlt;
                if ( EditorCommandRegistry::matchesPressedModifiers( shortcut, pressedModifier, io.KeySuper ) == false )
                    return false;

                return ImGui::IsKeyPressed( imKey, false );
            }

            /** @brief 커맨드 하나를 메뉴 항목으로 그립니다. 눌렸으면 실행합니다. */
            static void drawCommandItem( const EditorCommandDesc& desc )
            {
                fixed_string<constant::kMaxBuffer128> menuLabel;
                EditorCommandRegistry::formatMenuLabel( desc, menuLabel );

                fixed_string<constant::kMaxBuffer64> shortcutLabel;
                EditorCommandRegistry::formatShortcutLabel( desc, shortcutLabel );

                const utf8* pShortcut = shortcutLabel.empty() ? nullptr : shortcutLabel.c_str();
                const bool  bEnabled  = EditorCommandRegistry::isEnabled( desc );

                if ( ImGui::MenuItem( menuLabel.c_str(), pShortcut, false, bEnabled ) )
                    EditorCommandRegistry::executeDesc( desc );

                if ( desc._tooltip.empty() == false )
                {
                    fixed_string<constant::kMaxBuffer256> tooltip;
                    tooltip.append( desc._tooltip.c_str() );
                    if ( shortcutLabel.empty() == false )
                    {
                        tooltip.append( " (" );
                        tooltip.append( shortcutLabel.c_str() );
                        tooltip.append( ")" );
                    }
                    EditorWidgets::drawTooltip( tooltip.c_str() );
                }
            }

            /** @brief 메뉴 하나의 항목을 구분선과 함께 그립니다. */
            static void drawMenu( const EditorCommandRegistry& registry, const EditorMenu& menu )
            {
                const vector<EditorCommandDesc>& listCommand = registry.getCommands();
                for ( const EditorMenuItem& item : menu._listItem )
                {
                    if ( item._bSeparatorBefore )
                        ImGui::Separator();
                    drawCommandItem( listCommand[item._commandIndex] );
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_LOG_CALLER( "Editor" );

    void EditorCommandGui::registerDefaults()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
        {
            // 조용히 돌아가면 메뉴가 통째로 비고 단축키가 먹지 않는다. 실제 기동 검증이 이 줄을 잡는다.
            SW_LOG_ERROR( "커맨드를 등록할 EditorContext 가 없습니다 — 메뉴와 단축키가 비어 있게 됩니다" );
            return;
        }

        EditorCommandRegistry& registry = pContext->getCommandRegistry();
        registry.clear();

        for ( const EditorCommandGuiInternal::CommandRow& row : EditorCommandGuiInternal::_s_arrCommandRow )
        {
            EditorCommandDesc desc{};
            desc._id              = row._pId;
            desc._label           = row._pLabel;
            desc._icon            = row._pIcon;
            desc._category        = row._pCategory;
            desc._tooltip         = row._pTooltip;
            desc._detail          = row._pDetail;
            desc._shortcut        = row._shortcut;
            desc._altShortcut     = row._altShortcut;
            desc._bPaletteVisible = row._bPaletteVisible;
            desc._menuPath        = row._pMenuPath != nullptr ? row._pMenuPath : "";
            desc._menuOrder       = row._menuOrder;
            if ( row._pAction != nullptr )
                desc._action = row._pAction;
            if ( row._pEnabled != nullptr )
                desc._enabledPredicate = row._pEnabled;

            registry.registerCommand( std::move( desc ) );
        }

        string report;
        if ( registry.validate( report ) == false )
            SW_LOG_ERROR( "에디터 커맨드 표가 어긋났습니다:\n%#", report.c_str() );
        // 코드가 그리는 메뉴 경로에 표의 항목이 하나도 없으면 그 자리(툴바 팝업)가 비어 뜬다.
        for ( const utf8* pHostedPath : commandmenu::kArrHostedMenuPath )
        {
            if ( registry.findMenu( pHostedPath ) == nullptr )
                SW_LOG_ERROR( "Menu path '%#' is drawn by code but no command row uses it - the menu is empty", pHostedPath );
        }
    }

    void EditorCommandGui::processHotkeys()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;
        if ( ImGui::GetIO().WantTextInput )
            return;

        const EditorCommandRegistry& registry = pContext->getCommandRegistry();
        for ( const EditorCommandDesc& desc : registry.getCommands() )
        {
            const bool bPressed = ( EditorCommandGuiInternal::isShortcutPressed( desc._shortcut ) ||
                                    EditorCommandGuiInternal::isShortcutPressed( desc._altShortcut ) );
            if ( bPressed == false )
                continue;

            // 조합은 유일하다(registerDefaults 의 validate 가 지킨다). 그래서 처음 맞은 것만 실행한다.
            EditorCommandRegistry::executeDesc( desc );
            return;
        }
    }

    void EditorCommandGui::drawMainMenus()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        const EditorCommandRegistry& registry = pContext->getCommandRegistry();
        for ( const EditorMenu& menu : registry.getMenus() )
        {
            if ( menu._parentPath != commandmenu::kMainMenuBar )
                continue;
            if ( ImGui::BeginMenu( menu._name.c_str() ) )
            {
                EditorCommandGuiInternal::drawMenu( registry, menu );
                ImGui::EndMenu();
            }
        }
    }

    void EditorCommandGui::drawMenuItems( string_view menuPath )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        const EditorCommandRegistry& registry = pContext->getCommandRegistry();
        const EditorMenu*            pMenu    = registry.findMenu( menuPath );
        if ( pMenu != nullptr )
            EditorCommandGuiInternal::drawMenu( registry, *pMenu );
    }
} // namespace sw::editor
