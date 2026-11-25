/**
 * @file RenderDebugHUDSections.cpp
 * @brief 렌더링 런타임 디버그의 HUD 범주입니다 — `occlusion`(가림 타일 · 가려진 프리미티브), `streaming`(텍스처 스트리밍 — 요청 밉 대 올라온 밉 · 대기 · 메모리),
 *        `framedebug`(프레임 디버거 멈춤 지점). 게임 중에 에디터 렌더 디버거 창과 같은 값을 본다(유니티 Rendering Debugger 의 런타임 화면 자리).
 * @details 이미 있는 값에서 읽기만 한다: 가림 타일 CPU 사본(`FrameRenderer::getOcclusionSnapshot`), 프로파일러 카운터(`RT.TextureStreaming.*` · `RT.TextureUpload.waiting`),
 *          `RenderDebugRegistry`. 꺼진 섹션은 아무것도 읽지 않는다(HUD 계약).
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Console/DebugHUDSectionFormat.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/PrimitiveComponent.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Debug/OcclusionDebugStats.h"
#include "Engine/Renderer/Debug/RenderDebugRegistry.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererSubsystem.h"
#include "Engine/Scene/SceneManager.h"

namespace sw
{
    namespace
    {
        struct RenderDebugHUDSectionsInternal
        {
            /** @brief 바이트를 메가바이트로 보이는 배율입니다. */
            static constexpr float64 kBytesPerMegabyte = static_cast<float64>( constant::memory::kBytesPerMegabyte );
            /** @brief 섹션 순서 값입니다 — 엔진 `draw`(20) 와 `memory`(30) 사이. */
            static constexpr int32 kOcclusionOrder     = 22;
            static constexpr int32 kStreamingOrder     = 24;
            static constexpr int32 kFrameDebuggerOrder = 26;

            /** @brief 프로파일러 카운터 @p pName 의 마지막 프레임 값입니다. */
            static uint64 readCounter( const utf8* pName )
            {
                FrameProfiler& profiler = engine::getSubsystem<FrameProfiler>();
                return profiler.getLastFrameCount( profiler.registerScope( pName ) );
            }

            static void writeOcclusion( DebugHUDSectionWriter& writer )
            {
                const FrameRendererSubsystem* const           pRenderer = engine::findSubsystem<FrameRendererSubsystem>();
                const shared_ptr<const OcclusionTileSnapshot> pSnapshot = pRenderer != nullptr ? pRenderer->getFrameRenderer().getOcclusionSnapshot() : nullptr;
                if ( pSnapshot == nullptr || pSnapshot->isValid() == false )
                {
                    // 가림 판정이 꺼져 있거나(`-gv_occlusionCulling`) 컬링이 없는 백엔드다 — 사본이 없다.
                    writer.addLine( "tiles", "off" );
                    return;
                }
                const OcclusionTileStats stats = OcclusionTileStats::make( *pSnapshot );
                writer.addLineFormat( "tiles", "%# x %# (%# px)", pSnapshot->_columns, pSnapshot->_rows, pSnapshot->_tileSize );
                writer.addLineFormat( "covered", "%# / %#", stats._coveredTileCount, stats._tileCount );
                writer.addLineFormat( "nearest depth", "%.4f", static_cast<float64>( stats._nearestDepth ) );

                GameObjectManager* const pManager = engine::getSubsystem<SceneManager>().findActiveObjectManager();
                if ( pManager == nullptr )
                    return;
                vector<float4> listSphere;
                for ( const PrimitiveComponent* pPrimitive : pManager->getPrimitiveRegistry().getAll() )
                {
                    float3  center{};
                    float32 radius = 0.0f;
                    if ( pPrimitive != nullptr && pPrimitive->getWorldBounds( center, radius ) )
                        listSphere.push_back( float4{ center._x, center._y, center._z, radius } );
                }
                const uint32 occludedCount = OcclusionTileStats::countOccludedSpheres( *pSnapshot, vector_reference<const float4>( listSphere ) );
                writer.addLineFormat( "occluded", "%# / %# primitives", occludedCount, static_cast<uint32>( listSphere.size() ) );
            }

            static void writeStreaming( DebugHUDSectionWriter& writer )
            {
                const uint64 textureCount = readCounter( "RT.TextureStreaming.textures" );
                const uint64 belowWanted  = readCounter( "RT.TextureStreaming.belowWanted" );
                writer.addLineFormat( "textures", "%# streaming", textureCount );
                // 원하는 밉보다 거친 밉만 올라온 텍스처(요청 밉 대 올라온 밉) — 0 이 아니면 스트리밍이 따라오는 중이거나 예산이 모자라다.
                StringBuilder<constant::buffer::kMax64> belowWantedText;
                belowWantedText.appendFormat( "%# (%# mips)", belowWanted, readCounter( "RT.TextureStreaming.missingMips" ) );
                writer.addLine( "below wanted mip", belowWantedText.view(), belowWanted > 0 ? DebugHUDLineStyle::Warning : DebugHUDLineStyle::Neutral );
                writer.addLineFormat( "uploads waiting", "%#", readCounter( "RT.TextureUpload.waiting" ) );
                writer.addLineFormat( "resident", "%.1f MB", static_cast<float64>( readCounter( "RT.TextureStreaming.residentBytes" ) ) / kBytesPerMegabyte );
                writer.addLineFormat( "pool", "%.1f MB", static_cast<float64>( readCounter( "RT.TextureStreaming.poolBytes" ) ) / kBytesPerMegabyte );
                writer.addLineFormat( "in flight", "%.1f MB", static_cast<float64>( readCounter( "RT.TextureStreaming.bytesInFlight" ) ) / kBytesPerMegabyte );
            }

            static void writeFrameDebugger( DebugHUDSectionWriter& writer )
            {
                RenderDebugRegistry* const pDebug = engine::findSubsystem<RenderDebugRegistry>();
                if ( pDebug == nullptr )
                    return;
                const FrameDebuggerSettings settings = pDebug->getFrameDebugger();
                if ( settings._bEnabled == SW_FALSE )
                {
                    writer.addLine( "frame debugger", "off (framedebug <pass> <draw>)" );
                    return;
                }
                writer.addLine( "stop pass", settings._stopPass.empty() ? string_view{ "(whole frame)" } : string_view{ settings._stopPass }, DebugHUDLineStyle::Warning );
                writer.addLineFormat( "stop draw", "%#", settings._stopDraw );
                vector<FrameDebugDrawEvent> listEvent;
                pDebug->copyDrawEvents( listEvent );
                uint32 skippedCount = 0;
                for ( const FrameDebugDrawEvent& event : listEvent )
                {
                    skippedCount += event._bSkipped != SW_FALSE ? 1u : 0u;
                }
                writer.addLineFormat( "draws", "%# drawn, %# skipped", static_cast<uint32>( listEvent.size() ) - skippedCount, skippedCount );
            }
        };
    } // namespace

    SW_DEBUG_HUD_SECTION( occlusion, "occlusion", "Occlusion", RenderDebugHUDSectionsInternal::kOcclusionOrder, DebugHUDSectionFlag::kNone, &RenderDebugHUDSectionsInternal::writeOcclusion );
    SW_DEBUG_HUD_SECTION( streaming, "streaming", "Texture streaming", RenderDebugHUDSectionsInternal::kStreamingOrder, DebugHUDSectionFlag::kUsesFrameProfiler, &RenderDebugHUDSectionsInternal::writeStreaming );
    SW_DEBUG_HUD_SECTION( framedebug, "framedebug", "Frame debugger", RenderDebugHUDSectionsInternal::kFrameDebuggerOrder, DebugHUDSectionFlag::kNone, &RenderDebugHUDSectionsInternal::writeFrameDebugger );
} // namespace sw
