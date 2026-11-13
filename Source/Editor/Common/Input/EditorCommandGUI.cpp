/**
 * @file EditorCommandGUI.cpp
 * @brief 기본 커맨드 테이블을 정의하고, 이를 ImGui 의 메뉴 항목과 전역 단축키로 드러냅니다.
 */
#include "pch.h"

#include "Editor/Common/Input/EditorCommandGUI.h"

#include "Core/File/FileUtil.h"
#include "Core/String/fixed_string.h"

#include "DevTools/RenderDoc/RenderDocCapture.h"

#include "Editor/Common/Backend/FileDialog.h"
#include "Editor/Common/Commands/Asset/EditorToolAssetCommands.h"
#include "Editor/Common/Commands/Diagnostics/EditorGlobalVariableCommands.h"
#include "Editor/Common/Commands/EditorCommandRegistry.h"
#include "Editor/Common/Commands/Play/EditorScreenshotCommands.h"
#include "Editor/Common/Commands/Scene/EditorSceneFileCommands.h"
#include "Editor/Common/Commands/Scene/EditorTransformCommands.h"
#include "Editor/Common/Config/EditorIconGlyphTable.h"
#include "Editor/Common/Config/EditorPaths.h"
#include "Editor/Common/GUI/EditorMenuBar.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/CommandPalettePopup.h"
#include "Editor/Common/Input/EditorInputActions.h"
#include "Editor/Common/Input/QuickLauncherPopup.h"
#include "Editor/Common/Localization/EditorDataText.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/Asset/EditorAssetType.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorSelection.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorTransaction.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/Common/Workspace/Undo/CommandStack.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Scene/SceneDocument.h"

#include "ModuleAPI/Service/IModuleCompiler.h"

#include <imgui.h>

namespace sw::editor
{
    namespace
    {
        struct EditorCommandGUIInternal
        {
            /** @brief 명령 단추 하나(도구줄 아이콘 또는 아이콘 + 이름표). 정의는 `EditorCommandGUI::drawToolbarButton` 아래. */
            static bool drawCommandButton( string_view commandID, const EditorCommandTarget& target, bool bIconOnly );

            /**
             * @brief 커맨드 테이블과 등록 항목으로 레지스트리를 다시 만듭니다.
             * @details 정의는 아래 EditorCommandGUI 구현 옆에 있다. 테이블 `_s_arrCommandRow` 가 이 구조체의 끝에 있어 그 뒤에 둔다.
             */
            static void rebuild();

            /**
             * @brief 커맨드 GUI 가 프레임 사이에 들고 있는 상태입니다. 에디터 범위의 상태 저장소가 소유하므로 에디터 컨텍스트와 함께 처음 값으로 돌아갑니다.
             */
            struct GUIState
            {
                uint32 _syncedGeneration{ constant::invalid_index::kUint32 };   ///< 마지막으로 맞춘 커맨드 등록 세대
                uint32 _syncedTextRevision{ constant::invalid_index::kUint32 }; ///< 마지막으로 푼 현지화 글 판(언어 · 표가 바뀌면 명령 글을 다시 푼다)
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
                EditorAssetTypeRegistry::appendSuffixes( EditorAssetType{ SceneDocument::kAssetType }, params._listFilterExtension );

                params._initialDirectory = EditorPaths::getSceneDialogFolderPath(); // 저장 대화상자와 같은 폴더(없으면 대화상자가 정한다)

                FileDialog::open( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, onOpenSceneDialogResult ) );
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
                    pCompiler->compileModule( "EditorAll" ); // EditorModule 과 확장 모듈을 함께 컴파일한다(sw_add_editor_extension 이 의존성을 추가한다)
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
                                                                                  NotificationKind::Warning );
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
                editorContext.getSubsystem<EditorNotificationManager>().push( pTitle, pDetail, NotificationKind::Warning );
            }

            static void commandPasteComponentValues( EditorTransaction& /*transaction*/ )
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

            static void commandPasteComponentAsNew( EditorTransaction& /*transaction*/ )
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
                // XML 이 그 컴포넌트 타입과 맞지 않으면 실패한다. 명령(`preset.loadComponent`)은 창만 열었다 — 씬을 바꾸는 여기서 거래를 연다.
                EditorTransaction transaction;
                if ( EditorTransformCommands::loadComponentPreset( transaction, pComp, listPath[0] ) == false )
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
                FileDialog::open( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, onLoadPresetDialogResult ) );
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
                FileDialog::open( params, SW_DELEGATE_FUNCTION( FileDialogDelegate, onSavePresetDialogResult ) );
            }

            // 씬 표가 `void( EditorTransaction& )` 를 요구하므로 인자를 고정하는 함수는 필요하지만, 축마다 함수를 만들 이유는 없다. 축을 템플릿 인자로 받으면 표는 `&commandAlign<AlignAxis::X>` 로 가리킨다.
            // 축이 늘어나도 여기서 고칠 것은 없다.
            template <AlignAxis TAxis>
            static void commandAlign( EditorTransaction& /*transaction*/ )
            {
                EditorTransformCommands::alignSelectedObjects( TAxis, AlignType::Center );
            }

            template <AlignAxis TAxis>
            static void commandDistribute( EditorTransaction& /*transaction*/ )
            {
                EditorTransformCommands::distributeSelectedObjects( TAxis );
            }

            static void commandSnapToGround( EditorTransaction& /*transaction*/ )
            {
                EditorTransformCommands::snapSelectedToGround();
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

            static void commandApplyPrefabOverrides( EditorTransaction& /*transaction*/ )
            {
                // 선택한 인스턴스의 프리팹에만 쓴다. 포커스된 에셋 경로(콘텐츠 브라우저에서 마지막에 클릭한 씬, 머티리얼, 텍스처)를
                // 쓰면 그 파일을 프리팹으로 덮어쓰므로 주의한다.
                GameObject*  pObj = nullptr;
                const string path = findSelectedPrefabPath( pObj );
                if ( path.empty() )
                {
                    SW_LOG_WARNING_ID( "Editor.ApplyOverridesNeedsSelected", "Apply Overrides needs a selected prefab instance - nothing was written" );
                    return;
                }
                if ( EditorToolAssetCommands::applyPrefabOverridesToTemplate( pObj, AssetID{ path } ) == false )
                    SW_LOG_ERROR_ID( "Editor.ApplyOverridesCouldNot", "Apply Overrides could not write '%#'", path.c_str() );
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
            /** @brief 테이블의 항목 하나입니다. 동작만 든다 — 표시 · 배치는 `editor/commands/editor.commands.xml` 의 같은 id 줄이다. */
            struct CommandRow
            {
                const utf8* _pID;
                void ( *_pAction )();
                bool ( *_pEnabled )();
                EditorCommandEdit _edit; ///< `NoScene` 또는 `SceneUnrecorded` — 씬을 바꾸는 명령은 거래를 받는 표(`SceneCommandRow`)에 둔다
            };

            /** @brief 씬을 바꾸는 명령(`EditorCommandEdit::Scene`)의 항목입니다. 동작이 레지스트리가 연 거래 구간을 받는다 — 거래 없이 씬을 바꾸는 줄은 형식이 받지 않는다. */
            struct SceneCommandRow
            {
                const utf8* _pID;
                void ( *_pAction )( EditorTransaction& transaction );
                bool ( *_pEnabled )();
            };

            /**
             * @brief 에디터 커맨드의 원본 테이블입니다.
             * @details 툴팁 · 메뉴에 단축키를 손으로 적지 않는다. `drawCommandItem` 이 입력 맵의 바인딩으로 만들어 붙이므로 라벨과 실제 처리가 어긋나지 않는다.
             *          Exit 는 단축키를 적지 않는다 — Alt+F4 는 OS 가 창 닫기로 먼저 가로채는 조합이라 입력 맵이 처리하지 않는다.
             */
            inline static const CommandRow _s_arrCommandRow[] = {
                {                "scene.new",                                  &commandNewScene,               nullptr,         EditorCommandEdit::NoScene},
                {               "scene.open",                                 &commandOpenScene,               nullptr,         EditorCommandEdit::NoScene},
                {               "asset.save",      &EditorSceneFileCommands::saveFocusedOrScene,               nullptr,         EditorCommandEdit::NoScene},
                {          "scene.saveScene", &EditorSceneFileCommands::saveActiveSceneOrPrompt,               nullptr,         EditorCommandEdit::NoScene},
                {         "editor.quickOpen",                       &QuickLauncherPopup::toggle,               nullptr,         EditorCommandEdit::NoScene},
                {    "editor.commandPalette",                      &CommandPalettePopup::toggle,               nullptr,         EditorCommandEdit::NoScene},
                {              "editor.exit",             &EditorSceneFileCommands::requestExit,               nullptr,         EditorCommandEdit::NoScene},
                {                "edit.undo",                                      &commandUndo,               nullptr, EditorCommandEdit::SceneUnrecorded},
                {                "edit.redo",                                      &commandRedo,               nullptr, EditorCommandEdit::SceneUnrecorded},
                {     "editor.themeSettings",                   &EditorMenuBar::openThemeDialog,               nullptr,         EditorCommandEdit::NoScene},
                {        "build.compileGame",                               &commandCompileGame,       &isCompilerIdle,         EditorCommandEdit::NoScene},
                {      "build.compileEditor",                             &commandCompileEditor,       &isCompilerIdle,         EditorCommandEdit::NoScene},
                {         "build.compileAll",                                &commandCompileAll,       &isCompilerIdle,         EditorCommandEdit::NoScene},
                {             "build.cancel",                               &commandCancelBuild,       &isCompilerBusy,         EditorCommandEdit::NoScene},
                {               "play.start",                                      &commandPlay,               nullptr,         EditorCommandEdit::NoScene},
                {      "viewport.screenshot",     &EditorScreenshotCommands::captureFocusedView,               nullptr,         EditorCommandEdit::NoScene},
                {"viewport.renderDocCapture",                          &commandRenderDocCapture, &isRenderDocAvailable,         EditorCommandEdit::NoScene},
                {     "preset.loadComponent",                       &commandLoadComponentPreset,               nullptr, EditorCommandEdit::SceneUnrecorded}, // 파일 창을 연다 — 고르면 그때 기록한다
                {     "preset.saveComponent",                       &commandSaveComponentPreset,               nullptr,         EditorCommandEdit::NoScene},
            };

            /** @brief 씬을 바꾸는 명령의 표입니다. 한 번 실행이 되돌리기 한 단계다(`EditorCommandRegistry::executeDesc` 가 거래를 연다). */
            inline static const SceneCommandRow _s_arrSceneCommandRow[] = {
                {"clipboard.pasteComponentValues",     &commandPasteComponentValues,                    nullptr},
                { "clipboard.pasteComponentAsNew",      &commandPasteComponentAsNew,                    nullptr},
                {        "transform.snapToGround",             &commandSnapToGround,              &hasSelection},
                {              "transform.alignX",      &commandAlign<AlignAxis::X>,         &hasMultiSelection},
                {              "transform.alignY",      &commandAlign<AlignAxis::Y>,         &hasMultiSelection},
                {              "transform.alignZ",      &commandAlign<AlignAxis::Z>,         &hasMultiSelection},
                {         "transform.distributeX", &commandDistribute<AlignAxis::X>,         &hasMultiSelection},
                {         "transform.distributeY", &commandDistribute<AlignAxis::Y>,         &hasMultiSelection},
                {         "transform.distributeZ", &commandDistribute<AlignAxis::Z>,         &hasMultiSelection},
                {         "prefab.applyOverrides",     &commandApplyPrefabOverrides, &hasPrefabInstanceSelected}, // 프리팹 파일을 쓰고 놓인 인스턴스를 갱신 · 기록한다
            };

            // ------------------------------------------------------------------------------
            // 4) ImGui 연결 — 메뉴 항목. 단축키 글자는 에디터 입력 맵에서 만든다
            // ------------------------------------------------------------------------------
            /** @brief 커맨드 하나를 메뉴 항목으로 그립니다. 눌렸으면 커맨드를 실행합니다. */
            static void drawCommandItem( const EditorInputActions& inputActions, const EditorCommandDesc& desc )
            {
                fixed_string<constant::buffer::kMax128> menuLabel;
                EditorCommandRegistry::formatMenuLabel( desc, menuLabel );

                string shortcutLabel;
                inputActions.formatShortcutLabel( desc._inputAction, shortcutLabel );

                const utf8* pShortcut = shortcutLabel.empty() ? nullptr : shortcutLabel.c_str();
                const bool  bEnabled  = EditorCommandRegistry::isEnabled( desc, EditorCommandTarget::none() );

                if ( ImGui::MenuItem( menuLabel.c_str(), pShortcut, false, bEnabled ) )
                    (void)EditorCommandRegistry::executeDesc( desc, EditorCommandTarget::none() ); // 꺼진 항목은 그려질 때 이미 회색이다

                if ( desc._tooltip.empty() == false )
                {
                    fixed_string<constant::buffer::kMax256> tooltip;
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
    void EditorCommandGUI::registerDefaults()
    {
        EditorCommandGUIInternal::rebuild();
    }

    void EditorCommandGUI::syncWithRegistry()
    {
        const EditorCommandGUIInternal::GUIState& guiState     = getEditorState<EditorCommandGUIInternal::GUIState>();
        const uint32                              textRevision = EditorDataText::getRevision();
        const bool                                bSynced      = EditorExtensionRegistry<EditorCommandRegistration>::getList().getGeneration() == guiState._syncedGeneration && textRevision == guiState._syncedTextRevision;
        if ( bSynced )
            return;
        registerDefaults();
    }

    void EditorCommandGUIInternal::rebuild()
    {
        // 컨텍스트가 없으면 메뉴가 통째로 비고 단축키가 동작하지 않는다 — 조용히 반환하지 않고 필수 서비스 조회가 모든 구성에서 멈춘다.
        EditorCommandRegistry& registry = editor::getSubsystem<EditorContext>().getSubsystem<EditorCommandRegistry>();
        registry.clear();
        EditorCommandGUIInternal::GUIState& guiState = getEditorState<EditorCommandGUIInternal::GUIState>();
        guiState._syncedGeneration                   = EditorExtensionRegistry<EditorCommandRegistration>::getList().getGeneration();
        guiState._syncedTextRevision                 = EditorDataText::getRevision();

        // 표시 · 배치는 모듈마다 등록한 표시 파일이 정한다 — 다시 만들 때마다 읽는다(모듈 적재 · 해제로 파일 목록이 바뀐다).
        EditorCommandTextSet texts;
        texts.loadRegistered();
        for ( const EditorCommandGUIInternal::CommandRow& row : EditorCommandGUIInternal::_s_arrCommandRow )
        {
            EditorCommandDesc desc{};
            desc._id = row._pID;
            if ( texts.apply( desc ) == false )
                continue;
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
        for ( const EditorCommandGUIInternal::SceneCommandRow& row : EditorCommandGUIInternal::_s_arrSceneCommandRow )
        {
            EditorCommandDesc desc{};
            desc._id = row._pID;
            if ( texts.apply( desc ) == false )
                continue;
            desc._sceneAction = [pfnAction = row._pAction]( const EditorCommandTarget& /*target*/, EditorTransaction& transaction )
            { pfnAction( transaction ); };
            if ( row._pEnabled != nullptr )
                desc._enabledPredicate = [pfnEnabled = row._pEnabled]( const EditorCommandTarget& /*target*/ )
                { return pfnEnabled(); };
            desc._edit = EditorCommandEdit::Scene;

            registry.registerCommand( std::move( desc ) );
        }
        // 확장 모듈과 다른 파일의 등록 항목(SW_EDITOR_COMMAND)을 테이블과 같은 레지스트리에 합친다.
        EditorCommandTableUtil::appendRegistrations( registry, texts );
        registry.applyMenuLabels( texts );
        texts.reportUnused();

        string report;
        if ( registry.validate( report ) == false )
            SW_LOG_ERROR_ID( "Editor.EditorCommandTableInconsistent", "The editor command table is inconsistent:\n%#", report.c_str() );
        // 명령이 가리키는 액션이 입력 맵에 없으면 그 단축키는 조용히 없는 것이 된다 — 표 · 등록 줄과 editor.input.xml 이 어긋난 것이다.
        const InputMap& inputMap = editor::getSubsystem<EditorContext>().getSubsystem<EditorInputActions>().getInputMap();
        for ( const EditorCommandDesc& desc : registry.getCommands() )
        {
            if ( desc._inputAction.empty() == false && inputMap.hasAction( hashed_string( desc._inputAction.c_str() ) ) == false )
                SW_LOG_ERROR_ID( "Editor.CommandNamesInputAction", "Command '%#' names input action '%#' that the editor input map does not have", desc._id.c_str(), desc._inputAction.c_str() );
        }
        // 코드가 그리는 메뉴 경로에 테이블의 항목이 하나도 없으면 그 메뉴(툴바 팝업)가 비어 있는 채로 뜬다.
        for ( const utf8* pHostedPath : commandmenu::kArrHostedMenuPath )
        {
            if ( registry.findMenu( pHostedPath ) == nullptr )
                SW_LOG_ERROR_ID( "Editor.MenuPathDrawnCode", "Menu path '%#' is drawn by code but no command row uses it - the menu is empty", pHostedPath );
        }
    }

    void EditorCommandGUI::drawMainMenus()
    {
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();

        const EditorCommandRegistry& registry = editorContext.getSubsystem<EditorCommandRegistry>();
        for ( const EditorMenu& menu : registry.getMenus() )
        {
            if ( menu._parentPath != commandmenu::kMainMenuBar )
                continue;
            fixed_string<constant::buffer::kMax128> menuLabel;
            menuLabel.append( menu._label.c_str() );
            menuLabel.append( "###" ); // 보이는 이름은 언어마다 다르다 — ImGui ID 는 경로 마디로 고정한다
            menuLabel.append( menu._name.c_str() );
            if ( ImGui::BeginMenu( menuLabel.c_str() ) )
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

    bool EditorCommandGUI::drawToolbarButton( string_view commandID, const EditorCommandTarget& target )
    {
        return EditorCommandGUIInternal::drawCommandButton( commandID, target, true );
    }

    bool EditorCommandGUI::drawCommandButton( string_view commandID, const EditorCommandTarget& target )
    {
        return EditorCommandGUIInternal::drawCommandButton( commandID, target, false );
    }
} // namespace sw::editor

namespace sw::editor
{
    bool EditorCommandGUIInternal::drawCommandButton( string_view commandID, const EditorCommandTarget& target, bool bIconOnly )
    {
        EditorContext&               editorContext = editor::getSubsystem<EditorContext>();
        const EditorCommandRegistry& registry      = editorContext.getSubsystem<EditorCommandRegistry>();
        const EditorCommandDesc*     pDesc         = registry.find( commandID );
        if ( pDesc == nullptr )
            SW_FATAL( "Toolbar button names command '%#' which is not registered", string( commandID ).c_str() );
        EditorInputActions& inputActions = editorContext.getSubsystem<EditorInputActions>();

        const bool bEnabled = EditorCommandRegistry::isEnabled( *pDesc, target );
        ImGui::PushID( pDesc->_id.c_str() );
        ImGui::BeginDisabled( bEnabled == false );
        bool bPressed = false;
        if ( bIconOnly && pDesc->_iconName.empty() == false )
        {
            // 도구줄 아이콘은 상자 없이 둔다(drawIconToggle 과 같은 모양) — 정사각 한 변은 줄 높이라 글꼴 · DPI 를 따른다.
            const float32 side = ImGui::GetFrameHeight();
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4{ 0.0f, 0.0f, 0.0f, 0.0f } );
            ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.0f );
            bPressed = ImGui::Button( EditorIconGlyphTable::findGlyphByName( pDesc->_iconName.c_str() ), ImVec2{ side, side } );
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        else
        {
            fixed_string<constant::buffer::kMax128> label;
            EditorCommandRegistry::formatMenuLabel( *pDesc, label ); // 아이콘 + 이름표(메뉴와 같은 글)
            bPressed = ImGui::Button( label.c_str() );
        }
        ImGui::EndDisabled();
        ImGui::PopID();

        fixed_string<constant::buffer::kMax128> markKey{ "toolbar." };
        markKey.append( pDesc->_id.c_str() );
        EditorSelfTestMarks::record( markKey.c_str() );

        if ( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled ) )
        {
            string shortcutLabel;
            inputActions.formatShortcutLabel( pDesc->_inputAction, shortcutLabel );
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos( ImGui::GetFontSize() * SW_THEME_METRIC( "Layout", "TooltipWrapEm" ) );
            ImGui::TextUnformatted( pDesc->_label.c_str() );
            if ( shortcutLabel.empty() == false )
            {
                ImGui::SameLine();
                ImGui::TextDisabled( "%s", shortcutLabel.c_str() );
            }
            if ( pDesc->_tooltip.empty() == false )
                ImGui::TextDisabled( "%s", pDesc->_tooltip.c_str() );
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        if ( bPressed )
            inputActions.queueCommand( pDesc->_id, target );
        return bPressed;
    }
} // namespace sw::editor
