/**
 * @file AdminProtocol.h
 * @brief GM 명령의 와이어 — 메서드 번호(영역 `OnlineMethodRange::kAdmin`) · 권한 등급 · 결과 · 요청/응답 형식 · 명령별 필요 등급입니다.
 * @details 요청 · 응답은 명령 하나에 구조체 하나(쓰는 칸만 찬다) — GM 요청은 드물어 크기보다 한 형식이 낫다. 응답 몸 = `AdminResult` + 칸(업무 결과는 몸에).
 *          권한 등급 넷(`Viewer` < `Support` < `Operator` < `Super`) — 넥슨 · 엔씨 GM 툴의 권한 그룹, Nakama 콘솔의 역할과 같은 자리.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Audit/ServiceAuditLog.h"
#include "GameFramework/Base/Online/Identity/AccountDirectory.h"
#include "GameFramework/Base/Online/Ledger/LedgerTypes.h"
#include "GameFramework/Base/Online/Mail/ServiceMail.h"
#include "GameFramework/Base/Online/Sanction/ServiceSanction.h"
#include "GameFramework/Base/Online/Service/OnlineProtocol.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class BitReader;
    class BitWriter;

    /** @brief GM 명령 메서드입니다. 와이어 값 — 바꾸면 `AdminProtocol::kVersion` 을 올린다. */
    struct AdminMethod
    {
        static constexpr uint16 kLookupAccount  = OnlineMethodRange::kAdmin + 0x01;
        static constexpr uint16 kAdjustAsset    = OnlineMethodRange::kAdmin + 0x02;
        static constexpr uint16 kSetSanction    = OnlineMethodRange::kAdmin + 0x03;
        static constexpr uint16 kSendMail       = OnlineMethodRange::kAdmin + 0x04;
        static constexpr uint16 kBulkMail       = OnlineMethodRange::kAdmin + 0x05;
        static constexpr uint16 kCreateCampaign = OnlineMethodRange::kAdmin + 0x06;
        static constexpr uint16 kListAudit      = OnlineMethodRange::kAdmin + 0x07;
        static constexpr uint16 kSetRole        = OnlineMethodRange::kAdmin + 0x08;
        static constexpr int32  kCount          = 8;

        static_assert( OnlineMethodRange::isInRange( kSetRole, OnlineMethodRange::kAdmin ) && OnlineMethodRange::isMethod( kSetRole ) );

        /** @brief 0 부터의 번호(지표)입니다. 모르는 메서드면 −1 입니다. */
        static constexpr int32 toIndex( uint16 method )
        {
            return kLookupAccount <= method && method < kLookupAccount + kCount ? static_cast<int32>( method - kLookupAccount ) : -1;
        }
        /** @brief 상태를 바꾸는 명령인가입니다(멱등 키 · 메모 필수). */
        static constexpr bool isMutating( uint16 method ) { return method != kLookupAccount && method != kListAudit; }
    };
} // namespace sw

namespace sw
{
    /** @brief 권한 등급 — 큰 것이 작은 것을 포함한다. 저장 값이라 순서를 바꾸지 않는다. */
    enum class AdminRole : uint8
    {
        None = 0,
        Viewer,   ///< 조회 · 감사 열람
        Support,  ///< + 채팅 금지
        Operator, ///< + 지급 · 회수 · 정지 · 우편
        Super,    ///< + 영구 정지 · 일괄 지급 · 전체 우편 · 권한 부여
        Count
    };

    SW_GF_API const utf8* toString( AdminRole role );
} // namespace sw

namespace sw
{
    /** @brief GM 명령 결과입니다. 와이어 값 — 끝에만 더한다. */
    enum class AdminResult : uint8
    {
        Ok = 0,
        NotSignedIn,
        Forbidden,      ///< 등급이 모자란다 · 자기 등급 바꾸기
        InvalidRequest, ///< 몸 · 메모 · 멱등 키 · 상한
        UnknownAccount,
        InsufficientFunds, ///< 회수가 잔액보다 크다(환불 회수가 아니면 음수 금지)
        CapExceeded,
        Busy,
        Unavailable,
        Count
    };

    SW_GF_API const utf8* toString( AdminResult result );
} // namespace sw

namespace sw
{
    /** @brief GM 명령 하나의 입력(명령마다 쓰는 칸만)입니다. */
    struct AdminRequest
    {
        vector<ServiceMailAttachment> _listAttachment{}; ///< SendMail · BulkMail · CreateCampaign
        vector<AccountId>             _listAccountId{};  ///< BulkMail(16 이하)
        string                        _displayName{};    ///< LookupAccount — 계정 id 대신(이 프로세스에 붙어 있는 계정만)
        string                        _subject{};        ///< ListAudit — 주체 접두(`acct.<16 진>/` …)
        string                        _assetId{};
        string                        _reasonCode{}; ///< 제재 표시 사유(로컬라이제이션 키)
        string                        _memo{};       ///< 티켓 · 사유(바꾸는 명령은 필수, 1 KB 이하)
        string                        _titleKey{};
        string                        _body{};
        string                        _cursor{};
        AccountId                     _accountId{ kInvalidAccountId };
        uint64                        _batchId{ 0 }; ///< BulkMail 배치 · CreateCampaign id
        int64                         _amount{ 0 };  ///< AdjustAsset — 양수 지급, 음수 회수
        int64                         _untilMs{ 0 }; ///< SetSanction — 0 = 풂
        int64                         _startMs{ 0 };
        int64                         _endMs{ 0 };
        int32                         _maxCount{ 20 };
        ServiceSanctionKind           _sanctionKind{ ServiceSanctionKind::ChatMute };
        AdminRole                     _role{ AdminRole::None };
        uint8                         _bRefund{ SW_FALSE }; ///< AdjustAsset 회수 — 환불 · 결제 취소 회수(잔액이 모자라면 빚으로, 사유 "refund.revoke")
    };
} // namespace sw

namespace sw
{
    /** @brief 조회의 분개 한 줄 — 그 계정 쪽의 자산별 순변화입니다. */
    struct AdminJournalLine
    {
        vector<LedgerBalance> _listChange{};
        string                _reason{};
        string                _memo{};
        int64                 _timeMs{ 0 };
        uint64                _actorId{ 0 };
        LedgerActorKind       _actorKind{ LedgerActorKind::System };
    };
} // namespace sw

namespace sw
{
    /** @brief GM 명령의 응답입니다. */
    struct AdminReply
    {
        vector<LedgerBalance>     _listBalance{};
        vector<AdminJournalLine>  _listJournal{};
        vector<ServiceAuditEntry> _listAudit{};
        ServiceSanctionState      _sanction{};
        AccountIdentity           _identity{};
        string                    _nextCursor{};
        int32                     _processedCount{ 0 }; ///< BulkMail — 이번에 새로 보낸 수
        AdminRole                 _targetRole{ AdminRole::None };
        AdminResult               _result{ AdminResult::Ok };
        uint8                     _bOnline{ SW_FALSE };
        uint8                     _bReplayed{ SW_FALSE };
        uint8                     _bRevokeSessions{ SW_FALSE }; ///< 서버 안에서만 — 정지 · 영구 정지가 걸렸다(와이어에 싣지 않는다)
    };
} // namespace sw

namespace sw
{
    /** @brief 형식 쓰기 · 읽기 · 등급 표입니다. 읽기는 상한을 넘거나 모자라면 false 입니다. */
    struct SW_GF_API AdminProtocol
    {
        static constexpr uint32 kVersion        = 1;
        static constexpr int32  kMaxBulkCount   = 16;
        static constexpr int32  kMaxMemoSize    = 1024;
        static constexpr int32  kMaxLookupCount = 10;
        static constexpr int32  kMaxListCount   = 100;
        static constexpr int32  kMaxTextSize    = 256;

        static void               writeRequest( BitWriter& outWriter, const AdminRequest& request );
        [[nodiscard]] static bool readRequest( BitReader& reader, AdminRequest& outRequest );
        static void               writeReply( BitWriter& outWriter, const AdminReply& reply );
        [[nodiscard]] static bool readReply( BitReader& reader, AdminReply& outReply );
        /** @brief 공통 오류 코드(`OnlineError`) → 결과입니다. */
        static AdminResult fromErrorCode( uint16 errorCode );
        /** @brief 명령에 필요한 등급입니다(제재는 종류에 따라). 모르는 메서드는 `Count`(누구도 갖지 못한 등급)입니다. */
        static AdminRole getRequiredRole( uint16 method, ServiceSanctionKind sanctionKind );
    };
} // namespace sw
