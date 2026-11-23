/**
 * @file HZBOcclusion.cpp
 * @brief 뷰마다의 HZB 피라미드 짓기와 GPU 컬링의 가림 판정 상수 · 주 시점 가림 타일 되읽기입니다(헤더 머리말 참고).
 * @details 가림 판정: 컬링(gpu_cull.hlsl)이 절두체를 통과한 인스턴스의 경계 상자를 피라미드의 깊이를 그린 뷰-투영으로 옮겨, 사각형이 한 축 발자국 텍셀(설정 `_footprintTexels`)
 *          안에 드는 가장 고운 밉에서 덮는 텍셀의 최대 깊이보다 모두 멀면 그리지 않는다 — 큰 물체는 굵은 밉에서 판정한다(언리얼 HZB 가림 · Adria 의 2 단계 HZB 와 같은 모양).
 *          예전 가림 타일(16 px 밉 한 단)은 화면에서 타일 8 x 8 을 넘게 덮는 큰 물체를 판정하지 못했다.
 *          주 시점의 16 px 밉은 GPU 컬링 말고 게임 스레드도 쓴다 — `requestReadback` 이 그 밉의 최대 깊이(가림 타일 버퍼)를 `AsyncBufferReadback` 으로 몇 프레임 늦게 되읽어
 *          `OcclusionTileMailbox` 에 올리면 `RenderViewCollector` 가 모니터(보임 오브젝트)의 구를 판정한다(언리얼 `WasRecentlyRendered`).
 * @note 가림 판정은 기본 끔이다(`-gv_occlusionCulling`, 프레임 설정 사본으로 받는다). 켰을 때의 모양(2 단계 · 문턱)은 `Config/Engine/OcclusionCulling.json` 이다.
 */
#include "pch.h"

#include "Engine/Renderer/Frame/Execute/HZBOcclusion.h"

#include "Core/Log/LogOnce.h"
#include "Core/Memory/Memory.h"

#include "Engine/Config/OcclusionCullingConfig.h"
#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Frame/Execute/ViewGraphResource.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"
#include "Engine/Renderer/Frame/View/HZBPyramid.h"
#include "Engine/Renderer/Graph/FrameBufferTable.h"

namespace sw
{
    namespace
    {
        struct HZBOcclusionInternal
        {
            /// hzb_build.hlsl 의 루트 상수입니다 — `SwRootConstants` 와 같은 순서 · 크기다.
            struct RootConstants
            {
                uint32 _depthIndex{ 0 }; ///< 깊이 사본의 RW 텍스처 번호(네이티브). 에뮬은 RW 슬롯 0 의 서수 0
                uint32 _depthWidth{ 0 };
                uint32 _depthHeight{ 0 };
                uint32 _mip{ 0 };
                uint32 _tileColumns{ 0 }; ///< 0 이면 가림 타일을 쓰지 않는다
                uint32 _tileRows{ 0 };
            };
            static_assert( sizeof( RootConstants ) == 6 * sizeof( uint32 ), "hzb_build.hlsl 의 루트 상수와 크기가 같아야 한다" );

            /// 짓기 스레드 그룹 한 변입니다(hzb_build.hlsl 의 numthreads 와 같다).
            static constexpr uint32 kGroupSize = 8;
        };
        static_assert( ( 1u << ( HZBLayout::kTileMip + 1u ) ) == FrameRendererUtil::kOcclusionTileSize, "16 px 밉의 텍셀 하나가 가림 타일 한 변이어야 한다(hzb_build.hlsl kTileMip)" );

        /** @brief 프레임 패스의 배리어 — 앞뒤 전이는 그래프 레벨 콜백이 냈고, 밉 사이는 선언한 UAV 핸들로 기다린다. */
        struct HZBGraphBarriers
        {
            const RenderGraphPassContext& _context;
            uint32                        _viewSlot;

            void begin() const {}
            void betweenMips() const { _context.uavBarrier( HZBOcclusion::getPyramidUAV(), _viewSlot ); }
            void end() const {}
        };

        /** @brief 뷰 그래프 패스 안의 배리어 — 앞 UAV 는 레벨 콜백이 냈고(부르는 패스가 `ViewGraphResource::hzb` · `occlusionTiles` 를 UAV 로 선언했다), 밉 사이는 선언한 UAV 핸들로 기다리고, 끝에 같은 패스가 읽는 셰이더 읽기로 옮긴다. */
        struct HZBViewPassBarriers
        {
            const RenderGraphPassContext& _context;

            void begin() const {}
            void betweenMips() const { _context.uavBarrier( ViewGraphResource::hzb() ); }
            void end() const
            {
                // 컬링(t2) · 화면 공간 반사(t11)가 같은 패스 안에서 읽는다. 쓰기가 끝났음을 알린다(빠지면 DX12 · Vulkan 에서 지난 피라미드를 읽는다).
                _context.transition( ViewGraphResource::hzb(), RenderGraphAccess::ShaderRead );
                _context.transition( ViewGraphResource::occlusionTiles(), RenderGraphAccess::ShaderRead );
            }
        };
    } // namespace

    void HZBOcclusion::ensurePlaceholder( IRHIDevice* pDevice )
    {
        // 컬링 디스패치의 t2 자리표 — 가림을 보지 않는 슬롯도 빈 슬롯으로 디스패치하지 않는다(DX12 는 빈 디스크립터를 읽으면 폴트). 깊이 1 은 아무것도 가리지 않는다.
        constexpr float32 kFarDepth[2] = { 1.0f, 1.0f };
        if ( _placeholder.ensureCapacity( pDevice, sizeof( kFarDepth ), 1, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, kFarDepth ) == false )
            SW_LOG_ERROR_ID( "HZBOcclusion.FailedCreatePlaceholder", "Failed to create the HZB placeholder - GPU culling binds no buffer at t2" );
    }

    void HZBOcclusion::release( IRHIDevice* pDevice )
    {
        _placeholder.release( pDevice );
        _readback.shutdown();
        _mailbox.clear();
    }

    RenderGraphBufferUAV HZBOcclusion::getPyramidUAV()
    {
        static const hashed_string kName( "HZB" ); // 한 번만 intern 한다(전역 문자열 표의 뮤텍스를 프레임마다 잡지 않는다)
        return RenderGraphBufferUAV{ kName };
    }

    RenderGraphBufferUAV HZBOcclusion::getTilesUAV()
    {
        static const hashed_string kName( "OcclusionTiles" );
        return RenderGraphBufferUAV{ kName };
    }

    void HZBOcclusion::registerFrameResources( FrameBufferTable& table, uint32 viewSlot, const FrameViewTarget& target )
    {
        if ( target._hzb.isValid() == false || target._occlusionTiles.isValid() == false )
            return;
        // 프레임 시작의 상태는 셰이더 읽기다(지난 프레임 컬링 · 반사가 읽었다) — 아니어도 첫 사용(짓기, UAV)이 달라 전이는 늘 나간다.
        table.registerBuffer( getPyramidUAV()._name, viewSlot, target._hzb, RenderGraphAccess::ShaderRead );
        table.registerBuffer( getTilesUAV()._name, viewSlot, target._occlusionTiles, RenderGraphAccess::ShaderRead );
    }

    bool HZBOcclusion::record( const RenderGraphPassContext& graphContext, uint32 viewSlot, IRHIDevice& device, RHIPipelineStateHandle pso, uint32 depthWidth, uint32 depthHeight,
                               const float4x4& viewProj, bool bWriteTiles, FrameViewTarget& target ) const
    {
        // 버퍼가 없는 뷰는 등록하지 않았다(`registerFrameResources`) — 짓지 않는다. 있는데 선언 · 등록이 없으면 그래프가 전이를 내지 않았다 — 짓지 않는다(오류는 문맥이 남겼다).
        // 파이프라인 XML 의 `HZBBuild` 가 두 이름을 출력으로 적어야 한다.
        const bool bHasBuffers = target._hzb.isValid() && target._occlusionTiles.isValid();
        const bool bDeclared   = bHasBuffers && graphContext.getBuffer( getPyramidUAV(), viewSlot ) != 0 && graphContext.getBuffer( getTilesUAV(), viewSlot ) != 0;
        if ( graphContext.getCommandList() == nullptr || bDeclared == false )
        {
            target._bHZBValid = SW_FALSE;
            return false;
        }
        return recordPyramid( *graphContext.getCommandList(), device, pso, depthWidth, depthHeight, viewProj, bWriteTiles, target, HZBGraphBarriers{ graphContext, viewSlot } );
    }

    bool HZBOcclusion::recordInViewPass( const RenderGraphPassContext& graphContext, IRHIDevice& device, RHIPipelineStateHandle pso, uint32 depthWidth, uint32 depthHeight,
                                         const float4x4& viewProj, FrameViewTarget& target ) const
    {
        if ( graphContext.getCommandList() == nullptr )
        {
            target._bHZBValid = SW_FALSE;
            return false;
        }
        return recordPyramid( *graphContext.getCommandList(), device, pso, depthWidth, depthHeight, viewProj, false, target, HZBViewPassBarriers{ graphContext } );
    }

    template <typename Barriers>
    bool HZBOcclusion::recordPyramid( IRHIPassCommandList& cmd, IRHIDevice& device, RHIPipelineStateHandle pso, uint32 depthWidth, uint32 depthHeight, const float4x4& viewProj,
                                      bool bWriteTiles, FrameViewTarget& target, const Barriers& barriers ) const
    {
        using Internal         = HZBOcclusionInternal;
        target._bHZBValid      = SW_FALSE;
        const HZBLayout layout = HZBLayout::make( depthWidth, depthHeight );
        const bool      bReady = pso != 0 && target._hzb.isValid() && target._occlusionTiles.isValid() && target._depthHistory != 0 && target._depthHistoryUAV != kInvalidDescriptorIndex;
        if ( bReady == false || layout._mipCount == 0 )
            return false;
        if ( layout._texelCount > target._hzb._capacityElements )
        {
            // 버퍼는 풀(텍스처) 크기의 배치로 만든다(`FrameViewResources::ensureHZB`) — 사각형은 그보다 크지 않다. 여기 오면 두 곳의 크기가 어긋났다.
            SW_LOG_ERROR_ONCE_ID( "HZBOcclusion.PyramidOverCapacity", "HZB pyramid for %#x%# needs %# texels but the buffer holds %# - this view builds no HZB", depthWidth, depthHeight,
                                  layout._texelCount, target._hzb._capacityElements );
            return false;
        }

        // 깊이 사본은 방금 RT 로 썼고 복사 풀스크린이 끝에 RW 텍스처 상태로 넘겨 두었다(`FrameGraphExecutor::recordDepthHistory` · `recordCurrentDepthHZB` — 짓는 세 자리 모두 그 복사 뒤다).
        barriers.begin();
        cmd.setComputePipelineState( pso );
        // g_HZBRW(u0) / g_OcclusionTilesRW(u1) / 깊이 사본(RW 슬롯 0 — 에뮬만). hzb_build.hlsl 레지스터와 1:1 이다.
        cmd.bindComputeUAV( target._hzb._uav, 0 );
        cmd.bindComputeUAV( target._occlusionTiles._uav, 1 );
        cmd.bindComputeUAV( target._depthHistoryUAV, shaderslot::kComputeTextureUAV0 );
        Internal::RootConstants constants{};
        // 네이티브는 RW 텍스처 배열의 번호로, 에뮬은 RW 슬롯 0 의 서수로 고른다(Bindless::loadRwTexture2D — GPU 입자와 같다).
        constants._depthIndex  = device.supportsNativeBindlessSampling() ? static_cast<uint32>( target._depthHistoryUAV ) : 0u;
        constants._depthWidth  = depthWidth;
        constants._depthHeight = depthHeight;
        constants._tileColumns = bWriteTiles ? target._occlusionColumns : 0u;
        constants._tileRows    = bWriteTiles ? target._occlusionRows : 0u;
        for ( uint32 mip = 0; mip < layout._mipCount; ++mip )
        {
            // 밉 m 은 밉 m-1 을 읽는다 — 앞 디스패치의 쓰기가 끝나야 한다(같은 버퍼라 상태 전이로는 알 수 없다).
            if ( mip > 0 )
                barriers.betweenMips();
            constants._mip               = mip;
            const HZBLayout::Mip& level  = layout._arrMip[mip];
            const bool            bTiles = mip == HZBLayout::kTileMip && constants._tileColumns > 0;
            const uint32          countX = bTiles && target._occlusionColumns > level._width ? target._occlusionColumns : level._width;
            const uint32          countY = bTiles && target._occlusionRows > level._height ? target._occlusionRows : level._height;
            cmd.setComputeRootConstants( 0, sizeof( constants ) / sizeof( uint32 ), &constants );
            cmd.dispatchThreads( countX, countY, 1, Internal::kGroupSize, Internal::kGroupSize, 1 );
        }
        barriers.end();
        target._hzbLayout   = layout;
        target._hzbViewProj = viewProj;
        target._bHZBValid   = SW_TRUE;
        return true;
    }

    void HZBOcclusion::requestReadback( IRHICommandList& cmd, IRHIDevice* pDevice, bool bEnabled, const FrameViewTarget& view )
    {
        // 켜져 있지 않으면 고리를 비워 둔다(읽기 버퍼를 만들지 않는다). 켰다 끄면 남은 사본도 치운다.
        const bool bReadable = bEnabled && pDevice != nullptr;
        if ( bReadable == false )
        {
            if ( _readback.isInitialized() )
            {
                _readback.shutdown();
                _mailbox.clear();
            }
            return;
        }

        // 먼저 도착한 사본을 우편함에 올린다. 요청 때 기록을 버리는 프레임이었으면 올리지 않고 비운다.
        vector<uint8> bytes;
        uint64        serial = 0;
        if ( _readback.takeLatest( bytes, serial ) )
        {
            const ReadbackMeta& meta = _arrReadbackMeta[serial % kReadbackMetaCount];
            if ( meta._bValid == SW_FALSE )
            {
                _mailbox.clear();
            }
            else
            {
                shared_ptr<OcclusionTileSnapshot> pSnapshot = make_shared<OcclusionTileSnapshot>();
                pSnapshot->_viewProj                        = meta._viewProj;
                pSnapshot->_columns                         = meta._columns;
                pSnapshot->_rows                            = meta._rows;
                pSnapshot->_tileSize                        = FrameRendererUtil::kOcclusionTileSize;
                pSnapshot->_depthWidth                      = meta._depthWidth;
                pSnapshot->_depthHeight                     = meta._depthHeight;
                pSnapshot->_serial                          = serial;
                pSnapshot->_listTileDepth.resize( bytes.size() / sizeof( float32 ) );
                Memory::copy( pSnapshot->_listTileDepth.data(), bytes.data(), pSnapshot->_listTileDepth.size() * sizeof( float32 ) );
                if ( pSnapshot->isValid() )
                    _mailbox.publish( std::move( pSnapshot ) );
                else
                    _mailbox.clear();
            }
        }

        // 이번 프레임에 타일을 짓지 않았으면 복사할 것이 없다. 격자 크기가 바뀌었으면 읽기 버퍼를 다시 만든다.
        if ( view._bOcclusionTilesThisFrame == SW_FALSE || view._occlusionTiles.isValid() == false )
            return;
        const uint32 elementCount = view._occlusionColumns * view._occlusionRows;
        const bool   bResized     = _readback.isInitialized() && _readback.getElementCount() != elementCount;
        if ( bResized )
        {
            _readback.shutdown();
            _mailbox.clear();
        }
        if ( _readback.isInitialized() == false && _readback.initialize( pDevice, sizeof( float32 ), elementCount ) == false )
            return; // 만들지 못했다 — 이유는 initialize 가 남겼다. 가림을 보지 않는 채로 둔다.

        const uint64  next = _readbackSerial + 1;
        ReadbackMeta& meta = _arrReadbackMeta[next % kReadbackMetaCount];
        meta._viewProj     = view._hzbViewProj;
        meta._columns      = view._occlusionColumns;
        meta._rows         = view._occlusionRows;
        meta._depthWidth   = static_cast<float32>( view._hzbLayout._depthWidth ); // 타일을 지은 사각형과 같다(지난 사각형)
        meta._depthHeight  = static_cast<float32>( view._hzbLayout._depthHeight );
        meta._bValid       = view._bHistoryReset == SW_FALSE ? SW_TRUE : SW_FALSE;
        // 프레임 그래프가 끝나 타일은 등록 접근(셰이더 읽기 — `registerFrameResources`)이다 — 복사 뒤에도 그 상태로 되돌린다.
        if ( _readback.request( cmd, view._occlusionTiles._buffer, RHIBufferState::ShaderResource, next ) )
            _readbackSerial = next;
    }

    void HZBOcclusion::fillCullParams( const FrameViewTarget& view, const OcclusionCullingConfig& config, FrameRendererUtil::GPUCullParams& inoutParams )
    {
        const bool bOccludes          = view._bHZBValid == SW_TRUE && view._hzbLayout._mipCount > 0;
        inoutParams._arrHZBControl[3] = bOccludes ? config._footprintTexels : 0u;
        if ( bOccludes == false )
            return;
        Memory::copy( inoutParams._arrHZBViewProj, &view._hzbViewProj, sizeof( inoutParams._arrHZBViewProj ) );
        inoutParams._arrHZBControl[0] = view._hzbLayout._depthWidth;
        inoutParams._arrHZBControl[1] = view._hzbLayout._depthHeight;
        inoutParams._arrHZBControl[2] = view._hzbLayout._mipCount;
        inoutParams._arrHZBParams[0]  = config._depthBias;
    }
} // namespace sw
