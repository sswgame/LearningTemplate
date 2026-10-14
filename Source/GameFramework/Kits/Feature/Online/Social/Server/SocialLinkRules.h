/**
 * @file SocialLinkRules.h
 * @brief 관계 바꾸기 규칙입니다. (나에서 상대로, 상대에서 나로) 상태와 양쪽 개수를 받아 새 상태, 개수 변화, 결과, 알림을 정합니다. 저장소는 모르며 테스트가 테이블로 확인합니다.
 * @details - 서로 신청하면 바로 친구가 된다. 뒤에 신청한 쪽이 자동으로 수락하는 셈이다.
 *          - 나를 막은 사람에게 신청하면 아무것도 쓰지 않고 Ok 를 반환한다. 막힌 사실을 알리지 않기 위해서이며 Steam 과 Nakama 도 같다. 내가 막은 사람에게 신청하면 YouBlocked 이다.
 *          - 막으면 두 줄(친구와 신청)을 지운다. 상대가 나를 이미 막았으면 그 줄은 둔다. 서로 막은 상태가 된다.
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
    /** @brief 한 사람의 관계 개수입니다. `social_count` 레코드에 저장합니다. */
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
    /** @brief 규칙의 결정입니다. `_newOtherState` 는 "상대에서 나로" 가는 줄의 새 상태이며 None 이면 줄을 지웁니다. */
    struct SocialLinkDecision
    {
        SocialCounts    _selfDelta{};
        SocialCounts    _otherDelta{};
        SocialLinkState _newSelfState{ SocialLinkState::None };
        SocialLinkState _newOtherState{ SocialLinkState::None };
        SocialResult    _result{ SocialResult::Ok };
        uint8           _bWrite{ SW_FALSE }; ///< true 이면 쓴다. 아무것도 쓰지 않는 Ok 는 나를 막은 사람에게 신청했거나 이미 막은 경우이다
        uint8           _bNotifyOtherRequested{ SW_FALSE };
        uint8           _bNotifyBothAdded{ SW_FALSE };
        uint8           _bNotifyOtherRemoved{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 관계 바꾸기 규칙을 담은 순수 함수 모음입니다. */
    struct SW_KIT_SERVER_API SocialLinkRules
    {
        static SocialLinkDecision decide( SocialLinkOperation operation, SocialLinkState selfState, SocialLinkState otherState, const SocialCounts& selfCounts,
                                          const SocialCounts& otherCounts );
    };
} // namespace sw
