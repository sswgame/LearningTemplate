/**
 * @file MailboxProtocol.h
 * @brief 우편함 서비스의 와이어 형식입니다. 메서드와 요청 · 응답의 짝(`MailboxMethod`), 결과, 화면에 보이는 우편을 정의합니다. 프로토콜 서술자로 옮긴 첫 키트이고, 나머지 키트의 본보기입니다.
 * @details 메서드마다 요청과 응답 타입이 있어 메서드가 쓰지 않는 칸이 없다. 본문은 고정 배치형(B)이고 바이트 배치는 `PROPERTY` 선언 순서와 키가 정한다.
 *          결과는 두 층이다. 업무 결과(`MailboxResult`)는 응답의 `_result` 이고, 로그인 없음 · 깨진 요청 · 충돌 · 일시 장애는 공통 범주(`OnlineError`)로 `OnlineResult::_errorCode` 에 온다.
 *          캠페인 우편의 키는 `campaign/<id 16진>` 이다. 개인 우편의 키는 받는 계정의 16진 문자열로 시작하므로 겹치지 않는다.
 *          수령으로 바뀐 잔액은 서버가 경제 키트의 잔액 변경 알림(`EconomyMethod::BalanceChanged`)으로도 보내 지갑(`EconomyClient`)이 스스로 맞는다. 응답의 잔액은 표시용이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Mail/ServiceMailTypes.h"
#include "GameFramework/Runtime/Online/Service/OnlineLimit.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolRegistry.h"

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

    /** @brief 우편함 알림 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)입니다. 새 알림은 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class MailboxPushName : uint8
    {
        NewMail,
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief 우편함 업무 결과입니다. 공통 범주(로그인 · 충돌 · 일시 장애 · 버전)는 `OnlineError` 이고 여기 적지 않습니다. 와이어 값이므로 새 값은 끝에만 추가합니다. */
    ENUM( Count = Count )
    enum class MailboxResult : uint8
    {
        Ok = 0,
        NotFound, ///< 없는 우편이거나 다른 사람의 우편이다
        AlreadyClaimed,
        Expired,
        HasAttachments, ///< 받지 않은 첨부가 있어서 지울 수 없다
        CapExceeded,    ///< 받으면 상한을 넘는다. 우편은 그대로 남는다
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
        PROPERTY( MaxCount = ServiceMailConstant::kMaxAttachmentCount )
        vector<ServiceMailAttachment> _listAttachment{}; ///< 상한은 `ServiceMailConstant::kMaxAttachmentCount` 와 같다
        PROPERTY( MaxCount = OnlineLimit::kMaxKeySize )
        string _mailKey{}; ///< 상한은 `OnlineLimit::kMaxKeySize` 와 같다
        PROPERTY( MaxCount = ServiceMailConstant::kMaxTitleSize )
        string _titleKey{}; ///< 상한은 `ServiceMailConstant::kMaxTitleSize` 와 같다
        PROPERTY( MaxCount = ServiceMailConstant::kMaxBodySize )
        string _body{}; ///< 상한은 `ServiceMailConstant::kMaxBodySize` 와 같다
        PROPERTY( MaxCount = ServiceMailConstant::kMaxSenderNameSize )
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
    /** @brief 목록 요청입니다. */
    REFLECT()
    struct MailboxListRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = OnlineLimit::kMaxKeySize )
        string _cursor{}; ///< 앞 응답의 `_nextCursor` 이며 처음에는 빈 문자열이다. 상한은 `OnlineLimit::kMaxKeySize` 와 같다
        PROPERTY( Min = 1, Max = MailboxProtocol::kMaxListCount )
        int32 _maxCount{ 20 }; ///< 상한은 `MailboxProtocol::kMaxListCount` 와 같다
    };

    /** @brief 우편 하나를 가리키는 요청입니다(읽음 · 수령 · 지우기). */
    REFLECT()
    struct MailboxMailRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = OnlineLimit::kMaxKeySize )
        string _mailKey{}; ///< 상한은 `OnlineLimit::kMaxKeySize` 와 같다
    };

    /** @brief 목록 응답입니다. */
    REFLECT()
    struct MailboxListReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = MailboxProtocol::kMaxViewCount )
        vector<MailView> _listMail{}; ///< 캠페인이 개인 우편 앞에 끼어들므로 상한은 `MailboxProtocol::kMaxListCount` 의 두 배이다
        PROPERTY( MaxCount = OnlineLimit::kMaxKeySize )
        string _nextCursor{};
    };

    /** @brief 수령과 모두 받기의 응답입니다. */
    REFLECT()
    struct MailboxClaimReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = OnlineLimit::kMaxBalanceCount )
        vector<LedgerBalance> _listBalance{}; ///< 바뀐 잔액이다. 지갑은 같은 값을 잔액 변경 알림으로 받는다
        PROPERTY()
        int32 _claimedCount{ 0 };
        PROPERTY()
        MailboxResult _result{ MailboxResult::Ok };
    };

    /** @brief 결과만 있는 응답입니다(읽음 · 지우기). */
    REFLECT()
    struct MailboxResultReply
    {
        REFLECT_BODY();
        PROPERTY()
        MailboxResult _result{ MailboxResult::Ok };
    };
} // namespace sw

namespace sw
{
    /** @brief 새 우편 알림의 본문입니다. 서버 간 push 중계에도 실립니다. */
    REFLECT()
    struct SW_KIT_API MailboxNewMail
    {
        REFLECT_BODY();
        static constexpr uint32 kBusVersion = 1; ///< push 중계(`OnlinePushRelay`)의 의미 버전
        PROPERTY( MaxCount = OnlineLimit::kMaxKeySize )
        string _mailKey{}; ///< 상한은 `OnlineLimit::kMaxKeySize` 와 같다
        PROPERTY( MaxCount = ServiceMailConstant::kMaxTitleSize )
        string _titleKey{}; ///< 상한은 `ServiceMailConstant::kMaxTitleSize` 와 같다
    };
} // namespace sw

namespace sw
{
    /** @brief 우편함 메서드 표입니다. 번호는 열거자 순서에서 나오고, 요청 · 응답의 짝과 호출 규칙은 서술자가 정합니다. */
    struct MailboxMethod : OnlineMethodTable<MailboxMethodName, OnlineMethodRange::kMailbox, MailboxPushName>
    {
        using List     = Method<MethodName::List, MailboxListRequest, MailboxListReply>;
        using MarkRead = Method<MethodName::MarkRead, MailboxMailRequest, MailboxResultReply>;
        using Claim    = Method<MethodName::Claim, MailboxMailRequest, MailboxClaimReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using ClaimAll = Method<MethodName::ClaimAll, OnlineNoBody, MailboxClaimReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using Delete   = Method<MethodName::Delete, MailboxMailRequest, MailboxResultReply>;
        using NewMail  = Push<PushName::NewMail, MailboxNewMail>; ///< 새 우편이 왔다. 클라이언트는 목록을 다시 읽는다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct MailboxProtocol
     * @brief 우편함 키트의 프로토콜 표, 메시지 목록, 등록 항목, 목록 상한입니다.
     */
    struct SW_KIT_API MailboxProtocol
    {
        using Table        = OnlineProtocolTable<MailboxMethod, OnlineMethodList<MailboxMethod::List, MailboxMethod::MarkRead, MailboxMethod::Claim, MailboxMethod::ClaimAll, MailboxMethod::Delete>,
                                                 OnlinePushList<MailboxMethod::NewMail>>;
        using MessageTypes = Table::MessageTypes;
        using ResultName   = MailboxResult;

        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다
        static constexpr int32           kMaxListCount   = 50;
        static constexpr int32           kMaxViewCount   = kMaxListCount * 2; ///< 응답 한 번의 우편 수. 캠페인이 개인 우편 앞에 끼어든다
        static constexpr const utf8*     kCampaignPrefix = "campaign/";
    };
} // namespace sw
