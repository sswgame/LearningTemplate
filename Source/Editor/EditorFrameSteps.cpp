#include "pch.h"

#include "Editor/Common/Asset/EditorAssetValidation.h"
#include "Editor/Common/Commands/Play/EditorScreenshotCommands.h"
#include "Editor/Common/EditorProfile.h"
#include "Editor/Common/GUI/EditorCommandGUI.h"
#include "Editor/Common/GUI/EditorDockLayout.h"
#include "Editor/Common/GUI/EditorMenuBar.h"
#include "Editor/Common/GUI/EditorNotificationView.h"
#include "Editor/Common/GUI/EditorPanelManager.h"
#include "Editor/Common/GUI/EditorPopupManager.h"
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

#include "Engine/Config/PackEngineSettings.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Window/IWindow.h"

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
            static void runCommands( EditorContext& /*context*/ )
            {
                SW_EDITOR_PROFILE_SCOPE( "GT.Editor.step.commands" );
                EditorCommandGUI::processHotkeys();
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
                EditorDockLayout* pDockLayout = context.findSubsystem<EditorDockLayout>();
                if ( pDockLayout != nullptr )
                    pDockLayout->updateDefaultTabSelection();
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

            /** @brief 5) 창 제목에 게임, 씬, 미저장, 플레이 상태를 반영합니다. 같은 제목이면 `IWindow::setTitle` 이 바로 반환하므로 프레임마다 불러도 됩니다. */
            static void runWindowTitle( EditorContext& context )
            {
                SW_EDITOR_PROFILE_SCOPE( "GT.Editor.step.windowTitle" );
                IWindow*                  pWindow       = IWindow::getActiveWindow();
                const PackEngineSettings* pPackSettings = editor::findSubsystem<const PackEngineSettings>();
                if ( pWindow == nullptr || pPackSettings == nullptr )
                    return;
                const Scene* pScene    = editor::getActiveScene();
                const string scenePath = pScene == nullptr ? string{} : ( pScene->getSourcePath().empty() ? pScene->getName() : pScene->getSourcePath() );
                const bool   bDirty    = context.getSubsystem<EditorWorkspace>().isSceneDirty() || context.getSubsystem<EditorPanelManager>().countDirtyDocuments() > 0;
                const utf8*  pPlayState{ nullptr };
                if ( EditorPlaySession::isStopped() == false )
                    pPlayState = EditorPlaySession::isSimulating() ? "Simulating" : ( EditorPlaySession::isPaused() ? "Paused" : "Playing" );
                const string title = EditorWindowTitleUtil::makeTitle( pPackSettings->_windowTitle, scenePath, bDirty, pPlayState );
                pWindow->setTitle( title.c_str() );
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
    SW_EDITOR_FRAME_STEP( Commands, "commands", EditorFrameStepOrder::kCommands, &EditorFrameStepsInternal::runCommands );
    SW_EDITOR_FRAME_STEP( AssetReload, "assetReload", EditorFrameStepOrder::kAssetReload, &EditorFrameStepsInternal::runAssetReload );
    SW_EDITOR_FRAME_STEP( Panels, "panels", EditorFrameStepOrder::kPanels, &EditorFrameStepsInternal::runPanels );
    SW_EDITOR_FRAME_STEP( SelfTest, "selfTest", EditorFrameStepOrder::kSelfTest, &EditorFrameStepsInternal::runSelfTest );
    SW_EDITOR_FRAME_STEP( WindowTitle, "windowTitle", EditorFrameStepOrder::kWindowTitle, &EditorFrameStepsInternal::runWindowTitle );
    SW_EDITOR_FRAME_STEP( Screenshot, "screenshot", EditorFrameStepOrder::kScreenshot, &EditorFrameStepsInternal::runScreenshot );
} // namespace sw::editor
