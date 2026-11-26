/**
 * @file RenderScaleWatches.cpp
 * @brief 자동 렌더 배율 · 뷰 사각형의 자동화 감시 값입니다(`Render.AutoRenderScale` · `Render.ViewRectWidth` · `Render.ViewRectHeight` · `Render.ViewPoolWidth` ·
 *        `Render.ViewTransientRebuildCount` · `Render.OcclusionTilesBuilt`)과 2 단계 HZB 가림(`Render.OcclusionTwoPhase` · `Render.OcclusionLatePhase`)입니다.
 * @details 시나리오(game/playground/automation/renderscalestep · occlusion_twophase)만 읽는 값이라 렌더러 TU 가 아니라 이 파일에 둔다(`RenderViewWatches.cpp` 와 같은 모양).
 *          배율은 게임 스레드의 패킷 빌더가, 사각형 · 풀 · 가림 타일은 렌더 스레드가 원자값으로 올린 것이다(`FrameRenderer::getPublished*`).
 */
#include "pch.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"
#include "Engine/Renderer/Frame/Packet/RenderPacketBuilder.h"

namespace sw
{
    namespace
    {
        /** @brief 주 렌더러 서브시스템입니다. 렌더러가 없는 실행(서버 · 테스트)에는 값이 없다. */
        const FrameRendererSubsystem* findFrameRendererSubsystem()
        {
            return engine::findSubsystem<FrameRendererSubsystem>();
        }

        [[nodiscard]] bool readAutoRenderScale( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            const FrameRendererSubsystem* const pSubsystem = findFrameRendererSubsystem();
            if ( pSubsystem == nullptr )
                return false;
            outValue = static_cast<float64>( pSubsystem->getPacketBuilder().getLastAutoRenderScale() );
            return true;
        }

        [[nodiscard]] bool readViewRectWidth( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            const FrameRendererSubsystem* const pSubsystem = findFrameRendererSubsystem();
            if ( pSubsystem == nullptr )
                return false;
            outValue = static_cast<float64>( pSubsystem->getFrameRenderer().getPublishedViewRectWidth() );
            return true;
        }

        [[nodiscard]] bool readViewRectHeight( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            const FrameRendererSubsystem* const pSubsystem = findFrameRendererSubsystem();
            if ( pSubsystem == nullptr )
                return false;
            outValue = static_cast<float64>( pSubsystem->getFrameRenderer().getPublishedViewRectHeight() );
            return true;
        }

        [[nodiscard]] bool readViewPoolWidth( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            const FrameRendererSubsystem* const pSubsystem = findFrameRendererSubsystem();
            if ( pSubsystem == nullptr )
                return false;
            outValue = static_cast<float64>( pSubsystem->getFrameRenderer().getPublishedViewPoolWidth() );
            return true;
        }

        [[nodiscard]] bool readViewTransientRebuildCount( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            const FrameRendererSubsystem* const pSubsystem = findFrameRendererSubsystem();
            if ( pSubsystem == nullptr )
                return false;
            outValue = static_cast<float64>( pSubsystem->getFrameRenderer().getPublishedViewTransientRebuildCount().get() );
            return true;
        }

        [[nodiscard]] bool readOcclusionTilesBuilt( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            const FrameRendererSubsystem* const pSubsystem = findFrameRendererSubsystem();
            if ( pSubsystem == nullptr )
                return false;
            outValue = static_cast<float64>( pSubsystem->getFrameRenderer().getPublishedOcclusionTilesBuilt() );
            return true;
        }

        [[nodiscard]] bool readOcclusionTwoPhase( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            const FrameRendererSubsystem* const pSubsystem = findFrameRendererSubsystem();
            if ( pSubsystem == nullptr )
                return false;
            outValue = static_cast<float64>( pSubsystem->getFrameRenderer().getPublishedOcclusionTwoPhase() );
            return true;
        }

        [[nodiscard]] bool readOcclusionLatePhase( const GameObjectManager* pManager, float64& outValue )
        {
            (void)pManager;
            const FrameRendererSubsystem* const pSubsystem = findFrameRendererSubsystem();
            if ( pSubsystem == nullptr )
                return false;
            outValue = static_cast<float64>( pSubsystem->getFrameRenderer().getPublishedOcclusionLatePhase() );
            return true;
        }
    } // namespace

    SW_AUTOMATION_WATCH( renderAutoRenderScale, "Render.AutoRenderScale", "Auto render scale applied to the main view last frame (1 = off; gv_autoRenderScaleForce pins it)",
                         &readAutoRenderScale );
    SW_AUTOMATION_WATCH( renderViewRectWidth, "Render.ViewRectWidth", "Width (px) of the rectangle the main view drew inside its pool last frame", &readViewRectWidth );
    SW_AUTOMATION_WATCH( renderViewRectHeight, "Render.ViewRectHeight", "Height (px) of the rectangle the main view drew inside its pool last frame", &readViewRectHeight );
    SW_AUTOMATION_WATCH( renderViewPoolWidth, "Render.ViewPoolWidth", "Width (px) of the main view attachment pool last frame (an auto scale step must not change it)",
                         &readViewPoolWidth );
    SW_AUTOMATION_WATCH( renderViewTransientRebuildCount, "Render.ViewTransientRebuildCount",
                         "Times the main view attachment pool was rebuilt since start (an auto scale step must not add one)", &readViewTransientRebuildCount );
    SW_AUTOMATION_WATCH( renderOcclusionTilesBuilt, "Render.OcclusionTilesBuilt", "1 when the main view built occlusion tiles last frame (stays on below auto scale 1)",
                         &readOcclusionTilesBuilt );
    SW_AUTOMATION_WATCH( renderOcclusionTwoPhase, "Render.OcclusionTwoPhase",
                         "1 when two-phase HZB occlusion was usable last frame (occlusion on, Config/Engine/OcclusionCulling.json, culling and multi-draw-indirect backend)",
                         &readOcclusionTwoPhase );
    SW_AUTOMATION_WATCH( renderOcclusionLatePhase, "Render.OcclusionLatePhase", "1 when the main view recorded the two-phase late occlusion cull last frame (after the depth prepass)",
                         &readOcclusionLatePhase );
} // namespace sw
