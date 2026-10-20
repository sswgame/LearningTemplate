/**
 * @file ServiceAuditLog.h
 * @brief 지울 수 없는 감사 기록입니다. 누가, 언제, 무엇을, 누구에게 했는지와 전후 값을 남깁니다.
 * @details 기록은 **효과와 같은 트랜잭션에** "없어야 한다" 조건으로 붙입니다. 그래서 효과만 있고 기록이 없는 일이 생기지 않습니다. 지우는 API 는 없습니다.
 *          - 키는 `<subject>/<시각 16 진 16>/<고유 16 진 32>` 다. subject 는 `acct.<16 진>`, `trade.<16 진>`, `gm.<16 진>` 처럼 키 규칙 문자다.
 *            고유 값은 요청의 멱등 키(있으면)나 난수다. 같은 효과의 재시도는 같은 키라 줄이 둘 생기지 않는다(두 번째는 Conflict).
 *          - 상한을 넘는 줄(전후 2 KiB, 메모 1 KiB)은 자르지 않는다. `stageEntry` 가 트랜잭션을 Invalid 로 만들어 효과까지 커밋되지 않게 한다.
 *          - 보존 기간 정리는 운영(DB 쪽 작업)이 맡고, 게임 코드에는 지우는 경로가 없다. 원장 이동은 분개 자체가 감사 기록이라 따로 적지 않는다.
 *          - 값은 BitStream 으로 버전 1, 문자열 여섯 개(actor, action, subject, before, after, memo), 시각을 담는다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @brief 감사 줄 하나입니다.
     * @details 관리자 감사 목록 응답에 고정 배치형으로 실립니다. 앞의 셋은 관리자 프로토콜의 문구 상한(256)이고, 뒤의 셋은 아래 상수와 같은 값입니다.
     *          저장소 레코드는 이와 따로 `ServiceAuditLog` 의 버전 있는 형식으로 씁니다.
     */
    REFLECT()
    struct ServiceAuditEntry
    {
        REFLECT_BODY();
        static constexpr int32 kMaxStateSize = 2048; ///< `_before` · `_after`
        static constexpr int32 kMaxMemoSize  = 1024;

        PROPERTY( MaxCount = 256 )
        string _actor{}; ///< `acct.<16 진>`, `gm.<16 진>`, `system`
        PROPERTY( MaxCount = 256 )
        string _action{}; ///< 동작 코드 `[0-9a-z_.]`. 예: "trade.settle", "admin.sanction", "account.link"
        PROPERTY( MaxCount = 256 )
        string _subject{}; ///< 대상. 키의 앞부분이며 키 규칙을 따른다
        PROPERTY( MaxCount = 2048 )
        string _before{}; ///< 전 상태(짧은 JSON 또는 요약)
        PROPERTY( MaxCount = 2048 )
        string _after{};
        PROPERTY( MaxCount = 1024 )
        string _memo{};
        PROPERTY()
        int64 _timeMs{ 0 }; ///< UTC 벽시계 밀리초(0 이상)
    };
} // namespace sw

namespace sw
{
    /** @brief 감사 줄을 쓰고 읽습니다. 쓰기는 트랜잭션에 넣기만 하고, 읽기는 저장소 스레드(`IServiceStoreWork::run` 안)에서 합니다. */
    struct SW_GF_API ServiceAuditLog
    {
        static const hashed_string& getTable(); ///< "service_audit"
        static string               makeKey( const ServiceAuditEntry& entry, uint64 uniqueHigh, uint64 uniqueLow );
        /** @brief 상한, 키 규칙, 동작 코드 문자를 지키는지입니다. */
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
