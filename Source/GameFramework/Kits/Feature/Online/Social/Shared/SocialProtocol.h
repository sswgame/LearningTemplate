/**
 * @file SocialProtocol.h
 * @brief 친구 키트의 와이어 — 메서드 번호(영역 `OnlineMethodRange::kSocial`) · 요청/응답 · 알림 형식입니다.
 * @details - 응답 몸 = `SocialResult` + 칸(업무 결과는 몸에 — 경제 · 우편함 · 거래 키트와 같다). 오류 코드는 공통(`OnlineError`)만 — 깨진 몸 · 로그인 없음 · 저장소.
 *          - 요청 몸은 메서드마다 쓰는 칸만 다른 한 형식(`SocialRequest`)이다. 알림은 `kPushNotification` 하나 + 종류.
 *          - 길드는 같은 영역의 0x10.. — 길드 채팅 채널은 게임 조립이 길드 사건(`GuildEvent`)으로 채팅 키트에 잇는다(키트끼리 모른다).
 *          - 이름으로 친구 신청(`kRequestFriendByName`)은 정식 계정만 찾는다(계정 이름 색인 — 게스트의 만든 이름은 고유하지 않아 계정 id 로).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Social/Shared/SocialTypes.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief 친구 키트 메서드입니다. 와이어 값 — 바꾸면 `SocialProtocol::kVersion` 을 올린다. */
    struct SocialMethod
    {
        static constexpr uint16 kRequestFriend       = OnlineMethodRange::kSocial + 0x01; ///< 상대 id
        static constexpr uint16 kRequestFriendByName = OnlineMethodRange::kSocial + 0x02; ///< 상대 표시 이름(정식 계정) — 응답 `_otherId`
        static constexpr uint16 kRespondFriend       = OnlineMethodRange::kSocial + 0x03; ///< 신청한 이 · 수락 여부
        static constexpr uint16 kRemoveFriend        = OnlineMethodRange::kSocial + 0x04; ///< 친구 · 보낸 신청 · 받은 신청 지우기
        static constexpr uint16 kBlock               = OnlineMethodRange::kSocial + 0x05;
        static constexpr uint16 kUnblock             = OnlineMethodRange::kSocial + 0x06;
        static constexpr uint16 kListLinks           = OnlineMethodRange::kSocial + 0x07; ///< → 관계 목록(이 서버 메모리에도 올린다)
        static constexpr uint16 kSetPresence         = OnlineMethodRange::kSocial + 0x08; ///< 상태 · 활동 글
        static constexpr uint16 kFriendPresence      = OnlineMethodRange::kSocial + 0x09; ///< → 친구들의 접속 상태
        static constexpr uint16 kGuildCreate         = OnlineMethodRange::kSocial + 0x10; ///< 이름 → 길드(id · 이름)
        static constexpr uint16 kGuildInvite         = OnlineMethodRange::kSocial + 0x11; ///< 계정(임원 이상)
        static constexpr uint16 kGuildAccept         = OnlineMethodRange::kSocial + 0x12; ///< 길드 id
        static constexpr uint16 kGuildLeave          = OnlineMethodRange::kSocial + 0x13;
        static constexpr uint16 kGuildKick           = OnlineMethodRange::kSocial + 0x14; ///< 계정(높은 역할만)
        static constexpr uint16 kGuildSetRole        = OnlineMethodRange::kSocial + 0x15; ///< 계정 · 역할(길드장만 — Master 면 넘기기)
        static constexpr uint16 kGuildSetNotice      = OnlineMethodRange::kSocial + 0x16; ///< 글(임원 이상)
        static constexpr uint16 kGuildGet            = OnlineMethodRange::kSocial + 0x17; ///< → 내 길드(회원 목록까지)
        static constexpr uint16 kPushNotification    = OnlineMethodRange::kSocial + 0x80; ///< 알림 하나

        static_assert( OnlineMethodRange::isInRange( kGuildGet, OnlineMethodRange::kSocial ) && OnlineMethodRange::isMethod( kGuildGet ) );
        static_assert( OnlineMethodRange::isInRange( kPushNotification, OnlineMethodRange::kSocial ) && OnlineMethodRange::isMethod( kPushNotification ) == false );

        /** @brief 관계를 바꾸는 메서드인가입니다(멱등 키를 싣는다). */
        static constexpr bool isLinkChange( uint16 method ) { return kRequestFriend <= method && method <= kUnblock; }
        static constexpr bool isGuild( uint16 method ) { return kGuildCreate <= method && method <= kGuildGet; }
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 요청(쓰는 칸만)입니다. */
    struct SocialRequest
    {
        string               _text{};                       ///< 이름(kRequestFriendByName · kGuildCreate) · 활동 글(kSetPresence) · 공지(kGuildSetNotice)
        AccountId            _otherId{ kInvalidAccountId }; ///< 상대 · 길드 대상
        uint64               _guildId{ 0 };                 ///< kGuildAccept
        SocialPresenceStatus _status{ SocialPresenceStatus::Offline };
        GuildRole            _role{ GuildRole::Member }; ///< kGuildSetRole
        uint8                _bAccept{ SW_FALSE };       ///< kRespondFriend
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답(쓰는 칸만)입니다. */
    struct SocialReply
    {
        vector<SocialLink>     _listLink{};                   ///< kListLinks
        vector<SocialPresence> _listPresence{};               ///< kFriendPresence
        GuildInfo              _guild{};                      ///< kGuildCreate(id · 이름) · kGuildGet
        AccountId              _otherId{ kInvalidAccountId }; ///< 관계 바꾸기 — 상대(이름으로 찾았으면 찾은 계정)
        SocialResult           _result{ SocialResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 형식 쓰기 · 읽기입니다. 읽기는 상한을 넘거나 모자라면 false 입니다. */
    struct SW_GF_API SocialProtocol
    {
        static constexpr uint32 kVersion        = 1;
        static constexpr int32  kMaxTextSize    = GuildLimit::kMaxNoticeSize; ///< 이름 · 활동 글 · 공지 중 가장 긴 것(칸마다 상한은 로직이 본다)
        static constexpr int32  kMaxPresenceRow = SocialLimit::kMaxFriend;

        static void               writeRequest( BitWriter& outWriter, const SocialRequest& request );
        [[nodiscard]] static bool readRequest( BitReader& reader, SocialRequest& outRequest );
        static void               writeReply( BitWriter& outWriter, const SocialReply& reply );
        [[nodiscard]] static bool readReply( BitReader& reader, SocialReply& outReply );
        static void               writePresence( BitWriter& outWriter, const SocialPresence& presence );
        [[nodiscard]] static bool readPresence( BitReader& reader, SocialPresence& outPresence );
        static void               writeGuild( BitWriter& outWriter, const GuildInfo& guild );
        [[nodiscard]] static bool readGuild( BitReader& reader, GuildInfo& outGuild );
        static void               writeNotification( BitWriter& outWriter, const SocialNotification& notification );
        [[nodiscard]] static bool readNotification( BitReader& reader, SocialNotification& outNotification );
        /** @brief 공통 오류 코드(`OnlineError`) → 결과입니다(로그인 없음 · 깨진 몸 · 충돌, 나머지 Unavailable). */
        static SocialResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
