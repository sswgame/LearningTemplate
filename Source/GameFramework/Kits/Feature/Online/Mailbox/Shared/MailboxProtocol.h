/**
 * @file MailboxProtocol.h
 * @brief 우편함 서비스의 와이어 형식입니다. 메서드 번호(영역 `OnlineMethodRange::kMailbox`), 결과, 화면에 보이는 우편, 요청과 응답 형식을 정의합니다.
 * @details 요청과 응답은 `REFLECT` 구조체 하나씩이고 본문은 고정 배치형(B)이다. 바이트 배치는 `PROPERTY` 선언 순서와 키가 정한다.
 *          업무 결과는 응답 본문(`MailboxReply::_result`)에 담으며 경제와 거래 키트도 같다.
 *          캠페인 우편의 키는 `campaign/<id 16진>` 이다. 개인 우편의 키는 받는 계정의 16진 문자열로 시작하므로 겹치지 않는다.
 *          수령 응답의 잔액은 게임이 경제 클라이언트(`EconomyClient::applyBalances`)로 넘긴다. 키트끼리는 서로 모른다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 우편함 메서드 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)이고, 이름의 snake_case 가 지표 라벨입니다. 새 메서드는 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class MailboxMethodName : uint8
    {
        List,
        MarkRead,
        Claim,
        ClaimAll,
        Delete,
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief 우편함 메서드 번호입니다. 와이어 값이며 온라인 스키마 해시에 들어가지 않습니다(`OnlineProtocolRegistry.h`). */
    struct MailboxMethod : OnlineMethodTable<MailboxMethodName, OnlineMethodRange::kMailbox>
    {
        static constexpr uint16 kList     = toMethod( MailboxMethodName::List );
        static constexpr uint16 kMarkRead = toMethod( MailboxMethodName::MarkRead );
        static constexpr uint16 kClaim    = toMethod( MailboxMethodName::Claim );    ///< 멱등 키가 필수이다
        static constexpr uint16 kClaimAll = toMethod( MailboxMethodName::ClaimAll ); ///< 멱등 키가 필수이다
        static constexpr uint16 kDelete   = toMethod( MailboxMethodName::Delete );

        /** @brief 상태를 바꾸는 수령이면 true 입니다. 멱등 키가 필수입니다. */
        static constexpr bool isClaim( uint16 method ) { return method == kClaim || method == kClaimAll; }
    };
} // namespace sw

namespace sw
{
    /** @brief 우편함 결과입니다. 와이어 값이므로 새 값은 끝에만 추가합니다. */
    ENUM( Count = Count )
    enum class MailboxResult : uint8
    {
        Ok = 0,
        NotSignedIn,
        InvalidRequest,
        NotFound, ///< 없는 우편이거나 다른 사람의 우편이다
        AlreadyClaimed,
        Expired,
        HasAttachments, ///< 받지 않은 첨부가 있어서 지울 수 없다
        CapExceeded,    ///< 받으면 상한을 넘는다. 우편은 그대로 남는다
        Busy,
        Unavailable,
        UpdateRequired, ///< 온라인 스키마 해시가 다르다(`kVersionMismatch`). 클라이언트만 만들고 서버는 보내지 않는다. 다시 접속하지 않으므로 재시도하지 않는다
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief 화면에 보이는 우편 하나입니다. */
    REFLECT()
    struct MailView
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 8 )
        vector<ServiceMailAttachment> _listAttachment{}; ///< 상한은 `ServiceMailConstant::kMaxAttachmentCount` 와 같다
        PROPERTY( MaxCount = 256 )
        string _mailKey{}; ///< 상한은 `OnlineLimit::kMaxKeySize` 와 같다
        PROPERTY( MaxCount = 128 )
        string _titleKey{}; ///< 상한은 `ServiceMailConstant::kMaxTitleSize` 와 같다
        PROPERTY( MaxCount = 2048 )
        string _body{}; ///< 상한은 `ServiceMailConstant::kMaxBodySize` 와 같다
        PROPERTY( MaxCount = 64 )
        string _senderName{}; ///< 상한은 `ServiceMailConstant::kMaxSenderNameSize` 와 같다
        PROPERTY()
        int64 _createdMs{ 0 };
        PROPERTY()
        int64 _expiresMs{ 0 };
        PROPERTY()
        ServiceMailState _state{ ServiceMailState::Unread };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bLiteralText{ SW_FALSE };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bCampaign{ SW_FALSE };
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 요청입니다. 메서드마다 쓰는 필드만 채웁니다. */
    REFLECT()
    struct MailboxRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 256 )
        string _mailKey{}; ///< 상한은 `OnlineLimit::kMaxKeySize` 와 같다
        PROPERTY( MaxCount = 256 )
        string _cursor{}; ///< List 에서 쓴다. 앞 응답의 `_nextCursor` 이며 처음에는 빈 문자열이다
        PROPERTY( Min = 1, Max = 50 )
        int32 _maxCount{ 20 }; ///< 상한은 `MailboxProtocol::kMaxListCount` 와 같다
    };
} // namespace sw

namespace sw
{
    /** @brief 모든 메서드의 응답입니다. 메서드마다 쓰는 필드만 채웁니다. */
    REFLECT()
    struct MailboxReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 100 )
        vector<MailView> _listMail{}; ///< 캠페인이 개인 우편 앞에 끼어들므로 상한은 `MailboxProtocol::kMaxListCount` 의 두 배이다
        PROPERTY( MaxCount = 1024 )
        vector<LedgerBalance> _listBalance{}; ///< 수령과 모두 받기에서 바뀐 잔액이다. 게임이 EconomyClient::applyBalances 로 넘긴다
        PROPERTY( MaxCount = 256 )
        string _nextCursor{};
        PROPERTY()
        int32 _claimedCount{ 0 };
        PROPERTY()
        MailboxResult _result{ MailboxResult::Ok };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MailboxProtocol
     * @brief 우편함 키트의 메시지 타입 목록, 등록 항목, 목록 상한입니다. 본문은 고정 배치형(B)이며 `MessageTypes::write` · `read` 로만 쓰고 읽습니다.
     */
    struct SW_KIT_API MailboxProtocol
    {
        using MessageTypes = OnlineMessageTypes<MailboxRequest, MailboxReply>;

        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다
        static constexpr int32           kMaxListCount   = 50;
        static constexpr const utf8*     kCampaignPrefix = "campaign/";

        /** @brief 공통 오류 코드(`OnlineError`)를 결과로 바꿉니다. 로그인 없음, 손상된 본문, 충돌은 각각 대응하는 결과로, 나머지는 Unavailable 로 바꿉니다. */
        static MailboxResult fromErrorCode( uint16 errorCode );
    };
} // namespace sw
