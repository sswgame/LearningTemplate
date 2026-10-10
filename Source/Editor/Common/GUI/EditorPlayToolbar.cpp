#include "pch.h"

#include "Editor/Common/GUI/EditorPlayToolbar.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/GUI/EditorIconGlyphs.h"
#include "Editor/Common/GUI/EditorThemeUtil.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorCamera.h"

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

            /** @brief Play/Sim/Pause/Step/Stop 단추를 그립니다. */
            static void drawTransportControls()
            {
                const PlaySessionState currentState = EditorPlaySession::getState();
                EditorContext*         pContext     = EditorContext::get();
                const bool             bSceneDirty  = ( pContext != nullptr && pContext->getWorkspace().isSceneDirty() );
                const bool             bSimulating  = EditorPlaySession::isSimulating();

                if ( EditorPlaySession::isPlayQueued() )
                {
                    EditorWidgets::drawChip( "Starting", editor::style::kWarn );
                    EditorWidgets::drawTooltip( "씬을 여는 중 — 로드가 끝나면 플레이를 시작합니다 (Stop 으로 취소)" );
                    ImGui::SameLine();
                }
                if ( currentState == PlaySessionState::Playing && bSimulating == false )
                {
                    EditorWidgets::drawChip( "Playing", editor::style::kOk );
                    EditorWidgets::drawTooltip( "현재 게임 실행 중 — 게임 뷰가 게임 카메라를 그리고, 씬 뷰는 에디터 카메라로 둘러볼 수 있습니다" );
                }
                else
                {
                    if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPlay, "Play" ) ) )
                        requestSession( PendingSession::Play, bSceneDirty );
                    EditorSelfTestMarks::note( "toolbar.play" );
                    EditorWidgets::drawTooltip( "게임 플레이 모드를 시작합니다 (게임 뷰 입력 및 플레이어 컨트롤 활성화)" );
                }

                ImGui::SameLine();
                if ( currentState == PlaySessionState::Playing && bSimulating )
                {
                    EditorWidgets::drawChip( "Simulating", editor::style::kOk );
                    EditorWidgets::drawTooltip( "월드만 도는 중 — 게임 모듈 업데이트 · 게임 입력 없음" );
                }
                else
                {
                    if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kGlobe, "Simulate" ) ) )
                        requestSession( PendingSession::Simulate, bSceneDirty );
                    EditorSelfTestMarks::note( "toolbar.simulate" );
                    EditorWidgets::drawTooltip( "시뮬레이션 모드를 시작합니다 (씬만 틱 — 게임 모듈 업데이트 · 게임 입력 없음)" );
                }

                ImGui::SameLine();
                if ( currentState == PlaySessionState::Paused )
                {
                    EditorWidgets::drawChip( "Paused", editor::style::kWarn );
                    EditorWidgets::drawTooltip( "게임 일시 정지됨" );
                }
                else
                {
                    if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPause, "Pause" ) ) )
                        EditorPlaySession::pause();
                    EditorSelfTestMarks::note( "toolbar.pause" );
                    EditorWidgets::drawTooltip( "게임 실행을 일시 정지합니다" );
                }

                ImGui::SameLine();
                if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kStepForward, "Step" ) ) )
                    EditorPlaySession::stepOnce();
                EditorWidgets::drawTooltip( "게임을 정확히 1프레임 전진시킵니다" );

                ImGui::SameLine();
                ImGui::SetNextItemWidth( ImGui::GetFontSize() * 3.0f );
                ImGui::InputInt( "##StepFrameCount", &_s_stepFrameCount, 0, 0 );
                _s_stepFrameCount = MathUtil::clamp( _s_stepFrameCount, 1, static_cast<int32>( EditorPlaySession::kMaxStepFrameCount ) );
                EditorWidgets::drawTooltip( "'Step N' 이 진행할 프레임 수" );
                ImGui::SameLine();
                if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kStepForward, "Step N" ) ) )
                    EditorPlaySession::stepFrames( static_cast<uint32>( _s_stepFrameCount ) );
                EditorWidgets::drawTooltip( "왼쪽 칸의 프레임 수만큼 진행한 뒤 일시 정지합니다" );

                ImGui::SameLine();
                if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kStop, "Stop" ) ) )
                {
                    EditorPlaySession::stop();
                    GameObjectManager* pObjectManager = editor::getActiveObjectManager();
                    if ( pObjectManager != nullptr && pContext != nullptr )
                        pContext->getWorkspace().remapSelectionByObjectName( pObjectManager );
                }
                EditorSelfTestMarks::note( "toolbar.stop" );
                EditorWidgets::drawTooltip( "게임을 중지하고 초기 씬 상태로 복원합니다" );
            }

            /** @brief 카메라에서 시작 · 시간 배율을 그립니다. */
            static void drawSessionOptions()
            {
                ImGui::Checkbox( "Cam", &_s_bStartAtCamera );
                EditorWidgets::drawTooltip( "Play 를 에디터 카메라 위치에서 시작합니다 ('Player' 태그 오브젝트, 없으면 게임 카메라를 든 오브젝트를 옮깁니다)" );

                ImGui::SameLine();
                float32 timeScale = GameTimeScale::get();
                ImGui::SetNextItemWidth( ImGui::GetFontSize() * 5.0f );
                if ( ImGui::DragFloat( "##TimeScale", &timeScale, 0.01f, GameTimeScale::kMinScale, GameTimeScale::kMaxScale, "x%.2f" ) )
                    GameTimeScale::set( timeScale );
                if ( ImGui::IsItemClicked( ImGuiMouseButton_Right ) )
                    GameTimeScale::set( 1.0f );
                EditorWidgets::drawTooltip( "게임 시간 배율 (gv_timeScale) — 끌어서 바꾸고, 오른쪽 클릭으로 x1.00 으로 되돌립니다" );
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
                fixed_string<constant::kMaxBuffer256> tooltip;
                formatstring( tooltip.data(), tooltip.capacity(), "%s autoplay: %s (%s, console: autoplay on|off)", pActive->_pGameName, pActive->_pDescription,
                              pActive->_pVariableName );
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
                    ImGui::SetTooltip( "%s", bAvailable ? "다음 프레임을 RenderDoc 으로 캡처합니다 (Saved/RenderDoc/, Ctrl+F12)"
                                                        : "Start with -renderdoc or from RenderDoc — RenderDoc 이 이 프로세스에 붙어 있지 않습니다" );
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
