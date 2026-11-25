/**
 * @file RHIConstantBufferWriteLog.h
 * @brief 상수 버퍼 쓰기(기록 경로 · 페이지 구간)가 한 프레임에 같은 구간을 두 번 쓰는지 봅니다(개발 구성).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_set.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    /**
     * @class RHIConstantBufferWriteLog
     * @brief 이번 프레임에 쓴 상수 버퍼 구간(버퍼, 오프셋)의 집합입니다. 디바이스가 하나 든다(`IRHIDevice::beginFrame` 이 비운다).
     * @details DX12 · Vulkan · GL 은 쓰기가 버퍼의 이번 프레임 슬롯 하나에 가고 WebGPU 의 구간 쓰기는 큐에 먼저 나가므로, 같은 구간을 한 프레임에 두 번 쓰면 앞 드로우도
     *          마지막 값을 읽는다(DX11 의 Map 만 판을 나눈다). 상수 버퍼 쓰기는 두 길뿐이고 둘 다 여기를 지난다 — 기록 경로(`IRHICommandContext::updateConstantBuffer`, 오프셋 0)와
     *          페이지 구간(`ConstantBufferPagePool::write`). 구간은 쓸 때마다 새로 받아(언리얼 `RHIUpdateUniformBuffer` 의 새 할당과 같다) 두 번째 쓰기가 생기지 않는데, 풀이 칸을 너무 일찍
     *          다시 내주면 여기서 드러난다. 병렬 기록 워커가 함께 부르므로 잠근다.
     */
    class SW_API RHIConstantBufferWriteLog
    {
    public:
        /** @brief 새 프레임을 엽니다. 지난 프레임의 쓰기를 잊습니다. */
        void beginFrame();

        /** @brief @p buffer 의 @p offset 에서 시작하는 구간을 이번 프레임에 쓴다고 적습니다. 이번 프레임에 이미 썼으면 false 입니다(앞 드로우가 이 값을 읽게 된다). */
        [[nodiscard]] bool recordWrite( RHIBufferHandle buffer, uint32 offset );

    private:
        /** @brief 쓴 자리입니다 — 버퍼와 그 안의 오프셋. */
        struct Key
        {
            RHIBufferHandle _buffer{ 0 };
            uint32          _offset{ 0 };

            bool   operator==( const Key& other ) const { return _buffer == other._buffer && _offset == other._offset; }
            uint64 computeHash() const;
        };

    private:
        unordered_set<Key> _setWrittenThisFrame SW_GUARDED_BY( _mutex );
        mutex                                   _mutex;
    };
} // namespace sw
