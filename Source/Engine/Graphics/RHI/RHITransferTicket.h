/**
 * @file RHITransferTicket.h
 * @brief 제출한 전송 하나의 티켓과 전송 제출 결과입니다.
 */
#pragma once
#include "Engine/Graphics/RHI/RHIEnums.h"

namespace sw
{
    /**
     * @struct RHITransferTicket
     * @brief 제출한 전송 하나를 가리키는 단조 증가 시리얼과, 그것을 발급한 장치의 세대입니다. 시리얼 0 은 무효입니다.
     * @details 정수 별칭이 아니라 구조체라서 `uint64` 를 티켓 자리에 실수로 넘기는 일은 컴파일에서 막힌다. 멤버가 공개라 값을 직접 짓는 코드까지 막지는 못한다 — 그 틈은
     *          백엔드가 막는다: 시리얼 0, 다른 세대, 발급한 적 없는 시리얼은 `pollTransfer` 가 오류를 남기고 `Failed` 로 돌려준다(`RHITransferSerials::classify`).
     *          해제 함수가 없다 — 완료 워터마크가 시리얼을 덮는다. 읽기 티켓의 결과는 `takeTextureReadback` 으로 가져가거나 `discardTextureReadback` 으로 버린다.
     */
    struct RHITransferTicket
    {
        uint64 _serial{ 0 };           ///< 0 이면 무효
        uint32 _deviceGeneration{ 0 }; ///< 발급한 장치의 세대(`RHIBackendRegistry::createDevice` 가 붙인다). 장치가 바뀐 뒤의 옛 티켓을 가려낸다.

        /** @brief 제출이 받아들여져 발급된 티켓이면 true 입니다. */
        [[nodiscard]] constexpr bool isValid() const noexcept { return _serial != 0; }
        friend constexpr bool        operator==( const RHITransferTicket& lhs, const RHITransferTicket& rhs ) noexcept
        {
            return lhs._serial == rhs._serial && lhs._deviceGeneration == rhs._deviceGeneration;
        }
        friend constexpr bool operator!=( const RHITransferTicket& lhs, const RHITransferTicket& rhs ) noexcept { return ( lhs == rhs ) == false; }
    };

    /**
     * @struct RHITransferSubmit
     * @brief 전송 제출 함수의 반환값입니다. 결과를 버리면 컴파일 경고이고, 티켓은 `Accepted` 일 때만 유효합니다.
     * @details 결과를 보지 않으면 티켓도 얻을 수 없는 모양이라 "제출하고 잊기"가 쓰이지 않는다. `Busy` 의 재시도는 호출자가 다음 프레임에 한다.
     */
    struct [[nodiscard]] RHITransferSubmit
    {
        RHISubmitResult   _result{ RHISubmitResult::Rejected };
        RHITransferTicket _ticket{};

        /** @brief 받아들여졌고 티켓이 유효하면 true 입니다. */
        [[nodiscard]] constexpr bool isAccepted() const noexcept { return _result == RHISubmitResult::Accepted && _ticket.isValid(); }

        /**
         * @brief 받아들인 제출을 만듭니다. 시리얼 0 은 무효 티켓이라 받아들였다고 할 수 없다 — 백엔드가 0 을 넘기면 Debug 에서 단언이 걸린다.
         * @param deviceGeneration 발급한 장치의 세대입니다(`RHIBackendRegistry::createDevice` 가 붙인 값, 0 이 아니다).
         */
        [[nodiscard]] static RHITransferSubmit accepted( uint64 serial, uint32 deviceGeneration ) noexcept
        {
            SW_ASSERT( serial != 0 && deviceGeneration != 0 );
            return RHITransferSubmit{
                RHISubmitResult::Accepted, RHITransferTicket{ serial, deviceGeneration }
            };
        }
        /** @brief 스테이징이 모자라 받지 못한 결과입니다. */
        [[nodiscard]] static constexpr RHITransferSubmit busy() noexcept { return RHITransferSubmit{ RHISubmitResult::Busy, RHITransferTicket{} }; }
        /** @brief 검사에 걸려 거절한 결과입니다. */
        [[nodiscard]] static constexpr RHITransferSubmit rejected() noexcept { return RHITransferSubmit{ RHISubmitResult::Rejected, RHITransferTicket{} }; }
        /**
         * @brief 스테이징 할당이 `Accepted` 가 아닐 때 그 결과를 제출 결과로 옮깁니다. `Busy` 는 `busy()`, 나머지는 `rejected()` 입니다.
         * @details 네 백엔드가 `allocateStagingSpan` 의 결과를 이 한 함수로 옮긴다. `Accepted` 는 티켓이 필요해 부르는 쪽이 시리얼로 만든다(`RHITransferSerials::makeAccepted`) —
         *          `Accepted` 를 넘기면 Debug 에서 단언이 걸리고 `rejected()` 를 돌려준다(티켓 없는 `Accepted` 가 호출자에게 새지 않는다).
         */
        [[nodiscard]] static RHITransferSubmit fromUnaccepted( RHISubmitResult result ) noexcept
        {
            SW_ASSERT( result != RHISubmitResult::Accepted );
            return result == RHISubmitResult::Busy ? busy() : rejected();
        }
    };
} // namespace sw
