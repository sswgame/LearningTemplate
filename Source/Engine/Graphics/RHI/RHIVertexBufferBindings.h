/**
 * @file RHIVertexBufferBindings.h
 * @brief 커맨드 컨텍스트가 건 정점 버퍼 슬롯들입니다. 네 백엔드가 같은 칸 · 같은 규칙으로 들고, 드로우 때 PSO 가 읽는 슬롯만 겁니다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

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
    };
} // namespace sw
