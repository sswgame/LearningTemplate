/**
 * @file RHIBufferTransitionBatch.h
 * @brief 버퍼 상태 전이를 모아 한 번에 내는 묶음입니다(`IRHICommandList::transitionBuffers`).
 * @details 프레임 패스는 디스패치마다 자기 버퍼를 전이했다 — 컬링은 뷰 수 × 넷(앞 둘 · 뒤 둘)의 `transitionBuffer` 를 냈고, DX12 는 그때마다 `ResourceBarrier( 1 )`,
 *          Vulkan 은 `vkCmdPipelineBarrier` 한 번이었다. 같은 단계의 전이를 모아 한 번에 내면 드라이버가 배리어를 한 번에 풀고 그 사이 GPU 가 비지 않는다
 *          (언리얼 RDG 가 패스 앞 전이를 한 배치로 내는 것과 같은 자리, 기억에 의존한 비교).
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"

namespace sw
{
    class IRHICommandList;

    /**
     * @class RHIBufferTransitionBatch
     * @brief 한 단계에서 낼 버퍼 전이 목록입니다. 같은 버퍼를 다시 넣으면 나중 상태가 이긴다(한 단계 안에서 한 버퍼는 한 상태로만 간다).
     * @details 목록은 비운 뒤에도 용량을 지녀 프레임마다 할당하지 않는다. 기록 스레드 하나가 쓴다.
     */
    class SW_API RHIBufferTransitionBatch
    {
    public:
        /** @brief 버퍼 @p buffer 를 @p state 로 옮길 전이를 넣습니다. 핸들 0 은 넣지 않는다. 이미 든 버퍼면 상태만 바꾼다. */
        void add( RHIBufferHandle buffer, RHIBufferState state );
        /** @brief 모은 전이를 `transitionBuffers` 한 번으로 내고 목록을 비웁니다. 비었으면 아무것도 내지 않는다. */
        void flush( IRHICommandList& commands );
        /** @brief 아직 내지 않은 전이입니다(넣은 차례). */
        const vector<RHIBufferTransition>& getPending() const { return _listPending; }

    private:
        vector<RHIBufferTransition> _listPending;
    };
} // namespace sw
