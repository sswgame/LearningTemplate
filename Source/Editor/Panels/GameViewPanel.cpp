#include "pch.h"

#include "Editor/Panels/GameViewPanel.h"

#include "Core/Math/MathUtil.h"

#include "Editor/Common/EditorPlaySession.h"
#include "Editor/Common/Gui/EditorChrome.h"
#include "Editor/Common/Widgets/EditorWidgets.h"
#include "Editor/Common/Widgets/ViewportInputOverlay.h"
#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Common/Workspace/EditorWorkspace.h"

#include "Engine/Input/ActionMap.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include <imgui.h>

namespace sw::editor
{
    GameViewPanel::GameViewPanel()
        : _viewportClient{}
        , _bConfirmUnsavedPlay{ false }
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

        const bool    bFocused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
        const bool    bHovered = ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows );
        const float32 dt       = ImGui::GetIO().DeltaTime;

        pEditorContext->setGameViewFocused( bFocused );
        pEditorContext->setGameViewHovered( bHovered );

        _viewportClient.update( dt, bFocused, bHovered );

        if ( EditorChrome::beginToolbar( "##GameViewToolbar" ) )
        {
            drawTransportControls();
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
                EditorPlaySession::play();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if ( ImGui::Button( "Cancel" ) )
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        const ImVec2 size = ImGui::GetContentRegionAvail();
        if ( size.x > 1.0f && size.y > 1.0f )
        {
            const uint32          wantW       = static_cast<uint32>( MathUtil::round( size.x ) );
            const uint32          wantH       = static_cast<uint32>( MathUtil::round( size.y ) );
            const EditorGameView& view        = pEditorContext->getGameView();
            const int32           dW          = static_cast<int32>( wantW ) - static_cast<int32>( view._width );
            const int32           dH          = static_cast<int32>( wantH ) - static_cast<int32>( view._height );
            const bool            bNeedResize = ( dW > 1 || dW < -1 || dH > 1 || dH < -1 ) && wantW > 0 && wantH > 0;
            if ( bNeedResize )
                pEditorContext->ensureGameViewSize( wantW, wantH );
        }

        const ImVec2 imagePos = ImGui::GetCursorScreenPos();
        _viewportClient.draw( pEditorContext->getGameView()._pTextureId, float2{ size.x, size.y } );

        if ( size.x > 1.0f && size.y > 1.0f )
        {
            const float2 barAnchor{ imagePos.x + size.x * 0.5f, imagePos.y + 8.0f };
            _viewportClient.drawTransformBar( barAnchor );

            InputManager* pInput = getService<InputManager>();
            if ( pInput != nullptr && ViewportInputOverlay::getConfig()._bEnabled == SW_TRUE )
            {
                ActionMap* pActionMap = &pInput->getActionMap();
                ViewportInputOverlay::draw( ImGui::GetWindowDrawList(), imagePos, size, pInput, pActionMap );
            }
        }
    }

    void GameViewPanel::drawTransportControls()
    {
        const PlaySessionState currentState = EditorPlaySession::getState();
        EditorContext*         pContext     = EditorContext::get();
        const bool             bSceneDirty  = ( pContext != nullptr && pContext->getWorkspace().isSceneDirty() );

        if ( currentState == PlaySessionState::Playing )
        {
            EditorWidgets::drawChip( "Playing", editor::style::kOk );
            EditorWidgets::drawTooltip( "현재 게임 실행 중" );
            ImGui::SameLine();
        }
        else if ( ImGui::Button( "Play" ) )
        {
            if ( bSceneDirty && EditorPlaySession::isStopped() )
                _bConfirmUnsavedPlay = true;
            else
                EditorPlaySession::play();
        }
        if ( currentState != PlaySessionState::Playing )
            EditorWidgets::drawTooltip( "게임 플레이 모드를 시작합니다 (게임 뷰 입력 및 플레이어 컨트롤 활성화)" );

        ImGui::SameLine();
        if ( ImGui::Button( "Simulate" ) )
        {
            if ( bSceneDirty && EditorPlaySession::isStopped() )
                _bConfirmUnsavedPlay = true;
            else
                EditorPlaySession::play();
        }
        EditorWidgets::drawTooltip( "시뮬레이션 모드를 시작합니다 (에디터 카메라를 유지하며 물리/게임 로직 실행)" );

        ImGui::SameLine();
        if ( currentState == PlaySessionState::Paused )
        {
            EditorWidgets::drawChip( "Paused", editor::style::kWarn );
            EditorWidgets::drawTooltip( "게임 일시 정지됨" );
            ImGui::SameLine();
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
        if ( ImGui::Button( "Stop" ) )
        {
            EditorPlaySession::stop();
            SceneManager* pSceneManager = editor::getService<SceneManager>();
            if ( pSceneManager != nullptr )
            {
                Scene* pScene = pSceneManager->getActiveScene();
                if ( pScene != nullptr )
                    EditorContext::get()->getWorkspace().remapSelectionByObjectName( pScene->getObjectManager() );
            }
        }
        EditorWidgets::drawTooltip( "게임을 중지하고 초기 씬 상태로 복원합니다" );
    }
} // namespace sw::editor
