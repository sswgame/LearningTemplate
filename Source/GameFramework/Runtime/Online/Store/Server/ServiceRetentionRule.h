/**
 * @file ServiceRetentionRule.h
 * @brief 오래된 서비스 레코드를 보존 기간이 지난 것부터 지우는 쓸기입니다.
 * @details 테이블마다 규칙(`ServiceRetentionRule`) 하나를 둡니다. 규칙은 지울 테이블, 보존 기간, 레코드에서 기록 시각을 읽는 함수를 가집니다.
 *          시각을 읽는 함수는 그 테이블의 형식을 아는 쪽(`ServiceAuditLog`, `Ledger`, `ServiceReportStore`)이 내놓습니다. 이 파일은 키 형식을 모릅니다.
 *          - 시각을 읽지 못하는 레코드(형식이 다르거나 깨졌다)는 지우지 않습니다. 모르는 것을 지우는 쪽이 아니라 남기는 쪽으로 틀립니다.
 *          - 키 순서는 시각 순서가 아니므로(감사 기록은 대상이 앞이다) 커서로 테이블을 처음부터 끝까지 돌며 훑습니다. 한 번에 훑는 수와 지우는 수에 상한이 있어
 *            저장소를 오래 잡지 않고, 커서는 규칙에 남아 다음 회차가 이어서 훑습니다. 끝에 닿으면 커서를 비워 처음부터 다시 돕니다.
 *          - 지우는 쓰기는 읽은 버전을 조건으로 합니다. 그사이 바뀐 레코드는 Conflict 로 건너뛰고 다음 회차에 다시 봅니다.
 *          - 지우는 레코드가 다른 레코드에 합을 남겨야 하면(원장 분개 → 자산별 이월) 규칙의 `_pfnStageErase` 가 같은 트랜잭션에 그 쓰기를 붙인다.
 *            지우기와 이월이 함께 커밋되거나 함께 실패하므로, 지웠는데 합이 빠지는 틈이 없다.
 *          멱등 기록(`ServiceIdempotency`)은 키에 시각이 없어 이 쓸기가 지우지 못합니다. 저장소 구현의 만료 정리가 맡습니다.
 *
 *          만료 색인(`_indexTable`)을 둔 규칙은 전수 훑기 대신 범위 읽기로 끝난다. 색인 키는 `<기준 시각 16 진>/<대상>`(`makeIndexKey`)이라 키 순서가 시각 순서이고,
 *          쓸기는 색인을 앞에서부터 기준 시각이 기간을 넘긴 줄까지만 읽는다(커서를 남기지 않는다 — 지운 줄은 사라지고 남은 줄이 맨 앞이다).
 *          - 대상이 키 하나면 그 레코드를 읽어, 시각 읽기가 기간이 지났다고 하면 레코드와 색인 줄을 함께 지운다. 레코드가 없으면 색인 줄만 지운다.
 *            레코드의 시각이 그사이 늦춰졌거나(다시 쓴 쪽이 새 색인 줄을 넣었다) 읽지 못하면 레코드는 남기고 낡은 색인 줄만 지운다.
 *            시각 읽기가 비었으면 색인의 시각을 믿는다 — 한 번 쓰고 바꾸지 않는 레코드에만 쓴다.
 *          - 대상이 `/` 로 끝나면 접두어 묶음이다(채팅 채널 하나). 그 접두어 아래를 앞에서부터 읽어 기간이 지난 레코드를 지우고, 지나지 않은 레코드를 만나거나
 *            끝에 닿으면 색인 줄도 지운다. 접두어 아래는 키 순서가 시각 순서여야 한다.
 *          키트는 자기 레코드를 쓰는 트랜잭션에 색인 줄을 함께 넣고, 규칙은 키트 조립이 내놓는다(`IOnlineServerKit::addRetentionRule`).
 *          Nakama 의 채널 메시지 TTL 색인, PlayFab 의 보존 기간 정책과 같은 자리다 — 둘 다 테이블 주인이 기간을 정하고 저장소가 시각 순으로 지운다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

namespace sw
{
    struct ServiceRetentionReport;

    /** @brief 레코드에서 기록 시각(UTC 밀리초)을 읽습니다. 읽지 못하면 false 이고 그 레코드는 지우지 않습니다. */
    using ServiceRetentionTimeFn = bool ( * )( const ServiceRecord& record, int64& outTimeMs );

    /**
     * @brief 지울 레코드 @p listErased 와 같은 트랜잭션에 넣을 쓰기를 @p inoutTransaction 에 붙입니다. Ok 가 아니면 쓸기가 그 결과로 멈추고 아무것도 지우지 않습니다.
     * @details 붙인 쓰기가 많아 트랜잭션 상한을 넘으면 쓸기가 지울 묶음을 반으로 나눠 다시 부른다. 레코드 하나와 그 쓰기는 상한 안이어야 한다.
     */
    using ServiceRetentionStageFn = ServiceStoreResult ( * )( IServiceStoreConnection& connection, const vector<const ServiceRecord*>& listErased, ServiceTransaction& inoutTransaction );

    /** @brief 테이블 하나의 보존 규칙입니다. */
    struct SW_GF_API ServiceRetentionRule
    {
        hashed_string           _table{};
        hashed_string           _indexTable{}; ///< 비우면 `_table` 을 커서로 훑는다. 값이 있으면 이 만료 색인을 시각 순으로 범위 읽기한다
        string                  _keyPrefix{};  ///< 비우면 테이블 전체. 값이 있으면 이 접두어의 레코드만 훑는다(색인 규칙은 쓰지 않는다)
        int64                   _retentionMs{ 0 };
        ServiceRetentionTimeFn  _pfnReadTime{ nullptr };   ///< 색인 규칙은 비워도 된다(색인의 시각을 믿는다). 커서 규칙은 필수다
        ServiceRetentionStageFn _pfnStageErase{ nullptr }; ///< 비우면 지우기만 한다. 값이 있으면 지우는 트랜잭션에 이 쓰기를 함께 넣는다
        string                  _cursor{};                 ///< 다음 회차가 이어서 훑을 위치. 쓸기가 채운다

        // 보존 기간 쓸기입니다. 저장소 스레드(`IServiceStoreJob::run` 안)에서 씁니다. */
        static constexpr int32 kPageSize = 64; ///< 한 트랜잭션의 쓰기 상한에 맞춘 한 쪽의 크기

        /**
         * @brief 규칙 하나를 한 회차 쓸어 보존 기간이 지난 레코드를 지웁니다.
         * @param maxScanCount 이번 회차에 훑는 레코드 상한
         * @param maxDeleteCount 이번 회차에 지우는 레코드 상한
         * @return 읽기나 쓰기가 Unavailable 이면 그 결과입니다. 그 밖에는 Ok 이며 Conflict 로 건너뛴 레코드는 지운 수에 넣지 않습니다.
         */
        [[nodiscard]] static ServiceStoreResult sweep( IServiceStoreConnection& connection, ServiceRetentionRule& inoutRule, int64 nowMs, int32 maxScanCount, int32 maxDeleteCount,
                                                       ServiceRetentionReport& outReport );

        /** @brief 규칙이 쓸 수 있는 모양인지입니다. 기간이 양수이고, 커서 규칙이면 시각 읽기가 있어야 합니다. */
        bool isValid() const;

        /** @brief 만료 색인 키 `<기준 시각 16 진>/<대상>` 을 만듭니다. 대상이 `/` 로 끝나면 접두어 묶음입니다. 음수 시각은 0 으로 둡니다. */
        static string makeIndexKey( int64 timeMs, string_view target );
        /** @brief 만료 색인 키를 기준 시각과 대상으로 나눕니다. 형식이 틀리면 false 입니다. */
        [[nodiscard]] static bool parseIndexKey( string_view indexKey, int64& outTimeMs, string_view& outTarget );

    private:
        [[nodiscard]] static ServiceStoreResult sweepIndex( IServiceStoreConnection& connection, const ServiceRetentionRule& rule, int64 cutoffMs, int32 maxScanCount,
                                                            int32 maxDeleteCount, ServiceRetentionReport& outReport );
        /** @brief 색인 줄 하나가 가리키는 대상을 지우는 트랜잭션을 짜 커밋합니다. 지운 레코드 수를 @p inoutReport 에 더하고, 묶음이 남았으면 @p outbEntryDone 이 false 입니다. */
        [[nodiscard]] static ServiceStoreResult expireIndexEntry( IServiceStoreConnection& connection, const ServiceRetentionRule& rule, const ServiceRecord& entry,
                                                                  string_view target, int64 cutoffMs, ServiceRetentionReport& inoutReport, bool& outbEntryDone );
    };
} // namespace sw

namespace sw
{
    /** @brief 쓸기 한 번의 결과입니다. */
    struct ServiceRetentionReport
    {
        int32 _scannedCount{ 0 };
        int32 _deletedCount{ 0 };
        uint8 _bWrapped{ SW_FALSE }; ///< 테이블 끝까지 훑어 커서를 비웠다
    };
} // namespace sw
