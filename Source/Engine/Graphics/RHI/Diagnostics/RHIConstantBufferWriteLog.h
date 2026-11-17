/**
 * @file RHIConstantBufferWriteLog.h
 * @brief 기록 경로의 상수 버퍼 쓰기(`IRHICommandList::updateConstantBuffer`)가 한 프레임에 같은 버퍼를 두 번 쓰는지 봅니다.
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
     * @brief 이번 프레임에 기록 경로가 쓴 상수 버퍼의 집합입니다. 디바이스가 하나 든다(`IRHIDevice::beginFrame` 이 비운다).
     * @details DX12 · Vulkan · GL 은 기록 경로의 쓰기가 버퍼의 **이번 프레임 슬롯** 하나에 가므로, 같은 버퍼를 한 프레임에 두 번 쓰면 앞 드로우도
     *          마지막 값을 읽는다(DX11 만 Map 마다 판을 나눈다). 예전에는 이 규칙이 주석뿐이었다(R-12). 언리얼 `RHIUpdateUniformBuffer` 는 갱신마다 새 할당을
     *          받아 이 일이 없다 — 구간 할당(R-4)이 오면 쓰기가 구간을 돌려주는 형식으로 바뀌고, 그 전까지는 네 백엔드가 같은 자리
     *          (`IRHICommandContext::updateConstantBuffer`)에서 두 번째 쓰기를 진단 오류로 알린다. 병렬 기록 워커가 함께 부르므로 잠근다.
     */
    class SW_API RHIConstantBufferWriteLog
    {
    public:
        /** @brief 새 프레임을 엽니다. 지난 프레임의 쓰기를 잊습니다. */
        void beginFrame();

        /** @brief @p buffer 를 이번 프레임에 쓴다고 적습니다. 이번 프레임에 이미 썼으면 false 입니다(앞 드로우가 이 값을 읽게 된다). */
        [[nodiscard]] bool recordWrite( RHIBufferHandle buffer );

    private:
        unordered_set<RHIBufferHandle> _setWrittenThisFrame SW_GUARDED_BY( _mutex );
        mutex                                               _mutex;
    };
} // namespace sw
