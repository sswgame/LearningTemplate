/**
 * @file ServiceAuditLog.h
 * @brief 지울 수 없는 기록 — 누가 · 언제 · 무엇을 · 누구에게 · 전후. **효과와 같은 트랜잭션에** "없어야 한다" 조건으로 붙입니다(효과만 있고 기록이 없는 일이 없다). 지우는 API 는 없습니다.
 * @details - 키 `<subject>/<시각 16 진 16>/<고유 16 진 32>` — subject 는 `acct.<16 진>` · `trade.<16 진>` · `gm.<16 진>` 처럼 키 규칙 문자다.
 *            고유 값은 요청의 멱등 키(있으면) 또는 난수 — 같은 효과의 재시도는 같은 키라 줄이 둘 생기지 않는다(두 번째는 Conflict).
 *          - 상한을 넘는 줄(전후 2 KiB · 메모 1 KiB)은 자르지 않는다 — `stageEntry` 가 트랜잭션을 Invalid 로 만들어 효과까지 커밋되지 않는다.
 *          - 보존 기간 정리는 운영(DB 쪽 작업)의 몫이고 게임 코드에는 지우는 길이 없다. 원장 이동은 분개 자체가 감사 기록이라 따로 적지 않는다.
 *          - 값은 BitStream: 판 1 + 문자열 여섯(actor · action · subject · before · after · memo) + 시각.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 감사 줄 하나입니다. */
    struct ServiceAuditEntry
    {
        static constexpr int32 kMaxStateSize = 2048; ///< `_before` · `_after`
        static constexpr int32 kMaxMemoSize  = 1024;

        string _actor{};   ///< `acct.<16 진>` · `gm.<16 진>` · `system`
        string _action{};  ///< 동작 코드 `[0-9a-z_.]` — "trade.settle" · "admin.sanction" · "account.link"
        string _subject{}; ///< 대상(키 앞부분 — 키 규칙)
        string _before{};  ///< 전 상태(짧은 JSON 또는 요약)
        string _after{};
        string _memo{};
        int64  _timeMs{ 0 }; ///< UTC 벽시계 밀리초(0 이상)
    };
} // namespace sw

namespace sw
{
    /** @brief 감사 줄 쓰기 · 읽기입니다(쓰기는 트랜잭션에 넣기만, 읽기는 저장소 스레드 — `IServiceStoreWork::run` 안에서). */
    struct SW_GF_API ServiceAuditLog
    {
        static const hashed_string& getTable(); ///< "service_audit"
        static string               makeKey( const ServiceAuditEntry& entry, uint64 uniqueHigh, uint64 uniqueLow );
        /** @brief 상한 · 키 규칙 · 동작 코드 문자를 지키는가입니다. */
        static bool isValidEntry( const ServiceAuditEntry& entry );
        /** @brief @p inoutTransaction 에 줄을 넣습니다("없어야 한다" 조건). 줄이 틀리면 트랜잭션이 Invalid 가 되게 넣는다(효과도 커밋되지 않는다). */
        static void stageEntry( ServiceTransaction& inoutTransaction, const ServiceAuditEntry& entry, uint64 uniqueHigh, uint64 uniqueLow );
        /**
         * @brief @p subjectPrefix 로 시작하는 줄을 최근 것부터 읽습니다.
         * @param cursor 앞 호출의 @p outNextCursor(처음은 빈 글)
         * @param outNextCursor 더 있을 수 있으면 다음 호출의 커서, 끝이면 빈 글
         */
        [[nodiscard]] static ServiceStoreResult listEntries( IServiceStoreConnection& connection, string_view subjectPrefix, string_view cursor, int32 maxCount,
                                                             vector<ServiceAuditEntry>& outListEntry, string& outNextCursor );
    };
} // namespace sw
