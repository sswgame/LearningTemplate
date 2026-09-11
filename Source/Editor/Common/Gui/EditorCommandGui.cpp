/**
 * @file EditorCommandGui.cpp
 * @brief 기본 커맨드 표와 그 ImGui 표면 (메뉴 항목 · 전역 단축키)
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
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/SelectionManager.h"
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
            // 1) 커맨드 동작 — 표에서 함수 포인터로 가리킨다
            //
            // **여기 있는 함수는 전부 "표가 요구하는 모양으로 바꾸는 일" 을 한다.** 대상이 이미
            // `void()` / `bool()` 이라면 표가 그 함수를 **직접** 가리키면 되고, 감싸는 것은 이름만
            // 하나 늘리는 것이다(그렇게 감싸고만 있던 여덟 개는 걷어냈다 — `saveFocusedOrScene` ·
            // `requestExit` · `QuickLauncherPopup::toggle` 등은 지금 표가 직접 가리킨다).
            //
            // 그러니 여기 새로 함수를 만들기 전에 **왜 직접 못 가리키는지**가 있어야 한다. 남아 있는
            // 것들의 이유는 셋 중 하나다:
            //   - 반환형을 맞춘다   : `commandNewScene` (대상이 `bool` 인데 표는 `void()` 다)
            //   - 인자를 박는다     : `commandAlign<TAxis>` (대상이 축·정렬 두 인자를 받는다)
            //   - 대상을 찾아온다   : `commandUndo` (`getService<CommandStack>()`)
            //
            // 그리고 인자를 박는 경우에도 **값마다 함수를 하나씩 적지는 않는다** — 축 여섯 벌이
            // 그렇게 적혀 있었고, 템플릿 인자로 받아 둘로 줄였다.
            // ------------------------------------------------------------------------------
            static void onOpenSceneDialogResult( const vector<string>& listPath )
            {
                if ( listPath.empty() == false )
                    EditorContext::get()->getWorkspace().requestLoadScene( listPath[0] );
            }

            /// @brief 표는 `void()` 를 요구하고 `tryCreateNewScene` 은 `bool` 을 돌려준다 — 그 차이를 메우려고 남긴다.
            static void commandNewScene()
            {
                // 실패 사유(플레이 중 · SceneManager 없음)는 tryCreateNewScene 이 자체 처리한다.
                EditorAssetCommands::tryCreateNewScene();
            }

            static void commandOpenScene()
            {
                FileDialogParams params{};
                params._type                = FileDialogParams::Type::Open;
                params._title               = "Open Scene";
                params._description         = "Scene";
                params._bEnableMultiselect  = false;
                params._listFilterExtension = { ".scene.xml", ".xml" };

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
                getService<CommandStack>()->undo();
            }

            static void commandRedo()
            {
                getService<CommandStack>()->redo();
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
                return pContext->getSelectionManager().getPrimaryObject().get();
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
                const string fileName = FileUtil::removeExtension( FileUtil::getFileNamePart( listPath[0] ) );
                if ( fileName.empty() )
                    return;
                if ( EditorTransformCommands::saveComponentPreset( pComp, fileName ) == false )
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

            // 축만 다른 여섯 벌을 손으로 적고 있었다. 표가 `void()` 를 요구하므로 인자를 박는
            // 함수 자체는 있어야 하지만, **축마다 하나씩 적을 이유는 없다** — 축을 템플릿 인자로
            // 받으면 표는 그대로 `&commandAlign<AlignAxis::X>` 로 가리킨다. 축이 하나 늘어도
            // 여기 고칠 것은 없다.
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

            static void commandApplyPrefabOverrides()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return;
                GameObject* pObj = pContext->getSelectionManager().getPrimaryObject().get();
                string      path = pContext->getWorkspace().getFocusedAssetPath();
                if ( path.empty() && pObj != nullptr )
                    path = pContext->getWorkspace().getGameObjectPrefabPath( pObj->getObjectId() );
                EditorToolAssetCommands::applyPrefabOverridesToTemplate( pObj, path );
            }

            // ------------------------------------------------------------------------------
            // 2) 활성 조건 — 표에서 함수 포인터로 가리킨다
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
                return pContext->getSelectionManager().getSelectedObjectCount();
            }

            static bool hasSelection()
            {
                return selectedObjectCount() >= 1;
            }

            static bool hasMultiSelection()
            {
                return selectedObjectCount() >= 2;
            }

            // ------------------------------------------------------------------------------
            // 3) 기본 커맨드 표 — 커맨드를 더하려면 여기 한 줄
            // ------------------------------------------------------------------------------
            /** @brief 표 한 줄. 문자열은 전부 리터럴이므로 수명 걱정이 없다. */
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
                bool _bPaletteVisible;
            };

            /**
             * @brief 에디터 커맨드 정본.
             * @details 툴팁에 단축키를 손으로 적지 않는다 — `drawMenuItem` 이 이 표의 조합으로 만들어
             *          붙이므로 라벨과 실제 처리가 어긋날 수 없다.
             */
            inline static const CommandRow _s_arrCommandRow[] = {
                {                     "scene.new",                     "New Scene",               ICON_FA_FILE,     "Scene",                                     "새로운 빈 씬을 생성합니다",           "Replace the active scene with an empty one",                                                                    {},                                                              {},                               &commandNewScene,                       nullptr,  true},
                {                    "scene.open",                 "Open Scene...",        ICON_FA_FOLDER_OPEN,     "Scene",                  "디스크에서 기존 씬 파일(.scene.xml)을 엽니다",                          "Open a .scene.xml from disk",                            { EditorCommandKey::O, commandmod::kCtrl },                                                              {},                              &commandOpenScene,                       nullptr,  true},
                {                    "asset.save",                          "Save",        ICON_FA_FLOPPY_DISK,     "Scene",                  "현재 포커스된 에셋 또는 활성 씬을 저장합니다",          "Save the focused asset, or the active scene",                            { EditorCommandKey::S, commandmod::kCtrl },                                                              {},       &EditorAssetCommands::saveFocusedOrScene,                       nullptr,  true},
                {               "scene.saveScene",                    "Save Scene",        ICON_FA_FLOPPY_DISK,     "Scene",                        "현재 활성화된 씬을 디스크에 저장합니다", "Write the active scene, or prompt Save As if unsaved",                                                                    {},                                                              {},  &EditorAssetCommands::saveActiveSceneOrPrompt,                       nullptr,  true},
                {              "editor.quickOpen",                 "Quick Open...",   ICON_FA_MAGNIFYING_GLASS,    "Editor",                   "에셋, 씬, 스크립트를 빠르게 검색하여 엽니다",              "Fuzzy-search assets, scenes and scripts",                            { EditorCommandKey::P, commandmod::kCtrl },                                                              {},                    &QuickLauncherPopup::toggle,                       nullptr, false},
                {         "editor.commandPalette",            "Command Palette...",           ICON_FA_TERMINAL,    "Editor",                     "에디터 명령 및 액션을 검색하여 실행합니다",                       "Search and run editor commands",       { EditorCommandKey::P, commandmod::kCtrl | commandmod::kShift },                  { EditorCommandKey::Space, commandmod::kCtrl },                   &CommandPalettePopup::toggle,                       nullptr, false},
                {                   "editor.exit",                          "Exit", ICON_FA_RIGHT_FROM_BRACKET,      "File",                                           "에디터를 종료합니다",   "Close the editor after unsaved-change confirmation", { EditorCommandKey::F4, commandmod::kAlt | commandmod::kDisplayOnly },                                                              {},              &EditorAssetCommands::requestExit,                       nullptr,  true},

                {                     "edit.undo",                          "Undo",        ICON_FA_ROTATE_LEFT,      "Edit",                                 "마지막 편집 작업을 되돌립니다",                                   "Undo the last edit",                            { EditorCommandKey::Z, commandmod::kCtrl },                                                              {},                                   &commandUndo, &EditorPlaySession::isStopped,  true},
                {                     "edit.redo",                          "Redo",       ICON_FA_ROTATE_RIGHT,      "Edit",                            "되돌린 편집 작업을 다시 실행합니다",                            "Redo the last undone edit",                            { EditorCommandKey::Y, commandmod::kCtrl }, { EditorCommandKey::Z, commandmod::kCtrl | commandmod::kShift },                                   &commandRedo, &EditorPlaySession::isStopped,  true},
                {          "editor.themeSettings",      "Theme & Look and Feel...",            ICON_FA_PALETTE,    "Editor", "에디터 테마 프리셋, 액센트 색상 및 모서리 라운딩을 설정합니다",                       "Open the theme settings dialog",                                                                    {},                                                              {},                &EditorMenuBar::openThemeDialog,                       nullptr,  true},

                {             "build.compileGame",         "Compile Game (SWGame)",             ICON_FA_HAMMER,     "Build",       "게임 모듈(SWGame)을 라이브 코딩으로 즉시 재컴파일합니다",                      "Recompile and hot-reload SWGame",       { EditorCommandKey::F11, commandmod::kCtrl | commandmod::kAlt },                     { EditorCommandKey::F7, commandmod::kNone },                            &commandCompileGame,               &isCompilerIdle,  true},
                {           "build.compileEditor", "Compile Editor (EditorModule)",             ICON_FA_WRENCH,     "Build",    "에디터 모듈(EditorModule)을 라이브 코딩으로 재컴파일합니다",                               "Recompile EditorModule",                                                                    {},                                                              {},                          &commandCompileEditor,               &isCompilerIdle,  true},
                {              "build.compileAll",           "Compile All Modules",      ICON_FA_BOXES_STACKED,     "Build",               "엔진 및 모든 게임/에디터 모듈을 전체 빌드합니다",                    "Build the engine and every module",       { EditorCommandKey::B, commandmod::kCtrl | commandmod::kShift },                                                              {},                             &commandCompileAll,               &isCompilerIdle,  true},
                {                  "build.cancel",                  "Cancel Build",                ICON_FA_BAN,     "Build",                       "현재 진행 중인 컴파일 작업을 취소합니다",                           "Cancel the running compile",                                                                    {},                                                              {},                            &commandCancelBuild,               &isCompilerBusy,  true},

                {                    "play.start",                          "Play",                         "",      "Play",                                                              "",                                 "Start play-in-editor",                                                                    {},                                                              {},                                   &commandPlay,                       nullptr,  true},

                {"clipboard.pasteComponentValues",        "Paste Component Values",                         "", "Clipboard",                                                              "",  "Overwrite the selected component from the clipboard",                                                                    {},                                                              {},                   &commandPasteComponentValues,                       nullptr,  true},
                { "clipboard.pasteComponentAsNew",        "Paste Component As New",                         "", "Clipboard",                                                              "",      "Add the copied component to the selected object",                                                                    {},                                                              {},                    &commandPasteComponentAsNew,                       nullptr,  true},
                {          "preset.loadComponent",         "Load Component Preset",                         "",    "Preset",                                                              "",        "Apply a .preset.xml to the selected component",                                                                    {},                                                              {},                    &commandLoadComponentPreset,                       nullptr,  true},
                {          "preset.saveComponent",         "Save Component Preset",                         "",    "Preset",                                                              "",        "Write the selected component to a preset file",                                                                    {},                                                              {},                    &commandSaveComponentPreset,                       nullptr,  true},

                {        "transform.snapToGround",          "Snap to Ground (Y=0)",                         "", "Transform",                                                              "",          "Snap selected objects onto the ground plane",                                                                    {},                                                              {}, &EditorTransformCommands::snapSelectedToGround,                 &hasSelection,  true},
                {              "transform.alignX",              "Align X (Center)",                         "", "Transform",                                                              "",                          "Align selected objects on X",                                                                    {},                                                              {},                    &commandAlign<AlignAxis::X>,            &hasMultiSelection,  true},
                {              "transform.alignY",              "Align Y (Center)",                         "", "Transform",                                                              "",                          "Align selected objects on Y",                                                                    {},                                                              {},                    &commandAlign<AlignAxis::Y>,            &hasMultiSelection,  true},
                {              "transform.alignZ",              "Align Z (Center)",                         "", "Transform",                                                              "",                          "Align selected objects on Z",                                                                    {},                                                              {},                    &commandAlign<AlignAxis::Z>,            &hasMultiSelection,  true},
                {         "transform.distributeX",           "Distribute X Evenly",                         "", "Transform",                                                              "",                   "Evenly space selected objects on X",                                                                    {},                                                              {},               &commandDistribute<AlignAxis::X>,            &hasMultiSelection,  true},
                {         "transform.distributeY",           "Distribute Y Evenly",                         "", "Transform",                                                              "",                   "Evenly space selected objects on Y",                                                                    {},                                                              {},               &commandDistribute<AlignAxis::Y>,            &hasMultiSelection,  true},
                {         "transform.distributeZ",           "Distribute Z Evenly",                         "", "Transform",                                                              "",                   "Evenly space selected objects on Z",                                                                    {},                                                              {},               &commandDistribute<AlignAxis::Z>,            &hasMultiSelection,  true},

                {         "prefab.applyOverrides",               "Apply Overrides",                         "",    "Prefab",                                                              "", "Write instance overrides back to the prefab template",                                                                    {},                                                              {},                   &commandApplyPrefabOverrides,                       nullptr,  true}
            };

            // ------------------------------------------------------------------------------
            // 4) ImGui 연결
            // ------------------------------------------------------------------------------
            static_assert( ImGuiKey_Z - ImGuiKey_A == 25, "ImGuiKey 의 문자 키가 연속이 아닙니다" );
            static_assert( ImGuiKey_F12 - ImGuiKey_F1 == 11, "ImGuiKey 의 F 키가 연속이 아닙니다" );

            /** @brief 자체 키 열거형을 ImGuiKey 로 옮깁니다. 모르는 키는 ImGuiKey_None 입니다. */
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

            /**
             * @brief 이 조합이 이번 프레임에 정확히 눌렸으면 true입니다.
             * @details 수정자를 **정확히** 비교합니다. 예전 키 사다리는 필요한 수정자만 확인해서
             *          Ctrl+Shift+Z 가 undo(Ctrl+Z)까지 함께 발동했다 — Shift 가 눌려 있지 않다는
             *          것을 아무도 확인하지 않았기 때문이다.
             */
            static bool isShortcutPressed( const EditorCommandShortcut& shortcut )
            {
                if ( EditorCommandRegistry::isHandledShortcut( shortcut ) == false )
                    return false;

                const ImGuiKey imKey = toImGuiKey( shortcut._key );
                if ( imKey == ImGuiKey_None )
                    return false;

                const ImGuiIO& io = ImGui::GetIO();
                // macOS 의 Cmd 는 예전 코드와 같이 Ctrl 로 취급한다.
                const bool bCtrlDown  = ( io.KeyCtrl || io.KeySuper );
                const bool bWantCtrl  = ( ( shortcut._modifier & commandmod::kCtrl ) != 0 );
                const bool bWantShift = ( ( shortcut._modifier & commandmod::kShift ) != 0 );
                const bool bWantAlt   = ( ( shortcut._modifier & commandmod::kAlt ) != 0 );

                if ( bCtrlDown != bWantCtrl || io.KeyShift != bWantShift || io.KeyAlt != bWantAlt )
                    return false;

                return ImGui::IsKeyPressed( imKey, false );
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
            // 조용히 돌아가면 메뉴가 통째로 비고 단축키가 죽는다. 실기동 검증이 이 줄을 잡는다.
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
            if ( row._pAction != nullptr )
                desc._action = row._pAction;
            if ( row._pEnabled != nullptr )
                desc._enabledPredicate = row._pEnabled;

            registry.registerCommand( std::move( desc ) );
        }

        string report;
        if ( registry.validate( report ) == false )
            SW_LOG_ERROR( "에디터 커맨드 표가 어긋났습니다:\n%#", report.c_str() );
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

            // 조합은 유일하다 (registerDefaults 의 validate 가 지킨다) — 처음 맞은 것만 실행한다.
            EditorCommandRegistry::executeDesc( desc );
            return;
        }
    }

    bool EditorCommandGui::drawMenuItem( string_view commandId )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return false;

        const EditorCommandDesc* pDesc = pContext->getCommandRegistry().find( commandId );
        if ( pDesc == nullptr )
        {
            SW_LOG_WARNING( "메뉴에 없는 커맨드 id 입니다: %#", string{ commandId }.c_str() );
            return false;
        }

        fixed_string<constant::kMaxBuffer128> menuLabel;
        EditorCommandRegistry::formatMenuLabel( *pDesc, menuLabel );

        fixed_string<constant::kMaxBuffer64> shortcutLabel;
        EditorCommandRegistry::formatShortcutLabel( *pDesc, shortcutLabel );

        const utf8* pShortcut = shortcutLabel.empty() ? nullptr : shortcutLabel.c_str();
        const bool  bEnabled  = EditorCommandRegistry::isEnabled( *pDesc );

        const bool bActivated = ImGui::MenuItem( menuLabel.c_str(), pShortcut, false, bEnabled );
        if ( bActivated )
            EditorCommandRegistry::executeDesc( *pDesc );

        if ( pDesc->_tooltip.empty() == false )
        {
            fixed_string<constant::kMaxBuffer256> tooltip;
            tooltip.append( pDesc->_tooltip.c_str() );
            if ( shortcutLabel.empty() == false )
            {
                tooltip.append( " (" );
                tooltip.append( shortcutLabel.c_str() );
                tooltip.append( ")" );
            }
            EditorWidgets::drawTooltip( tooltip.c_str() );
        }

        return bActivated;
    }
} // namespace sw::editor
