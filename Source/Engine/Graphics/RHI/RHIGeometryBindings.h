/**
 * @file RHIGeometryBindings.h
 * @brief 커맨드 컨텍스트가 건 지오메트리 버퍼(정점 슬롯들과 인덱스 버퍼)와 PSO 의 정점 레이아웃입니다. 네 백엔드가 같은 칸 · 같은 규칙으로 들고 드로우 때 겁니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIDrawDiagnostics.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @struct RHIVertexLayout
     * @brief PSO 의 정점 레이아웃 — 셰이더가 읽는 정점 버퍼 슬롯(`RHIShaderRequest::toVertexSlotMask`)과 입력 레이아웃을 지은 메시 정점 형식입니다.
     * @details 네 백엔드의 PSO 기록이 하나씩 들고 드로우 때 `RHIGeometryBindings::bindVertexBuffersForDraw` 에 넘긴다. 정점 입력이 없는 셰이더는 슬롯이 비어 아무것도 걸지 않는다
     *          (언리얼의 빈 정점 선언 · Godot 의 빈 정점 포맷).
     */
    struct RHIVertexLayout
    {
        uint32              _slotMask{ 0 };
        RHIMeshVertexFormat _format{ RHIMeshVertexFormat::Packed };
    };

    /** @brief 버퍼 하나를 건 칸입니다 — 버퍼 · 원소 크기(정점 하나, 인덱스는 2 또는 4) · 시작 바이트. 버퍼 0 은 비어 있다는 뜻입니다. */
    struct RHIBufferBinding
    {
        RHIBufferHandle _buffer{ 0 };
        uint32          _stride{ 0 };
        uint32          _offset{ 0 };
    };

    /**
     * @struct RHIGeometryBindings
     * @brief `setVertexBuffer` · `setIndexBuffer` 로 건 정점 슬롯들(`constant::render::kVertexBufferSlotCount` 칸)과 인덱스 버퍼입니다.
     * @details 드로우 때 건다 — PSO 가 읽는 정점 슬롯이 비었거나 원소 크기가 형식과 다르거나, 건 버퍼가 부서졌거나, 인덱스 드로우에 인덱스 버퍼가 없으면 알리고 그 드로우를
     *          버린다(직전 드로우의 버퍼로 그리지 않는다). 판정과 알림은 여기 하나고, 백엔드는 버퍼 하나를 실제로 거는 일만 넘긴다. 예전에는 백엔드마다 칸과 규칙이 달라
     *          (DX11 은 인덱스를 곧바로 걸고 실패를 넘겼고, DX12 는 부서진 인덱스를 건너뛰어 앞 드로우의 인덱스로 그렸다) 한쪽만 고쳐졌다.
     */
    struct SW_API RHIGeometryBindings
    {
        RHIBufferBinding _arrVertexSlot[constant::render::kVertexBufferSlotCount];
        RHIBufferBinding _index{ 0, 4, 0 };

        /** @brief 정점 슬롯에 겁니다. 범위 밖 슬롯은 오류를 남기고 무시합니다. */
        void setVertexBuffer( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset );
        /** @brief 인덱스 버퍼를 겁니다. 원소 크기는 2 가 아니면 4 로 둡니다. */
        void setIndexBuffer( RHIBufferHandle buffer, uint32 indexStride, uint32 offset );
        /** @brief 정점 슬롯을 모두 비웁니다(`IRHICommandList::setPipelineState` 계약 — 정점 버퍼는 PSO 뒤에 건다). 인덱스 버퍼는 남긴다. */
        void clearVertexBuffers();
        /** @brief 그 버퍼를 건 칸(정점 · 인덱스)을 모두 비웁니다(버퍼를 지울 때 — 다음 드로우가 지운 핸들을 걸지 않게). */
        void forget( RHIBufferHandle buffer );

        /**
         * @brief PSO 가 읽는 정점 슬롯(@p layout)에만 겁니다. 슬롯 하나를 실제로 거는 일은 백엔드의 @p bindSlot( slot, binding ) 이 하고, 걸 버퍼가 없으면 false 를 돌려준다.
         * @return 읽는 슬롯이 비었거나 원소 크기가 형식과 다르면(`RHIDrawDiagnostics::checkVertexSlotsBound`), 건 버퍼가 부서졌으면 알리고 false 입니다 — 부르는 쪽은 그 드로우를 버린다.
         */
        template <typename BindSlotFn>
        [[nodiscard]] bool bindVertexBuffersForDraw( const utf8* pBackendName, const RHIVertexLayout& layout, BindSlotFn&& bindSlot ) const
        {
            if ( RHIDrawDiagnostics::checkVertexSlotsBound( pBackendName, layout, *this ) == false )
                return false;
            for ( uint32 slot = 0; slot < constant::render::kVertexBufferSlotCount; ++slot )
            {
                if ( ( layout._slotMask & ( 1u << slot ) ) == 0 )
                    continue;
                if ( bindSlot( slot, _arrVertexSlot[slot] ) == false )
                {
                    RHIDrawDiagnostics::reportDestroyedVertexBuffer( pBackendName, _arrVertexSlot[slot]._buffer );
                    return false;
                }
            }
            return true;
        }

        /**
         * @brief 인덱스 드로우 때 인덱스 버퍼를 겁니다. 거는 일은 백엔드의 @p bindIndex( binding ) 이 하고, 걸 버퍼가 없으면 false 를 돌려준다.
         * @return 걸지 않았거나 건 버퍼가 부서졌으면 알리고 false 입니다 — 부르는 쪽은 그 드로우를 버린다.
         */
        template <typename BindIndexFn>
        [[nodiscard]] bool bindIndexBufferForDraw( const utf8* pBackendName, BindIndexFn&& bindIndex ) const
        {
            if ( _index._buffer == 0 )
            {
                RHIDrawDiagnostics::reportIndexBufferUnbound( pBackendName );
                return false;
            }
            if ( bindIndex( _index ) == false )
            {
                RHIDrawDiagnostics::reportDestroyedIndexBuffer( pBackendName, _index._buffer );
                return false;
            }
            return true;
        }
    };
} // namespace sw
