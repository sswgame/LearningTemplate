/**
 * @file EditorCommandGUI.cpp
 * @brief 기본 커맨드 테이블을 정의하고, 이를 ImGui 의 메뉴 항목과 전역 단축키로 드러냅니다.
 */
#include "pch.h"

#include "Editor/Common/GUI/EditorCommandGUI.h"

#include "Core/File/FileUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/Dev/EditorGlobalVariableCommands.h"
#include "Editor/Common/Commands/Dev/EditorShortcutOverrides.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Play/EditorScreenshotCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneFileCommands.h"
#include "Editor/Common/Commands/Scene/EditorTransformCommands.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorMenuBar.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Popups/CommandPalettePopup.h"
#include "Editor/Popups/QuickLauncherPopup.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Renderer/Capture/RenderDocCapture.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Utility/CommandStack.h"

#include "RuntimeAPI/Service/IModuleCompiler.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorCommandGUIInternal
        {
            /**
             * @brief 커맨드 테이블과 등록 항목으로 레지스트리를 다시 만듭니다. 등록 항목이 [@p pExcludeBegin, @p pExcludeEnd) 안에 있으면 제외하고, 제외한 수를 반환합니다.
             * @details 정의는 아래 EditorCommandGUI 구현 옆에 있다. 테이블 `_s_arrCommandRow` 가 이 구조체의 끝에 있어 그 뒤에 둔다.
             */
            static uint32 rebuild( const void* pExcludeBegin, const void* pExcludeEnd );

            /** @brief 마지막으로 맞춘 커맨드 등록 세대입니다. EditorModule 을 다시 로드하면 함께 처음 값으로 돌아갑니다. */
            static uint32& getSyncedGeneration()
            {
                static uint32 s_syncedGeneration = invalid_index::kUint32;
                return s_syncedGeneration;
            }

            // ------------------------------------------------------------------------------
            // 1) 커맨드 동작. 테이블이 함수 포인터로 가리킨다
            //
            // 여기 있는 함수는 모두 테이블이 요구하는 시그니처에 맞추는 일을 한다. 대상이 이미 `void()` 나 `bool()` 이라면 테이블이
            // 그 함수를 직접 가리키면 되고, 감싸는 것은 이름만 하나 늘리는 일이다. `saveFocusedOrScene`, `requestExit`,
            // `QuickLauncherPopup::toggle` 등은 테이블이 직접 가리킨다.
            //
            // 그러니 여기에 새 함수를 만들기 전에 직접 가리킬 수 없는 이유가 있어야 한다. 남아 있는 함수의 이유는 셋 중
            // 하나다.
            //   - 반환형을 맞춘다: `commandNewScene` (대상은 `bool` 을 반환하는데 테이블은 `void()` 를 요구한다)
            //   - 인자를 고정한다: `commandAlign<TAxis>` (대상이 축과 정렬 두 인자를 받는다)
            //   - 대상을 찾아온다: `commandUndo` (`findSubsystem<CommandStack>()`)
            //
            // 인자를 고정하는 경우에도 값마다 함수를 하나씩 만들지 않는다. 축처럼 값만 다른 것은 템플릿 인자로 받는다.
            // ------------------------------------------------------------------------------
            static void onOpenSceneDialogResult( const vector<string>& listPath )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return;

                if ( listPath.empty() == false )
                    pContext->getSubsystem<EditorWorkspace>().requestLoadScene( listPath[0] );
            }

            /// @brief 테이블은 `void()` 를 요구하는데 `tryCreateNewScene` 은 `bool` 을 반환합니다. 이 차이를 맞추려고 둡니다.
            static void commandNewScene()
            {
                // 실패 사유(플레이 중, SceneManager 없음)는 tryCreateNewScene 이 직접 처리한다.
                (void)EditorSceneFileCommands::tryCreateNewScene();
            }

            static void commandOpenScene()
            {
                FileDialogParams params{};
                params._type               = FileDialogParams::Type::Open;
                params._title              = "Open Scene";
                params._description        = "Scene";
                params._bEnableMultiselect = false;
                // 씬 이름은 쿠커의 규칙 하나를 따른다(`EditorAssetTypeRegistry` → `AssetCookPath`). 맨 `.xml` 은 쿠커가 쿠킹하지 않는 이름이다.
                EditorAssetTypeRegistry::appendSuffixes( EditorAssetType::Scene, params._listFilterExtension );

                const string activePack = GameConfig::getActive()._packRoot;
                const string mapsDir    = ResourcePaths::getDomainFolderPath( activePack, path::kMapsFolder );
                if ( FileUtil::isDirectory( mapsDir ) )
                    params._initialDirectory = mapsDir;
                else if ( ResourcePaths::getDomainFolderPath( activePack ).empty() == false )
                    params._initialDirectory = ResourcePaths::getDomainFolderPath( activePack );
                else if ( ResourcePaths::getDomainFolderPath( path::kGamePack ).empty() == false )
                    params._initialDirectory = ResourcePaths::getDomainFolderPath( path::kGamePack );

                FileUtil::openFileDialog( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, onOpenSceneDialogResult ) );
            }

            // 포커스된 툴 창(미리보기 씬)이 자기 스택을 걸었으면 그것을 — 미리보기 창의 Undo 가 메인 씬 기록을 건드리지 않는다.
            static void commandUndo()
            {
                CommandStack* pStack = getFocusedCommandStack();
                if ( pStack != nullptr )
                    pStack->undo();
            }

            static void commandRedo()
            {
                CommandStack* pStack = getFocusedCommandStack();
                if ( pStack != nullptr )
                    pStack->redo();
            }

            static void commandCompileGame()
            {
                IModuleCompiler* pCompiler = findSubsystem<IModuleCompiler>();
                if ( pCompiler != nullptr )
                    pCompiler->compileModule( "SWGame" );
            }

            static void commandCompileEditor()
            {
                IModuleCompiler* pCompiler = findSubsystem<IModuleCompiler>();
                if ( pCompiler != nullptr )
                    pCompiler->compileModule( "EditorAll" ); // EditorModule 과 확장 모듈을 함께 컴파일한다(sw_addEditorExtension 이 의존성을 추가한다)
            }

            static void commandCompileAll()
            {
                IModuleCompiler* pCompiler = findSubsystem<IModuleCompiler>();
                if ( pCompiler != nullptr )
                    pCompiler->compileAll();
            }

            static void commandCancelBuild()
            {
                IModuleCompiler* pCompiler = findSubsystem<IModuleCompiler>();
                if ( pCompiler != nullptr )
                    pCompiler->cancel();
            }

            static void commandPlay()
            {
                EditorContext* pContext               = EditorContext::get();
                const bool     bWarnUnsavedBeforePlay = pContext != nullptr && pContext->getSubsystem<EditorWorkspace>().isSceneDirty() && EditorPlaySession::isStopped();
                if ( bWarnUnsavedBeforePlay )
                {
                    pContext->getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyPlayTitle" ), SW_LOCKEY( "EdCommon", "NotifyPlayUnsaved" ),
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
                return pContext->getSubsystem<EditorSelection>().getPrimaryObject();
            }

            static Component* selectedComponent()
            {
                EditorContext* pContext = EditorContext::get();
                GameObject*    pObj     = primaryObject();
                if ( pContext == nullptr || pObj == nullptr )
                    return nullptr;
                const uint64 componentID = pContext->getSubsystem<EditorWorkspace>().getSelectedComponentID();
                if ( componentID == 0 )
                    return nullptr;
                return pObj->findComponentByID( componentID );
            }

            static void warn( const utf8* pTitle, const utf8* pDetail )
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext != nullptr )
                    pContext->getSubsystem<EditorNotificationManager>().push( pTitle, pDetail, NotificationType::Warning );
            }

            static void commandPasteComponentValues()
            {
                EditorContext* pContext = EditorContext::get();
                Component*     pComp    = selectedComponent();
                if ( pContext == nullptr || pComp == nullptr )
                {
                    warn( SW_LOCKEY( "EdCommon", "NotifyPasteTitle" ), SW_LOCKEY( "EdCommon", "NotifySelectComponent" ) );
                    return;
                }
                if ( pContext->getSubsystem<EditorWorkspace>().hasCopiedComponent() == false )
                {
                    warn( SW_LOCKEY( "EdCommon", "NotifyPasteTitle" ), SW_LOCKEY( "EdCommon", "NotifyClipboardEmpty" ) );
                    return;
                }
                (void)EditorTransformCommands::pasteCopiedComponentValues( pContext->getSubsystem<EditorWorkspace>(), pComp ); // 실패는 붙여넣기 함수가 로그로 알린다
            }

            static void commandPasteComponentAsNew()
            {
                EditorContext* pContext = EditorContext::get();
                GameObject*    pObj     = primaryObject();
                if ( pContext == nullptr || pObj == nullptr )
                {
                    warn( SW_LOCKEY( "EdCommon", "NotifyPasteTitle" ), SW_LOCKEY( "EdCommon", "NotifySelectObject" ) );
                    return;
                }
                if ( pContext->getSubsystem<EditorWorkspace>().hasCopiedComponent() == false )
                {
                    warn( SW_LOCKEY( "EdCommon", "NotifyPasteTitle" ), SW_LOCKEY( "EdCommon", "NotifyClipboardEmpty" ) );
                    return;
                }
                (void)EditorTransformCommands::pasteCopiedComponentAsNew( pContext->getSubsystem<EditorWorkspace>(), pObj ); // 실패는 붙여넣기 함수가 로그로 알린다
            }

            static void onLoadPresetDialogResult( const vector<string>& listPath )
            {
                if ( listPath.empty() )
                    return;
                Component* pComp = selectedComponent();
                if ( pComp == nullptr )
                {
                    warn( SW_LOCKEY( "EdCommon", "NotifyPresetTitle" ), SW_LOCKEY( "EdCommon", "NotifySelectComponent" ) );
                    return;
                }
                // 반환값을 버리면 프리셋을 골랐는데 아무 일도 일어나지 않는다. 파일이 없거나
                // XML 이 그 컴포넌트 타입과 맞지 않으면 실패한다.
                if ( EditorTransformCommands::loadComponentPreset( pComp, listPath[0] ) == false )
                    warn( SW_LOCKEY( "EdCommon", "NotifyPresetTitle" ), SW_LOCKEY( "EdCommon", "NotifyPresetApplyFailed" ) );
            }

            static void onSavePresetDialogResult( const vector<string>& listPath )
            {
                if ( listPath.empty() )
                    return;
                Component* pComp = selectedComponent();
                if ( pComp == nullptr )
                {
                    warn( SW_LOCKEY( "EdCommon", "NotifyPresetTitle" ), SW_LOCKEY( "EdCommon", "NotifySelectComponent" ) );
                    return;
                }
                if ( EditorTransformCommands::saveComponentPresetTo( pComp, listPath[0] ) == false )
                    warn( SW_LOCKEY( "EdCommon", "NotifyPresetTitle" ), SW_LOCKEY( "EdCommon", "NotifyPresetWriteFailed" ) );
            }

            static void commandLoadComponentPreset()
            {
                if ( selectedComponent() == nullptr )
                {
                    warn( SW_LOCKEY( "EdCommon", "NotifyPresetTitle" ), SW_LOCKEY( "EdCommon", "NotifySelectComponent" ) );
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
                    warn( SW_LOCKEY( "EdCommon", "NotifyPresetTitle" ), SW_LOCKEY( "EdCommon", "NotifySelectComponent" ) );
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

            // 테이블이 `void()` 를 요구하므로 인자를 고정하는 함수는 필요하지만, 축마다 함수를 만들 이유는 없다. 축을 템플릿 인자로 받으면 테이블은 `&commandAlign<AlignAxis::X>` 로 가리킨다.
            // 축이 늘어나도 여기서 고칠 것은 없다.
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

            /** @brief 선택한 오브젝트를 스폰한 프리팹의 경로입니다. 프리팹 인스턴스가 아니면 비어 있습니다. */
            static string findSelectedPrefabPath( GameObject*& pOutObj )
            {
                EditorContext* pContext = EditorContext::get();
                pOutObj                 = ( pContext != nullptr ) ? pContext->getSubsystem<EditorSelection>().getPrimaryObject() : nullptr;
                if ( pOutObj == nullptr )
                    return {};
                return string{ pContext->getSubsystem<EditorWorkspace>().getGameObjectPrefabPath( pOutObj->getObjectID() ) };
            }

            static void commandApplyPrefabOverrides()
            {
                // 선택한 인스턴스의 프리팹에만 쓴다. 포커스된 에셋 경로(콘텐츠 브라우저에서 마지막에 클릭한 씬, 머티리얼, 텍스처)를
                // 쓰면 그 파일을 프리팹으로 덮어쓰므로 주의한다.
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
            // 2) 활성 조건. 테이블이 함수 포인터로 가리킨다
            // ------------------------------------------------------------------------------
            /** @brief RenderDoc 으로 다음 프레임을 캡처합니다(`viewport.renderDocCapture`). */
            static void commandRenderDocCapture() { RenderDocCapture::triggerCapture(); }

            /** @brief RenderDoc 이 이 프로세스에 붙어 있으면 true 입니다(`-renderdoc` 또는 RenderDoc 에서 실행). */
            static bool isRenderDocAvailable() { return RenderDocCapture::isAvailable(); }

            static bool isCompilerIdle()
            {
                IModuleCompiler* pCompiler = findSubsystem<IModuleCompiler>();
                return pCompiler != nullptr && pCompiler->isCompiling() == false;
            }

            static bool isCompilerBusy()
            {
                IModuleCompiler* pCompiler = findSubsystem<IModuleCompiler>();
                return pCompiler != nullptr && pCompiler->isCompiling();
            }

            static size_t selectedObjectCount()
            {
                EditorContext* pContext = EditorContext::get();
                if ( pContext == nullptr )
                    return 0;
                return pContext->getSubsystem<EditorSelection>().getSelectedObjectCount();
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
            // 3) 기본 커맨드 테이블. 커맨드를 추가하려면 여기에 한 줄을 더한다
            // ------------------------------------------------------------------------------
            /** @brief 테이블의 항목 하나입니다. 문자열은 모두 리터럴이므로 수명을 신경 쓰지 않아도 됩니다. */
            struct CommandRow
            {
                const utf8*           _pID;
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
                const utf8* _pMenuPath; ///< nullptr 이면 어느 메뉴에도 나오지 않는다(`EditorCommandDesc::_menuPath`)
                int32       _menuOrder; ///< 메뉴 안 순서. 백의 자리가 바뀌면 구분선이 들어가고, 메뉴끼리의 순서도 이 값으로 정한다
            };

            /**
             * @brief 에디터 커맨드의 원본 테이블입니다.
             * @details 툴팁에 단축키를 손으로 적지 않는다. `drawCommandItem` 이 이 테이블의 조합으로 만들어 붙이므로 라벨과 실제 처리가
             *          어긋나지 않는다.
             */
            inline static const CommandRow _s_arrCommandRow[] = {
                {                     "scene.new",                     "New Scene",       editoricon::kFile,     "Scene",                                     "새로운 빈 씬을 생성합니다",           "Replace the active scene with an empty one",                                                                              {},                                                                        {},                                  &commandNewScene,                       nullptr,  true,             "MainMenu/File", 1100},
                {                    "scene.open",                 "Open Scene...", editoricon::kFolderOpen,     "Scene",                  "디스크에서 기존 씬 파일(.scene.xml)을 엽니다",                          "Open a .scene.xml from disk",                                 { EditorCommandKey::O, commandmodifier::kCtrl },                                                                        {},                                 &commandOpenScene,                       nullptr,  true,             "MainMenu/File", 1110},
                {                    "asset.save",                          "Save",       editoricon::kSave,     "Scene",                  "현재 포커스된 에셋 또는 활성 씬을 저장합니다",          "Save the focused asset, or the active scene",                                 { EditorCommandKey::S, commandmodifier::kCtrl },                                                                        {},      &EditorSceneFileCommands::saveFocusedOrScene,                       nullptr,  true,             "MainMenu/File", 1120},
                {               "scene.saveScene",                    "Save Scene",       editoricon::kSave,     "Scene",                        "현재 활성화된 씬을 디스크에 저장합니다", "Write the active scene, or prompt Save As if unsaved",                                                                              {},                                                                        {}, &EditorSceneFileCommands::saveActiveSceneOrPrompt,                       nullptr,  true,             "MainMenu/File", 1130},
                {              "editor.quickOpen",                 "Quick Open...",     editoricon::kSearch,    "Editor",                   "에셋, 씬, 스크립트를 빠르게 검색하여 엽니다",              "Fuzzy-search assets, scenes and scripts",                                 { EditorCommandKey::P, commandmodifier::kCtrl },                                                                        {},                       &QuickLauncherPopup::toggle,                       nullptr, false,             "MainMenu/File", 1200},
                {         "editor.commandPalette",            "Command Palette...",   editoricon::kTerminal,    "Editor",                     "에디터 명령 및 액션을 검색하여 실행합니다",                       "Search and run editor commands",       { EditorCommandKey::P, commandmodifier::kCtrl | commandmodifier::kShift },                       { EditorCommandKey::Space, commandmodifier::kCtrl },                      &CommandPalettePopup::toggle,                       nullptr, false,             "MainMenu/File", 1210},
                {                   "editor.exit",                          "Exit",       editoricon::kExit,      "File",                                           "에디터를 종료합니다",   "Close the editor after unsaved-change confirmation", { EditorCommandKey::F4, commandmodifier::kAlt | commandmodifier::kDisplayOnly },                                                                        {},             &EditorSceneFileCommands::requestExit,                       nullptr,  true,             "MainMenu/File", 1300},

                {                     "edit.undo",                          "Undo",       editoricon::kUndo,      "Edit",                                 "마지막 편집 작업을 되돌립니다",                                   "Undo the last edit",                                 { EditorCommandKey::Z, commandmodifier::kCtrl },                                                                        {},                                      &commandUndo, &EditorPlaySession::isStopped,  true,             "MainMenu/Edit", 2100},
                {                     "edit.redo",                          "Redo",       editoricon::kRedo,      "Edit",                            "되돌린 편집 작업을 다시 실행합니다",                            "Redo the last undone edit",                                 { EditorCommandKey::Y, commandmodifier::kCtrl }, { EditorCommandKey::Z, commandmodifier::kCtrl | commandmodifier::kShift },                                      &commandRedo, &EditorPlaySession::isStopped,  true,             "MainMenu/Edit", 2110},
                {          "editor.themeSettings",      "Theme & Look and Feel...",    editoricon::kPalette,    "Editor", "에디터 테마 프리셋, 액센트 색상 및 모서리 라운딩을 설정합니다",                       "Open the theme settings dialog",                                                                              {},                                                                        {},                   &EditorMenuBar::openThemeDialog,                       nullptr,  true,             "MainMenu/Edit", 2200},

                {             "build.compileGame",         "Compile Game (SWGame)",     editoricon::kHammer,     "Build",       "게임 모듈(SWGame)을 라이브 코딩으로 즉시 재컴파일합니다",                      "Recompile and hot-reload SWGame",       { EditorCommandKey::F11, commandmodifier::kCtrl | commandmodifier::kAlt },                          { EditorCommandKey::F7, commandmodifier::kNone },                               &commandCompileGame,               &isCompilerIdle,  true,            "MainMenu/Build", 3100},
                {           "build.compileEditor", "Compile Editor (EditorModule)",     editoricon::kWrench,     "Build",    "에디터 모듈(EditorModule)을 라이브 코딩으로 재컴파일합니다",                               "Recompile EditorModule",                                                                              {},                                                                        {},                             &commandCompileEditor,               &isCompilerIdle,  true,            "MainMenu/Build", 3110},
                {              "build.compileAll",           "Compile All Modules",   editoricon::kBuildAll,     "Build",               "엔진 및 모든 게임/에디터 모듈을 전체 빌드합니다",                    "Build the engine and every module",       { EditorCommandKey::B, commandmodifier::kCtrl | commandmodifier::kShift },                                                                        {},                                &commandCompileAll,               &isCompilerIdle,  true,            "MainMenu/Build", 3120},
                {                  "build.cancel",                  "Cancel Build",     editoricon::kCancel,     "Build",                       "현재 진행 중인 컴파일 작업을 취소합니다",                           "Cancel the running compile",                                                                              {},                                                                        {},                               &commandCancelBuild,               &isCompilerBusy,  true,            "MainMenu/Build", 3200},

                {                    "play.start",                          "Play",       editoricon::kPlay,      "Play",                                                              "",                                 "Start play-in-editor",                                                                              {},                                                                        {},                                      &commandPlay,                       nullptr,  true,                     nullptr,    0},
                {           "viewport.screenshot",                    "Screenshot",     editoricon::kCamera,  "Viewport",         "포커스가 있는 뷰(게임 뷰 · 씬 뷰)를 PNG 로 저장합니다", "Save the focused view as PNG under Saved/Screenshots",                                { EditorCommandKey::F9, commandmodifier::kNone },                                                                        {},     &EditorScreenshotCommands::captureFocusedView,                       nullptr,  true,                     nullptr,    0},
                {     "viewport.renderDocCapture",             "RenderDoc Capture",        editoricon::kBug,  "Viewport",     "다음 프레임을 RenderDoc 으로 캡처합니다(Saved/RenderDoc/)",                "Capture the next frame with RenderDoc",                               { EditorCommandKey::F12, commandmodifier::kCtrl },                                                                        {},                          &commandRenderDocCapture,         &isRenderDocAvailable,  true,                     nullptr,    0},

                {"clipboard.pasteComponentValues",        "Paste Component Values",       editoricon::kCopy, "Clipboard",                                                              "",  "Overwrite the selected component from the clipboard",                                                                              {},                                                                        {},                      &commandPasteComponentValues,                       nullptr,  true,                     nullptr,    0},
                { "clipboard.pasteComponentAsNew",        "Paste Component As New",       editoricon::kPlus, "Clipboard",                                                              "",      "Add the copied component to the selected object",                                                                              {},                                                                        {},                       &commandPasteComponentAsNew,                       nullptr,  true,                     nullptr,    0},
                {          "preset.loadComponent",         "Load Component Preset", editoricon::kFolderOpen,    "Preset",                                                              "",        "Apply a .preset.xml to the selected component",                                                                              {},                                                                        {},                       &commandLoadComponentPreset,                       nullptr,  true,                     nullptr,    0},
                {          "preset.saveComponent",         "Save Component Preset",       editoricon::kSave,    "Preset",                                                              "",        "Write the selected component to a preset file",                                                                              {},                                                                        {},                       &commandSaveComponentPreset,                       nullptr,  true,                     nullptr,    0},

                {        "transform.snapToGround",          "Snap to Ground (Y=0)",     editoricon::kMagnet, "Transform",                                                              "",          "Snap selected objects onto the ground plane",                                                                              {},                                                                        {},    &EditorTransformCommands::snapSelectedToGround,                 &hasSelection,  true, commandmenu::kViewportAlign,  100},
                {              "transform.alignX",              "Align X (Center)",      editoricon::kAlign, "Transform",                                                              "",                          "Align selected objects on X",                                                                              {},                                                                        {},                       &commandAlign<AlignAxis::X>,            &hasMultiSelection,  true, commandmenu::kViewportAlign,  200},
                {              "transform.alignY",              "Align Y (Center)",      editoricon::kAlign, "Transform",                                                              "",                          "Align selected objects on Y",                                                                              {},                                                                        {},                       &commandAlign<AlignAxis::Y>,            &hasMultiSelection,  true, commandmenu::kViewportAlign,  210},
                {              "transform.alignZ",              "Align Z (Center)",      editoricon::kAlign, "Transform",                                                              "",                          "Align selected objects on Z",                                                                              {},                                                                        {},                       &commandAlign<AlignAxis::Z>,            &hasMultiSelection,  true, commandmenu::kViewportAlign,  220},
                {         "transform.distributeX",           "Distribute X Evenly",      editoricon::kAlign, "Transform",                                                              "",                   "Evenly space selected objects on X",                                                                              {},                                                                        {},                  &commandDistribute<AlignAxis::X>,            &hasMultiSelection,  true, commandmenu::kViewportAlign,  300},
                {         "transform.distributeY",           "Distribute Y Evenly",      editoricon::kAlign, "Transform",                                                              "",                   "Evenly space selected objects on Y",                                                                              {},                                                                        {},                  &commandDistribute<AlignAxis::Y>,            &hasMultiSelection,  true, commandmenu::kViewportAlign,  310},
                {         "transform.distributeZ",           "Distribute Z Evenly",      editoricon::kAlign, "Transform",                                                              "",                   "Evenly space selected objects on Z",                                                                              {},                                                                        {},                  &commandDistribute<AlignAxis::Z>,            &hasMultiSelection,  true, commandmenu::kViewportAlign,  320},

                {         "prefab.applyOverrides",               "Apply Overrides",     editoricon::kPrefab,    "Prefab",                                                              "", "Write instance overrides back to the prefab template",                                                                              {},                                                                        {},                      &commandApplyPrefabOverrides,    &hasPrefabInstanceSelected,  true,                     nullptr,    0}
            };

            // ------------------------------------------------------------------------------
            // 4) ImGui 연결
            // ------------------------------------------------------------------------------
            static_assert( ImGuiKey_Z - ImGuiKey_A == 25, "ImGuiKey 의 문자 키가 연속이 아닙니다" );
            static_assert( ImGuiKey_F12 - ImGuiKey_F1 == 11, "ImGuiKey 의 F 키가 연속이 아닙니다" );

            /**
             * @brief 자체 키 열거형을 ImGuiKey 로 변환합니다. 모르는 키는 ImGuiKey_None 입니다.
             * @details 테이블이 아니라 뺄셈으로 변환한다. 그래서 양쪽 열거형이 A..Z 와 F1..F12 구간에서 연속이라는 가정에
             *          기댄다. ImGui 쪽은 바로 위의 static_assert 가, 엔진 쪽은 `EditorCommandKey` 선언 아래의 static_assert 가
             *          이 가정을 지킨다.
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
                for ( const ExtraKeyRow& row : kArrExtraKey )
                {
                    if ( row._key == key )
                        return row._platformKey;
                }
                return ImGuiKey_None;
            }

            /** @brief Space 뒤에 추가한 키와 ImGuiKey 의 대응 테이블입니다. `toImGuiKey` 와 `findPressedShortcut` 이 함께 씁니다. */
            struct ExtraKeyRow
            {
                EditorCommandKey _key;
                ImGuiKey         _platformKey;
            };
            static constexpr ExtraKeyRow kArrExtraKey[] = {
                {     EditorCommandKey::Num0,          ImGuiKey_0},
                {     EditorCommandKey::Num1,          ImGuiKey_1},
                {     EditorCommandKey::Num2,          ImGuiKey_2},
                {     EditorCommandKey::Num3,          ImGuiKey_3},
                {     EditorCommandKey::Num4,          ImGuiKey_4},
                {     EditorCommandKey::Num5,          ImGuiKey_5},
                {     EditorCommandKey::Num6,          ImGuiKey_6},
                {     EditorCommandKey::Num7,          ImGuiKey_7},
                {     EditorCommandKey::Num8,          ImGuiKey_8},
                {     EditorCommandKey::Num9,          ImGuiKey_9},
                {   EditorCommandKey::Delete,     ImGuiKey_Delete},
                {   EditorCommandKey::Insert,     ImGuiKey_Insert},
                {     EditorCommandKey::Home,       ImGuiKey_Home},
                {      EditorCommandKey::End,        ImGuiKey_End},
                {   EditorCommandKey::PageUp,     ImGuiKey_PageUp},
                { EditorCommandKey::PageDown,   ImGuiKey_PageDown},
                {     EditorCommandKey::Left,  ImGuiKey_LeftArrow},
                {    EditorCommandKey::Right, ImGuiKey_RightArrow},
                {       EditorCommandKey::Up,    ImGuiKey_UpArrow},
                {     EditorCommandKey::Down,  ImGuiKey_DownArrow},
                {      EditorCommandKey::Tab,        ImGuiKey_Tab},
                {    EditorCommandKey::Enter,      ImGuiKey_Enter},
                {   EditorCommandKey::Escape,     ImGuiKey_Escape},
                {EditorCommandKey::Backspace,  ImGuiKey_Backspace},
                {    EditorCommandKey::Minus,      ImGuiKey_Minus},
                {    EditorCommandKey::Equal,      ImGuiKey_Equal},
                {    EditorCommandKey::Comma,      ImGuiKey_Comma},
                {   EditorCommandKey::Period,     ImGuiKey_Period},
                {    EditorCommandKey::Slash,      ImGuiKey_Slash},
            };
            static_assert( sizeof( kArrExtraKey ) / sizeof( kArrExtraKey[0] ) == static_cast<size_t>( EditorCommandKey::Count ) - static_cast<size_t>( EditorCommandKey::Num0 ),
                           "Space 뒤의 키마다 ImGuiKey 표에 한 줄" );

            /** @brief 현재 눌린 수정자를 `commandmodifier` 비트로 반환합니다. */
            static uint8 getPressedModifier()
            {
                const ImGuiIO& io              = ImGui::GetIO();
                uint8          pressedModifier = commandmodifier::kNone;
                if ( io.KeyCtrl )
                    pressedModifier |= commandmodifier::kCtrl;
                if ( io.KeyShift )
                    pressedModifier |= commandmodifier::kShift;
                if ( io.KeyAlt )
                    pressedModifier |= commandmodifier::kAlt;
                return pressedModifier;
            }

            static bool& getHotkeysSuspended()
            {
                static bool s_bSuspended = false;
                return s_bSuspended;
            }

            static EditorShortcutOverrides& getOverrides()
            {
                static EditorShortcutOverrides s_overrides;
                return s_overrides;
            }

            /** @brief 이 조합이 이번 프레임에 정확히 눌렸으면 true 입니다. 수정자 비교는 `EditorCommandRegistry::matchesPressedModifiers` 가 맡습니다. */
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

            /** @brief 커맨드 하나를 메뉴 항목으로 그립니다. 눌렸으면 커맨드를 실행합니다. */
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

    void EditorCommandGUI::registerDefaults()
    {
        (void)EditorCommandGUIInternal::rebuild( nullptr, nullptr ); // 제외 범위가 없으면 제외한 수는 항상 0 이다
    }

    void EditorCommandGUI::syncWithRegistry()
    {
        if ( EditorExtensionRegistry<EditorCommandRegistration>::getList().getGeneration() == EditorCommandGUIInternal::getSyncedGeneration() )
            return;
        registerDefaults();
    }

    uint32 EditorCommandGUI::releaseCommandsWithin( const void* pBegin, const void* pEnd )
    {
        return EditorCommandGUIInternal::rebuild( pBegin, pEnd );
    }

    void EditorCommandGUI::setHotkeysSuspended( bool bSuspended )
    {
        EditorCommandGUIInternal::getHotkeysSuspended() = bSuspended;
    }

    EditorShortcutOverrides& EditorCommandGUI::getShortcutOverrides()
    {
        return EditorCommandGUIInternal::getOverrides();
    }

    EditorUserFileResult EditorCommandGUI::loadShortcutOverrides()
    {
        return getShortcutOverrides().loadFromFile( EditorShortcutOverrides::getDefaultFilePath() );
    }

    bool EditorCommandGUI::findPressedShortcut( EditorCommandShortcut& outShortcut )
    {
        const uint8 modifier = EditorCommandGUIInternal::getPressedModifier();
        for ( uint8 keyValue = static_cast<uint8>( EditorCommandKey::A ); keyValue < static_cast<uint8>( EditorCommandKey::Count ); ++keyValue )
        {
            const EditorCommandKey key   = static_cast<EditorCommandKey>( keyValue );
            const ImGuiKey         imKey = EditorCommandGUIInternal::toImGuiKey( key );
            if ( imKey == ImGuiKey_None || ImGui::IsKeyPressed( imKey, false ) == false )
                continue;
            outShortcut._key      = key;
            outShortcut._modifier = modifier;
            return true;
        }
        return false;
    }

    uint32 EditorCommandGUIInternal::rebuild( const void* pExcludeBegin, const void* pExcludeEnd )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
        {
            // 조용히 반환하면 메뉴가 통째로 비고 단축키가 동작하지 않는다. 실제 기동 검증이 이 줄을 잡는다.
            SW_LOG_ERROR( "커맨드를 등록할 EditorContext 가 없습니다 — 메뉴와 단축키가 비어 있게 됩니다" );
            return 0;
        }

        EditorCommandRegistry& registry = pContext->getSubsystem<EditorCommandRegistry>();
        registry.clear();
        getSyncedGeneration() = EditorExtensionRegistry<EditorCommandRegistration>::getList().getGeneration();

        for ( const EditorCommandGUIInternal::CommandRow& row : EditorCommandGUIInternal::_s_arrCommandRow )
        {
            EditorCommandDesc desc{};
            desc._id              = row._pID;
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
        // 확장 모듈과 다른 파일의 등록 항목(SW_EDITOR_COMMAND)을 테이블과 같은 레지스트리에 합친다.
        const uint32 excludedCount = EditorCommandTableUtil::appendRegistrations( registry, pExcludeBegin, pExcludeEnd );
        // 사용자가 바꾼 단축키(Saved/Editor/Shortcuts.json)를 적용한다. 기본 조합은 각 커맨드의 _default* 에 남는다.
        (void)getOverrides().applyTo( registry ); // 적용한 수는 쓰지 않는다. 충돌은 아래 validate 가 알린다

        string report;
        if ( registry.validate( report ) == false )
            SW_LOG_ERROR( "에디터 커맨드 표가 어긋났습니다:\n%#", report.c_str() );
        // 코드가 그리는 메뉴 경로에 테이블의 항목이 하나도 없으면 그 메뉴(툴바 팝업)가 비어 있는 채로 뜬다.
        for ( const utf8* pHostedPath : commandmenu::kArrHostedMenuPath )
        {
            if ( registry.findMenu( pHostedPath ) == nullptr )
                SW_LOG_ERROR( "Menu path '%#' is drawn by code but no command row uses it - the menu is empty", pHostedPath );
        }
        return excludedCount;
    }

    void EditorCommandGUI::processHotkeys()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr || EditorCommandGUIInternal::getHotkeysSuspended() )
            return;
        if ( ImGui::GetIO().WantTextInput )
            return;

        const EditorCommandRegistry& registry = pContext->getSubsystem<EditorCommandRegistry>();
        for ( const EditorCommandDesc& desc : registry.getCommands() )
        {
            const bool bPressed = ( EditorCommandGUIInternal::isShortcutPressed( desc._shortcut ) ||
                                    EditorCommandGUIInternal::isShortcutPressed( desc._altShortcut ) );
            if ( bPressed == false )
                continue;

            // 조합은 유일하다(registerDefaults 의 validate 가 보장한다). 그래서 처음 맞은 것만 실행한다.
            EditorCommandRegistry::executeDesc( desc );
            return;
        }
    }

    void EditorCommandGUI::drawMainMenus()
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        const EditorCommandRegistry& registry = pContext->getSubsystem<EditorCommandRegistry>();
        for ( const EditorMenu& menu : registry.getMenus() )
        {
            if ( menu._parentPath != commandmenu::kMainMenuBar )
                continue;
            if ( ImGui::BeginMenu( menu._name.c_str() ) )
            {
                EditorCommandGUIInternal::drawMenu( registry, menu );
                ImGui::EndMenu();
            }
        }
    }

    void EditorCommandGUI::drawMenuItems( string_view menuPath )
    {
        EditorContext* pContext = EditorContext::get();
        if ( pContext == nullptr )
            return;

        const EditorCommandRegistry& registry = pContext->getSubsystem<EditorCommandRegistry>();
        const EditorMenu*            pMenu    = registry.findMenu( menuPath );
        if ( pMenu != nullptr )
            EditorCommandGUIInternal::drawMenu( registry, *pMenu );
    }
} // namespace sw::editor
