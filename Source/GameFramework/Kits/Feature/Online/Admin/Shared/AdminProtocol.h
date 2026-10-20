/**
 * @file AdminProtocol.h
 * @brief GM 명령의 와이어 형식입니다. 메서드 번호(영역 `OnlineMethodRange::kAdmin`), 권한 등급, 결과, 요청과 응답 형식, 명령별 필요 등급을 정의합니다.
 * @details 요청과 응답은 명령 하나에 `REFLECT` 구조체 하나를 쓰며 명령마다 쓰는 필드만 채운다. GM 요청은 드물어서 크기보다 형식을 하나로 두는 편이 낫다.
 *          본문은 고정 배치형(B)이고 바이트 배치는 `PROPERTY` 선언 순서와 키가 정한다. 업무 결과는 응답 본문(`AdminReply::_result`)에 담는다.
 *          권한 등급은 넷이며 `Viewer` < `Support` < `Operator` < `Super` 순서이다. 국내 게임사 GM 툴의 권한 그룹과 Nakama 콘솔의 역할에 해당한다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Report/ServiceReport.h"
#include "GameFramework/Base/Online/Sanction/ServiceSanction.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/Base/Online/Service/OnlineProtocolRegistry.h"
#include "GameFramework/GameFrameworkExports.h"

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
        PostNotice,
        RemoveNotice,
        ListLiveEvents,
        PutLiveEvent,
        RemoveLiveEvent,
        Count,
    };
} // namespace sw

namespace sw
{
    /** @brief GM 명령 메서드 번호입니다. 번호는 열거자 순서에서 나오며 그 이름 순서가 온라인 스키마 해시에 섞입니다(`OnlineMethodTable`). */
    struct AdminMethod : OnlineMethodTable<AdminMethodName, OnlineMethodRange::kAdmin, OnlineNoPushName>
    {
        static constexpr uint16 kLookupAccount    = toMethod( AdminMethodName::LookupAccount );
        static constexpr uint16 kAdjustAsset      = toMethod( AdminMethodName::AdjustAsset );
        static constexpr uint16 kSetSanction      = toMethod( AdminMethodName::SetSanction );
        static constexpr uint16 kSendMail         = toMethod( AdminMethodName::SendMail );
        static constexpr uint16 kBulkMail         = toMethod( AdminMethodName::BulkMail );
        static constexpr uint16 kCreateCampaign   = toMethod( AdminMethodName::CreateCampaign );
        static constexpr uint16 kListAudit        = toMethod( AdminMethodName::ListAudit );
        static constexpr uint16 kSetRole          = toMethod( AdminMethodName::SetRole );
        static constexpr uint16 kListReports      = toMethod( AdminMethodName::ListReports );      ///< 신고 목록(`_reportStatus` 가 Open 이면 열린 것, 아니면 닫힌 것)을 최근 것부터
        static constexpr uint16 kResolveReport    = toMethod( AdminMethodName::ResolveReport );    ///< 열린 신고 `_reportKey` 를 `_reportStatus` 로 닫는다. 메모가 처리 메모다
        static constexpr uint16 kSetMaintenance   = toMethod( AdminMethodName::SetMaintenance );   ///< 점검 창 `_scope` 를 건다(`_messageKey`, `_startMs`, `_endMs`, 허용 계정 `_listAccountID`)
        static constexpr uint16 kClearMaintenance = toMethod( AdminMethodName::ClearMaintenance ); ///< 점검 창 `_scope` 를 푼다
        static constexpr uint16 kPostNotice       = toMethod( AdminMethodName::PostNotice );       ///< 공지 `_noticeID` 를 올린다(`_body`, `_startMs`, `_endMs`, `_priority`, `_bLiteralText`)
        static constexpr uint16 kRemoveNotice     = toMethod( AdminMethodName::RemoveNotice );     ///< 공지 `_noticeID` 를 내린다
        static constexpr uint16 kListLiveEvents   = toMethod( AdminMethodName::ListLiveEvents );   ///< 라이브 이벤트 정의를 JSON 글로 돌려준다(`_listLiveEvent`)
        static constexpr uint16 kPutLiveEvent     = toMethod( AdminMethodName::PutLiveEvent );     ///< `_eventJSON` 정의 하나를 올리거나 덮어쓴다
        static constexpr uint16 kRemoveLiveEvent  = toMethod( AdminMethodName::RemoveLiveEvent );  ///< 이벤트 `_eventID` 를 지운다

        /** @brief 상태를 바꾸는 명령이면 true 입니다. 이 경우 멱등 키와 메모가 필수입니다. */
        static constexpr bool isMutating( uint16 method )
        {
            return method != kLookupAccount && method != kListAudit && method != kListReports && method != kListLiveEvents;
        }
        /** @brief 서버 디렉터리 키트(`IServerDirectoryControl`)가 효과를 쓰는 명령이면 true 입니다. GM 키트는 등급만 확인하고 넘깁니다. */
        static constexpr bool isDirectoryMethod( uint16 method )
        {
            return method == kSetMaintenance || method == kClearMaintenance || method == kPostNotice || method == kRemoveNotice;
        }
        /** @brief 라이브 운영 키트(`ILiveEventControl`)가 처리하는 명령이면 true 입니다. GM 키트는 등급만 확인하고 넘깁니다. */
        static constexpr bool isLiveEventMethod( uint16 method ) { return method == kListLiveEvents || method == kPutLiveEvent || method == kRemoveLiveEvent; }
        /** @brief 다른 키트에 넘기는 명령이면 true 입니다(저장소 작업은 등급과 형식만 확인한다). */
        static constexpr bool isForwardedMethod( uint16 method ) { return isDirectoryMethod( method ) || isLiveEventMethod( method ); }
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
        Ok = 0,
        NotSignedIn,
        Forbidden,      ///< 등급이 부족하거나 자기 등급을 바꾸려 했다
        InvalidRequest, ///< 본문, 메모, 멱등 키, 상한이 규칙에 맞지 않는다
        UnknownAccount,
        InsufficientFunds, ///< 회수가 잔액보다 크다. 환불 회수가 아니면 음수가 되지 않게 막는다
        CapExceeded,
        Busy,
        Unavailable,
        UpdateRequired, ///< 온라인 스키마 해시가 다르다(`kVersionMismatch`). 클라이언트만 만들고 서버는 보내지 않는다. 다시 접속하지 않으므로 재시도하지 않는다
        NotFound,       ///< 처리하려는 대상(열린 신고, 공지, 점검)이 없다
        Count
    };
} // namespace sw

namespace sw
{
    /** @brief GM 명령 하나의 입력입니다. 명령마다 쓰는 필드만 채웁니다. 문구 상한(256)은 관리자 프로토콜 공통 값입니다. */
    REFLECT()
    struct AdminRequest
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 8 )
        vector<ServiceMailAttachment> _listAttachment{}; ///< SendMail, BulkMail, CreateCampaign 에서 쓴다. 상한은 `ServiceMailConstant::kMaxAttachmentCount` 와 같다
        PROPERTY( MaxCount = 12 )
        vector<AccountID> _listAccountID{}; ///< BulkMail 에서 쓰며 12개 이하이다. 우편 하나가 쓰기 넷이라 감사와 멱등 기록까지 한 트랜잭션(64) 안에 든다
        PROPERTY( MaxCount = 256 )
        string _displayName{}; ///< LookupAccount 에서 계정 id 대신 쓴다. 이 프로세스에 접속해 있는 계정만 찾을 수 있다
        PROPERTY( MaxCount = 256 )
        string _subject{}; ///< ListAudit 의 주체 접두어이다(예: `acct.<16진>/`)
        PROPERTY( MaxCount = 48 )
        string _assetID{}; ///< 상한은 `LedgerConstant::kMaxAssetIDSize` 와 같다
        PROPERTY( MaxCount = 256 )
        string _reasonCode{}; ///< 제재 표시 사유이며 로컬라이제이션 키이다
        PROPERTY( MaxCount = 1024 )
        string _memo{}; ///< 티켓이나 사유이다. 상태를 바꾸는 명령에는 필수이며 1 KB 이하이다
        PROPERTY( MaxCount = 128 )
        string _titleKey{}; ///< 상한은 `ServiceMailConstant::kMaxTitleSize` 와 같다
        PROPERTY( MaxCount = 2048 )
        string _body{}; ///< 상한은 `ServiceMailConstant::kMaxBodySize` 와 같다
        PROPERTY( MaxCount = 256 )
        string _cursor{};
        PROPERTY( MaxCount = 16 )
        string _scope{}; ///< 점검 범위(`all` 또는 서버 종류)이다. 상한은 `ServerRecord::kMaxNameSize` 와 같다
        PROPERTY( MaxCount = 64 )
        string _messageKey{}; ///< 점검 안내 문구 키이다
        PROPERTY( MaxCount = 48 )
        string _eventID{}; ///< RemoveLiveEvent 의 이벤트 id 이다
        PROPERTY( MaxCount = 8192 )
        string _eventJSON{}; ///< PutLiveEvent 의 정의(`LiveEventDefinition` 의 리플렉션 JSON)이다. 상한은 `AdminProtocol::kMaxEventJSONSize` 와 같다
        PROPERTY( MaxCount = 96 )
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
        PROPERTY( Min = 1, Max = 100 )
        int32 _maxCount{ 20 }; ///< 상한은 `AdminProtocol::kMaxListCount` 와 같다
        PROPERTY()
        ServiceSanctionKind _sanctionKind{ ServiceSanctionKind::ChatMute };
        PROPERTY()
        uint64 _noticeID{ 0 }; ///< PostNotice, RemoveNotice 의 공지 id 이다
        PROPERTY()
        int32 _priority{ 0 }; ///< PostNotice 의 우선순위이며 큰 것이 위에 온다
        PROPERTY()
        AdminRole _role{ AdminRole::None };
        PROPERTY()
        ServiceReportStatus _reportStatus{ ServiceReportStatus::Open }; ///< ListReports 는 Open 이면 열린 목록, ResolveReport 는 Resolved 나 Dismissed
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bRefund{ SW_FALSE };
        PROPERTY( Min = 0, Max = 1 )
        uint8 _bLiteralText{ SW_FALSE }; ///< PostNotice 의 `_body` 가 GM 이 쓴 문구면 1, 로컬라이제이션 키면 0 이다 ///< AdjustAsset 회수에서 환불이나 결제 취소로 회수하는 경우이다. 잔액이 모자라면 빚으로 남기며 사유는 "refund.revoke" 이다
    };
} // namespace sw

namespace sw
{
    /** @brief 조회 결과의 분개 한 줄입니다. 그 계정 쪽의 자산별 순변화입니다. */
    REFLECT()
    struct AdminJournalLine
    {
        REFLECT_BODY();
        PROPERTY( MaxCount = 1024 )
        vector<LedgerBalance> _listChange{}; ///< 상한은 `OnlineLimit::kMaxBalanceCount` 와 같다
        PROPERTY( MaxCount = 32 )
        string _reason{}; ///< 상한은 `LedgerConstant::kMaxReasonSize` 와 같다
        PROPERTY( MaxCount = 256 )
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
        PROPERTY( MaxCount = 8192 )
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
        PROPERTY( MaxCount = 1024 )
        vector<LedgerBalance> _listBalance{}; ///< 상한은 `OnlineLimit::kMaxBalanceCount` 와 같다
        PROPERTY( MaxCount = 100 )
        vector<AdminJournalLine> _listJournal{}; ///< 상한은 `AdminProtocol::kMaxListCount` 와 같다
        PROPERTY( MaxCount = 100 )
        vector<ServiceAuditEntry> _listAudit{}; ///< 상한은 `AdminProtocol::kMaxListCount` 와 같다
        PROPERTY( MaxCount = 64 )
        vector<AdminLiveEventView> _listLiveEvent{}; ///< ListLiveEvents 의 결과이다. 상한은 `AdminProtocol::kMaxLiveEventCount` 와 같다
        PROPERTY( MaxCount = 10 )
        vector<ServiceReport> _listReport{}; ///< ListReports 의 결과이며 ResolveReport 는 닫힌 신고 하나이다. 상한은 `AdminProtocol::kMaxReportListCount` 와 같다
        PROPERTY()
        ServiceSanctionState _sanction{};
        PROPERTY()
        AccountIdentity _identity{};
        PROPERTY( MaxCount = 256 )
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
        using MessageTypes = OnlineMessageTypes<AdminRequest, AdminReply>;

        static const OnlineProtocolEntry kOnlineEntry; ///< 온라인 스키마 해시에 섞이는 이 키트의 등록 항목. 정의가 `.cpp` 에 있어 모듈이 오를 때 등록된다
        static constexpr int32           kMaxBulkCount       = 12;
        static constexpr int32           kMaxMemoSize        = 1024;
        static constexpr int32           kMaxLookupCount     = 10;
        static constexpr int32           kMaxListCount       = 100;
        static constexpr int32           kMaxReportListCount = 10; ///< 신고 하나가 문맥 줄 20 개를 담아 커서 한 번에 열 개까지만 싣는다
        static constexpr int32           kMaxEventJSONSize   = 8192;
        static constexpr int32           kMaxLiveEventCount  = 64;

        /** @brief 공통 오류 코드(`OnlineError`)를 결과로 바꿉니다. */
        static AdminResult fromErrorCode( uint16 errorCode );
        /** @brief 명령에 필요한 등급을 반환합니다. 제재는 종류에 따라 다릅니다. 모르는 메서드는 `Count` 이며 누구도 가질 수 없는 등급입니다. */
        static AdminRole getRequiredRole( uint16 method, ServiceSanctionKind sanctionKind );
    };
} // namespace sw
