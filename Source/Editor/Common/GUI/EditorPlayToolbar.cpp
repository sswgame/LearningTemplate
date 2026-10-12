#include "pch.h"

#include "Editor/Common/GUI/EditorPlayToolbar.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorNetModeCombo.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorNetPlaySession.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Renderer/Capture/RenderDocCapture.h"
#include "Engine/Utility/GameAutoplay.h"
#include "Engine/Utility/GameTimeScale.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace sw::editor
{
    namespace
    {
        struct EditorPlayToolbarInternal
        {
            /** @brief 플레이를 시작하는 단추의 종류입니다. 미저장 확인 모달이 어느 쪽을 이어 갈지 기억합니다. */
            enum class PendingSession : uint8
            {
                Play = 0,
                Simulate
            };

            static constexpr const utf8* kUnsavedPopupID = "##UnsavedScenePlay";

            inline static int32          _s_stepFrameCount      = 10; ///< "Step N" 이 진행할 프레임 수
            inline static PendingSession _s_pendingSession      = PendingSession::Play;
            inline static bool           _s_bConfirmUnsavedPlay = false;
            inline static bool           _s_bStartAtCamera      = false; ///< Play 를 에디터 카메라 위치에서 시작한다
            inline static bool           _s_bAutoplayDrawn      = false; ///< 마지막 프레임에 자동 플레이 단추를 그렸다

            /** @brief 세션을 시작합니다. 카메라에서 시작이 켜져 있으면 에디터 카메라 위치를 시작 위치로 넘깁니다. */
            static void startSession( PendingSession session )
            {
                // 카메라에서 시작은 플레이어가 조종하는 세션만 쓴다(Simulate 는 옮길 플레이어가 없다). 멈춤에서 시작할 때만 정한다.
                PlaySessionData* pData = EditorPlaySession::findData();
                if ( pData != nullptr && EditorPlaySession::isStopped() )
                {
                    const CameraComponent* pEditorCamera = EditorCamera::find( editor::getActiveScene() );
                    if ( _s_bStartAtCamera && pEditorCamera != nullptr )
                        EditorPlaySession::setStartPosition( *pData, pEditorCamera->getWorldPosition() );
                    else
                        EditorPlaySession::clearStartPosition( *pData );
                }
                if ( session == PendingSession::Simulate )
                    EditorPlaySession::simulate();
                else
                    EditorPlaySession::play();
            }

            /** @brief 미저장 씬이면 확인 모달을 열고, 아니면 바로 시작합니다. */
            static void requestSession( PendingSession session, bool bSceneDirty )
            {
                _s_pendingSession = session;
                if ( bSceneDirty && EditorPlaySession::isStopped() )
                    _s_bConfirmUnsavedPlay = true;
                else
                    startSession( session );
            }

            /**
             * @brief Net Mode 가 Standalone 이 아니면 멀티 프로세스 재생의 Play · Stop 을 그리고 true 입니다(에디터 안 재생 단추는 그리지 않는다).
             * @details 언리얼 PIE 의 Net Mode 드롭다운과 같은 자리 — Play 가 서버 · 클라이언트 프로세스를 띄우고 Stop 이 모두 끈다.
             */
            static bool drawNetPlayControls( EditorContext& context )
            {
                EditorNetPlaySession& session = context.getNetPlaySession();
                (void)EditorNetModeCombo::draw( session ); // 바뀐 구성은 다음 Play 에 쓰인다
                // 툴바는 한 줄 높이다 — 뒤 단추(Standalone 이면 에디터 안 Play)가 다음 줄로 내려가면 잘려 눌리지 않는다.
                ImGui::SameLine();
                if ( session.getSettings()._mode == EditorNetMode::Standalone && session.isRunning() == false )
                    return false;
                if ( session.isRunning() == false )
                {
                    if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPlay, "Play" ) ) )
                        (void)session.start( session.getSettings() ); // 실패 이유는 세션이 로그에 남긴다
                    EditorSelfTestMarks::note( "toolbar.netPlay" );
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "NetPlayStart" ) );
                    return true;
                }
                fixed_string<constant::kMaxBuffer64> chip;
                formatstring( chip.data(), chip.capacity(), "Net %d/%d", session.getConnectedClientCount(), session.getProcessCount() - 1 );
                EditorWidgets::drawChip( chip.c_str(), editor::style::kOk );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "NetPlayClients" ) );
                ImGui::SameLine();
                if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kStop, "Stop" ) ) )
                    (void)session.stop(); // 합친 트레이스 경로는 로그에 남는다
                EditorSelfTestMarks::note( "toolbar.netStop" );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "NetPlayStop" ) );
                return true;
            }

            /** @brief Play/Sim/Pause/Step/Stop 단추를 그립니다. */
            static void drawTransportControls()
            {
                const PlaySessionState currentState = EditorPlaySession::getState();
                EditorContext*         pContext     = EditorContext::get();
                const bool             bSceneDirty  = ( pContext != nullptr && pContext->getWorkspace().isSceneDirty() );
                const bool             bSimulating  = EditorPlaySession::isSimulating();

                // Net Mode 고르기는 에디터 안 재생이 멈췄을 때만 — 재생 중에 바꾸면 Stop 단추가 사라진다.
                if ( pContext != nullptr && EditorPlaySession::isStopped() && drawNetPlayControls( *pContext ) )
                    return;
                if ( EditorPlaySession::isPlayQueued() )
                {
                    EditorWidgets::drawChip( "Starting", editor::style::kWarn );
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayStarting" ) );
                    ImGui::SameLine();
                }
                if ( currentState == PlaySessionState::Playing && bSimulating == false )
                {
                    EditorWidgets::drawChip( "Playing", editor::style::kOk );
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayRunning" ) );
                }
                else
                {
                    if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPlay, "Play" ) ) )
                        requestSession( PendingSession::Play, bSceneDirty );
                    EditorSelfTestMarks::note( "toolbar.play" );
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayStart" ) );
                }

                ImGui::SameLine();
                if ( currentState == PlaySessionState::Playing && bSimulating )
                {
                    EditorWidgets::drawChip( "Simulating", editor::style::kOk );
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "SimulateRunning" ) );
                }
                else
                {
                    if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kGlobe, "Simulate" ) ) )
                        requestSession( PendingSession::Simulate, bSceneDirty );
                    EditorSelfTestMarks::note( "toolbar.simulate" );
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "SimulateStart" ) );
                }

                ImGui::SameLine();
                if ( currentState == PlaySessionState::Paused )
                {
                    EditorWidgets::drawChip( "Paused", editor::style::kWarn );
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayPaused" ) );
                }
                else
                {
                    if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPause, "Pause" ) ) )
                        EditorPlaySession::pause();
                    EditorSelfTestMarks::note( "toolbar.pause" );
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayPause" ) );
                }

                ImGui::SameLine();
                if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kStepForward, "Step" ) ) )
                    EditorPlaySession::stepOnce();
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayStepOnce" ) );

                ImGui::SameLine();
                ImGui::SetNextItemWidth( ImGui::GetFontSize() * 3.0f );
                ImGui::InputInt( "##StepFrameCount", &_s_stepFrameCount, 0, 0 );
                _s_stepFrameCount = MathUtil::clamp( _s_stepFrameCount, 1, static_cast<int32>( EditorPlaySession::kMaxStepFrameCount ) );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayStepFrameCount" ) );
                ImGui::SameLine();
                if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kStepForward, "Step N" ) ) )
                    EditorPlaySession::stepFrames( static_cast<uint32>( _s_stepFrameCount ) );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayStepFrames" ) );

                ImGui::SameLine();
                if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kStop, "Stop" ) ) )
                {
                    EditorPlaySession::stop();
                    GameObjectManager* pObjectManager = editor::getActiveObjectManager();
                    if ( pObjectManager != nullptr && pContext != nullptr )
                        pContext->getWorkspace().remapSelectionByObjectName( pObjectManager );
                }
                EditorSelfTestMarks::note( "toolbar.stop" );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayStop" ) );
            }

            /** @brief 카메라에서 시작 · 시간 배율을 그립니다. */
            static void drawSessionOptions()
            {
                ImGui::Checkbox( "Cam", &_s_bStartAtCamera );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayStartAtCamera" ) );

                ImGui::SameLine();
                float32 timeScale = GameTimeScale::get();
                ImGui::SetNextItemWidth( ImGui::GetFontSize() * 5.0f );
                if ( ImGui::DragFloat( "##TimeScale", &timeScale, 0.01f, GameTimeScale::kMinScale, GameTimeScale::kMaxScale, "x%.2f" ) )
                    GameTimeScale::set( timeScale );
                if ( ImGui::IsItemClicked( ImGuiMouseButton_Right ) )
                    GameTimeScale::set( 1.0f );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayTimeScale" ) );
            }

            /** @brief 게임이 자동 플레이를 등록했으면(`SW_GAME_AUTOPLAY`) 켜고 끄는 단추를 그립니다. */
            static void drawAutoplayButton()
            {
                _s_bAutoplayDrawn                       = false;
                const GameAutoplayRegistration* pActive = GameAutoplay::findActive();
                if ( pActive == nullptr )
                    return;
                ImGui::SameLine();
                const bool bOn = GameAutoplay::isOn();
                if ( EditorWidgets::drawToggleButton( "Auto", bOn, editor::style::kOk ) )
                    (void)GameAutoplay::setOn( bOn == false ); // 등록은 위에서 확인했다
                const string tooltip = SW_LOCKEYFORMAT( "EdCommon", "AutoplayToggle", TextArgumentList().addText( "game", pActive->_pGameName ).addText( "description", pActive->_pDescription ).addText( "variable", pActive->_pVariableName ) );
                EditorWidgets::drawTooltip( tooltip.c_str() );
                _s_bAutoplayDrawn = true;
            }

            /**
             * @brief RenderDoc 캡처 단추를 그립니다 — 다음 프레임 전체(씬 뷰 · 게임 뷰 · 에디터 UI)를 캡처한다.
             * @details RenderDoc 이 붙어 있지 않으면 회색이고, 툴팁은 회색 단추 위에서도 이유를 보인다(`AllowWhenDisabled`).
             */
            static void drawRenderDocButton()
            {
                const bool bAvailable = RenderDocCapture::isAvailable();
                ImGui::BeginDisabled( bAvailable == false );
                if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kBug, "RenderDoc" ) ) )
                    RenderDocCapture::triggerCapture();
                ImGui::EndDisabled();
                EditorSelfTestMarks::note( "toolbar.renderDoc" );
                if ( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort ) )
                {
                    const utf8* pTooltip = SW_LOCKEY( "EdCommon", "RenderDocCapture" );
                    if ( bAvailable == false )
                        pTooltip = SW_LOCKEY( "EdCommon", "RenderDocUnavailable" );
                    ImGui::SetTooltip( "%s", pTooltip );
                }
            }

            /** @brief 미저장 씬에서 Play 를 눌렀을 때의 확인 모달입니다. */
            static void drawUnsavedPlayPopup()
            {
                if ( _s_bConfirmUnsavedPlay )
                {
                    ImGui::OpenPopup( kUnsavedPopupID );
                    _s_bConfirmUnsavedPlay = false;
                }
                if ( ImGui::BeginPopupModal( kUnsavedPopupID, nullptr, ImGuiWindowFlags_AlwaysAutoResize ) == false )
                    return;
                ImGui::TextUnformatted( "Scene has unsaved changes. Play anyway?" );
                if ( ImGui::Button( "Play" ) )
                {
                    startSession( _s_pendingSession );
                    ImGui::CloseCurrentPopup();
                }
                EditorSelfTestMarks::note( "toolbar.playAnyway" );
                ImGui::SameLine();
                if ( ImGui::Button( "Cancel" ) )
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    void EditorPlayToolbar::draw()
    {
        // 멀티 프로세스 재생의 로그 · 종료 알림은 툴바가 프레임마다 모은다(Multiplayer 창을 닫아도).
        EditorContext* pContext = EditorContext::get();
        if ( pContext != nullptr )
            pContext->getNetPlaySession().update();

        ImGuiViewport*             pViewport = ImGui::GetMainViewport();
        const ImGuiStyle&          style     = ImGui::GetStyle();
        const float32              height    = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
        constexpr ImGuiWindowFlags kFlags    = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
        if ( ImGui::BeginViewportSideBar( "##EditorPlayToolbar", pViewport, ImGuiDir_Up, height, kFlags ) )
        {
            EditorPlayToolbarInternal::drawTransportControls();
            EditorWidgets::drawToolbarSeparator();
            EditorPlayToolbarInternal::drawSessionOptions();
            EditorPlayToolbarInternal::drawAutoplayButton();
            EditorWidgets::drawToolbarSeparator();
            EditorPlayToolbarInternal::drawRenderDocButton();
        }
        ImGui::End();
        EditorPlayToolbarInternal::drawUnsavedPlayPopup();
    }

    bool EditorPlayToolbar::wasAutoplayButtonDrawn()
    {
        return EditorPlayToolbarInternal::_s_bAutoplayDrawn;
    }
} // namespace sw::editor
