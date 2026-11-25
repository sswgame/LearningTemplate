/**
 * @file ServiceAuditLog.h
 * @brief 지울 수 없는 감사 기록입니다. 누가, 언제, 무엇을, 누구에게 했는지와 전후 값을 남깁니다.
 * @details 기록은 효과와 같은 트랜잭션에 "없어야 한다" 조건으로 붙입니다. 그래서 효과만 있고 기록이 없는 일이 생기지 않습니다. 지우는 API 는 없습니다.
 *          - 키는 `<subject>/<시각 16 진 16>/<고유 16 진 32>` 다. subject 는 `acct.<16 진>`, `trade.<16 진>`, `gm.<16 진>` 처럼 키 규칙 문자다.
 *            고유 값은 요청의 멱등 키(있으면)나 난수다. 같은 효과의 재시도는 같은 키라 줄이 둘 생기지 않는다(두 번째는 Conflict).
 *          - 상한을 넘는 줄(전후 2 KiB, 메모 1 KiB)은 자르지 않는다. `stageEntry` 가 트랜잭션을 Invalid 로 만들어 효과까지 커밋되지 않게 한다.
 *          - 보존 기간 정리는 서버 런타임의 예약 작업(`ServiceTTLMonitor`)만 한다. 줄을 넣는 트랜잭션이 TTL 색인 `service_audit_expiry`(`<시각>/<감사 키>`)를 함께 넣어
 *            쓸기가 표 전체를 훑지 않고 기간이 지난 줄만 범위로 읽는다(`makeTTLRule`). 색인 키가 감사 키보다 17 바이트 길어 대상(subject)은 그만큼 짧아야 한다. 게임 코드에는 지우는 경로가 없다. 원장 이동은 분개 자체가 감사 기록이라 따로 적지 않는다.
 *          - 값은 BitStream 으로 버전 1, 문자열 여섯 개(actor, action, subject, before, after, memo), 시각을 담는다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Time/TimePoint.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Audit/ServiceAuditEntry.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceTTLRule.h"

namespace sw
{
    /** @brief 감사 줄을 쓰고 읽습니다. 쓰기는 트랜잭션에 넣기만 하고, 읽기는 저장소 스레드(`ServiceStoreJob::run` 안)에서 합니다. */
    struct SW_GF_API ServiceAuditLog
    {
        static const hashed_string& getTable(); ///< "service_audit"
        /** @brief 행위자가 시스템(예약 작업 · 주기 리로드)일 때의 이름입니다. */
        static constexpr const utf8* kSystemActor = "system";
        /** @brief 계정 행위자 이름 `acct.<16 진>` 입니다. */
        static string makeAccountActor( uint64 accountID );
        /** @brief 운영자 행위자 이름 `gm.<16 진>` 입니다. @p operatorID 가 0(없는 계정)이면 시스템이 한 일이라 `kSystemActor` 입니다. */
        static string makeOperatorActor( uint64 operatorID );
        static string makeKey( const ServiceAuditEntry& entry, uint64 uniqueHigh, uint64 uniqueLow );
        /** @brief 줄의 키 `<대상>/<시각 16 진 16>/<고유 16 진 32>` 에서 기록 시각을 읽습니다(보존 기간 쓸기 `ServiceTTLRule` 용). 형식이 틀리면 false 입니다. */
        [[nodiscard]] static bool   readTimeMs( const ServiceRecord& record, int64& outTimeMs );
        static const hashed_string& getExpiryTable(); ///< "service_audit_expiry" — TTL 색인
        /** @brief 감사 표의 TTL 규칙입니다(TTL 색인 + 색인 없는 옛 줄의 백필). 기간은 운영 설정 `_auditRetention` 이 정한다. */
        static ServiceTTLRule makeTTLRule( int64 expireAfterMs );
        /** @brief 상한, 키 규칙, 동작 코드 문자를 지키는지입니다. */
        static bool isValidEntry( const ServiceAuditEntry& entry );
        /** @brief @p inoutTransaction 에 줄을 넣습니다("없어야 한다" 조건). 줄이 틀리면 트랜잭션이 Invalid 가 되게 넣는다(효과도 커밋되지 않는다). */
        static void stageEntry( ServiceTransaction& inoutTransaction, const ServiceAuditEntry& entry, uint64 uniqueHigh, uint64 uniqueLow );
        /**
         * @brief 권한이 없어 거절한 운영 요청을 감사 줄 하나로 따로 커밋합니다(효과는 없다). 동작 코드는 `<요청 동작>.denied` 이다.
         * @details 고유 값은 새 난수라 같은 요청이 나중에 허락돼도 효과 줄의 키와 겹치지 않는다. 커밋 실패는 경고 로그만 남기고 거절은 그대로다.
         */
        static void commitRejection( IServiceStoreConnection& connection, const ServiceAuditEntry& requestedEntry );
        /**
         * @brief @p subjectPrefix 로 시작하는 줄을 최근 것부터 읽습니다.
         * @param cursor 앞 호출의 @p outNextCursor(처음은 빈 글)
         * @param outNextCursor 더 있을 수 있으면 다음 호출의 커서, 끝이면 빈 글
         */
        [[nodiscard]] static ServiceStoreResult listEntries( IServiceStoreConnection& connection, string_view subjectPrefix, string_view cursor, int32 maxCount,
                                                             vector<ServiceAuditEntry>& outListEntry, string& outNextCursor );
    };

    /**
     * @struct ServiceAuditedWrite
     * @brief 운영 표(라이브 이벤트 · 점검 · 공지)의 레코드 하나를 쓰거나 지우고 감사 줄을 같은 트랜잭션에 넣는 일입니다.
     * @details 라이브 운영과 서버 디렉터리가 같은 작업 클래스(읽고 → 버전 조건으로 쓰기/지우기 → 감사 줄)를 따로 적었다. 저장소 스레드에서 `commit` 하나로 돈다.
     */
    /**
     * @class IServiceWriteGuard
     * @brief 운영 쓰기를 허락할지 같은 저장 일 안에서 봅니다(운영자 등급 등). 읽은 레코드는 저장소 입구의 읽기 집합이 커밋 조건으로 건다.
     * @details 등급 확인과 쓰기가 다른 저장 일이면 그 사이에 강등돼도 쓰기가 들어간다(감사 "다음 할 일" 5 번) — 확인을 쓰는 일 안으로 옮기는 자리다.
     */
    class IServiceWriteGuard
    {
    public:
        IServiceWriteGuard()          = default;
        virtual ~IServiceWriteGuard() = default;

        IServiceWriteGuard( const IServiceWriteGuard& )            = delete;
        IServiceWriteGuard& operator=( const IServiceWriteGuard& ) = delete;

        /** @brief 허락하면 Ok, 권한이 없으면 NotFound(허락할 근거가 없다), 읽지 못하면 Unavailable 입니다. */
        [[nodiscard]] virtual ServiceStoreResult validateAllowed( IServiceStoreConnection& connection ) const = 0;
    };

    /**
     * @brief 운영 쓰기를 거는 사람과 그 허락 확인(권한 증명)입니다. 운영 요청 처리기(라이브 운영 · 서버 디렉터리)는 이것을 받아야만 부를 수 있다.
     * @details 기본 생성자가 없어 가드를 적지 않고는 만들 수 없다. 가드가 null 이면 `ServiceAuditedWrite::commit` 이 거절하고 감사 줄을 남긴다.
     */
    struct ServiceOperator
    {
        uint64                               _actorID;
        shared_ptr<const IServiceWriteGuard> _pGuard;

        ServiceOperator( uint64 actorID, shared_ptr<const IServiceWriteGuard> pGuard )
            : _actorID{ actorID }
            , _pGuard{ std::move( pGuard ) }
        {
        }
    };

    /** @brief 운영 쓰기 하나의 결과입니다. */
    enum class ServiceAuditedWriteResult : uint8
    {
        Ok = 0,
        NotFound,   ///< 지울 레코드가 없다
        Forbidden,  ///< 가드가 허락하지 않았다(그사이 강등 등) — 가드가 없는 쓰기도 여기다. 거절 감사 줄(`<동작>.denied`)을 남긴다
        Unavailable ///< 닿지 못했거나 그사이 바뀌었다(다시 하면 된다)
    };

    struct SW_GF_API ServiceAuditedWrite
    {
        shared_ptr<const IServiceWriteGuard> _pGuard{}; ///< 필수. 같은 일 안에서 허락을 본다(`ServiceOperator::_pGuard`)
        ServiceAuditEntry                    _auditEntry{};
        vector<uint8>                        _bytes{}; ///< 비어 있으면 지우기이다
        string                               _key{};
        hashed_string                        _table{};
        NetIdempotencyKey                    _auditUnique{ NetIdempotencyKey::makeRandom() };

        /**
         * @brief 운영자가 건 쓰기의 감사 칸을 채웁니다 — 가드(`_pGuard`)와 감사 줄의 행위자 · 동작 · 대상 · 메모 · 시각입니다.
         * @details 라이브 운영 · 서버 디렉터리의 운영 요청마다 같은 여섯 줄을 적었다. 전후 상태(`_auditEntry._before` · `_after`)는 부르는 쪽이 이어서 적는다.
         */
        void setOperatorAudit( const ServiceOperator& actor, string_view action, string_view subject, UnixMilliseconds nowMs, string_view memo = {} );
        /**
         * @brief 가드로 허락을 본 뒤 읽은 버전을 조건으로 쓰거나 지우고 감사 줄을 넣어 커밋합니다. 가드가 읽은 레코드(등급)도 같은 커밋의 조건이다.
         * @return Ok · NotFound · Forbidden · Unavailable(닿지 못했거나 그사이 바뀌었다 — Conflict 도 "다시 하라" 는 뜻이라 여기로 모은다)
         */
        [[nodiscard]] ServiceAuditedWriteResult commit( IServiceStoreConnection& connection ) const;
    };
} // namespace sw
