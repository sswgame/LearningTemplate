/**
 * @file RHIUploadLog.h
 * @brief 버퍼 갱신 · 텍스처 전송이 지금 기록하는 프레임이 쓰는 자원에 가는지 봅니다(개발 구성).
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIHandles.h"

namespace sw
{
    /**
     * @class RHIUploadLog
     * @brief 자원마다 첫 내용을 올린 프레임과, 이번 프레임이 전이 · 복사 · 렌더 타깃으로 기록한 자원입니다. 디바이스가 버퍼 · 텍스처 하나씩 든다.
     * @details 프레임을 기록하지 않는 스레드의 버퍼 갱신과 모든 텍스처 전송은 아직 내지 않은 기록보다 먼저 실행된다(DX12 · Vulkan 은 앞서 내는 리스트, WebGPU 는 큐 쓰기, DX11 · GL 은
     *          즉시 컨텍스트). 이번 프레임이 이미 기록한 자원이면 추적 상태는 기록한 값인데 실제 상태는 프레임 앞의 것이라 배리어가 어긋나고, 먼저 기록한 드로우가 새 내용을 본다.
     *          판정은 백엔드 상태가 아니라 다섯 백엔드에 공통인 사실이다:
     *          - 이번 프레임이 그 자원의 전이 · 복사 · 렌더 타깃을 기록했거나(`hasRecordedUse`),
     *          - 첫 내용이 이번 프레임을 열기 **전에** 올라갔다(`hasEarlierContent`). 바인딩은 세지 않는다 — bindless 번호는 루트 상수 · 구조 버퍼 안의 값으로도 건너가 기록 자리에서 다 볼 수
     *            없다. 앞 프레임에 내용이 올라간 자원은 이번 프레임이 읽을 수 있는 자원이다.
     *          새 자원의 첫 내용(이번 프레임 안에 만들어 나눠 올리는 것도)은 지나간다. 언리얼은 다른 스레드의 갱신을 RHI 명령 리스트로만 받아 이 모양이 없다.
     */
    template <typename THandle>
    class RHIUploadLog
    {
    public:
        /** @brief 이 스레드가 기록할 새 프레임을 엽니다. 지난 프레임의 쓰임을 잊습니다. */
        void beginFrame();
        /** @brief 프레임을 닫습니다. 다음 `beginFrame` 까지 어느 갱신도 잘못이 아니다. */
        void endFrame();

        /** @brief @p handle 에 내용이 올라갔다고 적습니다(처음 한 번만 남는다). */
        void recordContent( THandle handle );
        /** @brief 이번 프레임이 @p handle 의 전이 · 복사 · 렌더 타깃을 기록했다고 적습니다. */
        void recordUse( THandle handle );
        /** @brief 놓은 자원을 잊습니다. */
        void remove( THandle handle );

        /** @brief 열린 프레임이 @p handle 의 쓰임을 기록했으면 true 입니다(어느 스레드가 묻든). */
        [[nodiscard]] bool hasRecordedUse( THandle handle ) const;
        /** @brief 프레임이 열려 있고 @p handle 의 첫 내용이 그 프레임보다 먼저 올라갔으면 true 입니다 — 그 프레임이 읽을 수 있다. */
        [[nodiscard]] bool hasEarlierContent( THandle handle ) const;
        /** @brief 다른 스레드가 지금 기록하는 프레임이 @p handle 을 쓰면 true 입니다(위 두 사실). 프레임을 기록하는 스레드 자신이면 false 입니다. */
        [[nodiscard]] bool isUsedByFrameOnOtherThread( THandle handle ) const;

    private:
        [[nodiscard]] bool hasRecordedUseLocked( THandle handle ) const SW_REQUIRES_LOCK( _mutex );
        [[nodiscard]] bool hasEarlierContentLocked( THandle handle ) const SW_REQUIRES_LOCK( _mutex );

    private:
        unordered_map<THandle, uint64> _mapContentFrame SW_GUARDED_BY( _mutex ); ///< 자원 → 첫 내용을 올린 프레임 번호
        unordered_set<THandle> _setUsedThisFrame        SW_GUARDED_BY( _mutex );
        std::thread::id _frameThreadID                  SW_GUARDED_BY( _mutex ); ///< 프레임을 기록하는 스레드(프레임 밖이면 빈 값)
        uint64 _frameNumber                             SW_GUARDED_BY( _mutex ){ 0 };
        mutable mutex                                   _mutex;
    };

    extern template class SW_API_EXTERN_TEMPLATE RHIUploadLog<RHIBufferHandle>;
    extern template class SW_API_EXTERN_TEMPLATE RHIUploadLog<RHITextureHandle>;
} // namespace sw
