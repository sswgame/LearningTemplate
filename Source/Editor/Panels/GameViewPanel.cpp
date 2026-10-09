#include "pch.h"

#include "Editor/Panels/GameViewPanel.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/ViewportInputOverlay.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorPlaySession.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/SelfTest/EditorSelfTestInput.h"
#include "Editor/Viewport/EditorCamera.h"

#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Utility/GameAutoplay.h"
#include "Engine/Utility/GameTimeScale.h"

#include <imgui.h>

namespace sw::editor
{
    SW_EDITOR_PANEL( GameViewPanel, "game_view", EditorPanelCategory::Core, 300 );

    GameViewPanel::GameViewPanel()
        : _viewportClient{}
        , _listOverlayRow{}
        , _listDebugCategory{}
        , _lastOverlayRowCount{ 0 }
        , _stepFrameCount{ 10 }
        , _pendingSession{ PendingSession::Play }
        , _bConfirmUnsavedPlay{ false }
        , _bStartAtCamera{ false }
        , _bShowOverlay{ true }
        , _bAutoplayButtonDrawn{ false }
    {
    }

    void GameViewPanel::onPanelCollapsed()
    {
        EditorContext* pClosedContext = EditorContext::get();
        if ( pClosedContext != nullptr )
        {
            pClosedContext->setGameViewFocused( false );
            pClosedContext->setGameViewHovered( false );
        }
    }

    void GameViewPanel::drawContent()
    {
        EditorContext* pEditorContext = EditorContext::get();
        if ( pEditorContext == nullptr )
            return;

        const bool bFocused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
        const bool bHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
        // 엔진 프레임의 실제 경과(시간 배율 · 정지와 무관) — 고정 프레임 시간(`-gv_fixedFrameDelta`)으로 도는 시나리오에서 카메라 비행이 결정적이다.
        const float32 dt = GameTimeScale::getUnscaledDeltaTime( ImGui::GetIO().DeltaTime );

        pEditorContext->setGameViewFocused( bFocused );
        pEditorContext->setGameViewHovered( bHovered );

        _viewportClient.update( dt, bFocused, bHovered );

        if ( EditorChrome::beginToolbar( "##GameViewToolbar" ) )
        {
            drawTransportControls();
            EditorWidgets::drawToolbarSeparator();
            drawSessionOptions();
            EditorWidgets::drawToolbarSeparator();
            _viewportClient.drawViewportToolbar( ImGui::GetContentRegionAvail().x );
        }
        EditorChrome::endToolbar();

        if ( _bConfirmUnsavedPlay )
        {
            ImGui::OpenPopup( "##UnsavedScenePlay" );
            _bConfirmUnsavedPlay = false;
        }
        if ( ImGui::BeginPopupModal( "##UnsavedScenePlay", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
        {
            ImGui::TextUnformatted( "Scene has unsaved changes. Play anyway?" );
            if ( ImGui::Button( "Play" ) )
            {
                startSession( _pendingSession );
                ImGui::CloseCurrentPopup();
            }
            EditorSelfTestMarks::note( "gameView.playAnyway" );
            ImGui::SameLine();
            if ( ImGui::Button( "Cancel" ) )
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        const ImVec2 size = ImGui::GetContentRegionAvail();
        if ( size.x > 1.0f && size.y > 1.0f )
        {
            const uint32          targetWidth  = static_cast<uint32>( MathUtil::round( size.x ) );
            const uint32          targetHeight = static_cast<uint32>( MathUtil::round( size.y ) );
            const EditorGameView& view         = pEditorContext->getGameView();
            const int32           dW           = static_cast<int32>( targetWidth ) - static_cast<int32>( view._width );
            const int32           dH           = static_cast<int32>( targetHeight ) - static_cast<int32>( view._height );
            const bool            bNeedResize  = ( dW > 1 || dW < -1 || dH > 1 || dH < -1 ) && targetWidth > 0 && targetHeight > 0;
            if ( bNeedResize )
                pEditorContext->ensureGameViewSize( targetWidth, targetHeight );
        }

        const ImVec2 imagePos = ImGui::GetCursorScreenPos();
        _viewportClient.draw( pEditorContext->getGameView()._pTextureId, float2{ size.x, size.y } );

        if ( size.x > 1.0f && size.y > 1.0f )
        {
            const float2  barAnchor{ imagePos.x + size.x * 0.5f, imagePos.y + 8.0f };
            const float32 barMaxWidth = size.x - 16.0f; // 게임 뷰 양쪽에 8 px 씩 남긴다
            _viewportClient.drawTransformBar( barAnchor, barMaxWidth );
            drawDebugOverlay( float2{ imagePos.x, imagePos.y }, float2{ size.x, size.y } );

            InputManager* pInput = getService<InputManager>();
            if ( pInput != nullptr && ViewportInputOverlay::getConfig()._bEnabled == SW_TRUE )
            {
                InputMap* pInputMap = &pInput->getInputMap();
                ViewportInputOverlay::draw( ImGui::GetWindowDrawList(), imagePos, size, pInput, pInputMap );
            }
        }
    }

    void GameViewPanel::drawTransportControls()
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
            EditorWidgets::drawTooltip( "현재 게임 실행 중" );
        }
        else
        {
            if ( ImGui::Button( "Play" ) )
            {
                _pendingSession = PendingSession::Play;
                if ( bSceneDirty && EditorPlaySession::isStopped() )
                    _bConfirmUnsavedPlay = true;
                else
                    startSession( PendingSession::Play );
            }
            EditorSelfTestMarks::note( "gameView.play" );
        }
        if ( currentState != PlaySessionState::Playing || bSimulating )
            EditorWidgets::drawTooltip( "게임 플레이 모드를 시작합니다 (게임 뷰 입력 및 플레이어 컨트롤 활성화)" );

        ImGui::SameLine();
        if ( currentState == PlaySessionState::Playing && bSimulating )
        {
            EditorWidgets::drawChip( "Simulating", editor::style::kOk );
            EditorWidgets::drawTooltip( "월드만 도는 중 — 게임 모듈 업데이트 · 게임 입력 없이 에디터 카메라로 봅니다" );
        }
        else if ( ImGui::Button( "Simulate" ) )
        {
            _pendingSession = PendingSession::Simulate;
            if ( bSceneDirty && EditorPlaySession::isStopped() )
                _bConfirmUnsavedPlay = true;
            else
                startSession( PendingSession::Simulate );
        }
        if ( currentState != PlaySessionState::Playing || bSimulating == false )
            EditorWidgets::drawTooltip( "시뮬레이션 모드를 시작합니다 (씬만 틱 — 게임 모듈 업데이트 · 게임 입력 없음, 에디터 카메라 유지)" );

        ImGui::SameLine();
        if ( currentState == PlaySessionState::Paused )
        {
            EditorWidgets::drawChip( "Paused", editor::style::kWarn );
            EditorWidgets::drawTooltip( "게임 일시 정지됨" );
        }
        else if ( ImGui::Button( "Pause" ) )
            EditorPlaySession::pause();
        if ( currentState != PlaySessionState::Paused )
            EditorWidgets::drawTooltip( "게임 실행을 일시 정지합니다" );

        ImGui::SameLine();
        if ( ImGui::Button( "Step" ) )
            EditorPlaySession::stepOnce();
        EditorWidgets::drawTooltip( "게임을 정확히 1프레임 전진시킵니다" );

        ImGui::SameLine();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * 3.0f );
        ImGui::InputInt( "##StepFrameCount", &_stepFrameCount, 0, 0 );
        _stepFrameCount = MathUtil::clamp( _stepFrameCount, 1, static_cast<int32>( EditorPlaySession::kMaxStepFrameCount ) );
        EditorWidgets::drawTooltip( "'Step N' 이 진행할 프레임 수" );
        ImGui::SameLine();
        if ( ImGui::Button( "Step N" ) )
            EditorPlaySession::stepFrames( static_cast<uint32>( _stepFrameCount ) );
        EditorWidgets::drawTooltip( "왼쪽 칸의 프레임 수만큼 진행한 뒤 일시 정지합니다" );

        ImGui::SameLine();
        if ( ImGui::Button( "Stop" ) )
        {
            EditorPlaySession::stop();
            GameObjectManager* pObjectManager = editor::getActiveObjectManager();
            if ( pObjectManager != nullptr && pContext != nullptr )
                pContext->getWorkspace().remapSelectionByObjectName( pObjectManager );
        }
        EditorSelfTestMarks::note( "gameView.stop" );
        EditorWidgets::drawTooltip( "게임을 중지하고 초기 씬 상태로 복원합니다" );
    }

    void GameViewPanel::drawSessionOptions()
    {
        ImGui::Checkbox( "Cam", &_bStartAtCamera );
        EditorWidgets::drawTooltip( "Play 를 에디터 카메라 위치에서 시작합니다 ('Player' 태그 오브젝트, 없으면 게임 카메라를 든 오브젝트를 옮깁니다)" );

        ImGui::SameLine();
        float32 timeScale = GameTimeScale::get();
        ImGui::SetNextItemWidth( ImGui::GetFontSize() * 5.0f );
        if ( ImGui::DragFloat( "##TimeScale", &timeScale, 0.01f, GameTimeScale::kMinScale, GameTimeScale::kMaxScale, "x%.2f" ) )
            GameTimeScale::set( timeScale );
        if ( ImGui::IsItemClicked( ImGuiMouseButton_Right ) )
            GameTimeScale::set( 1.0f );
        EditorWidgets::drawTooltip( "게임 시간 배율 (gv_timeScale) — 끌어서 바꾸고, 오른쪽 클릭으로 x1.00 으로 되돌립니다" );

        ImGui::SameLine();
        if ( ImGui::Button( "Dbg Cat" ) )
            ImGui::OpenPopup( "##DebugDrawCategories" );
        EditorWidgets::drawTooltip( "DebugDrawQueue 카테고리를 켜고 끕니다" );
        drawDebugCategoryPopup();

        ImGui::SameLine();
        ImGui::Checkbox( "HUD", &_bShowOverlay );
        EditorWidgets::drawTooltip( "게임이 DebugOverlayState 에 쓴 값을 캔버스 왼쪽 아래에 표시합니다" );

        drawAutoplayButton();
    }

    void GameViewPanel::drawAutoplayButton()
    {
        _bAutoplayButtonDrawn                   = false;
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
        _bAutoplayButtonDrawn = true;
    }

    void GameViewPanel::drawDebugCategoryPopup()
    {
        if ( ImGui::BeginPopup( "##DebugDrawCategories" ) == false )
            return;
        DebugDrawQueue* pQueue = getService<DebugDrawQueue>();
        _listDebugCategory.clear();
        if ( pQueue != nullptr )
            pQueue->collectCategories( _listDebugCategory );
        if ( _listDebugCategory.empty() )
            ImGui::TextDisabled( "No debug draw yet." );
        for ( const hashed_string& category : _listDebugCategory )
        {
            bool bEnabled = pQueue->isCategoryEnabled( category );
            if ( ImGui::Checkbox( category.c_str(), &bEnabled ) )
                pQueue->setCategoryEnabled( category, bEnabled );
        }
        ImGui::EndPopup();
    }

    void GameViewPanel::startSession( PendingSession session )
    {
        // 카메라에서 시작은 플레이어가 조종하는 세션만 쓴다(Simulate 는 옮길 플레이어가 없다). 멈춤에서 시작할 때만 정한다.
        PlaySessionData* pData = EditorPlaySession::findData();
        if ( pData != nullptr && EditorPlaySession::isStopped() )
        {
            const CameraComponent* pEditorCamera = EditorCamera::find( editor::getActiveScene() );
            if ( _bStartAtCamera && pEditorCamera != nullptr )
                EditorPlaySession::setStartPosition( *pData, pEditorCamera->getWorldPosition() );
            else
                EditorPlaySession::clearStartPosition( *pData );
        }
        if ( session == PendingSession::Simulate )
            EditorPlaySession::simulate();
        else
            EditorPlaySession::play();
    }

    void GameViewPanel::drawDebugOverlay( const float2& canvasPos, const float2& canvasSize )
    {
        _lastOverlayRowCount              = 0;
        const DebugOverlayState* pOverlay = getService<DebugOverlayState>();
        if ( _bShowOverlay == false || pOverlay == nullptr || pOverlay->_bVisible == SW_FALSE )
            return;
        pOverlay->collectRows( _listOverlayRow );
        if ( _listOverlayRow.empty() )
            return;

        // 키 열 너비를 맞춰 값이 한 줄로 서게 한다.
        float32 keyWidth   = 0.0f;
        float32 valueWidth = 0.0f;
        for ( const DebugOverlayRow& row : _listOverlayRow )
        {
            keyWidth   = MathUtil::max( keyWidth, ImGui::CalcTextSize( row._key.c_str() ).x );
            valueWidth = MathUtil::max( valueWidth, ImGui::CalcTextSize( row._value.c_str() ).x );
        }

        // 왼쪽 아래 — 위쪽은 트랜스폼 바 · 통계 오버레이 자리다.
        constexpr float32 kPadding   = 6.0f;
        constexpr float32 kColumnGap = 12.0f;
        constexpr float32 kMargin    = 8.0f;
        const float32     lineHeight = ImGui::GetTextLineHeight();
        const float32     boxHeight  = kPadding * 2.0f + lineHeight * static_cast<float32>( _listOverlayRow.size() );
        const ImVec2      boxMin{ canvasPos._x + kMargin, canvasPos._y + canvasSize._y - kMargin - boxHeight };
        const ImVec2      boxMax{ boxMin.x + kPadding * 2.0f + keyWidth + kColumnGap + valueWidth, boxMin.y + boxHeight };
        ImDrawList*       pDrawList = ImGui::GetWindowDrawList();
        pDrawList->AddRectFilled( boxMin, boxMax, IM_COL32( 0, 0, 0, 150 ), 4.0f );

        float32 lineY = boxMin.y + kPadding;
        for ( const DebugOverlayRow& row : _listOverlayRow )
        {
            pDrawList->AddText( ImVec2( boxMin.x + kPadding, lineY ), IM_COL32( 160, 200, 255, 255 ), row._key.c_str() );
            pDrawList->AddText( ImVec2( boxMin.x + kPadding + keyWidth + kColumnGap, lineY ), IM_COL32( 255, 255, 255, 255 ), row._value.c_str() );
            lineY += lineHeight;
        }
        _lastOverlayRowCount = static_cast<uint32>( _listOverlayRow.size() );
    }
} // namespace sw::editor
