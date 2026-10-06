/**
 * @file SocialTypes.h
 * @brief 친구 키트의 타입 — 결과 · 관계 상태 · 관계 한 줄 · 접속 상태 · 알림 종류 · 상한입니다.
 * @details 관계는 방향이 있다 — "나 → 상대" 한 줄의 상태(친구 · 보낸 신청 · 받은 신청 · 내가 막음). 친구는 양쪽 줄이 모두 친구다.
 *          접속 상태는 친구에게 보이는 "상태 + 짧은 활동 글"(Steam Rich Presence · EOS Presence 와 같은 자리)이다 — 계정이 어느 서버에 붙었는지(`IAccountPresence`)와 따로.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 친구 키트의 업무 결과입니다(응답 몸의 첫 값). 와이어 값 — 끝에만 더한다. */
    enum class SocialResult : uint8
    {
        Ok = 0,
        NotSignedIn,
        Invalid,  ///< 자기 자신 · 0 · 규칙 밖 값
        NotFound, ///< 그런 관계 · 신청 · 계정이 없다
        AlreadyFriend,
        AlreadyRequested,
        FriendLimit,
        PendingLimit, ///< 내 보낸 신청 · 상대가 받은 신청이 가득
        BlockLimit,
        YouBlocked,  ///< 내가 막은 상대
        Unavailable, ///< 저장소 · 캐시 — 다시 하면 된다
        Conflict,    ///< 다시 해도 경합 — 잠시 뒤
        Count
    };

    SW_GF_API const utf8* toString( SocialResult result );
} // namespace sw

namespace sw
{
    /** @brief "나 → 상대" 한 줄의 상태입니다. 와이어 · 레코드 값. */
    enum class SocialLinkState : uint8
    {
        None = 0,
        Friend,
        Outgoing, ///< 내가 신청함
        Incoming, ///< 신청을 받음
        Blocked,  ///< 내가 막음
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 관계 한 줄입니다. */
    struct SocialLink
    {
        AccountId       _otherId{ kInvalidAccountId };
        int64           _sinceMs{ 0 };
        SocialLinkState _state{ SocialLinkState::None };
    };
} // namespace sw

namespace sw
{
    /** @brief 접속 상태입니다. 와이어 값. */
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
    /** @brief 한 계정의 접속 상태(친구에게 보이는 것)입니다. */
    struct SocialPresence
    {
        string               _activity{}; ///< "던전 3 층" — `SocialLimit::kMaxActivitySize` 바이트 이하
        AccountId            _accountId{ kInvalidAccountId };
        SocialPresenceStatus _status{ SocialPresenceStatus::Offline };
    };
} // namespace sw

namespace sw
{
    /** @brief 알림 종류입니다. 와이어 값 — 끝에만 더한다. */
    enum class SocialNotificationKind : uint8
    {
        FriendRequested = 0, ///< 받은 신청(상대 = 신청한 이)
        FriendAdded,         ///< 친구가 됨
        FriendRemoved,
        PresenceChanged, ///< 친구의 접속 상태
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 알림 하나입니다. 받는 이(`_recipientId`)는 와이어에 싣지 않는다(서버가 고른 연결로 간다). */
    struct SocialNotification
    {
        SocialPresence         _presence{}; ///< PresenceChanged
        AccountId              _recipientId{ kInvalidAccountId };
        AccountId              _otherId{ kInvalidAccountId };
        SocialNotificationKind _kind{ SocialNotificationKind::FriendRequested };
    };
} // namespace sw

namespace sw
{
    /** @brief 상한입니다. 상한은 저장소 트랜잭션이 판 조건으로 지킨다(경합으로 넘지 않는다). */
    struct SocialLimit
    {
        static constexpr int32 kMaxFriend       = 200;
        static constexpr int32 kMaxIncoming     = 100;
        static constexpr int32 kMaxOutgoing     = 100;
        static constexpr int32 kMaxBlocked      = 500;
        static constexpr int32 kMaxActivitySize = 64;   ///< UTF-8 바이트
        static constexpr int32 kMaxLinkPage     = 1000; ///< 친구 + 받은 신청 + 보낸 신청 + 막음 합보다 크게
    };
} // namespace sw
