#include "pch.h"

#include "Editor/Common/Asset/EditorAssetValidation.h"
#include "Editor/Common/Commands/Play/EditorScreenshotCommands.h"
#include "Editor/Common/EditorProfile.h"
#include "Editor/Common/GUI/EditorDockLayout.h"
#include "Editor/Common/GUI/EditorMenuBar.h"
#include "Editor/Common/GUI/EditorNotificationView.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/Input/EditorInputActions.h"
#include "Editor/Common/Input/EditorPopupManager.h"
#include "Editor/Common/SourceControl/EditorSourceControl.h"
#include "Editor/Common/Workspace/AssetHotReload.h"
#include "Editor/Common/Workspace/ConfigHotReload.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorFrameStep.h"
#include "Editor/Common/Workspace/EditorNotificationManager.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWindowTitle.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/SelfTest/EditorSelfTest.h"

#include "Engine/Automation/AutomationStepRegistry.h"
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
        struct EditorFrameStepsInternal
        {
            /// @brief 알림 뷰가 메인 뷰포트를 모를 때 쓰는 화면 크기입니다.
            static constexpr float32 kFallbackScreenWidth  = 1920.0f;
            static constexpr float32 kFallbackScreenHeight = 1080.0f;

            /** @brief 1) 단축키, 메뉴가 미룬 패널 열기 요청, 씬 세션(세대 동기화 · 미저장 확인)을 처리합니다. */
            static void runCommands( EditorContext& context )
            {
                SW_EDITOR_PROFILE_SCOPE( "GT.Editor.step.commands" );
                context.getSubsystem<EditorInputActions>().processFrame( GameTimeScale::getUnscaledDeltaTime() );
                EditorMenuBar::processOpenPanelRequests();
                EditorMenuBar::processSceneSession();
            }

            /** @brief 2) 에셋 · 설정 파일 감시, 저장 직후 검증, 버전 관리 상태를 갱신합니다. 파일 감시는 에디터 프레임에서만 돈다. */
            static void runAssetReload( EditorContext& context )
            {
                SW_EDITOR_PROFILE_SCOPE( "GT.Editor.step.assetReload" );
                context.getSubsystem<AssetHotReload>().update();
                context.getSubsystem<ConfigHotReload>().update();
                context.getSubsystem<EditorAssetValidation>().update();
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
                EditorNotificationView::draw( notificationManager, kFallbackScreenWidth, kFallbackScreenHeight );
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
                const string              scenePath     = pScene == nullptr ? string{} : ( pScene->getSourcePath().empty() ? pScene->getName() : pScene->getSourcePath() );
                const bool                bDirty        = context.getSubsystem<EditorWorkspace>().isSceneDirty() || context.getSubsystem<EditorPanelManager>().countDirtyDocuments() > 0;
                const utf8*               pPlayState{ nullptr };
                if ( EditorPlaySession::isStopped() == false )
                    pPlayState = EditorPlaySession::isSimulating() ? "Simulating" : ( EditorPlaySession::isPaused() ? "Paused" : "Playing" );
                const string title = EditorWindowTitleUtil::makeTitle( pPackSettings->_windowTitle, scenePath, bDirty, pPlayState );
                pWindowHost->setBaseTitle( title ); // 창에 거는 일은 엔진이 한다 — 시나리오 실행 정보를 이 제목 뒤에 붙여 건다(AutomationRunTitleTracker)
            }

            /** @brief 6) 끝난 스크린샷 요청의 토스트를 띄웁니다. */
            static void runScreenshot( EditorContext& /*context*/ )
            {
                SW_EDITOR_PROFILE_SCOPE( "GT.Editor.step.screenshot" );
                EditorScreenshotCommands::update();
            }

            /** @brief 시계 잡기 조건 — 저장 검증(2 단계가 돌린다)이 아직 로그로 남기지 않은 결과가 있으면 참입니다. 에디터가 없으면 잡지 않습니다. */
            static bool isAssetValidationBusy()
            {
                EditorContext*               pContext    = editor::findSubsystem<EditorContext>();
                const EditorAssetValidation* pValidation = pContext != nullptr ? pContext->findSubsystem<EditorAssetValidation>() : nullptr;
                return pValidation != nullptr && pValidation->isBusy();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    SW_EDITOR_FRAME_STEP( Commands, "commands", EditorFrameStepOrder::kCommands, &EditorFrameStepsInternal::runCommands );
    SW_EDITOR_FRAME_STEP( AssetReload, "assetReload", EditorFrameStepOrder::kAssetReload, &EditorFrameStepsInternal::runAssetReload );
    SW_EDITOR_FRAME_STEP( Panels, "panels", EditorFrameStepOrder::kPanels, &EditorFrameStepsInternal::runPanels );
    SW_EDITOR_FRAME_STEP( SelfTest, "selfTest", EditorFrameStepOrder::kSelfTest, &EditorFrameStepsInternal::runSelfTest );
    SW_EDITOR_FRAME_STEP( WindowTitle, "windowTitle", EditorFrameStepOrder::kWindowTitle, &EditorFrameStepsInternal::runWindowTitle );
    SW_EDITOR_FRAME_STEP( Screenshot, "screenshot", EditorFrameStepOrder::kScreenshot, &EditorFrameStepsInternal::runScreenshot );
    // 검증은 저장 뒤 파일을 늦게 읽는다 — 자동화 시계가 앞서 가면 뒤 단계(임시 폴더 지우기)가 바꾼 트리를 읽고 결과가 다음 시나리오 창에 떨어진다.
    SW_AUTOMATION_CLOCK_HOLDER( editorAssetValidation, "EditorAssetValidation", &EditorFrameStepsInternal::isAssetValidationBusy );
} // namespace sw::editor
