/**
 * @file EditorSelfTestDevTools.cpp
 * @brief 개발 편의 기능(디버그 드로우 · 디버그 오버레이)이 Game View 에 실제로 그려지는지 보는 에디터 자체 시험입니다.
 */
#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Editor/Common/Workspace/EditorContext.h"
#include "Editor/Common/Workspace/EditorService.h"
#include "Editor/Panels/EditorPanelManager.h"
#include "Editor/Panels/GameViewPanel.h"
#include "Editor/SelfTest/EditorSelfTest.h"
#include "Editor/Viewport/EditorCamera.h"
#include "Editor/Viewport/EditorVisualizerGeometry.h"

#include "Engine/Graphics/Renderer/Debug/DebugDrawQueue.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Utility/Debug/DebugOverlayState.h"

namespace sw::editor
{
    namespace
    {
        struct EditorSelfTestDevToolsInternal
        {
            static constexpr const utf8* kGameViewPanelId = "game_view";
            static constexpr uint32      kMaxWaitFrame    = 60;
            static constexpr const utf8* kOverlayKey      = "selftest.overlay";

            /** @brief Game View 를 열고 그 패널을 돌려줍니다. 없으면 실패로 적고 nullptr 입니다. */
            static GameViewPanel* openGameView( EditorSelfTestContext& context )
            {
                EditorContext* pContext = EditorContext::get();
                if ( context.expect( pContext != nullptr, "no editor context" ) == false )
                    return nullptr;
                (void)pContext->getPanelManager().setPanelOpen( kGameViewPanelId, true );
                GameViewPanel* pPanel = static_cast<GameViewPanel*>( pContext->getPanelManager().findPanel( kGameViewPanelId ) );
                (void)context.expect( pPanel != nullptr, "no game view panel" );
                return pPanel;
            }

            // ------------------------------------------------------------------------------
            // gameView.debugDraw — DebugDrawQueue 의 선 · 상자 · 화살표 · 글자가 Game View 에 투영돼 그려진다
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runDebugDrawReachesTheGameView( EditorSelfTestContext& context )
            {
                if ( openGameView( context ) == nullptr )
                    return EditorSelfTestStep::Done;
                DebugDrawQueue* pQueue = getService<DebugDrawQueue>();
                if ( context.expect( pQueue != nullptr, "no DebugDrawQueue service" ) == false )
                    return EditorSelfTestStep::Done;

                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    // 그리는 카메라(멈춤에서는 에디터 카메라) 앞 5 m 에 둔다. 넣은 것은 프레임 끝에 확정되고 다음 에디터 프레임에 그려진다.
                    const CameraComponent* pCamera = EditorCamera::find( editor::getActiveScene() );
                    if ( context.expect( pCamera != nullptr, "no editor camera" ) == false )
                        return EditorSelfTestStep::Done;
                    const float4x4 world   = pCamera->getWorldMatrix();
                    const float3   forward = float3::transformVector( float3{ 0.0f, 0.0f, 1.0f }, world ).normalize();
                    const float3   center  = pCamera->getWorldPosition() + forward * 5.0f;
                    const float4   color{ 1.0f, 0.3f, 0.1f, 1.0f };
                    pQueue->drawBox( center, float3{ 0.5f, 0.5f, 0.5f }, color, 2.0f, "SelfTest" );
                    pQueue->drawArrow( center, center + float3{ 1.0f, 0.0f, 0.0f }, color, 2.0f, "SelfTest" );
                    pQueue->drawText( center, "self test", color, 2.0f, "SelfTest" );
                    return EditorSelfTestStep::Continue;
                }

                const EditorDebugDrawStats& stats  = EditorDebugDrawStats::get();
                const bool                  bDrawn = stats._segmentCount >= 12 && stats._textCount >= 1;
                if ( bDrawn == false && stepIndex < kMaxWaitFrame )
                    return EditorSelfTestStep::Continue;
                (void)context.expect( bDrawn, "the debug_draw visualizer never drew the queued box, arrow and text" );
                return EditorSelfTestStep::Done;
            }

            // ------------------------------------------------------------------------------
            // gameView.debugOverlay — 게임이 DebugOverlayState 에 쓴 값을 Game View 가 캔버스에 그린다
            // ------------------------------------------------------------------------------
            static EditorSelfTestStep runDebugOverlayIsDrawn( EditorSelfTestContext& context )
            {
                GameViewPanel* pPanel = openGameView( context );
                if ( pPanel == nullptr )
                    return EditorSelfTestStep::Done;
                DebugOverlayState* pOverlay = getService<DebugOverlayState>();
                if ( context.expect( pOverlay != nullptr, "no DebugOverlayState service" ) == false )
                    return EditorSelfTestStep::Done;

                const uint32 stepIndex = context.getStepIndex();
                if ( stepIndex == 0 )
                {
                    pOverlay->setFloat( hashed_string( kOverlayKey ), 42.0f );
                    return EditorSelfTestStep::Continue;
                }
                const bool bDrawn = pPanel->getLastOverlayRowCount() >= 1;
                if ( bDrawn == false && stepIndex < kMaxWaitFrame )
                    return EditorSelfTestStep::Continue;
                (void)context.expect( bDrawn, "the game view never drew the DebugOverlayState rows" );
                pOverlay->remove( hashed_string( kOverlayKey ) );
                return EditorSelfTestStep::Done;
            }
        };
    } // namespace

    SW_EDITOR_SELF_TEST( GameViewDebugDraw, "gameView.debugDraw", 710, &EditorSelfTestDevToolsInternal::runDebugDrawReachesTheGameView );
    SW_EDITOR_SELF_TEST( GameViewDebugOverlay, "gameView.debugOverlay", 720, &EditorSelfTestDevToolsInternal::runDebugOverlayIsDrawn );
} // namespace sw::editor
