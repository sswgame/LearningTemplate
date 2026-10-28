/**
 * @file EditorCommandGUI.cpp
 * @brief 기본 커맨드 테이블을 정의하고, 이를 ImGui 의 메뉴 항목과 전역 단축키로 드러냅니다.
 */
#include "pch.h"

#include "Editor/Common/Input/EditorCommandGUI.h"

#include "Core/File/FileUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/Dev/EditorGlobalVariableCommands.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Play/EditorScreenshotCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneFileCommands.h"
#include "Editor/Common/Commands/Scene/EditorTransformCommands.h"
#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorMenuBar.h"
#include "Editor/Common/Input/EditorInputActions.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Popups/CommandPalettePopup.h"
#include "Editor/Popups/QuickLauncherPopup.h"

#include "Engine/Asset/AssetPaths.h"
#include "Engine/Config/GameConfig.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Renderer/Capture/RenderDocCapture.h"
#include "Engine/Scene/CommandStack.h"

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

            /**
             * @brief 커맨드 GUI 가 프레임 사이에 들고 있는 상태입니다. 에디터 범위의 상태 저장소가 소유하므로 에디터 컨텍스트와 함께 처음 값으로 돌아갑니다.
             */
            struct GUIState
            {
                uint32 _syncedGeneration{ invalid_index::kUint32 }; ///< 마지막으로 맞춘 커맨드 등록 세대
            };

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
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();

                if ( listPath.empty() == false )
                    editorContext.getSubsystem<EditorWorkspace>().requestLoadScene( listPath[0] );
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

                const string activePack = editor::getSubsystem<const GameConfig>()._packRoot.get();
                const string mapsDir    = AssetPaths::findLooseDomainFolder( activePack, path::kMapsFolder ).getDiskPath();
                if ( FileUtil::isDirectory( mapsDir ) )
                    params._initialDirectory = mapsDir;
                else if ( AssetPaths::findLooseDomainFolder( activePack ).getDiskPath().empty() == false )
                    params._initialDirectory = AssetPaths::findLooseDomainFolder( activePack ).getDiskPath();
                else if ( AssetPaths::findLooseDomainFolder( path::kGamePack ).getDiskPath().empty() == false )
                    params._initialDirectory = AssetPaths::findLooseDomainFolder( path::kGamePack ).getDiskPath();

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
                IModuleCompiler* pCompiler = findSubsystem<IModuleCompiler>(); // 선택: Shipping 에는 모듈 컴파일러가 없다(GameModuleManager 가 SW_SHIPPING 에서 만들지 않는다)
                if ( pCompiler != nullptr )
                    pCompiler->compileModule( "SWGame" );
            }

            static void commandCompileEditor()
            {
                IModuleCompiler* pCompiler = findSubsystem<IModuleCompiler>(); // 선택: Shipping 에는 모듈 컴파일러가 없다(GameModuleManager 가 SW_SHIPPING 에서 만들지 않는다)
                if ( pCompiler != nullptr )
                    pCompiler->compileModule( "EditorAll" ); // EditorModule 과 확장 모듈을 함께 컴파일한다(sw_addEditorExtension 이 의존성을 추가한다)
            }

            static void commandCompileAll()
            {
                IModuleCompiler* pCompiler = findSubsystem<IModuleCompiler>(); // 선택: Shipping 에는 모듈 컴파일러가 없다(GameModuleManager 가 SW_SHIPPING 에서 만들지 않는다)
                if ( pCompiler != nullptr )
                    pCompiler->compileAll();
            }

            static void commandCancelBuild()
            {
                IModuleCompiler* pCompiler = findSubsystem<IModuleCompiler>(); // 선택: Shipping 에는 모듈 컴파일러가 없다(GameModuleManager 가 SW_SHIPPING 에서 만들지 않는다)
                if ( pCompiler != nullptr )
                    pCompiler->cancel();
            }

            static void commandPlay()
            {
                EditorContext& editorContext          = editor::getSubsystem<EditorContext>();
                const bool     bWarnUnsavedBeforePlay = editorContext.getSubsystem<EditorWorkspace>().isSceneDirty() && EditorPlaySession::isStopped();
                if ( bWarnUnsavedBeforePlay )
                {
                    editorContext.getSubsystem<EditorNotificationManager>().push( SW_LOCKEY( "EdCommon", "NotifyPlayTitle" ), SW_LOCKEY( "EdCommon", "NotifyPlayUnsaved" ),
                                                                                  NotificationType::Warning );
                    return;
                }
                EditorPlaySession::play();
            }

            /** @brief 전역 명령이 고칠 오브젝트 — 겨누는 창의 선택의 주 오브젝트입니다(언리얼은 포커스된 툴킷의 명령 목록이 받는다). */
            static GameObject* primaryObject()
            {
                const EditorSelection* pSelection = editor::getFocusedSelection();
                return pSelection != nullptr ? pSelection->getPrimaryObject() : nullptr;
            }

            /** @brief 인스펙터에서 고른 컴포넌트입니다. 컴포넌트 고르기는 메인 워크스페이스의 것이라 메인 씬의 선택에서 찾는다. */
            static Component* selectedComponent()
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                GameObject*    pObj          = editor::getMainSceneSelection().getPrimaryObject();
                if ( pObj == nullptr )
                    return nullptr;
                const uint64 componentID = editorContext.getSubsystem<EditorWorkspace>().getSelectedComponentID();
                if ( componentID == 0 )
                    return nullptr;
                return pObj->findComponentByID( componentID );
            }

            static void warn( const utf8* pTitle, const utf8* pDetail )
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                editorContext.getSubsystem<EditorNotificationManager>().push( pTitle, pDetail, NotificationType::Warning );
            }

            static void commandPasteComponentValues()
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                Component*     pComp         = selectedComponent();
                if ( pComp == nullptr )
                {
                    warn( SW_LOCKEY( "EdCommon", "NotifyPasteTitle" ), SW_LOCKEY( "EdCommon", "NotifySelectComponent" ) );
                    return;
                }
                if ( editorContext.getSubsystem<EditorWorkspace>().hasCopiedComponent() == false )
                {
                    warn( SW_LOCKEY( "EdCommon", "NotifyPasteTitle" ), SW_LOCKEY( "EdCommon", "NotifyClipboardEmpty" ) );
                    return;
                }
                (void)EditorTransformCommands::pasteCopiedComponentValues( editorContext.getSubsystem<EditorWorkspace>(), pComp ); // 실패는 붙여넣기 함수가 로그로 알린다
            }

            static void commandPasteComponentAsNew()
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                GameObject*    pObj          = primaryObject();
                if ( pObj == nullptr )
                {
                    warn( SW_LOCKEY( "EdCommon", "NotifyPasteTitle" ), SW_LOCKEY( "EdCommon", "NotifySelectObject" ) );
                    return;
                }
                if ( editorContext.getSubsystem<EditorWorkspace>().hasCopiedComponent() == false )
                {
                    warn( SW_LOCKEY( "EdCommon", "NotifyPasteTitle" ), SW_LOCKEY( "EdCommon", "NotifyClipboardEmpty" ) );
                    return;
                }
                (void)EditorTransformCommands::pasteCopiedComponentAsNew( editorContext.getSubsystem<EditorWorkspace>(), pObj ); // 실패는 붙여넣기 함수가 로그로 알린다
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

            /** @brief 선택한 오브젝트를 스폰한 프리팹의 경로입니다. 프리팹 인스턴스가 아니면 비어 있습니다. 놓인 프리팹 테이블은 메인 씬의 것이라 메인 선택을 본다. */
            static string findSelectedPrefabPath( GameObject*& pOutObj )
            {
                EditorContext& editorContext = editor::getSubsystem<EditorContext>();
                pOutObj                      = editor::getMainSceneSelection().getPrimaryObject();
                if ( pOutObj == nullptr )
                    return {};
                return string{ editorContext.getSubsystem<EditorWorkspace>().findPrefabPath( pOutObj ) };
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
                if ( EditorToolAssetCommands::applyPrefabOverridesToTemplate( pObj, AssetID{ path } ) == false )
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
                IModuleCompiler* pCompiler = findSubsystem<IModuleCompiler>(); // 선택: Shipping 에는 모듈 컴파일러가 없다(GameModuleManager 가 SW_SHIPPING 에서 만들지 않는다)
                return pCompiler != nullptr && pCompiler->isCompiling() == false;
            }

            static bool isCompilerBusy()
            {
                IModuleCompiler* pCompiler = findSubsystem<IModuleCompiler>(); // 선택: Shipping 에는 모듈 컴파일러가 없다(GameModuleManager 가 SW_SHIPPING 에서 만들지 않는다)
                return pCompiler != nullptr && pCompiler->isCompiling();
            }

            /** @brief 전역 명령(정렬 · 분배 · 바닥에 붙이기)의 활성 조건 — 명령이 고칠 겨누는 창의 선택 수입니다. */
            static size_t selectedObjectCount()
            {
                const EditorSelection* pSelection = editor::getFocusedSelection();
                return pSelection != nullptr ? pSelection->getSelectedObjectCount() : 0;
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
                const utf8* _pID;
                const utf8* _pLabel;
                const utf8* _pIcon;
                const utf8* _pCategory;
                const utf8* _pTooltip;
                const utf8* _pDetail;
                const utf8* _pInputAction; ///< 에디터 입력 맵(`editor.input.xml`)의 액션 이름. nullptr 이면 단축키가 없다
                void ( *_pAction )();
                bool ( *_pEnabled )();
                EditorCommandEdit _edit; ///< 씬을 바꾸는지(필수 — `EditorCommandEdit`)
                bool              _bPaletteVisible;
                const utf8*       _pMenuPath; ///< nullptr 이면 어느 메뉴에도 나오지 않는다(`EditorCommandDesc::_menuPath`)
                int32             _menuOrder; ///< 메뉴 안 순서. 백의 자리가 바뀌면 구분선이 들어가고, 메뉴끼리의 순서도 이 값으로 정한다
            };

            /**
             * @brief 에디터 커맨드의 원본 테이블입니다.
             * @details 툴팁 · 메뉴에 단축키를 손으로 적지 않는다. `drawCommandItem` 이 입력 맵의 바인딩으로 만들어 붙이므로 라벨과 실제 처리가 어긋나지 않는다.
             *          Exit 는 단축키를 적지 않는다 — Alt+F4 는 OS 가 창 닫기로 먼저 가로채는 조합이라 입력 맵이 처리하지 않는다.
             */
            inline static const CommandRow _s_arrCommandRow[] = {
                {                     "scene.new",                     "New Scene",       editoricon::kFile,     "Scene",                                     "새로운 빈 씬을 생성합니다",           "Replace the active scene with an empty one",                     nullptr,                                  &commandNewScene,                    nullptr, EditorCommandEdit::NoScene,  true,             "MainMenu/File", 1100},
                {                    "scene.open",                 "Open Scene...", editoricon::kFolderOpen,     "Scene",                  "디스크에서 기존 씬 파일(.scene.xml)을 엽니다",                          "Open a .scene.xml from disk",                "scene.open",                                 &commandOpenScene,                    nullptr, EditorCommandEdit::NoScene,  true,             "MainMenu/File", 1110},
                {                    "asset.save",                          "Save",       editoricon::kSave,     "Scene",                  "현재 포커스된 에셋 또는 활성 씬을 저장합니다",          "Save the focused asset, or the active scene",                "asset.save",      &EditorSceneFileCommands::saveFocusedOrScene,                    nullptr, EditorCommandEdit::NoScene,  true,             "MainMenu/File", 1120},
                {               "scene.saveScene",                    "Save Scene",       editoricon::kSave,     "Scene",                        "현재 활성화된 씬을 디스크에 저장합니다", "Write the active scene, or prompt Save As if unsaved",                     nullptr, &EditorSceneFileCommands::saveActiveSceneOrPrompt,                    nullptr, EditorCommandEdit::NoScene,  true,             "MainMenu/File", 1130},
                {              "editor.quickOpen",                 "Quick Open...",     editoricon::kSearch,    "Editor",                   "에셋, 씬, 스크립트를 빠르게 검색하여 엽니다",              "Fuzzy-search assets, scenes and scripts",          "editor.quickOpen",                       &QuickLauncherPopup::toggle,                    nullptr, EditorCommandEdit::NoScene, false,             "MainMenu/File", 1200},
                {         "editor.commandPalette",            "Command Palette...",   editoricon::kTerminal,    "Editor",                     "에디터 명령 및 액션을 검색하여 실행합니다",                       "Search and run editor commands",     "editor.commandPalette",                      &CommandPalettePopup::toggle,                    nullptr, EditorCommandEdit::NoScene, false,             "MainMenu/File", 1210},
                {                   "editor.exit",                          "Exit",       editoricon::kExit,      "File",                                           "에디터를 종료합니다",   "Close the editor after unsaved-change confirmation",                     nullptr,             &EditorSceneFileCommands::requestExit,                    nullptr, EditorCommandEdit::NoScene,  true,             "MainMenu/File", 1300},

                {                     "edit.undo",                          "Undo",       editoricon::kUndo,      "Edit",                                 "마지막 편집 작업을 되돌립니다",                                   "Undo the last edit",                 "edit.undo",                                      &commandUndo,                    nullptr,   EditorCommandEdit::Scene,  true,             "MainMenu/Edit", 2100},
                {                     "edit.redo",                          "Redo",       editoricon::kRedo,      "Edit",                            "되돌린 편집 작업을 다시 실행합니다",                            "Redo the last undone edit",                 "edit.redo",                                      &commandRedo,                    nullptr,   EditorCommandEdit::Scene,  true,             "MainMenu/Edit", 2110},
                {          "editor.themeSettings",      "Theme & Look and Feel...",    editoricon::kPalette,    "Editor", "에디터 테마 프리셋, 액센트 색상 및 모서리 라운딩을 설정합니다",                       "Open the theme settings dialog",                     nullptr,                   &EditorMenuBar::openThemeDialog,                    nullptr, EditorCommandEdit::NoScene,  true,             "MainMenu/Edit", 2200},

                {             "build.compileGame",         "Compile Game (SWGame)",     editoricon::kHammer,     "Build",       "게임 모듈(SWGame)을 라이브 코딩으로 즉시 재컴파일합니다",                      "Recompile and hot-reload SWGame",         "build.compileGame",                               &commandCompileGame,            &isCompilerIdle, EditorCommandEdit::NoScene,  true,            "MainMenu/Build", 3100},
                {           "build.compileEditor", "Compile Editor (EditorModule)",     editoricon::kWrench,     "Build",    "에디터 모듈(EditorModule)을 라이브 코딩으로 재컴파일합니다",                               "Recompile EditorModule",                     nullptr,                             &commandCompileEditor,            &isCompilerIdle, EditorCommandEdit::NoScene,  true,            "MainMenu/Build", 3110},
                {              "build.compileAll",           "Compile All Modules",   editoricon::kBuildAll,     "Build",               "엔진 및 모든 게임/에디터 모듈을 전체 빌드합니다",                    "Build the engine and every module",          "build.compileAll",                                &commandCompileAll,            &isCompilerIdle, EditorCommandEdit::NoScene,  true,            "MainMenu/Build", 3120},
                {                  "build.cancel",                  "Cancel Build",     editoricon::kCancel,     "Build",                       "현재 진행 중인 컴파일 작업을 취소합니다",                           "Cancel the running compile",                     nullptr,                               &commandCancelBuild,            &isCompilerBusy, EditorCommandEdit::NoScene,  true,            "MainMenu/Build", 3200},

                {                    "play.start",                          "Play",       editoricon::kPlay,      "Play",                                                              "",                                 "Start play-in-editor",                     nullptr,                                      &commandPlay,                    nullptr, EditorCommandEdit::NoScene,  true,                     nullptr,    0},
                {           "viewport.screenshot",                    "Screenshot",     editoricon::kCamera,  "Viewport",         "포커스가 있는 뷰(게임 뷰 · 씬 뷰)를 PNG 로 저장합니다", "Save the focused view as PNG under Saved/Screenshots",       "viewport.screenshot",     &EditorScreenshotCommands::captureFocusedView,                    nullptr, EditorCommandEdit::NoScene,  true,                     nullptr,    0},
                {     "viewport.renderDocCapture",             "RenderDoc Capture",        editoricon::kBug,  "Viewport",     "다음 프레임을 RenderDoc 으로 캡처합니다(Saved/RenderDoc/)",                "Capture the next frame with RenderDoc", "viewport.renderDocCapture",                          &commandRenderDocCapture,      &isRenderDocAvailable, EditorCommandEdit::NoScene,  true,                     nullptr,    0},

                {"clipboard.pasteComponentValues",        "Paste Component Values",       editoricon::kCopy, "Clipboard",                                                              "",  "Overwrite the selected component from the clipboard",                     nullptr,                      &commandPasteComponentValues,                    nullptr,   EditorCommandEdit::Scene,  true,                     nullptr,    0},
                { "clipboard.pasteComponentAsNew",        "Paste Component As New",       editoricon::kPlus, "Clipboard",                                                              "",      "Add the copied component to the selected object",                     nullptr,                       &commandPasteComponentAsNew,                    nullptr,   EditorCommandEdit::Scene,  true,                     nullptr,    0},
                {          "preset.loadComponent",         "Load Component Preset", editoricon::kFolderOpen,    "Preset",                                                              "",        "Apply a .preset.xml to the selected component",                     nullptr,                       &commandLoadComponentPreset,                    nullptr,   EditorCommandEdit::Scene,  true,                     nullptr,    0},
                {          "preset.saveComponent",         "Save Component Preset",       editoricon::kSave,    "Preset",                                                              "",        "Write the selected component to a preset file",                     nullptr,                       &commandSaveComponentPreset,                    nullptr, EditorCommandEdit::NoScene,  true,                     nullptr,    0},

                {        "transform.snapToGround",          "Snap to Ground (Y=0)",     editoricon::kMagnet, "Transform",                                                              "",          "Snap selected objects onto the ground plane",                     nullptr,    &EditorTransformCommands::snapSelectedToGround,              &hasSelection,   EditorCommandEdit::Scene,  true, commandmenu::kViewportAlign,  100},
                {              "transform.alignX",              "Align X (Center)",      editoricon::kAlign, "Transform",                                                              "",                          "Align selected objects on X",                     nullptr,                       &commandAlign<AlignAxis::X>,         &hasMultiSelection,   EditorCommandEdit::Scene,  true, commandmenu::kViewportAlign,  200},
                {              "transform.alignY",              "Align Y (Center)",      editoricon::kAlign, "Transform",                                                              "",                          "Align selected objects on Y",                     nullptr,                       &commandAlign<AlignAxis::Y>,         &hasMultiSelection,   EditorCommandEdit::Scene,  true, commandmenu::kViewportAlign,  210},
                {              "transform.alignZ",              "Align Z (Center)",      editoricon::kAlign, "Transform",                                                              "",                          "Align selected objects on Z",                     nullptr,                       &commandAlign<AlignAxis::Z>,         &hasMultiSelection,   EditorCommandEdit::Scene,  true, commandmenu::kViewportAlign,  220},
                {         "transform.distributeX",           "Distribute X Evenly",      editoricon::kAlign, "Transform",                                                              "",                   "Evenly space selected objects on X",                     nullptr,                  &commandDistribute<AlignAxis::X>,         &hasMultiSelection,   EditorCommandEdit::Scene,  true, commandmenu::kViewportAlign,  300},
                {         "transform.distributeY",           "Distribute Y Evenly",      editoricon::kAlign, "Transform",                                                              "",                   "Evenly space selected objects on Y",                     nullptr,                  &commandDistribute<AlignAxis::Y>,         &hasMultiSelection,   EditorCommandEdit::Scene,  true, commandmenu::kViewportAlign,  310},
                {         "transform.distributeZ",           "Distribute Z Evenly",      editoricon::kAlign, "Transform",                                                              "",                   "Evenly space selected objects on Z",                     nullptr,                  &commandDistribute<AlignAxis::Z>,         &hasMultiSelection,   EditorCommandEdit::Scene,  true, commandmenu::kViewportAlign,  320},

                {         "prefab.applyOverrides",               "Apply Overrides",     editoricon::kPrefab,    "Prefab",                                                              "", "Write instance overrides back to the prefab template",                     nullptr,                      &commandApplyPrefabOverrides, &hasPrefabInstanceSelected,   EditorCommandEdit::Scene,  true,                     nullptr,    0}
            };

            // ------------------------------------------------------------------------------
            // 4) ImGui 연결 — 메뉴 항목. 단축키 글자는 에디터 입력 맵에서 만든다
            // ------------------------------------------------------------------------------
            /** @brief 커맨드 하나를 메뉴 항목으로 그립니다. 눌렸으면 커맨드를 실행합니다. */
            static void drawCommandItem( const EditorInputActions& inputActions, const EditorCommandDesc& desc )
            {
                fixed_string<constant::kMaxBuffer128> menuLabel;
                EditorCommandRegistry::formatMenuLabel( desc, menuLabel );

                string shortcutLabel;
                inputActions.formatShortcutLabel( desc._inputAction, shortcutLabel );

                const utf8* pShortcut = shortcutLabel.empty() ? nullptr : shortcutLabel.c_str();
                const bool  bEnabled  = EditorCommandRegistry::isEnabled( desc, EditorCommandTarget::none() );

                if ( ImGui::MenuItem( menuLabel.c_str(), pShortcut, false, bEnabled ) )
                    (void)EditorCommandRegistry::executeDesc( desc, EditorCommandTarget::none() ); // 꺼진 항목은 그려질 때 이미 회색이다

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
                const EditorInputActions&        inputActions = editor::getSubsystem<EditorContext>().getSubsystem<EditorInputActions>();
                const vector<EditorCommandDesc>& listCommand  = registry.getCommands();
                for ( const EditorMenuItem& item : menu._listItem )
                {
                    if ( item._bSeparatorBefore )
                        ImGui::Separator();
                    drawCommandItem( inputActions, listCommand[item._commandIndex] );
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
        if ( EditorExtensionRegistry<EditorCommandRegistration>::getList().getGeneration() == getEditorState<EditorCommandGUIInternal::GUIState>()._syncedGeneration )
            return;
        registerDefaults();
    }

    uint32 EditorCommandGUI::releaseCommandsWithin( const void* pBegin, const void* pEnd )
    {
        return EditorCommandGUIInternal::rebuild( pBegin, pEnd );
    }

    uint32 EditorCommandGUIInternal::rebuild( const void* pExcludeBegin, const void* pExcludeEnd )
    {
        // 컨텍스트가 없으면 메뉴가 통째로 비고 단축키가 동작하지 않는다 — 조용히 반환하지 않고 필수 서비스 조회가 모든 구성에서 멈춘다.
        EditorCommandRegistry& registry = editor::getSubsystem<EditorContext>().getSubsystem<EditorCommandRegistry>();
        registry.clear();
        EditorCommandGUIInternal::GUIState& guiState = getEditorState<EditorCommandGUIInternal::GUIState>();
        guiState._syncedGeneration                   = EditorExtensionRegistry<EditorCommandRegistration>::getList().getGeneration();

        for ( const EditorCommandGUIInternal::CommandRow& row : EditorCommandGUIInternal::_s_arrCommandRow )
        {
            EditorCommandDesc desc{};
            desc._id              = row._pID;
            desc._label           = row._pLabel;
            desc._icon            = row._pIcon;
            desc._category        = row._pCategory;
            desc._tooltip         = row._pTooltip;
            desc._detail          = row._pDetail;
            desc._inputAction     = row._pInputAction != nullptr ? row._pInputAction : "";
            desc._bPaletteVisible = row._bPaletteVisible;
            desc._menuPath        = row._pMenuPath != nullptr ? row._pMenuPath : "";
            desc._menuOrder       = row._menuOrder;
            // 표의 명령은 모두 전역 명령이라 대상을 받지 않는다 — 대상을 버리고 부른다.
            if ( row._pAction != nullptr )
                desc._action = [pfnAction = row._pAction]( const EditorCommandTarget& /*target*/ )
                { pfnAction(); };
            if ( row._pEnabled != nullptr )
                desc._enabledPredicate = [pfnEnabled = row._pEnabled]( const EditorCommandTarget& /*target*/ )
                { return pfnEnabled(); };
            desc._edit = row._edit;

            registry.registerCommand( std::move( desc ) );
        }
        // 확장 모듈과 다른 파일의 등록 항목(SW_EDITOR_COMMAND)을 테이블과 같은 레지스트리에 합친다.
        const uint32 excludedCount = EditorCommandTableUtil::appendRegistrations( registry, pExcludeBegin, pExcludeEnd );

        string report;
        if ( registry.validate( report ) == false )
            SW_LOG_ERROR( "The editor command table is inconsistent:\n%#", report.c_str() );
        // 명령이 가리키는 액션이 입력 맵에 없으면 그 단축키는 조용히 없는 것이 된다 — 표 · 등록 줄과 editor.input.xml 이 어긋난 것이다.
        const InputMap& inputMap = editor::getSubsystem<EditorContext>().getSubsystem<EditorInputActions>().getInputMap();
        for ( const EditorCommandDesc& desc : registry.getCommands() )
        {
            if ( desc._inputAction.empty() == false && inputMap.hasAction( hashed_string( desc._inputAction.c_str() ) ) == false )
                SW_LOG_ERROR( "Command '%#' names input action '%#' that the editor input map does not have", desc._id.c_str(), desc._inputAction.c_str() );
        }
        // 코드가 그리는 메뉴 경로에 테이블의 항목이 하나도 없으면 그 메뉴(툴바 팝업)가 비어 있는 채로 뜬다.
        for ( const utf8* pHostedPath : commandmenu::kArrHostedMenuPath )
        {
            if ( registry.findMenu( pHostedPath ) == nullptr )
                SW_LOG_ERROR( "Menu path '%#' is drawn by code but no command row uses it - the menu is empty", pHostedPath );
        }
        return excludedCount;
    }

    void EditorCommandGUI::drawMainMenus()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        const EditorCommandRegistry& registry = editorContext.getSubsystem<EditorCommandRegistry>();
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
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        const EditorCommandRegistry& registry = editorContext.getSubsystem<EditorCommandRegistry>();
        const EditorMenu*            pMenu    = registry.findMenu( menuPath );
        if ( pMenu != nullptr )
            EditorCommandGUIInternal::drawMenu( registry, *pMenu );
    }
} // namespace sw::editor
