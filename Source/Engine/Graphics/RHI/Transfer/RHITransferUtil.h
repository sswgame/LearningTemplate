/**
 * @file RHITransferUtil.h
 * @brief 전송(업로드 · 되읽기)의 동기 변형입니다. 제출한 뒤 `pollTransfer` 를 되풀이하며 기다립니다(docs/plans/RHIAsyncUploads.md 2.2).
 * @details 백엔드가 아니라 Engine 코드라 vtable 에 없다. 백엔드에 "기다리는 두 번째 경로"가 생기지 않는 것이 핵심이다.
 *          사용처는 테스트, 도구, 스크린샷, 에디터 캡처, 동기 텍스처 `acquire` 이다. **RT 프레임 셋업 구간에서는 쓰지 않는다**(그 자리는 제출 → 다음 프레임 폴링).
 *          셋업 구간은 `ScopedForbidBlockingTransfer` 로 표시하고, 그 안에서 `...AndWait` 를 부르면 Debug 에서 단언이 걸린다.
 *          기다리는 상한은 DX11 `waitIdle` 의 상한과 같은 10 초이고, 넘으면 오류를 남기고 실패를 돌려준다(시간 초과는 `RHITransferWaitResult::TimedOut` 이라 어떤
 *          `RHITransferStatus` 와도 섞이지 않는다). 기다리는 동안은 몇 번 `yield` 한 뒤 짧게 잔다.
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
     * @enum RHITransferWaitResult
     * @brief `RHITransferUtil` 의 대기가 끝난 이유입니다. 백엔드가 돌려주는 `RHITransferStatus` 에 "시간 초과" 를 끼워 넣지 않으려고 따로 둡니다.
     */
    enum class RHITransferWaitResult : uint8
    {
        Complete = 0, ///< 전송이 끝났다.
        Failed   = 1, ///< 백엔드가 `Failed` 를 돌려줬다(장치 손실, 잘못된 티켓). 오류는 백엔드가 남겼다.
        TimedOut = 2, ///< 상한 안에 끝나지 않았다. 오류는 대기 쪽이 남겼다. 읽기 티켓이면 호출자가 버려야 한다.
    };

    /**
     * @class RHITransferUtil
     * @brief 제출 → 폴링을 한 호출로 묶은 동기 헬퍼 모음입니다.
     */
    class SW_API RHITransferUtil
    {
    public:
        /** @brief 기다리는 상한(밀리초)입니다. DX11 `waitIdle` 의 상한(10 초)과 같습니다. */
        static constexpr uint32 kWaitTimeoutMilliseconds = 10000;
        /** @brief 이 횟수까지는 `yield` 만 하고, 그 뒤로는 짧게 잡니다. 끝나기 직전이면 곧바로 보고, 오래 걸리면 코어를 놓아 줍니다. */
        static constexpr uint32 kYieldCountBeforeSleep = 32;
        /** @brief 양보를 마친 뒤 한 번에 자는 시간(마이크로초)입니다. */
        static constexpr uint32 kSleepMicroseconds = 200;

        /**
         * @brief 폴링 함수 `poll()`(`RHITransferStatus` 를 돌려주는 호출 가능한 것)가 `Pending` 을 벗어날 때까지 되풀이합니다.
         * @details 템플릿인 까닭은 가짜 폴링으로 시간 초과와 `Failed` 경로를 디바이스 없이 시험하기 위해서다. 실제 호출은 팩토리를 받는 오버로드가 한다.
         * @return `Complete`, `Failed`, 또는 시간이 다 되면 오류를 남기고 `TimedOut` 입니다.
         */
        template <typename PollFn>
        [[nodiscard]] static RHITransferWaitResult waitWhilePending( PollFn&& poll, uint32 timeoutMilliseconds = kWaitTimeoutMilliseconds )
        {
            const Deadline deadline = Deadline::afterMilliseconds( timeoutMilliseconds );
            for ( uint32 iteration = 0;; ++iteration )
            {
                const RHITransferStatus status = poll();
                if ( status == RHITransferStatus::Complete )
                    return RHITransferWaitResult::Complete;
                if ( status == RHITransferStatus::Failed )
                    return RHITransferWaitResult::Failed;
                if ( deadline.isExpired() )
                {
                    reportTimeout( timeoutMilliseconds );
                    return RHITransferWaitResult::TimedOut;
                }
                backoff( iteration );
            }
        }

        /**
         * @brief 제출 함수 `submit()`(`RHITransferSubmit` 을 돌려주는 호출 가능한 것)을 `Busy` 가 아닐 때까지 되풀이합니다.
         * @details 스테이징이 모자라 `Busy` 이면 GPU 가 진행해 구간을 회수하기를 기다린다. `Busy` 는 다시 시도하면 될 수 있을 때만 쓰는 값이라 이 되풀이가 끝난다.
         * @param outSubmit 마지막 제출의 결과입니다(`Accepted`, `Rejected`, 시간 초과면 마지막 `Busy`).
         * @return 시간 안에 `Busy` 를 벗어났으면 true 입니다. 시간이 다 되면 오류를 남기고 false 입니다(`Rejected` 로 바꿔 돌려주지 않는다).
         */
        template <typename SubmitFn>
        [[nodiscard]] static bool submitWhileBusy( SubmitFn&& submit, RHITransferSubmit& outSubmit, uint32 timeoutMilliseconds = kWaitTimeoutMilliseconds )
        {
            const Deadline deadline = Deadline::afterMilliseconds( timeoutMilliseconds );
            for ( uint32 iteration = 0;; ++iteration )
            {
                outSubmit = submit();
                if ( outSubmit._result != RHISubmitResult::Busy )
                    return true;
                if ( deadline.isExpired() )
                {
                    reportTimeout( timeoutMilliseconds );
                    return false;
                }
                backoff( iteration );
            }
        }

        /** @brief 티켓이 `Complete` 가 될 때까지 기다립니다. `Failed` 이거나 시간이 다 되면 false 입니다. 무효 티켓도 false 입니다. */
        [[nodiscard]] static bool waitForTransfer( IRHIResourceFactory& factory, RHITransferTicket ticket );

        /** @brief `submitTextureUpload` 를 제출하고 끝날 때까지 기다립니다. 거절, 실패, 시간 초과면 false 입니다. */
        [[nodiscard]] static bool uploadTexture2DAndWait( IRHIResourceFactory& factory, RHITextureHandle texture, const RHITextureUploadDesc& desc );

        /** @brief `submitTextureRegionUpload` 를 제출하고 끝날 때까지 기다립니다. 거절, 실패, 시간 초과면 false 입니다. */
        [[nodiscard]] static bool uploadTexture2DRegionAndWait( IRHIResourceFactory& factory, RHITextureHandle texture, const RHITextureRegionUploadDesc& desc );

        /**
         * @brief `submitTextureReadback` 을 제출하고 끝날 때까지 기다린 뒤 결과를 가져옵니다. 거절, 실패, 시간 초과면 false 입니다.
         * @details 실패나 시간 초과로 돌아갈 때는 읽기 티켓을 버려(`PendingReadback` 의 소멸자) 백엔드가 쥔 스테이징이 새지 않는다.
         */
        [[nodiscard]] static bool readbackTexture2DAndWait( IRHIResourceFactory& factory, RHITextureHandle texture, uint32 mip, uint32 arraySlice, vector<uint8>& outBytes,
                                                            RHITextureMipSpan& outLayout );

        /**
         * @class ScopedForbidBlockingTransfer
         * @brief 이 스코프 안(이 스레드)에서 `...AndWait` 를 부르면 Debug 단언이 걸리게 합니다. RT 프레임 셋업 구간을 표시하는 데 씁니다.
         * @details 셋업 구간에서 GPU 를 기다리면 프레임 히치가 된다(docs/plans/RHIAsyncUploads.md 2.1). 규칙을 주석이 아니라 코드로 두려는 것이다. 중첩할 수 있다.
         */
        class SW_API ScopedForbidBlockingTransfer
        {
        public:
            ScopedForbidBlockingTransfer();
            ~ScopedForbidBlockingTransfer();
            ScopedForbidBlockingTransfer( const ScopedForbidBlockingTransfer& )            = delete;
            ScopedForbidBlockingTransfer& operator=( const ScopedForbidBlockingTransfer& ) = delete;
        };

        /**
         * @class PendingReadback
         * @brief 읽기 티켓의 소유자입니다. 가져가지 않은 채 사라지면 소멸자가 `discardTextureReadback` 으로 결과를 버립니다.
         * @details 읽기 티켓은 백엔드가 결과(또는 스테이징)를 들고 있어 짝이 되는 `take` 나 `discard` 가 없으면 새어 나간다. 손으로 짝을 맞추지 않게 이동만 되는 RAII 로 둔다.
         *          쥔 동안은 이 스레드가 제출하는 `...AndWait`(업로드 · 읽기)를 부르면 Debug 에서 단언이 걸린다. 쥔 티켓들이 스테이징의 남은 자리를 다 차지하면 그 제출이
         *          `Busy` 로 10 초를 기다리다 시간 초과가 되기 때문이다. 한 스레드에서 만들고 그 스레드에서 놓는다.
         *          한계: 다른 스레드가 쥔 티켓과 `PendingReadback` 에 넣지 않은 날 티켓은 세지 못한다(이유만: 그 수는 백엔드의 표에 있어 이 헬퍼가 볼 수 없다).
         *          **팩토리(장치)보다 오래 살면 안 된다.** 백엔드를 바꾸기 전에 비운다(옛 티켓은 장치 세대 때문에 새 장치에서 `Failed` 이지만, 팩토리 포인터는 지켜 주지 못한다).
         */
        class SW_API PendingReadback
        {
        public:
            /** @brief 비어 있는 소유자입니다. */
            PendingReadback() noexcept
                : _pFactory{ nullptr }
                , _ticket{}
                , _pHeldSlot{ nullptr }
            {
            }

            /** @brief `submitTextureReadback` 이 `Accepted` 로 돌려준 티켓을 넘겨받습니다. 유효한 티켓이면 이 스레드의 쥔 티켓 수를 하나 올립니다. */
            PendingReadback( IRHIResourceFactory& factory, RHITransferTicket ticket ) noexcept
                : _pFactory{ &factory }
                , _ticket{ ticket }
                , _pHeldSlot{ ticket.isValid() ? acquireHeldSlot() : nullptr }
            {
            }

            ~PendingReadback() { discard(); }

            PendingReadback( const PendingReadback& )            = delete;
            PendingReadback& operator=( const PendingReadback& ) = delete;

            PendingReadback( PendingReadback&& other ) noexcept
                : _pFactory{ other._pFactory }
                , _ticket{ other._ticket }
                , _pHeldSlot{ other._pHeldSlot }
            {
                other._pFactory  = nullptr;
                other._ticket    = RHITransferTicket{};
                other._pHeldSlot = nullptr;
            }

            PendingReadback& operator=( PendingReadback&& other ) noexcept
            {
                if ( this != &other )
                {
                    discard();
                    _pFactory        = other._pFactory;
                    _ticket          = other._ticket;
                    _pHeldSlot       = other._pHeldSlot;
                    other._pFactory  = nullptr;
                    other._ticket    = RHITransferTicket{};
                    other._pHeldSlot = nullptr;
                }
                return *this;
            }

            /** @brief 티켓을 들고 있으면 true 입니다. */
            [[nodiscard]] bool isValid() const noexcept { return _pFactory != nullptr && _ticket.isValid(); }

            /** @brief 들고 있는 티켓입니다. */
            [[nodiscard]] RHITransferTicket getTicket() const noexcept { return _ticket; }

            /** @brief 진행 상태를 묻습니다. 비어 있으면 `Failed` 입니다. */
            [[nodiscard]] RHITransferStatus poll() const
            {
                return isValid() ? _pFactory->pollTransfer( _ticket ) : RHITransferStatus::Failed;
            }

            /** @brief 완료된 결과를 가져옵니다. 성공하면 티켓을 놓고(소멸자가 버리지 않는다), 실패하면 계속 들고 있어 호출자가 다시 시도하거나 버리게 둡니다. */
            [[nodiscard]] bool take( vector<uint8>& outBytes )
            {
                if ( isValid() == false )
                    return false;
                if ( _pFactory->takeTextureReadback( _ticket, outBytes ) == false )
                    return false;
                releaseHeldSlot( _pHeldSlot );
                _pFactory  = nullptr;
                _ticket    = RHITransferTicket{};
                _pHeldSlot = nullptr;
                return true;
            }

            /** @brief 결과를 버립니다. 비어 있으면 아무 일도 하지 않습니다. */
            void discard() noexcept
            {
                if ( isValid() )
                    _pFactory->discardTextureReadback( _ticket );
                releaseHeldSlot( _pHeldSlot );
                _pFactory  = nullptr;
                _ticket    = RHITransferTicket{};
                _pHeldSlot = nullptr;
            }

        private:
            IRHIResourceFactory* _pFactory;
            RHITransferTicket    _ticket;
            void*                _pHeldSlot; ///< 쥔 티켓 수를 올린 스레드의 계수 칸입니다(그 스레드에서만 놓을 수 있다). 비어 있으면 nullptr 입니다.
        };

    private:
        /** @brief 시간 초과 오류를 로그로 남깁니다(템플릿 본문이 로그 매크로를 헤더에 들이지 않게 cpp 에 둡니다). */
        static void reportTimeout( uint32 timeoutMilliseconds );
        /** @brief 기다리는 `iteration` 번째 걸음입니다. 처음 `kYieldCountBeforeSleep` 번은 다른 스레드에 양보하고, 그 뒤로는 `kSleepMicroseconds` 만큼 잡니다. */
        static void backoff( uint32 iteration );
        /** @brief 이 스레드가 `ScopedForbidBlockingTransfer` 안이면 Debug 에서 단언이 걸립니다. */
        static void assertBlockingAllowed();
        /** @brief `assertBlockingAllowed` 에 더해, 이 스레드가 `PendingReadback` 을 쥐고 있어도 Debug 에서 단언이 걸립니다(제출하는 `...AndWait` 진입에서 부릅니다). */
        static void assertNoHeldReadbackBeforeSubmit();
        /** @brief 이 스레드의 쥔 읽기 티켓 수를 올리고 그 계수 칸을 돌려줍니다. */
        static void* acquireHeldSlot() noexcept;
        /** @brief `acquireHeldSlot` 의 칸을 놓습니다. 다른 스레드에서 놓으면 단언이 걸리고 아무것도 하지 않습니다. nullptr 이면 아무 일도 하지 않습니다. */
        static void releaseHeldSlot( void* pSlot ) noexcept;
    };
} // namespace sw
