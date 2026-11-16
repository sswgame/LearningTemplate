/**
 * @file AdminProtocol.h
 * @brief GM 명령의 와이어 형식입니다. 메서드 번호(영역 `OnlineMethodRange::kAdmin`), 권한 등급, 결과, 요청과 응답 형식, 명령별 필요 등급을 정의합니다.
 * @details 요청과 응답은 명령 하나에 `REFLECT` 구조체 하나를 쓰며 명령마다 쓰는 필드만 채운다. GM 요청은 드물어서 크기보다 형식을 하나로 두는 편이 낫다.
 *          본문은 고정 배치형(B)이고 바이트 배치는 `PROPERTY` 선언 순서와 키가 정한다. 업무 결과는 응답 본문(`AdminReply::_result`)에 담는다.
 *          권한 등급은 넷이며 `Viewer` < `Support` < `Operator` < `Super` 순서이다. 국내 게임사 GM 툴의 권한 그룹과 Nakama 콘솔의 역할에 해당한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Audit/ServiceAuditEntry.h"
#include "GameFramework/Runtime/Online/Directory/ServerRecord.h"
#include "GameFramework/Runtime/Online/Identity/AccountIdentity.h"
#include "GameFramework/Runtime/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Runtime/Online/Mail/ServiceMailTypes.h"
#include "GameFramework/Runtime/Online/Report/ServiceReport.h"
#include "GameFramework/Runtime/Online/Sanction/ServiceSanctionState.h"
#include "GameFramework/Runtime/Online/Service/OnlineLimit.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocol.h"
#include "GameFramework/Runtime/Online/Service/OnlineProtocolRegistry.h"

namespace sw
{
    /** @brief GM 명령 메서드 이름입니다. 열거자 순서가 와이어 번호(`OnlineMethodTable`)이고, 이름의 snake_case 가 지표 라벨입니다. 새 메서드는 `Count` 앞에만 더합니다. */
    ENUM( Count = Count )
    enum class AdminMethodName : uint8
    {
        LookupAccount,
        AdjustAsset,
        SetSanction,
        SendMail,
        BulkMail,
        CreateCampaign,
        ListAudit,
        SetRole,
        ListReports,
        ResolveReport,
        SetMaintenance,
        ClearMaintenance,
        PostAnnouncement,
        RemoveAnnouncement,
        ListLiveEvents,
        PutLiveEvent,
        RemoveLiveEvent,
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief 권한 등급입니다. 큰 등급이 작은 등급을 포함합니다. 저장 값이므로 순서를 바꾸지 않습니다. `Count` 는 누구도 가질 수 없는 등급이며 와이어에 오르지 않습니다. */
    ENUM( Count = Count )
    enum class AdminRole : uint8
    {
        None = 0,
        Viewer,   ///< 조회, 감사 기록과 신고 열람이 가능하다
        Support,  ///< Viewer 권한에 채팅 금지와 신고 처리가 더해진다
        Operator, ///< Support 권한에 지급, 회수, 정지, 우편이 더해진다
        Super,    ///< Operator 권한에 영구 정지, 일괄 지급, 전체 우편, 권한 부여가 더해진다
        Count
    };

    SW_KIT_API const utf8* toString( AdminRole role );
} // namespace sw

namespace sw
{
    /** @brief GM 명령 결과입니다. 와이어 값이므로 새 값은 끝에만 추가합니다. */
    ENUM( Count = Count )
    enum class AdminResult : uint8
    {
        Ok = 0, ///< 0 이어야 한다 — 멱등 기록이 결과 한 바이트(`Ok`)를 저장소에 남긴다(`AdminStoreLogic::execute`)
        UnknownAccount,
        InsufficientFunds, ///< 회수가 잔액보다 크다. 환불 회수가 아니면 음수가 되지 않게 막는다
        CapExceeded,
        NotFound, ///< 처리하려는 대상(열린 신고, 공지, 점검)이 없다
        Count
    };

    /**
     * @brief GM 명령의 결과입니다. 업무 결과(`AdminResult`) 또는 공통 범주(`OnlineError`)입니다.
     * @details 등급 부족 · 모르는 명령은 `kForbidden`, 본문 · 메모 · 멱등 키 · 상한 위반은 `kInvalidRequest`, 버전 충돌이 상한을 넘으면 `kBusy`, 저장소 장애는 `kUnavailable`,
     *          로그인 없음은 `kUnauthenticated` 이다. 서버 로직, 같은 프로세스 호출, 에디터 도구가 같은 형식을 쓴다.
     */
    using AdminServiceResult = OnlineServiceResult<AdminResult>;
} // namespace sw

namespace sw
{
    /** @brief GM 명령 하나의 입력입니다. 명령마다 쓰는 필드만 채웁니다. 문구 상한(256)은 관리자 프로토콜 공통 값입니다. */
    REFLECT()
    struct AdminRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = ServiceMailConstant::kMaxAttachmentCount )
        vector<ServiceMailAttachment> _listAttachment{}; ///< SendMail, BulkMail, CreateCampaign 에서 쓴다. 상한은 `ServiceMailConstant::kMaxAttachmentCount` 와 같다
        PROPERTY( MaxCount = AdminProtocol::kMaxBulkCount )
        vector<AccountID> _listAccountID{}; ///< BulkMail 에서 쓰며 12개 이하이다. 우편 하나가 쓰기 넷이라 감사와 멱등 기록까지 한 트랜잭션(64) 안에 들어간다
        PROPERTY( MaxCount = AdminProtocol::kMaxTextSize )
        string _displayName{}; ///< LookupAccount 에서 계정 id 대신 쓴다. 계정 키트의 이름 색인으로 찾으므로 접속하지 않은 계정도 찾는다
        PROPERTY( MaxCount = ServiceAuditEntry::kMaxTextSize )
        string _subject{}; ///< ListAudit 의 주체 접두어이다(예: `acct.<16진>/`)
        PROPERTY( MaxCount = LedgerConstant::kMaxAssetIDSize )
        string _assetID{}; ///< 상한은 `LedgerConstant::kMaxAssetIDSize` 와 같다
        PROPERTY( MaxCount = ServiceSanctionState::kMaxReasonCodeSize )
        string _reasonCode{}; ///< 제재 표시 사유이며 로컬라이제이션 키이다
        PROPERTY( MaxCount = AdminProtocol::kMaxMemoSize )
        string _memo{}; ///< 티켓이나 사유이다. 상태를 바꾸는 명령에는 필수이며 1 KB 이하이다
        PROPERTY( MaxCount = ServiceMailConstant::kMaxTitleSize )
        string _titleKey{}; ///< 상한은 `ServiceMailConstant::kMaxTitleSize` 와 같다
        PROPERTY( MaxCount = ServiceMailConstant::kMaxBodySize )
        string _body{}; ///< 상한은 `ServiceMailConstant::kMaxBodySize` 와 같다
        PROPERTY( MaxCount = OnlineLimit::kMaxKeySize )
        string _cursor{};
        PROPERTY( MaxCount = ServerRecord::kMaxNameSize )
        string _scope{}; ///< 점검 범위(`all` 또는 서버 종류)이다. 상한은 `ServerRecord::kMaxNameSize` 와 같다
        PROPERTY( MaxCount = AdminProtocol::kMaxMessageKeySize )
        string _messageKey{}; ///< 점검 안내 문구 키이다
        PROPERTY( MaxCount = AdminProtocol::kMaxEventIDSize )
        string _eventID{}; ///< RemoveLiveEvent 의 이벤트 id 이다
        PROPERTY( MaxCount = AdminProtocol::kMaxEventJSONSize )
        string _eventJSON{}; ///< PutLiveEvent 의 정의(`LiveEventDef` 의 리플렉션 JSON)이다. 상한은 `AdminProtocol::kMaxEventJSONSize` 와 같다
        PROPERTY( MaxCount = ServiceReportConstant::kMaxReportKeySize )
        string _reportKey{}; ///< ResolveReport 의 대상이다. 상한은 `ServiceReportConstant::kMaxReportKeySize` 와 같다
        PROPERTY()
        AccountID _accountID{ kInvalidAccountID };
        PROPERTY()
        uint64 _batchID{ 0 }; ///< BulkMail 의 배치 id 이거나 CreateCampaign 의 id 이다
        PROPERTY()
        int64 _amount{ 0 }; ///< AdjustAsset 에서 양수는 지급이고 음수는 회수이다
        PROPERTY()
        int64 _untilMs{ 0 }; ///< SetSanction 의 종료 시각이다. 0이면 해제한다
        PROPERTY()
        int64 _startMs{ 0 };
        PROPERTY()
        int64 _endMs{ 0 };
        PROPERTY( Min = 1, Max = AdminProtocol::kMaxListCount )
        int32 _maxCount{ 20 }; ///< 상한은 `AdminProtocol::kMaxListCount` 와 같다
        PROPERTY()
        ServiceSanctionKind _sanctionKind{ ServiceSanctionKind::ChatMute };
        PROPERTY()
        uint64 _announcementID{ 0 }; ///< PostAnnouncement, RemoveAnnouncement 의 공지 id 이다
        PROPERTY()
        int32 _priority{ 0 }; ///< PostAnnouncement 의 우선순위이며 큰 것이 위에 온다
        PROPERTY()
        AdminRole _role{ AdminRole::None };
        PROPERTY()
        ServiceReportStatus _reportStatus{ ServiceReportStatus::Open }; ///< ListReports 는 Open 이면 열린 목록, ResolveReport 는 Resolved 나 Dismissed
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bRefund{ SW_FALSE }; ///< AdjustAsset 회수에서 환불이나 결제 취소로 회수하는 경우이다. 잔액이 모자라면 빚으로 남기며 사유는 "refund.revoke" 이다
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bLiteralText{ SW_FALSE }; ///< PostAnnouncement 의 `_body` 가 GM 이 쓴 문구면 1, 로컬라이제이션 키면 0 이다
    };
} // namespace sw

namespace sw
{
    /** @brief 조회 결과의 분개 한 줄입니다. 그 계정 쪽의 자산별 순변화입니다. */
    REFLECT()
    struct AdminJournalLine
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = OnlineLimit::kMaxBalanceCount )
        vector<LedgerBalance> _listChange{}; ///< 상한은 `OnlineLimit::kMaxBalanceCount` 와 같다
        PROPERTY( MaxCount = LedgerConstant::kMaxReasonSize )
        string _reason{}; ///< 상한은 `LedgerConstant::kMaxReasonSize` 와 같다
        PROPERTY( MaxCount = LedgerConstant::kMaxMemoSize )
        string _memo{}; ///< 상한은 `LedgerConstant::kMaxMemoSize` 와 같다
        PROPERTY()
        int64 _timeMs{ 0 };
        PROPERTY()
        uint64 _actorID{ 0 };
        PROPERTY()
        LedgerActorKind _actorKind{ LedgerActorKind::System };
    };
} // namespace sw

namespace sw
{
    /** @brief 라이브 이벤트 정의 하나입니다. 가변 길이 글의 목록이라 원소를 구조체로 감쌌습니다. */
    REFLECT()
    struct AdminLiveEventView
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = AdminProtocol::kMaxEventJSONSize )
        string _definitionJSON{}; ///< 상한은 `AdminProtocol::kMaxEventJSONSize` 와 같다
    };
} // namespace sw

namespace sw
{
    /** @brief GM 명령의 응답입니다. */
    REFLECT()
    struct AdminReply
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = OnlineLimit::kMaxBalanceCount )
        vector<LedgerBalance> _listBalance{}; ///< 상한은 `OnlineLimit::kMaxBalanceCount` 와 같다
        PROPERTY( MaxCount = AdminProtocol::kMaxListCount )
        vector<AdminJournalLine> _listJournal{}; ///< 상한은 `AdminProtocol::kMaxListCount` 와 같다
        PROPERTY( MaxCount = AdminProtocol::kMaxListCount )
        vector<ServiceAuditEntry> _listAudit{}; ///< 상한은 `AdminProtocol::kMaxListCount` 와 같다
        PROPERTY( MaxCount = AdminProtocol::kMaxLiveEventCount )
        vector<AdminLiveEventView> _listLiveEvent{}; ///< ListLiveEvents 의 결과이다. 상한은 `AdminProtocol::kMaxLiveEventCount` 와 같다
        PROPERTY( MaxCount = AdminProtocol::kMaxReportListCount )
        vector<ServiceReport> _listReport{}; ///< ListReports 의 결과이며 ResolveReport 는 닫힌 신고 하나이다. 상한은 `AdminProtocol::kMaxReportListCount` 와 같다
        PROPERTY()
        ServiceSanctionState _sanction{};
        PROPERTY()
        AccountIdentity _identity{};
        PROPERTY( MaxCount = OnlineLimit::kMaxKeySize )
        string _nextCursor{};
        PROPERTY()
        int32 _processedCount{ 0 }; ///< BulkMail 에서 이번에 새로 보낸 수이다
        PROPERTY()
        AdminRole _targetRole{ AdminRole::None };
        PROPERTY()
        AdminResult _result{ AdminResult::Ok };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bOnline{ SW_FALSE };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bReplayed{ SW_FALSE };
        uint8 _bRevokeSessions{ SW_FALSE }; ///< 서버 안에서만 쓴다. 정지나 영구 정지가 걸렸다는 뜻이며 `PROPERTY` 가 아니어서 와이어에 싣지 않는다

        /** @brief 다시 시도할 업무 결과가 없다 — 충돌 · 일시 장애는 공통 범주(`kBusy` · `kUnavailable`)로 오고 보존은 그 범주가 정한다. */
        [[nodiscard]] bool isRetryable() const { return false; }
    };
} // namespace sw

namespace sw
{
    /** @brief Admin 메서드 표입니다. 번호는 열거자 순서에서 나오고, 요청 · 응답의 짝과 호출 규칙은 서술자가 정합니다. */
    struct AdminMethod : OnlineMethodTable<AdminMethodName, OnlineMethodRange::kAdmin, OnlineNoPushName>
    {
        using LookupAccount      = Method<MethodName::LookupAccount, AdminRequest, AdminReply>;
        using AdjustAsset        = Method<MethodName::AdjustAsset, AdminRequest, AdminReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using SetSanction        = Method<MethodName::SetSanction, AdminRequest, AdminReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using SendMail           = Method<MethodName::SendMail, AdminRequest, AdminReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using BulkMail           = Method<MethodName::BulkMail, AdminRequest, AdminReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using CreateCampaign     = Method<MethodName::CreateCampaign, AdminRequest, AdminReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using ListAudit          = Method<MethodName::ListAudit, AdminRequest, AdminReply>;
        using SetRole            = Method<MethodName::SetRole, AdminRequest, AdminReply, OnlineMethodFlag::RequiresIdempotencyKey>;
        using ListReports        = Method<MethodName::ListReports, AdminRequest, AdminReply>;                                                  ///< 신고 목록(`_reportStatus` 가 Open 이면 열린 것, 아니면 닫힌 것)을 최근 것부터
        using ResolveReport      = Method<MethodName::ResolveReport, AdminRequest, AdminReply, OnlineMethodFlag::RequiresIdempotencyKey>;      ///< 열린 신고 `_reportKey` 를 `_reportStatus` 로 닫는다. 메모가 처리 메모다
        using SetMaintenance     = Method<MethodName::SetMaintenance, AdminRequest, AdminReply, OnlineMethodFlag::RequiresIdempotencyKey>;     ///< 점검 창 `_scope` 를 건다(`_messageKey`, `_startMs`, `_endMs`, 허용 계정 `_listAccountID`)
        using ClearMaintenance   = Method<MethodName::ClearMaintenance, AdminRequest, AdminReply, OnlineMethodFlag::RequiresIdempotencyKey>;   ///< 점검 창 `_scope` 를 푼다
        using PostAnnouncement   = Method<MethodName::PostAnnouncement, AdminRequest, AdminReply, OnlineMethodFlag::RequiresIdempotencyKey>;   ///< 공지 `_announcementID` 를 게시한다(`_body`, `_startMs`, `_endMs`, `_priority`, `_bLiteralText`)
        using RemoveAnnouncement = Method<MethodName::RemoveAnnouncement, AdminRequest, AdminReply, OnlineMethodFlag::RequiresIdempotencyKey>; ///< 공지 `_announcementID` 를 삭제한다
        using ListLiveEvents     = Method<MethodName::ListLiveEvents, AdminRequest, AdminReply>;                                               ///< 라이브 이벤트 정의를 JSON 글로 돌려준다(`_listLiveEvent`)
        using PutLiveEvent       = Method<MethodName::PutLiveEvent, AdminRequest, AdminReply, OnlineMethodFlag::RequiresIdempotencyKey>;       ///< `_eventJSON` 정의 하나를 올리거나 덮어쓴다
        using RemoveLiveEvent    = Method<MethodName::RemoveLiveEvent, AdminRequest, AdminReply, OnlineMethodFlag::RequiresIdempotencyKey>;    ///< 이벤트 `_eventID` 를 지운다

        /** @brief 상태를 바꾸는 명령이면 true 입니다. 이 경우 멱등 키와 메모가 필수입니다. 멱등 키는 서술자의 호출 규칙과 같습니다. */
        static constexpr bool isMutating( uint16 method )
        {
            return method != LookupAccount::kID && method != ListAudit::kID && method != ListReports::kID && method != ListLiveEvents::kID;
        }
        /** @brief 서버 디렉터리 키트(`IServerDirectoryControl`)가 효과를 쓰는 명령이면 true 입니다. GM 키트는 등급만 확인하고 넘깁니다. */
        static constexpr bool isDirectoryMethod( uint16 method )
        {
            return method == SetMaintenance::kID || method == ClearMaintenance::kID || method == PostAnnouncement::kID || method == RemoveAnnouncement::kID;
        }
        /** @brief 라이브 운영 키트(`ILiveEventControl`)가 처리하는 명령이면 true 입니다. GM 키트는 등급만 확인하고 넘깁니다. */
        static constexpr bool isLiveEventMethod( uint16 method ) { return method == ListLiveEvents::kID || method == PutLiveEvent::kID || method == RemoveLiveEvent::kID; }
        /** @brief 다른 키트에 넘기는 명령이면 true 입니다(저장소 작업은 등급과 형식만 확인한다). */
        static constexpr bool isForwardedMethod( uint16 method ) { return isDirectoryMethod( method ) || isLiveEventMethod( method ); }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct AdminProtocol
     * @brief GM 명령의 메시지 타입 목록, 등록 항목, 상한, 등급 테이블입니다. 본문은 고정 배치형(B)이며 `MessageTypes::write` · `read` 로만 쓰고 읽습니다.
     */
    struct SW_KIT_API AdminProtocol
    {
        using Table        = OnlineProtocolTable<AdminMethod, OnlineMethodList<AdminMethod::LookupAccount, AdminMethod::AdjustAsset, AdminMethod::SetSanction, AdminMethod::SendMail, AdminMethod::BulkMail, AdminMethod::CreateCampaign, AdminMethod::ListAudit, AdminMethod::SetRole, AdminMethod::ListReports, AdminMethod::ResolveReport, AdminMethod::SetMaintenance, AdminMethod::ClearMaintenance, AdminMethod::PostAnnouncement, AdminMethod::RemoveAnnouncement, AdminMethod::ListLiveEvents, AdminMethod::PutLiveEvent, AdminMethod::RemoveLiveEvent>, OnlinePushList<>>;
        using MessageTypes = Table::MessageTypes;
        using ResultName   = AdminResult;

        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다
        static constexpr int32           kMaxBulkCount       = 12;
        static constexpr int32           kMaxMemoSize        = 1024;
        static constexpr int32           kMaxLookupCount     = 10;
        static constexpr int32           kMaxListCount       = 100;
        static constexpr int32           kMaxReportListCount = 10; ///< 신고 하나가 문맥 줄 20 개를 담아 커서 한 번에 열 개까지만 싣는다
        static constexpr int32           kMaxEventJSONSize   = 8192;
        static constexpr int32           kMaxLiveEventCount  = 64;
        static constexpr int32           kMaxTextSize        = 256; ///< 문구 상한이다(표시 이름 조회). 감사 줄과 제재 사유의 상한과 같다(아래 static_assert)
        static constexpr int32           kMaxMessageKeySize  = 64;
        static constexpr int32           kMaxEventIDSize     = 48;

        /** @brief 명령에 필요한 등급을 반환합니다. 제재는 종류에 따라 다릅니다. 모르는 메서드는 `Count` 이며 누구도 가질 수 없는 등급입니다. */
        static AdminRole getRequiredRole( uint16 method, ServiceSanctionKind sanctionKind );
    };

    static_assert( AdminProtocol::kMaxTextSize == ServiceAuditEntry::kMaxTextSize && AdminProtocol::kMaxTextSize == ServiceSanctionState::kMaxReasonCodeSize,
                   "the admin text limit, the audit entry text limit and the sanction reason limit are one wire value" );
} // namespace sw
