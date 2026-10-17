/**
 * @file SocialProtocol.h
 * @brief 친구 키트의 와이어 형식입니다. 메서드 번호(영역 `OnlineMethodRange::kSocial`), 요청과 응답, 알림, 서버 버스 본문의 형식을 정의합니다.
 * @details - 요청, 응답, 알림은 `REFLECT` 구조체 하나씩이고 본문은 고정 배치형(B)이다. 바이트 배치는 `PROPERTY` 선언 순서와 키가 정한다.
 *            업무 결과는 응답 본문(`SocialReply::_result`)에 담으며 경제, 우편함, 거래 키트도 같다. 오류 코드는 공통(`OnlineError`)만 쓰며 손상된 본문, 로그인 없음, 저장소 오류가 해당한다.
 *          - 요청 본문은 메서드마다 쓰는 필드만 다른 한 가지 형식(`SocialRequest`)이다. 알림은 `kPushNotification` 하나에 종류를 붙여 보낸다.
 *          - 길드는 같은 영역의 0x10 이후 번호를 쓴다. 길드 채팅 채널은 게임 조립 단계가 길드 이벤트(`GuildEvent`)로 채팅 키트에 연결한다. 키트끼리는 서로 모른다.
 *          - 이름으로 친구 신청(`kRequestFriendByName`)은 정식 계정만 찾는다(계정 이름 인덱스). 게스트가 만든 이름은 고유하지 않으므로 계정 id 로 신청한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Social/Shared/SocialTypes.h"

namespace sw
{
    /** @brief 소셜 메서드 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 메서드는 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class SocialMethodName : uint8
    {
        RequestFriend,
        RequestFriendByName,
        RespondFriend,
        RemoveFriend,
        Block,
        Unblock,
        ListLinks,
        SetPresence,
        FriendPresence,
        GuildCreate,
        GuildInvite,
        GuildAccept,
        GuildLeave,
        GuildKick,
        GuildSetRole,
        GuildSetNotice,
        GuildGet,
        GuildDeposit,
        GuildWithdraw,
        GuildStorage,
        Count,
    };

    /** @brief 소셜 알림 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 알림은 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class SocialPushName : uint8
    {
        Notification,
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief 친구 키트 메서드 번호입니다. 번호는 열거자 순서에서 나오며 그 이름 순서가 온라인 스키마 해시에 섞입니다(`OnlineMethodTable`). */
    struct SocialMethod : OnlineMethodTable<SocialMethodName, OnlineMethodRange::kSocial, SocialPushName>
    {
        static constexpr uint16 kRequestFriend       = toMethod( SocialMethodName::RequestFriend );       ///< 상대 id
        static constexpr uint16 kRequestFriendByName = toMethod( SocialMethodName::RequestFriendByName ); ///< 상대의 표시 이름(정식 계정)을 받으며 응답의 `_otherID` 에 찾은 계정이 들어간다
        static constexpr uint16 kRespondFriend       = toMethod( SocialMethodName::RespondFriend );       ///< 신청한 사람과 수락 여부를 받는다
        static constexpr uint16 kRemoveFriend        = toMethod( SocialMethodName::RemoveFriend );        ///< 친구, 보낸 신청, 받은 신청을 지운다
        static constexpr uint16 kBlock               = toMethod( SocialMethodName::Block );
        static constexpr uint16 kUnblock             = toMethod( SocialMethodName::Unblock );
        static constexpr uint16 kListLinks           = toMethod( SocialMethodName::ListLinks );      ///< 관계 목록을 돌려주며 이 서버의 메모리에도 올린다
        static constexpr uint16 kSetPresence         = toMethod( SocialMethodName::SetPresence );    ///< 상태와 활동 문구를 받는다
        static constexpr uint16 kFriendPresence      = toMethod( SocialMethodName::FriendPresence ); ///< 친구들의 접속 상태를 돌려준다
        static constexpr uint16 kGuildCreate         = toMethod( SocialMethodName::GuildCreate );    ///< 이름을 받아 길드(id 와 이름)를 돌려준다
        static constexpr uint16 kGuildInvite         = toMethod( SocialMethodName::GuildInvite );    ///< 계정을 받으며 임원 이상만 부를 수 있다
        static constexpr uint16 kGuildAccept         = toMethod( SocialMethodName::GuildAccept );    ///< 길드 id
        static constexpr uint16 kGuildLeave          = toMethod( SocialMethodName::GuildLeave );
        static constexpr uint16 kGuildKick           = toMethod( SocialMethodName::GuildKick );      ///< 계정을 받으며 더 높은 역할만 부를 수 있다
        static constexpr uint16 kGuildSetRole        = toMethod( SocialMethodName::GuildSetRole );   ///< 계정과 역할을 받으며 길드장만 부를 수 있다. 역할이 Master 이면 길드장을 넘기는 것이다
        static constexpr uint16 kGuildSetNotice      = toMethod( SocialMethodName::GuildSetNotice ); ///< 문구를 받으며 임원 이상만 부를 수 있다
        static constexpr uint16 kGuildGet            = toMethod( SocialMethodName::GuildGet );       ///< 내 길드를 멤버 목록까지 돌려준다
        static constexpr uint16 kGuildDeposit        = toMethod( SocialMethodName::GuildDeposit );   ///< 자산과 양을 받아 내 계정에서 길드 창고로 옮긴다. 멱등 키가 필수이다
        static constexpr uint16 kGuildWithdraw       = toMethod( SocialMethodName::GuildWithdraw );  ///< 자산과 양을 받아 길드 창고에서 내 계정으로 옮긴다. 임원 이상, 멱등 키 필수
        static constexpr uint16 kGuildStorage        = toMethod( SocialMethodName::GuildStorage );   ///< 길드 창고의 잔액 목록을 돌려준다
        static constexpr uint16 kPushNotification    = toPush( SocialPushName::Notification );       ///< 알림 하나이다

        /** @brief 관계를 바꾸는 메서드이면 true 입니다. 이 경우 멱등 키를 싣습니다. */
        static constexpr bool isLinkChange( uint16 method ) { return kRequestFriend <= method && method <= kUnblock; }
        static constexpr bool isGuild( uint16 method ) { return kGuildCreate <= method && method <= kGuildStorage; }
        /** @brief 읽기만 하는 길드 메서드이면 true 입니다. 나머지 길드 메서드는 멱등 키를 싣습니다. */
        static constexpr bool isGuildRead( uint16 method ) { return method == kGuildGet || method == kGuildStorage; }
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 요청입니다. 메서드마다 쓰는 필드만 채웁니다. 필드마다의 문구 상한은 로직이 검사합니다. */
    REFLECT()
    struct SocialRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 256 )
        string _text{}; ///< kRequestFriendByName 과 kGuildCreate 의 이름, kSetPresence 의 활동 문구, kGuildSetNotice 의 공지이다. 상한은 가장 긴 공지(`GuildLimit::kMaxNoticeSize`)와 같다
        PROPERTY( MaxCount = 48 )
        string _assetID{}; ///< kGuildDeposit · kGuildWithdraw 의 자산. 상한은 `LedgerConstant::kMaxAssetIDSize` 와 같다
        PROPERTY()
        AccountID _otherID{ kInvalidAccountID }; ///< 상대이거나 길드 작업의 대상이다
        PROPERTY()
        uint64 _guildID{ 0 }; ///< kGuildAccept
        PROPERTY()
        int64 _amount{ 0 }; ///< kGuildDeposit · kGuildWithdraw 의 양(1 이상)
        PROPERTY()
        SocialPresenceStatus _status{ SocialPresenceStatus::Offline };
        PROPERTY()
        GuildRole _role{ GuildRole::Member }; ///< kGuildSetRole
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bAccept{ SW_FALSE }; ///< kRespondFriend
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답입니다. 메서드마다 쓰는 필드만 채웁니다. */
    REFLECT()
    struct SocialReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 1000 )
        vector<SocialLink> _listLink{}; ///< kListLinks. 상한은 `SocialLimit::kMaxLinkPage` 와 같다
        PROPERTY( MaxCount = 200 )
        vector<SocialPresence> _listPresence{}; ///< kFriendPresence. 상한은 `SocialLimit::kMaxFriend` 와 같다
        PROPERTY( MaxCount = 128 )
        vector<GuildStorageItem> _listStorage{}; ///< kGuildStorage 는 창고 전체, kGuildDeposit · kGuildWithdraw 는 옮긴 자산의 창고 잔액 하나. 상한은 `GuildLimit::kMaxStorageItem` 와 같다
        PROPERTY()
        GuildInfo _guild{}; ///< kGuildCreate 에서는 id 와 이름이고 kGuildGet 에서는 전체 정보이다
        PROPERTY()
        AccountID _otherID{ kInvalidAccountID }; ///< 관계를 바꾼 상대이다. 이름으로 찾았으면 찾은 계정이다
        PROPERTY()
        SocialResult _result{ SocialResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 버스 `SocialBus::kLinksTopic` 의 본문입니다. 관계가 바뀐 두 사람이며, 받은 서버는 그 계정이 자기에게 있으면 관계를 다시 읽습니다. */
    REFLECT()
    struct SocialLinkChanged
    {
        REFLECT_BODY();
        PROPERTY()
        AccountID _firstID{ kInvalidAccountID };
        PROPERTY()
        AccountID _secondID{ kInvalidAccountID };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SocialProtocol
     * @brief 친구 키트의 메시지 타입 목록과 등록 항목입니다. 본문은 고정 배치형(B)이며 `MessageTypes::write` · `read` 로만 쓰고 읽습니다.
     * @details 알림 본문은 `SocialNotification`, 서버 버스 본문은 `SocialPresence` · `SocialLinkChanged` 입니다.
     */
    struct SW_KIT_API SocialProtocol
    {
        using MessageTypes = OnlineMessageTypes<SocialRequest, SocialReply, SocialNotification, SocialPresence, SocialLinkChanged>;

        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다

        /**
         * @brief 접속 상태 캐시 값을 읽고 씁니다. 배포 중에 다른 빌드의 서버가 읽을 수 있으므로 자기 기술형(A)이며 레코드 판을 머리에 둡니다.
         * @details 다시 만들 수 있는 캐시이므로 `ServiceRecordUtil::readCache` 로 읽습니다. 다른 판이거나 깨졌거나 선언된 범위를 벗어나면 로그 없이 false 이고,
         *          읽는 쪽은 그 친구를 접속 끊김으로 봅니다.
         */
        static vector<uint8>      encodePresenceRecord( const SocialPresence& presence );
        [[nodiscard]] static bool decodePresenceRecord( const vector<uint8>& bytes, SocialPresence& outPresence );

        /** @brief 공통 오류 코드(`OnlineError`)를 결과로 바꿉니다. 로그인 없음, 손상된 본문, 충돌은 각각 대응하는 결과로, 나머지는 Unavailable 로 바꿉니다. */
        static SocialResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
