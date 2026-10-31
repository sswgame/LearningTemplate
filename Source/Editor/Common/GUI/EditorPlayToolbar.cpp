#include "pch.h"

#include "Editor/Common/GUI/EditorPlayToolbar.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "DevTools/RenderDoc/RenderDocCapture.h"

#include "Editor/Common/GUI/Theme/EditorIconGlyphs.h"
#include "Editor/Common/GUI/Theme/EditorThemeUtil.h"
#include "Editor/Common/Input/EditorInputGUI.h"
#include "Editor/Common/Widgets/EditorNetModeCombo.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Workspace/EditorCamera.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorStateStore.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Common/Workspace/NetPlay/EditorNetPlaySession.h"
#include "Editor/Common/Workspace/Play/EditorPlaySession.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"

#include "Engine/Config/GameAutoplay.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Localization/LocText.h"
#include "Engine/Localization/TextFormatter.h"
#include "Engine/Object/Component/CameraComponent.h"

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

            /** @brief 재생 단추 뒤의 묶음입니다. 줄에 들지 않는 묶음부터 끝까지 넘침 펼침으로 옮긴다. */
            enum class Section : uint8
            {
                SessionOptions = 0, ///< 카메라에서 시작 · 시간 배율 · 자동 플레이
                RenderDoc,
                Count
            };

            static constexpr const utf8* kUnsavedPopupID  = "##UnsavedScenePlay";
            static constexpr const utf8* kOverflowPopupID = "##EditorPlayToolbarOverflow";

            /** @brief 도구줄이 프레임 사이에 기억하는 값입니다. 에디터 범위의 상태 저장소가 소유한다. */
            struct State
            {
                int32          _stepFrameCount{ 10 }; ///< "Step N" 이 진행할 프레임 수
                PendingSession _pendingSession{ PendingSession::Play };
                bool           _bConfirmUnsavedPlay{ false };
                bool           _bStartAtCamera{ false };                                  ///< Play 를 에디터 카메라 위치에서 시작한다
                bool           _bAutoplayOffered{ false };                                ///< 마지막 프레임에 자동 플레이 단추를 내놓았다(줄에 그렸거나 ">>" 펼침 안에 있다)
                float32        _arrSectionWidth[static_cast<size_t>( Section::Count )]{}; ///< 묶음마다 마지막으로 그린 폭(넘침 판정)
            };

            static State& getState() { return getEditorState<State>(); }

            /** @brief 세션을 시작합니다. 카메라에서 시작이 켜져 있으면 에디터 카메라 위치를 시작 위치로 넘깁니다. */
            static void startSession( PendingSession session )
            {
                // 카메라에서 시작은 플레이어가 조종하는 세션만 쓴다(Simulate 는 옮길 플레이어가 없다). 멈춤에서 시작할 때만 정한다.
                PlaySessionData* pData = EditorPlaySession::findData();
                if ( pData != nullptr && EditorPlaySession::isStopped() )
                {
                    const CameraComponent* pEditorCamera = EditorCamera::find( editor::getActiveScene() );
                    if ( getState()._bStartAtCamera && pEditorCamera != nullptr )
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
                getState()._pendingSession = session;
                if ( bSceneDirty && EditorPlaySession::isStopped() )
                    getState()._bConfirmUnsavedPlay = true;
                else
                    startSession( session );
            }

            /**
             * @brief Net Mode 가 Standalone 이 아니면 멀티 프로세스 재생의 Play · Stop 을 그리고 true 입니다(에디터 안 재생 단추는 그리지 않는다).
             * @details 언리얼 PIE 의 Net Mode 드롭다운에 해당한다 — Play 가 서버 · 클라이언트 프로세스를 띄우고 Stop 이 모두 끈다.
             */
            static bool drawNetPlayControls( EditorContext& context )
            {
                EditorNetPlaySession& session = context.getSubsystem<EditorNetPlaySession>();
                (void)EditorNetModeCombo::draw( session, "toolbar.netMode" ); // 바뀐 구성은 다음 Play 에 쓰인다
                // 툴바는 한 줄 높이다 — 뒤 단추(Standalone 이면 에디터 안 Play)가 다음 줄로 내려가면 잘려 눌리지 않는다.
                ImGui::SameLine();
                if ( session.getSettings()._mode == EditorNetMode::Standalone && session.isRunning() == false )
                    return false;
                if ( session.isRunning() == false )
                {
                    ImGui::BeginDisabled( session.isStopping() ); // 앞 세션을 거두는 동안은 `start` 가 거절한다
                    if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kPlay, "Play" ) ) )
                        (void)session.start( session.getSettings() ); // 실패 이유는 세션이 로그에 남긴다
                    ImGui::EndDisabled();
                    EditorSelfTestMarks::note( "toolbar.netPlay" );
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "NetPlayStart" ) );
                    return true;
                }
                fixed_string<constant::kMaxBuffer64> chip;
                formatstring( chip.data(), chip.capacity(), "Net %d/%d", session.getConnectedClientCount(), session.getProcessCount() - 1 );
                EditorWidgets::drawChip( chip.c_str(), EditorThemeUtil::getSuccessColor() );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "NetPlayClients" ) );
                ImGui::SameLine();
                if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kStop, "Stop" ) ) )
                    session.requestStop(); // 합친 트레이스는 Profiler 창의 프레임 단계가 Timeline 탭에 연다
                EditorSelfTestMarks::note( "toolbar.netStop" );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "NetPlayStop" ) );
                return true;
            }

            /** @brief Play/Sim/Pause/Step/Stop 단추를 그립니다. */
            static void drawTransportControls()
            {
                const PlaySessionState currentState  = EditorPlaySession::getState();
                EditorContext&         editorContext = editor::getSubsystem<EditorContext>();
                const bool             bSceneDirty   = ( editorContext.getSubsystem<EditorWorkspace>().isSceneDirty() );
                const bool             bSimulating   = EditorPlaySession::isSimulating();

                // Net Mode 고르기는 에디터 안 재생이 멈췄을 때만 — 재생 중에 바꾸면 Stop 단추가 사라진다.
                const bool bNetPlayControlsHandled = EditorPlaySession::isStopped() && drawNetPlayControls( editorContext );
                if ( bNetPlayControlsHandled )
                    return;
                if ( EditorPlaySession::isPlayQueued() )
                {
                    EditorWidgets::drawChip( "Starting", EditorThemeUtil::getWarningColor() );
                    EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayStarting" ) );
                    ImGui::SameLine();
                }
                if ( currentState == PlaySessionState::Playing && bSimulating == false )
                {
                    EditorWidgets::drawChip( "Playing", EditorThemeUtil::getSuccessColor() );
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
                    EditorWidgets::drawChip( "Simulating", EditorThemeUtil::getSuccessColor() );
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
                    EditorWidgets::drawChip( "Paused", EditorThemeUtil::getWarningColor() );
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
                ImGui::InputInt( "##StepFrameCount", &getState()._stepFrameCount, 0, 0 );
                getState()._stepFrameCount = MathUtil::clamp( getState()._stepFrameCount, 1, static_cast<int32>( EditorPlaySession::kMaxStepFrameCount ) );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayStepFrameCount" ) );
                ImGui::SameLine();
                if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kStepForward, "Step N" ) ) )
                    EditorPlaySession::stepFrames( static_cast<uint32>( getState()._stepFrameCount ) );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayStepFrames" ) );

                ImGui::SameLine();
                if ( ImGui::Button( EditorThemeUtil::makeIconLabel( editoricon::kStop, "Stop" ) ) )
                {
                    EditorPlaySession::stop();
                    GameObjectManager* pObjectManager = editor::getActiveObjectManager();
                    if ( pObjectManager != nullptr )
                        editorContext.getSubsystem<EditorWorkspace>().remapSelectionByObjectName( pObjectManager );
                }
                EditorSelfTestMarks::note( "toolbar.stop" );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayStop" ) );
            }

            /** @brief 카메라에서 시작 · 시간 배율을 그립니다. */
            static void drawSessionOptions()
            {
                ImGui::Checkbox( "Cam", &getState()._bStartAtCamera );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayStartAtCamera" ) );

                ImGui::SameLine();
                float32 timeScale = editor::getSubsystem<GameTimeScale>().get();
                ImGui::SetNextItemWidth( ImGui::GetFontSize() * 5.0f );
                if ( ImGui::DragFloat( "##TimeScale", &timeScale, 0.01f, GameTimeScale::kMinScale, GameTimeScale::kMaxScale, "x%.2f" ) )
                    editor::getSubsystem<GameTimeScale>().set( timeScale );
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "PlayTimeScale" ) );
                // 되돌리기는 보이는 메뉴 항목이다(예전에는 오른쪽 클릭이 말없이 x1 로 되돌렸다 — 숨은 동작).
                if ( EditorInputGUI::beginItemContextMenu( "##TimeScaleMenu" ) )
                {
                    if ( ImGui::MenuItem( "Reset to x1.00", nullptr, false, timeScale != 1.0f ) )
                        editor::getSubsystem<GameTimeScale>().set( 1.0f );
                    EditorSelfTestMarks::note( "toolbar.timeScale.reset" );
                    ImGui::EndPopup();
                }
            }

            /** @brief 게임이 자동 플레이를 등록했으면(`SW_GAME_AUTOPLAY`) 켜고 끄는 단추를 그립니다. */
            static void drawAutoplayButton()
            {
                const GameAutoplayRegistration* pActive = GameAutoplay::findActive();
                if ( pActive == nullptr )
                    return;
                ImGui::SameLine();
                const bool bOn = GameAutoplay::isOn();
                if ( EditorWidgets::drawToggleButton( "Auto", bOn, EditorThemeUtil::getSuccessColor() ) )
                    (void)GameAutoplay::setOn( bOn == false ); // 등록은 위에서 확인했다
                const string tooltip = SW_LOCKEYFORMAT( "EdCommon", "AutoplayToggle", TextArgumentList().addText( "game", pActive->_pGameName ).addText( "description", pActive->_pDescription ).addText( "variable", pActive->_pVariableName ) );
                EditorWidgets::drawTooltip( tooltip.c_str() );
                getState()._bAutoplayOffered = true;
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

            /** @brief 묶음 하나를 그리고 그 폭을 다음 프레임의 넘침 판정에 남깁니다. */
            static void drawSection( Section section )
            {
                ImGui::BeginGroup();
                if ( section == Section::SessionOptions )
                {
                    drawSessionOptions();
                    drawAutoplayButton();
                }
                else
                    drawRenderDocButton();
                ImGui::EndGroup();
                getState()._arrSectionWidth[static_cast<size_t>( section )] = ImGui::GetItemRectSize().x;
            }

            /**
             * @brief 재생 단추 뒤의 묶음을 그립니다. 줄에 들지 않는 묶음부터는 "»" 단추의 펼침에 넣는다.
             * @details 툴바는 한 줄 높이라 넘친 단추는 잘려 눌리지 않는다 — 언리얼 SToolBar 의 넘침 펼침과 같다. 폭은 앞 프레임에 잰 값이다(처음 한 프레임은 모두 줄에 둔다).
             */
            static void drawTrailingSections()
            {
                getState()._bAutoplayOffered     = false;
                constexpr size_t  kSectionCount  = static_cast<size_t>( Section::Count );
                const ImGuiStyle& style          = ImGui::GetStyle();
                const float32     separatorWidth = style.ItemSpacing.x * 2.0f + ImGui::CalcTextSize( "|" ).x;
                const float32     overflowWidth  = style.ItemSpacing.x + ImGui::CalcTextSize( ">>" ).x + style.FramePadding.x * 2.0f;
                const float32     rightX         = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
                float32           endX           = ImGui::GetItemRectMax().x;
                float32           allEndX        = endX;
                for ( const float32 width : getState()._arrSectionWidth )
                    allEndX += separatorWidth + width;
                size_t firstOverflow = kSectionCount;
                if ( allEndX > rightX )
                {
                    // 넘치면 "»" 단추 자리를 남기고 든 데까지만 줄에 둔다.
                    firstOverflow = 0;
                    while ( firstOverflow < kSectionCount && endX + separatorWidth + getState()._arrSectionWidth[firstOverflow] + overflowWidth <= rightX )
                        endX += separatorWidth + getState()._arrSectionWidth[firstOverflow++];
                }
                for ( size_t i = 0; i < firstOverflow; ++i )
                {
                    EditorWidgets::drawToolbarSeparator();
                    drawSection( static_cast<Section>( i ) );
                }
                if ( firstOverflow == kSectionCount )
                    return;
                ImGui::SameLine();
                if ( ImGui::Button( ">>" ) )
                    EditorInputGUI::openPopup( kOverflowPopupID );
                EditorSelfTestMarks::note( "toolbar.overflow" );
                // 접힌 펼침 안의 단추도 내놓은 것이다 — 기본 배율(모니터 DPI)이 크면 세션 옵션이 펼침에 들어 자체 테스트가 "단추 없음" 으로 졌다.
                if ( firstOverflow <= static_cast<size_t>( Section::SessionOptions ) )
                    getState()._bAutoplayOffered = GameAutoplay::findActive() != nullptr;
                EditorWidgets::drawTooltip( SW_LOCKEY( "EdCommon", "ToolbarOverflow" ) );
                if ( ImGui::BeginPopup( kOverflowPopupID ) == false )
                    return;
                for ( size_t i = firstOverflow; i < kSectionCount; ++i )
                    drawSection( static_cast<Section>( i ) );
                ImGui::EndPopup();
            }

            /** @brief 미저장 씬에서 Play 를 눌렀을 때의 확인 모달입니다. */
            static void drawUnsavedPlayPopup()
            {
                if ( getState()._bConfirmUnsavedPlay )
                {
                    EditorInputGUI::openPopup( kUnsavedPopupID );
                    getState()._bConfirmUnsavedPlay = false;
                }
                if ( EditorWidgets::beginCenteredModal( kUnsavedPopupID ) == false )
                    return;
                ImGui::TextUnformatted( "Scene has unsaved changes. Play anyway?" );
                if ( ImGui::Button( "Play" ) )
                {
                    startSession( getState()._pendingSession );
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
        EditorContext& editorContext = editor::getSubsystem<EditorContext>();
        editorContext.getSubsystem<EditorNetPlaySession>().update();

        ImGuiViewport*             pViewport = ImGui::GetMainViewport();
        const ImGuiStyle&          style     = ImGui::GetStyle();
        const float32              height    = ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f;
        constexpr ImGuiWindowFlags kFlags    = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
        if ( ImGui::BeginViewportSideBar( "##EditorPlayToolbar", pViewport, ImGuiDir_Up, height, kFlags ) )
        {
            EditorPlayToolbarInternal::drawTransportControls();
            EditorPlayToolbarInternal::drawTrailingSections();
        }
        ImGui::End();
        EditorPlayToolbarInternal::drawUnsavedPlayPopup();
    }

    bool EditorPlayToolbar::wasAutoplayButtonOffered()
    {
        return EditorPlayToolbarInternal::getState()._bAutoplayOffered;
    }
} // namespace sw::editor
