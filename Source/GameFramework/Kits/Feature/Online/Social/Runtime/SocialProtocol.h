/**
 * @file SocialProtocol.h
 * @brief 친구 키트의 와이어 형식입니다. 메서드 번호(영역 `OnlineMethodRange::kSocial`), 요청과 응답, 알림, 서버 버스 본문의 형식을 정의합니다.
 * @details - 요청, 응답, 알림은 `REFLECT` 구조체 하나씩이고 본문은 고정 배치형(B)이다. 바이트 배치는 `PROPERTY` 선언 순서와 키가 정한다.
 *            업무 결과는 응답 본문(`SocialReply::_result`)에 담으며 경제, 우편함, 거래 키트도 같다. 오류 코드는 공통(`OnlineError`)만 쓰며 손상된 본문, 로그인 없음, 저장소 오류가 해당한다.
 *          - 요청은 메서드마다 쓰는 칸만 가진 형식이다. 알림은 `Notification` 하나에 종류를 붙여 보낸다.
 *          - 길드 채팅 채널은 게임 조립 단계가 길드 이벤트(`GuildEvent`)로 채팅 키트에 연결한다. 키트끼리는 서로 모른다.
 *          - 이름으로 친구 신청(`RequestFriendByName`)은 정식 계정만 찾는다(계정 이름 인덱스). 게스트가 만든 이름은 고유하지 않으므로 계정 id 로 신청한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Social/Runtime/SocialTypes.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolRegistry.h"

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
        GuildSetJoinPolicy,
        GuildApply,
        GuildApplications,
        GuildApprove,
        GuildReject,
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
    /** @brief 계정 하나를 가리키는 요청입니다(친구 신청 · 지우기 · 차단, 길드 초대 · 내보내기 · 신청 처리). */
    REFLECT()
    struct SocialAccountRequest
    {
        REFLECT_BODY();
        PROPERTY()
        AccountID _otherID{ kInvalidAccountID };
    };

    /** @brief 글 하나를 받는 요청입니다(이름으로 친구 신청, 길드 만들기, 길드 공지). 글마다의 상한은 로직이 검사합니다. */
    REFLECT()
    struct SocialTextRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = GuildLimit::kMaxNoticeSize )
        string _text{}; ///< 상한은 가장 긴 공지(`GuildLimit::kMaxNoticeSize`)와 같다
    };

    /** @brief 친구 신청 응답(`RespondFriend`)의 요청입니다. */
    REFLECT()
    struct SocialRespondRequest
    {
        REFLECT_BODY();
        PROPERTY()
        AccountID _otherID{ kInvalidAccountID };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bAccept{ SW_FALSE };
    };

    /** @brief 접속 상태(`SetPresence`)의 요청입니다. */
    REFLECT()
    struct SocialPresenceRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = GuildLimit::kMaxNoticeSize )
        string _text{}; ///< 활동 문구이다. 상한은 로직이 검사한다
        PROPERTY()
        SocialPresenceStatus _status{ SocialPresenceStatus::Offline };
    };

    /** @brief 길드 하나를 가리키는 요청입니다(초대 수락, 가입 신청). */
    REFLECT()
    struct GuildIDRequest
    {
        REFLECT_BODY();
        PROPERTY()
        uint64 _guildID{ 0 };
    };

    /** @brief 역할 바꾸기(`GuildSetRole`)의 요청입니다. */
    REFLECT()
    struct GuildRoleRequest
    {
        REFLECT_BODY();
        PROPERTY()
        AccountID _otherID{ kInvalidAccountID };
        PROPERTY()
        GuildRole _role{ GuildRole::Member };
    };

    /** @brief 길드 창고 이동(`GuildDeposit` · `GuildWithdraw`)의 요청입니다. */
    REFLECT()
    struct GuildAssetRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = LedgerConstant::kMaxAssetIDSize )
        string _assetID{}; ///< 상한은 `LedgerConstant::kMaxAssetIDSize` 와 같다
        PROPERTY()
        int64 _amount{ 0 }; ///< 1 이상
    };

    /** @brief 공개 길드 켜고 끄기(`GuildSetJoinPolicy`)의 요청입니다. */
    REFLECT()
    struct GuildJoinPolicyRequest
    {
        REFLECT_BODY();
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bOpen{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답입니다. 로직의 완료 하나에서 나오므로 메서드마다 쓰는 필드만 채웁니다. */
    REFLECT()
    struct SocialReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = SocialLimit::kMaxLinkPage )
        vector<SocialLink> _listLink{}; ///< `ListLinks`. 상한은 `SocialLimit::kMaxLinkPage` 와 같다
        PROPERTY( MaxCount = SocialLimit::kMaxFriend )
        vector<SocialPresence> _listPresence{}; ///< `FriendPresence`. 상한은 `SocialLimit::kMaxFriend` 와 같다
        PROPERTY( MaxCount = GuildLimit::kMaxStorageItem )
        vector<GuildStorageItem> _listStorage{}; ///< `GuildStorage` 는 창고 전체, `GuildDeposit` · `GuildWithdraw` 는 옮긴 자산의 창고 잔액 하나. 상한은 `GuildLimit::kMaxStorageItem` 와 같다
        PROPERTY( MaxCount = GuildLimit::kMaxApplication )
        vector<GuildApplication> _listApplication{}; ///< `GuildApplications`. 상한은 `GuildLimit::kMaxApplication` 와 같다
        PROPERTY()
        GuildInfo _guild{}; ///< `GuildCreate` 에서는 id 와 이름이고 `GuildGet` 에서는 전체 정보이다
        PROPERTY()
        AccountID _otherID{ kInvalidAccountID }; ///< 관계를 바꾼 상대이다. 이름으로 찾았으면 찾은 계정이다
        PROPERTY()
        SocialResult _result{ SocialResult::Ok };

        /** @brief 같은 멱등 키로 다시 시도할 결과(일시 장애 · 경합)인지입니다. 서버는 이 답을 기억하지 않는다(`OnlineServiceHost::respondReply`). */
        [[nodiscard]] bool isRetryable() const { return _result == SocialResult::Unavailable || _result == SocialResult::Conflict; }
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 버스 `SocialBus::kLinksTopic` 의 본문입니다. 관계가 바뀐 두 사람이며, 받은 서버는 그 계정이 자기에게 있으면 관계를 다시 읽습니다. 버스에만 쓰므로 `MessageTypes` 에 없습니다(A). */
    REFLECT()
    struct SW_KIT_API SocialLinkChanged
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< 의미 버전
        PROPERTY()
        AccountID _firstID{ kInvalidAccountID };
        PROPERTY()
        AccountID _secondID{ kInvalidAccountID };
    };
} // namespace sw

namespace sw
{
    /** @brief Social 메서드 표입니다. 번호는 열거자 순서에서 나오고, 요청 · 응답의 짝과 호출 규칙은 서술자가 정합니다. */
    struct SocialMethod : OnlineMethodTable<SocialMethodName, OnlineMethodRange::kSocial, SocialPushName>
    {
        using RequestFriend       = Method<MethodName::RequestFriend, SocialAccountRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using RequestFriendByName = Method<MethodName::RequestFriendByName, SocialTextRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>; ///< 상대의 표시 이름(정식 계정)을 받으며 응답의 `_otherID` 에 찾은 계정이 들어간다
        using RespondFriend       = Method<MethodName::RespondFriend, SocialRespondRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using RemoveFriend        = Method<MethodName::RemoveFriend, SocialAccountRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>; ///< 친구, 보낸 신청, 받은 신청을 지운다
        using Block               = Method<MethodName::Block, SocialAccountRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using Unblock             = Method<MethodName::Unblock, SocialAccountRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using ListLinks           = Method<MethodName::ListLinks, OnlineNoBody, SocialReply>; ///< 관계 목록을 돌려주며 이 서버의 메모리에도 로드한다
        using SetPresence         = Method<MethodName::SetPresence, SocialPresenceRequest, SocialReply>;
        using FriendPresence      = Method<MethodName::FriendPresence, OnlineNoBody, SocialReply>;                                                ///< 친구들의 접속 상태를 돌려준다
        using GuildCreate         = Method<MethodName::GuildCreate, SocialTextRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>;    ///< 이름을 받아 길드(id 와 이름)를 돌려준다
        using GuildInvite         = Method<MethodName::GuildInvite, SocialAccountRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>; ///< 임원 이상만 부를 수 있다
        using GuildAccept         = Method<MethodName::GuildAccept, GuildIDRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using GuildLeave          = Method<MethodName::GuildLeave, OnlineNoBody, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using GuildKick           = Method<MethodName::GuildKick, SocialAccountRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>;            ///< 더 높은 역할만 부를 수 있다
        using GuildSetRole        = Method<MethodName::GuildSetRole, GuildRoleRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>;             ///< 길드장만 부를 수 있다. 역할이 Master 이면 길드장을 넘기는 것이다
        using GuildSetNotice      = Method<MethodName::GuildSetNotice, SocialTextRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>;          ///< 임원 이상만 부를 수 있다
        using GuildGet            = Method<MethodName::GuildGet, OnlineNoBody, SocialReply>;                                                               ///< 내 길드를 멤버 목록까지 돌려준다
        using GuildDeposit        = Method<MethodName::GuildDeposit, GuildAssetRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>;            ///< 내 계정에서 길드 창고로 옮긴다
        using GuildWithdraw       = Method<MethodName::GuildWithdraw, GuildAssetRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>;           ///< 길드 창고에서 내 계정으로 옮긴다. 임원 이상
        using GuildStorage        = Method<MethodName::GuildStorage, OnlineNoBody, SocialReply>;                                                           ///< 길드 창고의 잔액 목록을 돌려준다
        using GuildSetJoinPolicy  = Method<MethodName::GuildSetJoinPolicy, GuildJoinPolicyRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>; ///< 임원 이상
        using GuildApply          = Method<MethodName::GuildApply, GuildIDRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>;                 ///< 공개 길드에 가입 신청을 낸다. 다시 내면 기한이 늘어난다
        using GuildApplications   = Method<MethodName::GuildApplications, OnlineNoBody, SocialReply>;                                                      ///< 내 길드의 살아 있는 신청 목록이다. 임원 이상
        using GuildApprove        = Method<MethodName::GuildApprove, SocialAccountRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>;         ///< 신청한 계정을 받아들인다. 임원 이상
        using GuildReject         = Method<MethodName::GuildReject, SocialAccountRequest, SocialReply, OnlineMethodFlag::RequiresIdempotencyKey>;          ///< 신청한 계정의 신청을 지운다. 임원 이상
        using Notification        = Push<PushName::Notification, SocialNotification>;                                                                      ///< 알림 하나이다
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
        using Table        = OnlineProtocolTable<SocialMethod, OnlineMethodList<SocialMethod::RequestFriend, SocialMethod::RequestFriendByName, SocialMethod::RespondFriend, SocialMethod::RemoveFriend, SocialMethod::Block, SocialMethod::Unblock, SocialMethod::ListLinks, SocialMethod::SetPresence, SocialMethod::FriendPresence, SocialMethod::GuildCreate, SocialMethod::GuildInvite, SocialMethod::GuildAccept, SocialMethod::GuildLeave, SocialMethod::GuildKick, SocialMethod::GuildSetRole, SocialMethod::GuildSetNotice, SocialMethod::GuildGet, SocialMethod::GuildDeposit, SocialMethod::GuildWithdraw, SocialMethod::GuildStorage, SocialMethod::GuildSetJoinPolicy, SocialMethod::GuildApply, SocialMethod::GuildApplications, SocialMethod::GuildApprove, SocialMethod::GuildReject>, OnlinePushList<SocialMethod::Notification>>;
        using MessageTypes = Table::MessageTypes;
        using ResultName   = SocialResult;

        /** @brief 공통 범주 표입니다. 로직이 낸 값이 표에 있으면 서버는 공통 오류로 답하고(지표 · 보존이 여기서 나온다), 클라이언트는 공통 오류를 이 값으로 채웁니다. 세 번째 칸은 클라이언트 쪽만 쓰는 줄입니다. */
        static constexpr OnlineErrorResult<SocialResult> kArrCommonResult[] = {
            { OnlineError::kInvalidRequest, SocialResult::Invalid },
            { OnlineError::kUnavailable, SocialResult::Unavailable },
            { OnlineError::kBusy, SocialResult::Conflict },
            { OnlineError::kConflict, SocialResult::Conflict, true },
            { OnlineError::kUnauthenticated, SocialResult::NotSignedIn },
            { OnlineError::kVersionMismatch, SocialResult::UpdateRequired },
            { OnlineError::kUpdateRequired, SocialResult::UpdateRequired },
        };

        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다

        /**
         * @brief 접속 상태 캐시 값을 읽고 씁니다. 배포 중에 다른 빌드의 서버가 읽을 수 있으므로 자기 기술형(A)이며 레코드 버전을 머리에 둡니다.
         * @details 다시 만들 수 있는 캐시이므로 `ServiceRecordUtil::readCache` 로 읽습니다. 다른 버전이거나 깨졌거나 선언된 범위를 벗어나면 로그 없이 false 이고,
         *          읽는 쪽은 그 친구를 접속 끊김으로 봅니다.
         */
        [[nodiscard]] static bool encodePresenceRecord( const SocialPresence& presence, vector<uint8>& outBytes );
        [[nodiscard]] static bool decodePresenceRecord( const vector<uint8>& bytes, SocialPresence& outPresence );
    };
} // namespace sw
