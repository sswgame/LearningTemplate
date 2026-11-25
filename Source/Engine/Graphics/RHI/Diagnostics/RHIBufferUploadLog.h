/**
 * @file RHIBufferUploadLog.h
 * @brief 다른 스레드의 버퍼 갱신이 지금 기록하는 프레임이 쓰는 버퍼에 가는지 봅니다(개발 구성).
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
     * @class RHIBufferUploadLog
     * @brief 버퍼마다 첫 내용을 올린 프레임과, 이번 프레임이 전이 · 복사를 기록한 버퍼입니다. 디바이스가 하나 든다.
     * @details 프레임을 기록하지 않는 스레드의 갱신은 다음 제출의 맨 앞에 실행된다(DX12 · Vulkan 은 앞쪽 업로드 리스트, WebGPU 는 큐 쓰기, DX11 · GL 은 즉시 컨텍스트). 그 프레임이
     *          기록한 읽기 · 전이와 순서가 없으므로, 이번 프레임이 쓰는 버퍼면 잘못된 갱신이다. 판정은 백엔드 상태가 아니라 다섯 백엔드에 공통인 사실이다:
     *          - 이번 프레임이 그 버퍼의 전이 · 복사를 기록했거나,
     *          - 첫 내용이 이번 프레임을 열기 **전에** 올라갔다. 바인딩은 세지 않는다 — bindless 번호는 루트 상수 · 구조 버퍼 안의 값으로도 건너가 기록 자리에서 다 볼 수 없다.
     *            앞 프레임에 내용이 올라간 버퍼는 이번 프레임이 읽을 수 있는 버퍼다.
     *          새 버퍼의 첫 내용(이번 프레임 안에 만들어 나눠 올리는 것도)은 지나간다. 언리얼은 다른 스레드의 갱신을 RHI 명령 리스트로만 받아 이 모양이 없다.
     */
    class SW_API RHIBufferUploadLog
    {
    public:
        /** @brief 이 스레드가 기록할 새 프레임을 엽니다. 지난 프레임의 전이를 잊습니다. */
        void beginFrame();
        /** @brief 프레임을 닫습니다. 다음 `beginFrame` 까지 어느 갱신도 잘못이 아니다. */
        void endFrame();

        /** @brief @p buffer 에 내용이 올라갔다고 적습니다(처음 한 번만 남는다). */
        void recordContent( RHIBufferHandle buffer );
        /** @brief 이번 프레임이 @p buffer 의 전이 · 복사를 기록했다고 적습니다. */
        void recordUse( RHIBufferHandle buffer );
        /** @brief 놓은 버퍼를 잊습니다. */
        void remove( RHIBufferHandle buffer );

        /** @brief 다른 스레드가 지금 기록하는 프레임이 @p buffer 를 쓰면 true 입니다(위 판정). 프레임을 기록하는 스레드 자신이면 false 입니다. */
        [[nodiscard]] bool isUsedByFrameOnOtherThread( RHIBufferHandle buffer ) const;

    private:
        unordered_map<RHIBufferHandle, uint64> _mapContentFrame SW_GUARDED_BY( _mutex ); ///< 버퍼 → 첫 내용을 올린 프레임 번호
        unordered_set<RHIBufferHandle> _setUsedThisFrame        SW_GUARDED_BY( _mutex );
        std::thread::id _frameThreadID                          SW_GUARDED_BY( _mutex ); ///< 프레임을 기록하는 스레드(프레임 밖이면 빈 값)
        uint64 _frameNumber                                     SW_GUARDED_BY( _mutex ){ 0 };
        mutable mutex                                           _mutex;
    };
} // namespace sw
