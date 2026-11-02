/**
 * @file RHIVertexBufferBindings.h
 * @brief 커맨드 컨텍스트가 건 정점 버퍼 슬롯들입니다. 네 백엔드가 같은 칸 · 같은 규칙으로 들고, 드로우 때 PSO 가 읽는 슬롯만 겁니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/Diagnostics/RHIDrawDiagnostics.h"
#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /** @brief 정점 버퍼 슬롯 하나에 건 버퍼 · 원소 크기 · 시작 바이트입니다. 버퍼 0 은 비어 있다는 뜻입니다. */
    struct RHIVertexBufferBinding
    {
        RHIBufferHandle _buffer{ 0 };
        uint32          _stride{ 0 };
        uint32          _offset{ 0 };
    };

    /**
     * @struct RHIVertexInputState
     * @brief PSO 가 정한 정점 입력 — 셰이더가 읽는 정점 버퍼 슬롯(`RHIShaderRequest::toVertexSlotMask`)과 입력 레이아웃을 지은 메시 정점 형식입니다.
     * @details 네 백엔드의 PSO 기록이 하나씩 들고 드로우 때 `RHIVertexBufferBindings::bindForDraw` 에 넘긴다. 정점 입력이 없는 셰이더는 슬롯이 비어 아무것도 걸지 않는다.
     */
    struct RHIVertexInputState
    {
        uint32              _slotMask{ 0 };
        RHIMeshVertexFormat _format{ RHIMeshVertexFormat::Packed };
    };

    /**
     * @struct RHIVertexBufferBindings
     * @brief `setVertexBuffer` 로 건 슬롯들(`constant::render::kVertexBufferSlotCount` 칸)입니다.
     * @details 백엔드마다 메시 · 인스턴스 슬롯 칸을 따로 두던 것을 슬롯 배열 하나로 묶었다 — 슬롯을 더해도 백엔드는 루프만 돈다.
     */
    struct SW_API RHIVertexBufferBindings
    {
        RHIVertexBufferBinding _arrSlot[constant::render::kVertexBufferSlotCount];

        /** @brief 슬롯에 겁니다. 범위 밖 슬롯은 오류를 남기고 무시합니다. */
        void set( uint32 slot, RHIBufferHandle buffer, uint32 stride, uint32 offset );
        /** @brief 그 버퍼를 건 슬롯을 모두 비웁니다(버퍼를 지울 때 — 다음 드로우가 지운 핸들을 걸지 않게). */
        void forget( RHIBufferHandle buffer );
        /** @brief 모든 슬롯을 비웁니다. */
        void clear();

        /**
         * @brief PSO 가 읽는 슬롯(@p input)에만 버퍼를 겁니다. 슬롯 하나를 실제로 거는 일은 백엔드의 @p bindSlot( slot, binding ) 이 하고, 걸 버퍼가 없으면 false 를 돌려준다.
         * @details 판정과 알림은 네 백엔드가 같다 — 읽는 슬롯이 비었거나 원소 크기가 형식과 다르면(`RHIDrawDiagnostics::checkVertexSlotsBound`), 건 버퍼가 부서졌으면
         *          (세대가 달라 풀리지 않는다 — `reportDestroyedVertexBuffer`) 알리고 false 다. 부르는 쪽은 그 드로우를 버린다(직전 드로우의 버퍼로 그리지 않는다).
         */
        template <typename BindSlotFn>
        [[nodiscard]] bool bindForDraw( const utf8* pBackendName, const RHIVertexInputState& input, BindSlotFn&& bindSlot ) const
        {
            if ( RHIDrawDiagnostics::checkVertexSlotsBound( pBackendName, input, *this ) == false )
                return false;
            for ( uint32 slot = 0; slot < constant::render::kVertexBufferSlotCount; ++slot )
            {
                if ( ( input._slotMask & ( 1u << slot ) ) == 0 )
                    continue;
                if ( bindSlot( slot, _arrSlot[slot] ) == false )
                {
                    RHIDrawDiagnostics::reportDestroyedVertexBuffer( pBackendName, _arrSlot[slot]._buffer );
                    return false;
                }
            }
            return true;
        }
    };
} // namespace sw
