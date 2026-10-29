#include "pch.h"

#include "Editor/Common/Backend/FileDialog.h"
#include "Editor/Common/Commands/Play/EditorScreenshotCommands.h"
#include "Editor/Common/EditorProfile.h"
#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/GUI/EditorMenuBar.h"
#include "Editor/Common/GUI/Panel/EditorDockLayout.h"
#include "Editor/Common/GUI/Panel/EditorPanelManager.h"
#include "Editor/Common/GUI/Status/EditorNotificationView.h"
#include "Editor/Common/Input/EditorInputActions.h"
#include "Editor/Common/Input/EditorPopupManager.h"
#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorExtensionSubsystems.h"
#include "Editor/Common/Workspace/EditorFrameStep.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWindowTitle.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/HotReload/AssetHotReload.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "Engine/Automation/Scenario/AutomationStepRegistry.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Window/IWindow.h"
#include "Engine/Window/MainWindowSubsystem.h"

#include <imgui.h>

// 셸이 아는 UI 프레임 단계입니다. 순서 값과 그 뜻은 `EditorFrameStepOrder`(EditorFrameStep.h)에 있습니다.
// 단계마다 계측 구간(`GT.Editor.step.*`)을 두어, 셸의 `GT.Editor.frameSteps` 시간이 어느 단계에 쓰였는지 나눠 봅니다.
namespace sw::editor
{
    namespace
    {
        /**
         * @class EditorWindowTitleState
         * @brief 창 제목을 마지막으로 지은 입력과 결과입니다(`runWindowTitle` 이 입력이 바뀐 프레임에만 다시 짓는다).
         * @details 에디터 서브시스템이라 에디터 컨텍스트와 함께 생기고 사라진다 — 정적 칸에 두면 모듈을 다시 올리거나 컨텍스트를 새로 만들 때 지난 값이 남는다.
         */
        class EditorWindowTitleState final : public EditorSubsystem
        {
        public:
            static constexpr const utf8* kSubsystemName = "EditorWindowTitleState";

            string      _scenePath{};
            string      _gameTitle{};
            string      _title{};
            const utf8* _pPlayState{ nullptr };
            bool        _bDirty{ false };
            bool        _bBuilt{ false }; ///< 한 번이라도 지었는지(첫 프레임은 입력이 기본값과 같아도 짓는다)

            const utf8* getSubsystemName() const override { return kSubsystemName; }
        };

        struct EditorFrameStepsInternal
        {
            /** @brief 1) 파일 대화상자 결과, 단축키, 메뉴가 미룬 패널 열기 요청, 씬 세션(세대 동기화 · 미저장 확인)을 처리합니다. */
            static void runCommands( EditorContext& context )
            {
                SW_EDITOR_PROFILE_SCOPE( "GT.Editor.step.commands" );
                // 대화상자는 별도 스레드가 띄우고 결과만 큐에 담는다 — 여기(메인 스레드)서 넘긴다.
                FileDialog::pumpResults();
                context.getSubsystem<EditorInputActions>().processFrame( editor::getSubsystem<GameTimeScale>().getUnscaledDeltaTime() );
                EditorMenuBar::processOpenPanelRequests();
                EditorMenuBar::processSceneSession();
            }

            /** @brief 2) 에셋 · 설정 파일 감시, 버전 관리 상태를 갱신합니다. 파일 감시는 에디터 프레임에서만 돈다. */
            static void runAssetReload( EditorContext& context )
            {
                SW_EDITOR_PROFILE_SCOPE( "GT.Editor.step.assetReload" );
                context.getSubsystem<AssetHotReload>().update();
                context.getSubsystem<EditorSourceControl>().update();
            }

            /** @brief 3) 열린 패널, 팝업, 알림을 그립니다. */
            static void runPanels( EditorContext& context )
            {
                SW_EDITOR_PROFILE_SCOPE( "GT.Editor.step.panels" );
                context.getSubsystem<EditorPanelManager>().drawOpenPanels();
                // 패널이 처음 나타나며 가져간 포커스보다 나중에 실행해야 한다
                context.getSubsystem<EditorDockLayout>().updateDefaultTabSelection(); // 필수 - 아래 패널 관리자와 같은 컨텍스트 서브시스템이다
                context.getSubsystem<EditorPopupManager>().drawOpenPopups();
                EditorNotificationManager& notificationManager = context.getSubsystem<EditorNotificationManager>();
                notificationManager.update( ImGui::GetIO().DeltaTime );
                EditorNotificationView::draw( notificationManager );
            }

            /** @brief 4) 자체 테스트(-gv_editorSelfTest, Test Runner 창)의 한 단계를 돌립니다. 실행이 없으면 아무것도 하지 않는다. */
            static void runSelfTest( EditorContext& /*context*/ )
            {
                SW_EDITOR_PROFILE_SCOPE( "GT.Editor.step.selfTest" );
                EditorSelfTestRunner::runFrame();
            }

            /** @brief 5) 창 제목에 게임, 씬, 미저장, 플레이 상태를 반영합니다. 여기서는 평소 제목만 정하고(`MainWindowSubsystem::setBaseTitle`), 창에 거는 것은 엔진이 프레임 끝에 한다. */
            static void runWindowTitle( EditorContext& context )
            {
                SW_EDITOR_PROFILE_SCOPE( "GT.Editor.step.windowTitle" );
                // 선택(②): 창 호스트 · 창은 헤드리스 실행에 없다. 없으면 제목을 쓸 곳이 없다.
                MainWindowSubsystem* pWindowHost = editor::findSubsystem<MainWindowSubsystem>();
                IWindow*             pWindow     = pWindowHost != nullptr ? pWindowHost->getWindow() : nullptr;
                if ( pWindow == nullptr )
                    return;
                const PackEngineSettings* pPackSettings = &editor::getSubsystem<const PackEngineSettings>(); // 필수 - 창이 있으면 팩 설정도 있다
                const Scene*              pScene        = editor::getActiveScene();
                static const string       s_emptyPath{};
                const string&             scenePath = pScene == nullptr ? s_emptyPath : ( pScene->getSourcePath().empty() ? pScene->getName() : pScene->getSourcePath() );
                const bool                bDirty    = context.getSubsystem<EditorWorkspace>().isSceneDirty() || context.getSubsystem<EditorPanelManager>().countDirtyDocuments() > 0;
                const utf8*               pPlayState{ nullptr };
                if ( EditorPlaySession::isStopped() == false )
                    pPlayState = EditorPlaySession::isSimulating() ? "Simulating" : ( EditorPlaySession::isPaused() ? "Paused" : "Playing" );
                // 입력이 같고 창의 평소 제목도 그대로면(셸 종료가 되돌리지 않았다) 문자열을 다시 짓지 않는다.
                EditorWindowTitleState& state      = editor::getSubsystem<EditorWindowTitleState>(); // 필수 - 이 파일의 SW_EDITOR_SUBSYSTEM 이 컨텍스트와 함께 만든다
                const bool              bUnchanged = state._bBuilt && state._pPlayState == pPlayState && state._bDirty == bDirty && state._scenePath == scenePath &&
                                        state._gameTitle == pPackSettings->_windowTitle && pWindowHost->getBaseTitle() == state._title;
                if ( bUnchanged )
                    return;
                state._scenePath  = scenePath;
                state._gameTitle  = pPackSettings->_windowTitle;
                state._pPlayState = pPlayState;
                state._bDirty     = bDirty;
                state._bBuilt     = true;
                state._title      = EditorWindowTitleUtil::makeTitle( pPackSettings->_windowTitle, scenePath, bDirty, pPlayState );
                pWindowHost->setBaseTitle( state._title ); // 창에 거는 일은 엔진이 한다 — 시나리오 실행 정보를 이 제목 뒤에 붙여 건다(AutomationRunTitleTracker)
            }

            /** @brief 6) 끝난 스크린샷 요청의 토스트를 띄웁니다. */
            static void runScreenshot( EditorContext& /*context*/ )
            {
                SW_EDITOR_PROFILE_SCOPE( "GT.Editor.step.screenshot" );
                EditorScreenshotCommands::update();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_SUBSYSTEM( EditorWindowTitleState, 9600 );

    SW_EDITOR_FRAME_STEP( Commands, "commands", EditorFrameStepOrder::kCommands, &EditorFrameStepsInternal::runCommands );
    SW_EDITOR_FRAME_STEP( AssetReload, "assetReload", EditorFrameStepOrder::kAssetReload, &EditorFrameStepsInternal::runAssetReload );
    SW_EDITOR_FRAME_STEP( Panels, "panels", EditorFrameStepOrder::kPanels, &EditorFrameStepsInternal::runPanels );
    SW_EDITOR_FRAME_STEP( SelfTest, "selfTest", EditorFrameStepOrder::kSelfTest, &EditorFrameStepsInternal::runSelfTest );
    SW_EDITOR_FRAME_STEP( WindowTitle, "windowTitle", EditorFrameStepOrder::kWindowTitle, &EditorFrameStepsInternal::runWindowTitle );
    SW_EDITOR_FRAME_STEP( Screenshot, "screenshot", EditorFrameStepOrder::kScreenshot, &EditorFrameStepsInternal::runScreenshot );
} // namespace sw::editor
