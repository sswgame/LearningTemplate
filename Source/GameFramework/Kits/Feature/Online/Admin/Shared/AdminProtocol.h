/**
 * @file AdminProtocol.h
 * @brief GM 명령의 와이어 형식입니다. 메서드 번호(영역 `OnlineMethodRange::kAdmin`), 권한 등급, 결과, 요청과 응답 형식, 명령별 필요 등급을 정의합니다.
 * @details 요청과 응답은 명령 하나에 구조체 하나를 쓰며 명령마다 쓰는 필드만 채운다. GM 요청은 드물어서 크기보다 형식을 하나로 두는 편이 낫다. 응답 본문은 `AdminResult` 뒤에 필드가 이어지며 업무 결과는 본문에 담는다.
 *          권한 등급은 넷이며 `Viewer` < `Support` < `Operator` < `Super` 순서이다. 국내 게임사 GM 툴의 권한 그룹과 Nakama 콘솔의 역할에 해당한다.
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

    /** @brief GM 명령 메서드 번호입니다. 와이어 값이므로 바꾸면 `AdminProtocol::kVersion` 을 올려야 합니다. */
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

        /** @brief 0부터 시작하는 번호입니다. 지표의 인덱스로 씁니다. 모르는 메서드이면 -1 입니다. */
        static constexpr int32 toIndex( uint16 method )
        {
            return kLookupAccount <= method && method < kLookupAccount + kCount ? static_cast<int32>( method - kLookupAccount ) : -1;
        }
        /** @brief 상태를 바꾸는 명령이면 true 입니다. 이 경우 멱등 키와 메모가 필수입니다. */
        static constexpr bool isMutating( uint16 method ) { return method != kLookupAccount && method != kListAudit; }
    };
} // namespace sw

namespace sw
{
    /** @brief 권한 등급입니다. 큰 등급이 작은 등급을 포함합니다. 저장 값이므로 순서를 바꾸지 않습니다. */
    enum class AdminRole : uint8
    {
        None = 0,
        Viewer,   ///< 조회와 감사 기록 열람이 가능하다
        Support,  ///< Viewer 권한에 채팅 금지가 더해진다
        Operator, ///< Support 권한에 지급, 회수, 정지, 우편이 더해진다
        Super,    ///< Operator 권한에 영구 정지, 일괄 지급, 전체 우편, 권한 부여가 더해진다
        Count
    };

    SW_KIT_API const utf8* toString( AdminRole role );
} // namespace sw

namespace sw
{
    /** @brief GM 명령 결과입니다. 와이어 값이므로 새 값은 끝에만 추가합니다. */
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
        Count
    };

    SW_KIT_API const utf8* toString( AdminResult result );
} // namespace sw

namespace sw
{
    /** @brief GM 명령 하나의 입력입니다. 명령마다 쓰는 필드만 채웁니다. */
    struct AdminRequest
    {
        vector<ServiceMailAttachment> _listAttachment{}; ///< SendMail, BulkMail, CreateCampaign 에서 쓴다
        vector<AccountID>             _listAccountID{};  ///< BulkMail 에서 쓰며 16개 이하이다
        string                        _displayName{};    ///< LookupAccount 에서 계정 id 대신 쓴다. 이 프로세스에 접속해 있는 계정만 찾을 수 있다
        string                        _subject{};        ///< ListAudit 의 주체 접두어이다(예: `acct.<16진>/`)
        string                        _assetID{};
        string                        _reasonCode{}; ///< 제재 표시 사유이며 로컬라이제이션 키이다
        string                        _memo{};       ///< 티켓이나 사유이다. 상태를 바꾸는 명령에는 필수이며 1 KB 이하이다
        string                        _titleKey{};
        string                        _body{};
        string                        _cursor{};
        AccountID                     _accountID{ kInvalidAccountID };
        uint64                        _batchID{ 0 }; ///< BulkMail 의 배치 id 이거나 CreateCampaign 의 id 이다
        int64                         _amount{ 0 };  ///< AdjustAsset 에서 양수는 지급이고 음수는 회수이다
        int64                         _untilMs{ 0 }; ///< SetSanction 의 종료 시각이다. 0이면 해제한다
        int64                         _startMs{ 0 };
        int64                         _endMs{ 0 };
        int32                         _maxCount{ 20 };
        ServiceSanctionKind           _sanctionKind{ ServiceSanctionKind::ChatMute };
        AdminRole                     _role{ AdminRole::None };
        uint8                         _bRefund{ SW_FALSE }; ///< AdjustAsset 회수에서 환불이나 결제 취소로 회수하는 경우이다. 잔액이 모자라면 빚으로 남기며 사유는 "refund.revoke" 이다
    };
} // namespace sw

namespace sw
{
    /** @brief 조회 결과의 분개 한 줄입니다. 그 계정 쪽의 자산별 순변화입니다. */
    struct AdminJournalLine
    {
        vector<LedgerBalance> _listChange{};
        string                _reason{};
        string                _memo{};
        int64                 _timeMs{ 0 };
        uint64                _actorID{ 0 };
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
        int32                     _processedCount{ 0 }; ///< BulkMail 에서 이번에 새로 보낸 수이다
        AdminRole                 _targetRole{ AdminRole::None };
        AdminResult               _result{ AdminResult::Ok };
        uint8                     _bOnline{ SW_FALSE };
        uint8                     _bReplayed{ SW_FALSE };
        uint8                     _bRevokeSessions{ SW_FALSE }; ///< 서버 안에서만 쓴다. 정지나 영구 정지가 걸렸다는 뜻이며 와이어에 싣지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 형식을 읽고 쓰며 등급 테이블을 제공합니다. 읽을 때 상한을 넘거나 모자라면 false 를 반환합니다. */
    struct SW_KIT_API AdminProtocol
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
        /** @brief 공통 오류 코드(`OnlineError`)를 결과로 바꿉니다. */
        static AdminResult fromErrorCode( uint16 errorCode );
        /** @brief 명령에 필요한 등급을 반환합니다. 제재는 종류에 따라 다릅니다. 모르는 메서드는 `Count` 이며 누구도 가질 수 없는 등급입니다. */
        static AdminRole getRequiredRole( uint16 method, ServiceSanctionKind sanctionKind );
    };
} // namespace sw
