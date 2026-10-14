/**
 * @file SocialProtocol.h
 * @brief 친구 키트의 와이어 형식입니다. 메서드 번호(영역 `OnlineMethodRange::kSocial`), 요청과 응답, 알림의 형식을 정의합니다.
 * @details - 응답 본문은 `SocialResult` 뒤에 필드가 이어지는 형식이다. 업무 결과는 본문에 담으며 경제, 우편함, 거래 키트도 같다. 오류 코드는 공통(`OnlineError`)만 쓰며 손상된 본문, 로그인 없음, 저장소 오류가 해당한다.
 *          - 요청 본문은 메서드마다 쓰는 필드만 다른 한 가지 형식(`SocialRequest`)이다. 알림은 `kPushNotification` 하나에 종류를 붙여 보낸다.
 *          - 길드는 같은 영역의 0x10 이후 번호를 쓴다. 길드 채팅 채널은 게임 조립 단계가 길드 이벤트(`GuildEvent`)로 채팅 키트에 연결한다. 키트끼리는 서로 모른다.
 *          - 이름으로 친구 신청(`kRequestFriendByName`)은 정식 계정만 찾는다(계정 이름 인덱스). 게스트가 만든 이름은 고유하지 않으므로 계정 id 로 신청한다.
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

    /** @brief 친구 키트 메서드 번호입니다. 와이어 값이므로 바꾸면 `SocialProtocol::kVersion` 을 올려야 합니다. */
    struct SocialMethod
    {
        static constexpr uint16 kRequestFriend       = OnlineMethodRange::kSocial + 0x01; ///< 상대 id
        static constexpr uint16 kRequestFriendByName = OnlineMethodRange::kSocial + 0x02; ///< 상대의 표시 이름(정식 계정)을 받으며 응답의 `_otherID` 에 찾은 계정이 들어간다
        static constexpr uint16 kRespondFriend       = OnlineMethodRange::kSocial + 0x03; ///< 신청한 사람과 수락 여부를 받는다
        static constexpr uint16 kRemoveFriend        = OnlineMethodRange::kSocial + 0x04; ///< 친구, 보낸 신청, 받은 신청을 지운다
        static constexpr uint16 kBlock               = OnlineMethodRange::kSocial + 0x05;
        static constexpr uint16 kUnblock             = OnlineMethodRange::kSocial + 0x06;
        static constexpr uint16 kListLinks           = OnlineMethodRange::kSocial + 0x07; ///< 관계 목록을 돌려주며 이 서버의 메모리에도 올린다
        static constexpr uint16 kSetPresence         = OnlineMethodRange::kSocial + 0x08; ///< 상태와 활동 문구를 받는다
        static constexpr uint16 kFriendPresence      = OnlineMethodRange::kSocial + 0x09; ///< 친구들의 접속 상태를 돌려준다
        static constexpr uint16 kGuildCreate         = OnlineMethodRange::kSocial + 0x10; ///< 이름을 받아 길드(id 와 이름)를 돌려준다
        static constexpr uint16 kGuildInvite         = OnlineMethodRange::kSocial + 0x11; ///< 계정을 받으며 임원 이상만 부를 수 있다
        static constexpr uint16 kGuildAccept         = OnlineMethodRange::kSocial + 0x12; ///< 길드 id
        static constexpr uint16 kGuildLeave          = OnlineMethodRange::kSocial + 0x13;
        static constexpr uint16 kGuildKick           = OnlineMethodRange::kSocial + 0x14; ///< 계정을 받으며 더 높은 역할만 부를 수 있다
        static constexpr uint16 kGuildSetRole        = OnlineMethodRange::kSocial + 0x15; ///< 계정과 역할을 받으며 길드장만 부를 수 있다. 역할이 Master 이면 길드장을 넘기는 것이다
        static constexpr uint16 kGuildSetNotice      = OnlineMethodRange::kSocial + 0x16; ///< 문구를 받으며 임원 이상만 부를 수 있다
        static constexpr uint16 kGuildGet            = OnlineMethodRange::kSocial + 0x17; ///< 내 길드를 멤버 목록까지 돌려준다
        static constexpr uint16 kGuildDeposit        = OnlineMethodRange::kSocial + 0x18; ///< 자산과 양을 받아 내 계정에서 길드 창고로 옮긴다. 멱등 키가 필수이다
        static constexpr uint16 kGuildWithdraw       = OnlineMethodRange::kSocial + 0x19; ///< 자산과 양을 받아 길드 창고에서 내 계정으로 옮긴다. 임원 이상, 멱등 키 필수
        static constexpr uint16 kGuildStorage        = OnlineMethodRange::kSocial + 0x1A; ///< 길드 창고의 잔액 목록을 돌려준다
        static constexpr uint16 kPushNotification    = OnlineMethodRange::kSocial + 0x80; ///< 알림 하나이다

        static_assert( OnlineMethodRange::isInRange( kGuildStorage, OnlineMethodRange::kSocial ) && OnlineMethodRange::isMethod( kGuildStorage ) );
        static_assert( OnlineMethodRange::isInRange( kPushNotification, OnlineMethodRange::kSocial ) && OnlineMethodRange::isMethod( kPushNotification ) == false );

        /** @brief 관계를 바꾸는 메서드이면 true 입니다. 이 경우 멱등 키를 싣습니다. */
        static constexpr bool isLinkChange( uint16 method ) { return kRequestFriend <= method && method <= kUnblock; }
        static constexpr bool isGuild( uint16 method ) { return kGuildCreate <= method && method <= kGuildStorage; }
        /** @brief 읽기만 하는 길드 메서드이면 true 입니다. 나머지 길드 메서드는 멱등 키를 싣습니다. */
        static constexpr bool isGuildRead( uint16 method ) { return method == kGuildGet || method == kGuildStorage; }
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 요청입니다. 메서드마다 쓰는 필드만 채웁니다. */
    struct SocialRequest
    {
        string               _text{};                       ///< kRequestFriendByName 과 kGuildCreate 의 이름, kSetPresence 의 활동 문구, kGuildSetNotice 의 공지이다
        string               _assetID{};                    ///< kGuildDeposit · kGuildWithdraw 의 자산
        AccountID            _otherID{ kInvalidAccountID }; ///< 상대이거나 길드 작업의 대상이다
        uint64               _guildID{ 0 };                 ///< kGuildAccept
        int64                _amount{ 0 };                  ///< kGuildDeposit · kGuildWithdraw 의 양(1 이상)
        SocialPresenceStatus _status{ SocialPresenceStatus::Offline };
        GuildRole            _role{ GuildRole::Member }; ///< kGuildSetRole
        uint8                _bAccept{ SW_FALSE };       ///< kRespondFriend
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답입니다. 메서드마다 쓰는 필드만 채웁니다. */
    struct SocialReply
    {
        vector<SocialLink>       _listLink{};                   ///< kListLinks
        vector<SocialPresence>   _listPresence{};               ///< kFriendPresence
        vector<GuildStorageItem> _listStorage{};                ///< kGuildStorage 는 창고 전체, kGuildDeposit · kGuildWithdraw 는 옮긴 자산의 창고 잔액 하나
        GuildInfo                _guild{};                      ///< kGuildCreate 에서는 id 와 이름이고 kGuildGet 에서는 전체 정보이다
        AccountID                _otherID{ kInvalidAccountID }; ///< 관계를 바꾼 상대이다. 이름으로 찾았으면 찾은 계정이다
        SocialResult             _result{ SocialResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 형식을 읽고 씁니다. 읽을 때 상한을 넘거나 모자라면 false 를 반환합니다. */
    struct SW_KIT_API SocialProtocol
    {
        static constexpr uint32 kVersion        = 2;                          ///< 2: 길드 창고(맡기기 · 꺼내기 · 목록)
        static constexpr int32  kMaxTextSize    = GuildLimit::kMaxNoticeSize; ///< 이름, 활동 문구, 공지 중 가장 긴 것의 상한이다. 필드마다의 상한은 로직이 검사한다
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
        /** @brief 공통 오류 코드(`OnlineError`)를 결과로 바꿉니다. 로그인 없음, 손상된 본문, 충돌은 각각 대응하는 결과로, 나머지는 Unavailable 로 바꿉니다. */
        static SocialResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
