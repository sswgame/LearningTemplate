/**
 * @file FrameRendererOcclusion.cpp
 * @brief GPU 컬링의 가림 판정입니다 — 지난 프레임 장면 깊이(깊이 히스토리)를 타일마다 최대 깊이로 줄여(가림 타일) 컬링이 경계 구를 그 뒤에서 거릅니다.
 * @details 흐름: 프레임 처음 컴퓨트 프리패스에서 깊이 히스토리를 옮긴 직후 뷰마다 가림 타일을 짓고(`recordOcclusionTiles`, occlusiontiles.hlsl), 컬링(gpucull.hlsl)이
 *          절두체를 통과한 인스턴스의 경계 구를 감싼 상자를 그 깊이를 그린 프레임의 뷰-투영으로 옮겨, 덮는 타일의 최대 깊이보다 모두 멀면 그리지 않는다.
 *          언리얼의 HZB 가림(지난 프레임 깊이의 최대 밉 사슬)을 밉 한 단만 둔 모양이다 — 화면에서 타일 8 x 8 을 넘게 덮는 큰 물체는 판정하지 않는다(보인다).
 *          지난 프레임 깊이로 보므로 그 사이에 드러난 물체는 한 프레임 늦게 나오고, 기록을 버리는 프레임(처음 · 컷)은 판정하지 않는다.
 * @note 기본은 끔이다(`gv_occlusionCulling`) — 켜는 것은 Release 측정으로 이득을 보인 뒤 정한다(측정 없이 최적화하지 않는다).
 */
#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Memory/Memory.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"

namespace sw
{
    SW_LOG_CALLER( "FrameRenderer" );

    /**
     * @brief `-gv_occlusionCulling=1`: GPU 컬링이 지난 프레임 장면 깊이로 가려진 인스턴스를 거릅니다(가림 타일). 컬링이 없는 백엔드(DX11)는 보지 않습니다.
     * @details 기본 끔 — 켜기 전에 무거운 장면에서 Release 로 재어 이득을 확인한다. 켜면 드러나는 물체가 한 프레임 늦게 나온다(지난 프레임 깊이로 본다).
     */
    SW_GLOBAL_VARIABLE( bool, gv_occlusionCulling, false, "GPU 컬링의 가림 판정 — 지난 프레임 장면 깊이의 타일 최대 깊이 뒤의 인스턴스를 거른다 (컬링이 있는 백엔드)" );

    namespace
    {
        struct FrameRendererOcclusionInternal
        {
            /// occlusiontiles.hlsl 의 루트 상수입니다 — `SwRootConstants` 와 같은 순서 · 크기다.
            struct RootConstants
            {
                uint32 _depthIndex{ 0 }; ///< 깊이 히스토리의 RW 텍스처 번호(네이티브). 에뮬은 RW 슬롯 0 의 서수 0
                uint32 _depthWidth{ 0 };
                uint32 _depthHeight{ 0 };
                uint32 _columnCount{ 0 };
                uint32 _rowCount{ 0 };
                uint32 _tileSize{ 0 };
            };
            static_assert( sizeof( RootConstants ) == 6 * sizeof( uint32 ), "occlusiontiles.hlsl 의 루트 상수와 크기가 같아야 한다" );

            /// 타일 짓기 스레드 그룹 한 변입니다(occlusiontiles.hlsl 의 numthreads 와 같다).
            static constexpr uint32 kGroupSize = 8;

            static uint32 divideRoundUp( uint32 value, uint32 divisor )
            {
                return ( value + divisor - 1u ) / divisor;
            }
        };
    } // namespace

    void FrameRenderer::ensureOcclusionTiles( ViewTarget& view )
    {
        using Internal        = FrameRendererOcclusionInternal;
        const bool bNotNeeded = _pDevice == nullptr || view._depthHistory == 0 || view._occlusionTiles.isValid();
        if ( bNotNeeded )
            return;
        const TransientAttachmentPool::Attachment depth = view._transientPool.find( attachmentNames()._sceneDepth.view() );
        if ( depth._texture == 0 )
            return;
        view._occlusionColumns = Internal::divideRoundUp( depth._width, FrameRendererUtil::kOcclusionTileSize );
        view._occlusionRows    = Internal::divideRoundUp( depth._height, FrameRendererUtil::kOcclusionTileSize );
        const bool bCreated    = view._occlusionTiles.ensureCapacity( _pDevice, sizeof( float32 ), view._occlusionColumns * view._occlusionRows,
                                                                      RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource | RHIBufferUsage::UnorderedAccess, true, true,
                                                                      nullptr );
        if ( bCreated == false )
        {
            SW_LOG_ERROR( "Failed to create the occlusion tiles (%#x%# floats) - this view culls without occlusion", view._occlusionColumns, view._occlusionRows );
            view._occlusionTiles.release( _pDevice );
        }
    }

    void FrameRenderer::recordOcclusionTiles( ViewTarget& target )
    {
        using Internal                       = FrameRendererOcclusionInternal;
        target._bOcclusionTilesThisFrame     = SW_FALSE;
        const RHIPipelineStateHandle pso     = getEnginePSO( RenderPassType::OcclusionTiles );
        const bool                   bBuilds = static_cast<bool>( gv_occlusionCulling ) && pso != 0 && target._occlusionTiles.isValid() && canCopyDepthHistory( target );
        if ( bBuilds == false || _pCmd == nullptr )
            return;
        const TransientAttachmentPool::Attachment depth = target._transientPool.find( attachmentNames()._sceneDepth.view() );

        // 깊이 히스토리는 방금 RT 로 썼다 — RW 텍스처로 읽도록 옮긴다(GPU 입자 시뮬레이션도 같은 상태로 읽는다).
        _pCmd->prepareTextureForUnorderedAccess( target._depthHistory );
        _pCmd->transitionBuffer( target._occlusionTiles._buffer, RHIBufferState::UnorderedAccess );
        _pCmd->setComputePipelineState( pso );
        Internal::RootConstants constants{};
        // 네이티브는 RW 텍스처 배열의 번호로, 에뮬은 RW 슬롯 0 의 서수로 고른다(Bindless::loadRwTexture2D — GPU 입자와 같다).
        constants._depthIndex  = _pDevice->supportsNativeBindlessSampling() ? static_cast<uint32>( target._depthHistoryUav ) : 0u;
        constants._depthWidth  = depth._width;
        constants._depthHeight = depth._height;
        constants._columnCount = target._occlusionColumns;
        constants._rowCount    = target._occlusionRows;
        constants._tileSize    = FrameRendererUtil::kOcclusionTileSize;
        // g_OcclusionTilesRW(u0) / 깊이 히스토리(RW 슬롯 0 — 에뮬만). occlusiontiles.hlsl 레지스터와 1:1 이다.
        _pCmd->bindComputeUav( target._occlusionTiles._uav, 0 );
        _pCmd->bindComputeUav( target._depthHistoryUav, shaderslot::kComputeTextureUav0 );
        _pCmd->setComputeRootConstants( 0, sizeof( constants ) / sizeof( uint32 ), &constants );
        _pCmd->dispatchCompute( Internal::divideRoundUp( target._occlusionColumns, Internal::kGroupSize ), Internal::divideRoundUp( target._occlusionRows, Internal::kGroupSize ), 1 );
        // 컬링이 t2 로 읽는다. 쓰기가 끝났음을 알린다(빠지면 DX12 · Vulkan 에서 지난 프레임 타일을 읽는다).
        _pCmd->transitionBuffer( target._occlusionTiles._buffer, RHIBufferState::ShaderResource );
        target._bOcclusionTilesThisFrame = SW_TRUE;
    }

    void FrameRenderer::fillOcclusionParams( const ViewTarget& view, FrameRendererUtil::GPUCullParams& inoutParams ) const
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
