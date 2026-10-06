/**
 * @file ServiceIdempotency.h
 * @brief 멱등 요청 기록 — (범위, 멱등 키 128 비트) → 처음 낸 응답 바이트. 효과와 **같은 트랜잭션**에 넣어, 응답을 잃고 다시 온 요청이 효과를 두 번 내지 않게 합니다.
 * @details - 기록은 `kAbsentVersion` 조건(SQL 에서는 기본 키 고유 제약)으로 넣는다. 같은 요청이 두 서버에서 동시에 커밋되어도 하나만 이긴다
 *            (다른 쪽은 `Conflict` → 기록을 읽어 그 응답을 돌려준다).
 *          - 범위는 요청을 낸 주체(계정 id 16 진수)다. 키는 클라이언트가 재시도마다 같은 값을 쓰는 멱등 키(상위 · 하위 64 비트)다.
 *            전송 층의 멱등 기억은 메모리라 서버 재시작을 넘지 못한다 — 이 기록이 그것을 잇는다.
 *          - 오래된 기록 지우기는 구현의 몫(SQL 구현은 만료 표시 · 정리 일). 메모리 구현은 지우지 않는다.
 *          Stripe 의 Idempotency-Key · AWS 의 ClientToken 과 같은 모양이다.
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
    /** @brief 멱등 기록 도우미입니다(저장소 스레드 — `IServiceStoreWork::run` 안에서). */
    struct SW_GF_API ServiceIdempotency
    {
        static const hashed_string& getTable();
        static string               makeKey( string_view scope, uint64 keyHigh, uint64 keyLow );
        /** @brief 이미 처리한 요청이면 Ok 와 그때의 응답, 처음이면 NotFound 입니다. */
        [[nodiscard]] static ServiceStoreResult findReply( IServiceStoreConnection& connection, string_view scope, uint64 keyHigh, uint64 keyLow, vector<uint8>& outReplyBytes );
        /** @brief @p transaction 에 기록을 넣습니다("없어야 한다" 조건). */
        static void addReply( ServiceTransaction& transaction, string_view scope, uint64 keyHigh, uint64 keyLow, vector<uint8> replyBytes );
    };
} // namespace sw
