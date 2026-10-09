/**
 * @file SocialLinkRules.h
 * @brief 관계 바꾸기 규칙 — (나 → 상대, 상대 → 나) 상태와 양쪽 개수를 받아 새 상태 · 개수 차이 · 결과 · 알림을 정합니다. 저장소를 모릅니다(시험이 표로 본다).
 * @details - 서로 신청하면 바로 친구(뒤에 신청한 쪽이 자동 수락).
 *          - 나를 막은 사람에게 신청하면 아무것도 쓰지 않고 Ok — 막힌 것을 알리지 않는다(Steam · Nakama 와 같다). 내가 막은 사람에게 신청하면 YouBlocked.
 *          - 막으면 두 줄(친구 · 신청)을 지운다. 상대가 나를 이미 막았으면 그 줄은 둔다(서로 막음).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Social/Shared/SocialTypes.h"

namespace sw
{
    /** @brief 관계 바꾸기 종류입니다. */
    enum class SocialLinkOperation : uint8
    {
        Request = 0,
        Accept,
        Decline,
        Remove,
        Block,
        Unblock
    };
} // namespace sw

namespace sw
{
    /** @brief 한 사람의 개수입니다(`social_count` 레코드). */
    struct SocialCounts
    {
        int32 _friendCount{ 0 };
        int32 _incomingCount{ 0 };
        int32 _outgoingCount{ 0 };
        int32 _blockedCount{ 0 };

        bool isZero() const { return _friendCount == 0 && _incomingCount == 0 && _outgoingCount == 0 && _blockedCount == 0; }
    };
} // namespace sw

namespace sw
{
    /** @brief 규칙의 결정입니다. `_newOtherState` 는 "상대 → 나" 줄의 새 상태다(None = 지움). */
    struct SocialLinkDecision
    {
        SocialCounts    _selfDelta{};
        SocialCounts    _otherDelta{};
        SocialLinkState _newSelfState{ SocialLinkState::None };
        SocialLinkState _newOtherState{ SocialLinkState::None };
        SocialResult    _result{ SocialResult::Ok };
        uint8           _bWrite{ SW_FALSE }; ///< 아무것도 쓰지 않는 Ok(나를 막은 사람에게 신청 · 이미 막음)
        uint8           _bNotifyOtherRequested{ SW_FALSE };
        uint8           _bNotifyBothAdded{ SW_FALSE };
        uint8           _bNotifyOtherRemoved{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 관계 바꾸기 규칙(순수 함수)입니다. */
    struct SW_GF_API SocialLinkRules
    {
        static SocialLinkDecision decide( SocialLinkOperation operation, SocialLinkState selfState, SocialLinkState otherState, const SocialCounts& selfCounts,
                                          const SocialCounts& otherCounts );
    };
} // namespace sw
