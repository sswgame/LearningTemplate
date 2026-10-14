/**
 * @file SocialTypes.h
 * @brief 친구 키트의 타입입니다. 결과, 관계 상태, 관계 한 줄, 접속 상태, 알림 종류, 상한, 길드(역할, 멤버, 정보, 상한)를 정의합니다.
 * @details 관계에는 방향이 있다. "나에서 상대로" 가는 한 줄이 상태를 가지며, 상태는 친구, 보낸 신청, 받은 신청, 내가 막음 중 하나이다. 친구는 양쪽 줄이 모두 친구인 경우이다.
 *          접속 상태는 친구에게 보이는 "상태와 짧은 활동 문구"이다. Steam Rich Presence 와 EOS Presence 에 해당한다.
 *          계정이 어느 서버에 접속해 있는지(`IAccountPresence`)와는 별개이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 친구 키트의 업무 결과입니다. 응답 본문의 첫 값입니다. 와이어 값이므로 새 값은 끝에만 추가합니다. */
    enum class SocialResult : uint8
    {
        Ok = 0,
        NotSignedIn,
        Invalid,  ///< 자기 자신이거나 0이거나 규칙에 맞지 않는 값이다
        NotFound, ///< 그런 관계, 신청, 계정이 없다
        AlreadyFriend,
        AlreadyRequested,
        FriendLimit,
        PendingLimit, ///< 내가 보낸 신청이나 상대가 받은 신청이 가득 찼다
        BlockLimit,
        YouBlocked,  ///< 내가 막은 상대이다
        Unavailable, ///< 저장소나 캐시를 쓸 수 없다. 다시 하면 된다
        Conflict,    ///< 다시 해도 경합이 난다. 잠시 뒤에 시도한다
        GuildNameTaken,
        AlreadyInGuild,
        NotInGuild,
        NotAllowed, ///< 길드 역할이 부족하다
        GuildFull,
        InviteExpired,
        InsufficientFunds, ///< 길드 창고 — 맡기는 계정이나 꺼낼 창고의 잔액이 모자란다
        CapExceeded,       ///< 길드 창고 — 꺼내면 계정 잔액 상한을 넘는다
        StorageNotEmpty,   ///< 길드 창고에 남은 것이 있어 길드를 해산할 수 없다(먼저 꺼낸다)
        Count
    };

    SW_KIT_API const utf8* toString( SocialResult result );
} // namespace sw

namespace sw
{
    /** @brief "나에서 상대로" 가는 한 줄의 상태입니다. 와이어와 레코드에 쓰는 값입니다. */
    enum class SocialLinkState : uint8
    {
        None = 0,
        Friend,
        Outgoing, ///< 내가 신청했다
        Incoming, ///< 신청을 받았다
        Blocked,  ///< 내가 막았다
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 관계 한 줄입니다. */
    struct SocialLink
    {
        AccountID       _otherID{ kInvalidAccountID };
        int64           _sinceMs{ 0 };
        SocialLinkState _state{ SocialLinkState::None };
    };
} // namespace sw

namespace sw
{
    /** @brief 접속 상태입니다. 와이어 값입니다. */
    enum class SocialPresenceStatus : uint8
    {
        Offline = 0,
        Online,
        Away,
        Busy,
        InGame,
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 한 계정의 접속 상태입니다. 친구에게 보이는 값입니다. */
    struct SocialPresence
    {
        string               _activity{}; ///< 활동 문구이다(예: "던전 3층"). `SocialLimit::kMaxActivitySize` 바이트 이하여야 한다
        AccountID            _accountID{ kInvalidAccountID };
        SocialPresenceStatus _status{ SocialPresenceStatus::Offline };
    };
} // namespace sw

namespace sw
{
    /** @brief 알림 종류입니다. 와이어 값이므로 새 값은 끝에만 추가합니다. */
    enum class SocialNotificationKind : uint8
    {
        FriendRequested = 0, ///< 받은 신청이다. 상대는 신청한 사람이다
        FriendAdded,         ///< 친구가 되었다
        FriendRemoved,
        PresenceChanged, ///< 친구의 접속 상태가 바뀌었다
        GuildInvited,    ///< 길드 초대이다. 상대는 초대한 사람이고 길드는 `_guildID` 이다
        GuildChanged,    ///< 멤버, 역할, 공지가 바뀌었다. 다시 조회해야 하며, 내보내졌다면 조회 결과가 NotInGuild 이다
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 알림 하나입니다. 받는 사람(`_recipientID`)은 와이어에 싣지 않으며, 서버가 고른 연결로 전달됩니다. */
    struct SocialNotification
    {
        SocialPresence         _presence{}; ///< PresenceChanged
        AccountID              _recipientID{ kInvalidAccountID };
        AccountID              _otherID{ kInvalidAccountID };
        uint64                 _guildID{ 0 }; ///< 길드 알림의 길드 id 이다
        SocialNotificationKind _kind{ SocialNotificationKind::FriendRequested };
    };
} // namespace sw

namespace sw
{
    /** @brief 상한 값들입니다. 상한은 저장소 트랜잭션이 버전 조건으로 지키므로 경합이 나도 넘지 않습니다. */
    struct SocialLimit
    {
        static constexpr int32 kMaxFriend       = 200;
        static constexpr int32 kMaxIncoming     = 100;
        static constexpr int32 kMaxOutgoing     = 100;
        static constexpr int32 kMaxBlocked      = 500;
        static constexpr int32 kMaxActivitySize = 64;   ///< UTF-8 바이트 수이다
        static constexpr int32 kMaxLinkPage     = 1000; ///< 친구, 받은 신청, 보낸 신청, 막음의 합보다 크게 잡는다
    };
} // namespace sw

namespace sw
{
    /** @brief 길드 역할입니다. 와이어와 레코드에 쓰는 값입니다. */
    enum class GuildRole : uint8
    {
        Member = 0,
        Officer, ///< 초대, 공지, 멤버 내보내기를 할 수 있다
        Master,  ///< 길드에 하나뿐이다. 역할 바꾸기와 길드장 넘기기를 할 수 있고, 혼자일 때만 떠날 수 있으며 그러면 길드가 해산된다
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 길드 멤버 하나입니다. */
    struct GuildMember
    {
        AccountID _accountID{ kInvalidAccountID };
        int64     _joinedMs{ 0 };
        GuildRole _role{ GuildRole::Member };
    };
} // namespace sw

namespace sw
{
    /** @brief 길드 정보입니다. 조회는 멤버 목록까지 담고, 만들기 응답은 id 와 이름만 담습니다. */
    struct GuildInfo
    {
        vector<GuildMember> _listMember{};
        string              _name{};
        string              _notice{};
        uint64              _guildID{ 0 };
        AccountID           _masterID{ kInvalidAccountID };
        int64               _createdMs{ 0 };
        int32               _memberCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 길드 창고의 자산 하나입니다. 원장의 맡김(`esc/guild/<길드 16진>`) 잔액입니다. */
    struct GuildStorageItem
    {
        string _assetID{};
        int64  _amount{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 길드 상한 값들입니다. */
    struct GuildLimit
    {
        static constexpr int32 kMaxStorageItem   = 128; ///< 창고 목록 응답의 자산 수 상한이다
        static constexpr int32 kMaxMember        = 100;
        static constexpr int32 kMaxNameSize      = 24;        ///< UTF-8 바이트 수이다
        static constexpr int32 kMaxNoticeSize    = 256;       ///< UTF-8 바이트 수이다
        static constexpr int64 kInviteLifetimeMs = 604800000; ///< 7일이다
    };
} // namespace sw
