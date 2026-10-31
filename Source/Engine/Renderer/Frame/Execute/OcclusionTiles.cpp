/**
 * @file OcclusionTiles.cpp
 * @brief GPU 컬링의 가림 판정입니다 — 지난 프레임 장면 깊이(깊이 히스토리)를 타일마다 최대 깊이로 줄여(가림 타일) 컬링이 경계 구를 그 뒤에서 거릅니다.
 * @details 흐름: 프레임 처음 컴퓨트 프리패스에서 깊이 히스토리를 옮긴 직후 뷰마다 가림 타일을 짓고(`recordOcclusionTiles`, occlusion_tiles.hlsl), 컬링(gpu_cull.hlsl)이
 *          절두체를 통과한 인스턴스의 경계 구를 감싼 상자를 그 깊이를 그린 프레임의 뷰-투영으로 옮겨, 덮는 타일의 최대 깊이보다 모두 멀면 그리지 않는다.
 *          언리얼의 HZB 가림(지난 프레임 깊이의 최대 밉 사슬)을 밉 한 단만 둔 모양이다 — 화면에서 타일 8 x 8 을 넘게 덮는 큰 물체는 판정하지 않는다(보인다).
 *          지난 프레임 깊이로 보므로 그 사이에 드러난 물체는 한 프레임 늦게 나오고, 기록을 버리는 프레임(처음 · 컷)은 판정하지 않는다.
 *          주 시점의 타일은 GPU 컬링 말고 게임 스레드도 쓴다 — `requestOcclusionReadback` 이 타일을 `AsyncBufferReadback` 으로 몇 프레임 늦게 되읽어
 *          `OcclusionTileMailbox` 에 올리면 `RenderViewCollector` 가 모니터(보임 오브젝트)의 구를 같은 식으로 판정한다(언리얼 `WasRecentlyRendered`).
 * @note 기본은 끔이다(`gv_occlusionCulling`) — 켜는 것은 Release 측정으로 이득을 보인 뒤 정한다(측정 없이 최적화하지 않는다).
 */
#include "pch.h"

#include "Engine/Renderer/Frame/Execute/OcclusionTiles.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"

namespace sw
{
    SW_LOG_CALLER( "OcclusionTiles" );

    /**
     * @brief `-gv_occlusionCulling=1`: GPU 컬링이 지난 프레임 장면 깊이로 가려진 인스턴스를 거릅니다(가림 타일). 컬링이 없는 백엔드(DX11)는 보지 않습니다.
     * @details 기본 끔 — 켜기 전에 무거운 장면에서 Release 로 재어 이득을 확인한다. 켜면 드러나는 물체가 한 프레임 늦게 나온다(지난 프레임 깊이로 본다).
     */
    SW_GLOBAL_VARIABLE( bool, gv_occlusionCulling, false, "GPU 컬링의 가림 판정 — 지난 프레임 장면 깊이의 타일 최대 깊이 뒤의 인스턴스를 거른다 (컬링이 있는 백엔드)" );

    namespace
    {
        struct OcclusionTilesInternal
        {
            /// occlusion_tiles.hlsl 의 루트 상수입니다 — `SwRootConstants` 와 같은 순서 · 크기다.
            struct RootConstants
            {
                uint32 _depthIndex{ 0 }; ///< 깊이 히스토리의 RW 텍스처 번호(네이티브). 에뮬은 RW 슬롯 0 의 서수 0
                uint32 _depthWidth{ 0 };
                uint32 _depthHeight{ 0 };
                uint32 _columnCount{ 0 };
                uint32 _rowCount{ 0 };
                uint32 _tileSize{ 0 };
            };
            static_assert( sizeof( RootConstants ) == 6 * sizeof( uint32 ), "occlusion_tiles.hlsl 의 루트 상수와 크기가 같아야 한다" );

            /// 타일 짓기 스레드 그룹 한 변입니다(occlusion_tiles.hlsl 의 numthreads 와 같다).
            static constexpr uint32 kGroupSize = 8;
        };
    } // namespace

    void OcclusionTiles::ensurePlaceholder( IRHIDevice* pDevice )
    {
        // 컬링 디스패치의 t2 자리표 — 가림을 보지 않는 슬롯도 빈 슬롯으로 디스패치하지 않는다(DX12 는 빈 디스크립터를 읽으면 폴트). 깊이 1 은 아무것도 가리지 않는다.
        constexpr float32 kFarDepth = 1.0f;
        if ( _placeholder.ensureCapacity( pDevice, sizeof( float32 ), 1, RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource, true, false, &kFarDepth ) == false )
            SW_LOG_ERROR( "Failed to create the occlusion tiles placeholder - GPU culling binds no buffer at t2" );
    }

    void OcclusionTiles::release( IRHIDevice* pDevice )
    {
        _placeholder.release( pDevice );
        _readback.shutdown();
        _mailbox.clear();
    }

    void OcclusionTiles::record( IRHICommandList* pCmd, IRHIDevice& device, RHIPipelineStateHandle pso, bool bDepthHistoryReady, FrameViewTarget& target ) const
    {
        using Internal                   = OcclusionTilesInternal;
        target._bOcclusionTilesThisFrame = SW_FALSE;
        const bool bBuilds               = static_cast<bool>( gv_occlusionCulling ) && pso != 0 && target._occlusionTiles.isValid() && bDepthHistoryReady;
        if ( bBuilds == false || pCmd == nullptr )
            return;
        const TransientAttachmentPool::Attachment depth = target._transientPool.find( attachmentNames()._sceneDepth.view() );

        // 깊이 히스토리는 방금 RT 로 썼다 — RW 텍스처로 읽도록 옮긴다(GPU 입자 시뮬레이션도 같은 상태로 읽는다).
        pCmd->prepareTextureForUnorderedAccess( target._depthHistory );
        pCmd->transitionBuffer( target._occlusionTiles._buffer, RHIBufferState::UnorderedAccess );
        pCmd->setComputePipelineState( pso );
        Internal::RootConstants constants{};
        // 네이티브는 RW 텍스처 배열의 번호로, 에뮬은 RW 슬롯 0 의 서수로 고른다(Bindless::loadRwTexture2D — GPU 입자와 같다).
        constants._depthIndex  = device.supportsNativeBindlessSampling() ? static_cast<uint32>( target._depthHistoryUAV ) : 0u;
        constants._depthWidth  = depth._width;
        constants._depthHeight = depth._height;
        constants._columnCount = target._occlusionColumns;
        constants._rowCount    = target._occlusionRows;
        constants._tileSize    = FrameRendererUtil::kOcclusionTileSize;
        // g_OcclusionTilesRW(u0) / 깊이 히스토리(RW 슬롯 0 — 에뮬만). occlusion_tiles.hlsl 레지스터와 1:1 이다.
        pCmd->bindComputeUAV( target._occlusionTiles._uav, 0 );
        pCmd->bindComputeUAV( target._depthHistoryUAV, shaderslot::kComputeTextureUAV0 );
        pCmd->setComputeRootConstants( 0, sizeof( constants ) / sizeof( uint32 ), &constants );
        pCmd->dispatchCompute( FrameRendererUtil::divideRoundUp( target._occlusionColumns, Internal::kGroupSize ), FrameRendererUtil::divideRoundUp( target._occlusionRows, Internal::kGroupSize ), 1 );
        // 컬링이 t2 로 읽는다. 쓰기가 끝났음을 알린다(빠지면 DX12 · Vulkan 에서 지난 프레임 타일을 읽는다).
        pCmd->transitionBuffer( target._occlusionTiles._buffer, RHIBufferState::ShaderResource );
        target._bOcclusionTilesThisFrame = SW_TRUE;
    }

    void OcclusionTiles::requestReadback( IRHICommandList* pCmd, IRHIDevice* pDevice, const FrameViewTarget& view )
    {
        // 켜져 있지 않으면 고리를 비워 둔다(읽기 버퍼를 만들지 않는다). 켰다 끄면 남은 사본도 치운다.
        const bool bEnabled = static_cast<bool>( gv_occlusionCulling ) && pDevice != nullptr && pCmd != nullptr;
        if ( bEnabled == false )
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

        const TransientAttachmentPool::Attachment depth = view._transientPool.find( attachmentNames()._sceneDepth.view() );
        const uint64                              next  = _readbackSerial + 1;
        ReadbackMeta&                             meta  = _arrReadbackMeta[next % kReadbackMetaCount];
        meta._viewProj                                  = view._depthHistoryViewProj;
        meta._columns                                   = view._occlusionColumns;
        meta._rows                                      = view._occlusionRows;
        meta._depthWidth                                = static_cast<float32>( depth._width );
        meta._depthHeight                               = static_cast<float32>( depth._height );
        meta._bValid                                    = view._bHistoryReset == SW_FALSE ? SW_TRUE : SW_FALSE;
        // 타일은 이 프레임의 컬링이 읽으므로 복사 뒤 SRV 상태로 되돌린다(`recordOcclusionTiles` 가 끝낸 상태).
        if ( _readback.request( *pCmd, view._occlusionTiles._buffer, RHIBufferState::ShaderResource, next ) )
            _readbackSerial = next;
    }

    void OcclusionTiles::fillCullParams( const FrameViewTarget& view, FrameRendererUtil::GPUCullParams& inoutParams )
    {
        const bool bOccludes             = view._bOcclusionTilesThisFrame == SW_TRUE && view._bHistoryReset == SW_FALSE;
        inoutParams._arrOcclusionGrid[3] = bOccludes ? 1u : 0u;
        if ( bOccludes == false )
            return;
        const TransientAttachmentPool::Attachment depth = view._transientPool.find( attachmentNames()._sceneDepth.view() );
        Memory::copy( inoutParams._arrOcclusionViewProj, &view._depthHistoryViewProj, sizeof( inoutParams._arrOcclusionViewProj ) );
        inoutParams._arrOcclusionGrid[0]      = view._occlusionColumns;
        inoutParams._arrOcclusionGrid[1]      = view._occlusionRows;
        inoutParams._arrOcclusionGrid[2]      = FrameRendererUtil::kOcclusionTileSize;
        inoutParams._arrOcclusionDepthSize[0] = static_cast<float32>( depth._width );
        inoutParams._arrOcclusionDepthSize[1] = static_cast<float32>( depth._height );
    }
} // namespace sw
