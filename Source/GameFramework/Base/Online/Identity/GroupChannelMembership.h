/**
 * @file GroupChannelMembership.h
 * @brief 길드와 파티의 채팅 채널 멤버를 넣고 빼는 인터페이스입니다. 채팅 키트가 구현해 서버 조립 문맥(`OnlineServerKitContext::_pGroupChannels`)에 내놓고,
 *        친구 키트(길드)와 매칭 키트(파티)가 자기 이벤트에서 부릅니다.
 * @details - 키트끼리 include 하지 않도록 채널은 (종류, 그룹 id)로만 가리킵니다. 채널 id 규칙(`guild.<16진>`, `party.<16진>`)은 채팅 키트가 정합니다.
 *          - 멤버는 이 서버에 접속한 계정이어야 합니다. 다른 서버의 계정은 그 서버가 자기 이벤트(로그인 뒤 길드 붙이기)에서 넣습니다.
 *          - 서비스 스레드 하나에서만 씁니다. 채팅 키트가 시작하기 전이나 내린 뒤의 호출은 무시됩니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"

namespace sw
{
    /** @brief 그룹 채널의 종류입니다. */
    enum class GroupChannelKind : uint8
    {
        Guild = 0,
        Party
    };
} // namespace sw

namespace sw
{
    /**
     * @class IGroupChannelMembership
     * @brief 그룹 채널 멤버를 바꾸는 인터페이스입니다.
     */
    class IGroupChannelMembership
    {
    public:
        IGroupChannelMembership()          = default;
        virtual ~IGroupChannelMembership() = default;

        IGroupChannelMembership( const IGroupChannelMembership& )            = delete;
        IGroupChannelMembership& operator=( const IGroupChannelMembership& ) = delete;

        /** @brief @p accountID 를 그룹 채널에 넣습니다. 이 서버에 없는 계정이거나 채널 수 상한이면 넣지 않습니다(경고 로그). */
        virtual void addGroupMember( GroupChannelKind kind, uint64 groupID, AccountID accountID, int64 nowMs ) = 0;
        virtual void removeGroupMember( GroupChannelKind kind, uint64 groupID, AccountID accountID )           = 0;
    };
} // namespace sw
