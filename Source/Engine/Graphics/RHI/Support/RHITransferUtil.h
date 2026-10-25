/**
 * @file RHITransferUtil.h
 * @brief 전송(업로드 · 되읽기)의 동기 변형입니다. 제출한 뒤 `pollTransfer` 를 양보하며 되풀이합니다(docs/plans/RhiAsyncUploads.md 2.2).
 * @details 백엔드가 아니라 Engine 코드라 vtable 에 없다. 백엔드에 "기다리는 두 번째 경로"가 생기지 않는 것이 핵심이다.
 *          사용처는 테스트, 도구, 스크린샷, 에디터 캡처, 동기 텍스처 `acquire` 이다. **RT 프레임 셋업 구간에서는 쓰지 않는다**(그 자리는 제출 → 다음 프레임 폴링).
 *          기다리는 상한은 DX11 `waitIdle` 의 상한과 같은 10 초이고, 넘으면 오류를 남기고 실패를 돌려준다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Graphics/RHI/IRHIResourceFactory.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
    /**
     * @class RHITransferUtil
     * @brief 제출 → 폴링을 한 호출로 묶은 동기 헬퍼 모음입니다.
     */
    class SW_API RHITransferUtil
    {
    public:
        /** @brief 기다리는 상한(밀리초)입니다. DX11 `waitIdle` 의 상한(10 초)과 같습니다. */
        static constexpr uint32 kWaitTimeoutMilliseconds = 10000;

        /**
         * @brief 폴링 함수 `poll()`(`RHITransferStatus` 를 돌려주는 호출 가능한 것)가 `Pending` 을 벗어날 때까지 양보하며 되풀이합니다.
         * @details 템플릿인 까닭은 가짜 폴링으로 시간 초과와 `Failed` 경로를 디바이스 없이 시험하기 위해서다. 실제 호출은 팩토리를 받는 오버로드가 한다.
         * @return `Complete` 또는 `Failed` 입니다. 시간이 다 되면 오류를 남기고 `Pending` 을 돌려줍니다(호출자는 실패로 다룬다).
         */
        template <typename PollFn>
        [[nodiscard]] static RHITransferStatus waitWhilePending( PollFn&& poll, uint32 timeoutMilliseconds = kWaitTimeoutMilliseconds )
        {
            const Deadline deadline = Deadline::afterMilliseconds( timeoutMilliseconds );
            for ( ;; )
            {
                const RHITransferStatus status = poll();
                if ( status != RHITransferStatus::Pending )
                    return status;
                if ( deadline.isExpired() )
                {
                    reportTimeout( timeoutMilliseconds );
                    return RHITransferStatus::Pending;
                }
                yieldThread();
            }
        }

        /**
         * @brief 제출 함수 `submit()`(`RHITransferSubmit` 을 돌려주는 호출 가능한 것)을 `Busy` 가 아닐 때까지 되풀이합니다.
         * @details 스테이징이 모자라 `Busy` 이면 GPU 가 진행해 구간을 회수하기를 양보하며 기다린다. 시간이 다 되면 오류를 남기고 `Rejected` 를 돌려준다.
         */
        template <typename SubmitFn>
        [[nodiscard]] static RHITransferSubmit submitWhileBusy( SubmitFn&& submit, uint32 timeoutMilliseconds = kWaitTimeoutMilliseconds )
        {
            const Deadline deadline = Deadline::afterMilliseconds( timeoutMilliseconds );
            for ( ;; )
            {
                const RHITransferSubmit result = submit();
                if ( result._result != RHISubmitResult::Busy )
                    return result;
                if ( deadline.isExpired() )
                {
                    reportTimeout( timeoutMilliseconds );
                    return RHITransferSubmit::rejected();
                }
                yieldThread();
            }
        }

        /** @brief 티켓이 `Complete` 가 될 때까지 기다립니다. `Failed` 이거나 시간이 다 되면 false 입니다. 무효 티켓도 false 입니다. */
        [[nodiscard]] static bool waitForTransfer( IRHIResourceFactory& factory, RHITransferTicket ticket );

        /** @brief `submitTextureUpload` 를 제출하고 끝날 때까지 기다립니다. 거절, 실패, 시간 초과면 false 입니다. */
        [[nodiscard]] static bool uploadTexture2DAndWait( IRHIResourceFactory& factory, RHITextureHandle texture, const RHITextureUploadDesc& desc );

        /** @brief `submitTextureRegionUpload` 를 제출하고 끝날 때까지 기다립니다. 거절, 실패, 시간 초과면 false 입니다. */
        [[nodiscard]] static bool uploadTexture2DRegionAndWait( IRHIResourceFactory& factory, RHITextureHandle texture, const RHITextureRegionUploadDesc& desc );

        /** @brief `submitTextureReadback` 을 제출하고 끝날 때까지 기다린 뒤 결과를 가져옵니다. 거절, 실패, 시간 초과면 false 입니다. */
        [[nodiscard]] static bool readbackTexture2DAndWait( IRHIResourceFactory& factory, RHITextureHandle texture, uint32 mip, uint32 arraySlice, vector<uint8>& outBytes,
                                                            RHITextureMipSpan& outLayout );

    private:
        /** @brief 시간 초과 오류를 로그로 남깁니다(템플릿 본문이 로그 매크로를 헤더에 들이지 않게 cpp 에 둡니다). */
        static void reportTimeout( uint32 timeoutMilliseconds );
        /** @brief 다른 스레드에 양보합니다. */
        static void yieldThread();
    };
} // namespace sw
