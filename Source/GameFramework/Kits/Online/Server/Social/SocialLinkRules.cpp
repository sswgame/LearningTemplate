#include "pch.h"

#include "GameFramework/Kits/Online/Server/Social/SocialLinkRules.h"

namespace sw
{
    namespace
    {
        struct SocialLinkRulesInternal
        {
            static SocialLinkDecision makeResult( SocialResult result )
            {
                SocialLinkDecision decision;
                decision._result = result;
                return decision;
            }

            /** @brief 받은 신청을 받아 친구가 되는 결정(수락 · 서로 신청)입니다 — 친구 상한을 본다. */
            static SocialLinkDecision makeFriends( const SocialCounts& selfCounts, const SocialCounts& otherCounts )
            {
                if ( selfCounts._friendCount >= SocialLimit::kMaxFriend || otherCounts._friendCount >= SocialLimit::kMaxFriend )
                    return makeResult( SocialResult::FriendLimit );
                SocialLinkDecision decision;
                decision._bWrite                    = SW_TRUE;
                decision._newSelfState              = SocialLinkState::Friend;
                decision._newOtherState             = SocialLinkState::Friend;
                decision._selfDelta._friendCount    = 1;
                decision._selfDelta._incomingCount  = -1;
                decision._otherDelta._friendCount   = 1;
                decision._otherDelta._outgoingCount = -1;
                decision._bNotifyBothAdded          = SW_TRUE;
                return decision;
            }

            /** @brief 지금 두 줄을 지울 때의 개수 차이를 더합니다. */
            static void subtractExisting( SocialLinkState selfState, SocialLinkDecision& inoutDecision )
            {
                if ( selfState == SocialLinkState::Friend )
                {
                    inoutDecision._selfDelta._friendCount -= 1;
                    inoutDecision._otherDelta._friendCount -= 1;
                }
                else if ( selfState == SocialLinkState::Outgoing )
                {
                    inoutDecision._selfDelta._outgoingCount -= 1;
                    inoutDecision._otherDelta._incomingCount -= 1;
                }
                else if ( selfState == SocialLinkState::Incoming )
                {
                    inoutDecision._selfDelta._incomingCount -= 1;
                    inoutDecision._otherDelta._outgoingCount -= 1;
                }
            }

            static SocialLinkDecision decideRequest( SocialLinkState selfState, SocialLinkState otherState, const SocialCounts& selfCounts, const SocialCounts& otherCounts )
            {
                if ( selfState == SocialLinkState::Blocked )
                    return makeResult( SocialResult::YouBlocked );
                if ( otherState == SocialLinkState::Blocked )
                    return makeResult( SocialResult::Ok ); // 나를 막은 사람 — 아무것도 하지 않고 성공처럼
                if ( selfState == SocialLinkState::Friend )
                    return makeResult( SocialResult::AlreadyFriend );
                if ( selfState == SocialLinkState::Outgoing )
                    return makeResult( SocialResult::AlreadyRequested );
                if ( selfState == SocialLinkState::Incoming )
                    return makeFriends( selfCounts, otherCounts ); // 서로 신청 — 바로 친구
                const bool bPendingFull = selfCounts._outgoingCount >= SocialLimit::kMaxOutgoing || otherCounts._incomingCount >= SocialLimit::kMaxIncoming;
                if ( bPendingFull )
                    return makeResult( SocialResult::PendingLimit );
                SocialLinkDecision decision;
                decision._bWrite                    = SW_TRUE;
                decision._newSelfState              = SocialLinkState::Outgoing;
                decision._newOtherState             = SocialLinkState::Incoming;
                decision._selfDelta._outgoingCount  = 1;
                decision._otherDelta._incomingCount = 1;
                decision._bNotifyOtherRequested     = SW_TRUE;
                return decision;
            }

            static SocialLinkDecision decideRemove( SocialLinkOperation operation, SocialLinkState selfState )
            {
                const bool bDeclinable = selfState == SocialLinkState::Incoming;
                const bool bRemovable  = selfState == SocialLinkState::Friend || selfState == SocialLinkState::Outgoing || selfState == SocialLinkState::Incoming;
                if ( ( operation == SocialLinkOperation::Decline ? bDeclinable : bRemovable ) == false )
                    return makeResult( SocialResult::NotFound );
                SocialLinkDecision decision;
                decision._bWrite              = SW_TRUE;
                decision._bNotifyOtherRemoved = selfState == SocialLinkState::Friend ? SW_TRUE : SW_FALSE;
                subtractExisting( selfState, decision );
                return decision;
            }

            static SocialLinkDecision decideBlock( SocialLinkState selfState, SocialLinkState otherState, const SocialCounts& selfCounts )
            {
                if ( selfState == SocialLinkState::Blocked )
                    return makeResult( SocialResult::Ok );
                if ( selfCounts._blockedCount >= SocialLimit::kMaxBlocked )
                    return makeResult( SocialResult::BlockLimit );
                SocialLinkDecision decision;
                decision._bWrite                  = SW_TRUE;
                decision._newSelfState            = SocialLinkState::Blocked;
                decision._newOtherState           = otherState == SocialLinkState::Blocked ? SocialLinkState::Blocked : SocialLinkState::None; // 서로 막음은 둔다
                decision._selfDelta._blockedCount = 1;
                decision._bNotifyOtherRemoved     = selfState == SocialLinkState::Friend ? SW_TRUE : SW_FALSE;
                subtractExisting( selfState, decision );
                return decision;
            }

            static SocialLinkDecision decideUnblock( SocialLinkState selfState, SocialLinkState otherState )
            {
                if ( selfState != SocialLinkState::Blocked )
                    return makeResult( SocialResult::NotFound );
                SocialLinkDecision decision;
                decision._bWrite                  = SW_TRUE;
                decision._newSelfState            = SocialLinkState::None;
                decision._newOtherState           = otherState; // 상대 줄(상대가 나를 막았으면 그대로)은 손대지 않는다
                decision._selfDelta._blockedCount = -1;
                return decision;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SocialLinkDecision SocialLinkRules::decide( SocialLinkOperation operation, SocialLinkState selfState, SocialLinkState otherState, const SocialCounts& selfCounts,
                                                const SocialCounts& otherCounts )
    {
        using Internal = SocialLinkRulesInternal;
        switch ( operation )
        {
            case SocialLinkOperation::Request:
                return Internal::decideRequest( selfState, otherState, selfCounts, otherCounts );
            case SocialLinkOperation::Accept:
                return selfState == SocialLinkState::Incoming ? Internal::makeFriends( selfCounts, otherCounts ) : Internal::makeResult( SocialResult::NotFound );
            case SocialLinkOperation::Decline:
            case SocialLinkOperation::Remove:
                return Internal::decideRemove( operation, selfState );
            case SocialLinkOperation::Block:
                return Internal::decideBlock( selfState, otherState, selfCounts );
            case SocialLinkOperation::Unblock:
                return Internal::decideUnblock( selfState, otherState );
        }
        return Internal::makeResult( SocialResult::Invalid );
    }
} // namespace sw
