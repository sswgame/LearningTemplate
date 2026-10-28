/**
 * @file ServiceRetention.h
 * @brief 오래된 서비스 레코드를 보존 기간이 지난 것부터 지우는 쓸기입니다.
 * @details 테이블마다 규칙(`ServiceRetentionRule`) 하나를 둡니다. 규칙은 지울 테이블, 보존 기간, 레코드에서 기록 시각을 읽는 함수를 가집니다.
 *          시각을 읽는 함수는 그 테이블의 형식을 아는 쪽(`ServiceAuditLog`, `Ledger`, `ServiceReportStore`)이 내놓습니다. 이 파일은 키 형식을 모릅니다.
 *          - 시각을 읽지 못하는 레코드(형식이 다르거나 깨졌다)는 지우지 않습니다. 모르는 것을 지우는 쪽이 아니라 남기는 쪽으로 틀립니다.
 *          - 키 순서는 시각 순서가 아니므로(감사 기록은 대상이 앞이다) 커서로 테이블을 처음부터 끝까지 돌며 훑습니다. 한 번에 훑는 수와 지우는 수에 상한이 있어
 *            저장소를 오래 잡지 않고, 커서는 규칙에 남아 다음 회차가 이어서 훑습니다. 끝에 닿으면 커서를 비워 처음부터 다시 돕니다.
 *          - 지우는 쓰기는 읽은 버전을 조건으로 합니다. 그사이 바뀐 레코드는 Conflict 로 건너뛰고 다음 회차에 다시 봅니다.
 *          멱등 기록(`ServiceIdempotency`)은 키에 시각이 없어 이 쓸기가 지우지 못합니다. 저장소 구현의 만료 정리가 맡습니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 레코드에서 기록 시각(UTC 밀리초)을 읽습니다. 읽지 못하면 false 이고 그 레코드는 지우지 않습니다. */
    using ServiceRetentionTimeFn = bool ( * )( const ServiceRecord& record, int64& outTimeMs );

    /** @brief 테이블 하나의 보존 규칙입니다. */
    struct ServiceRetentionRule
    {
        hashed_string          _table{};
        string                 _keyPrefix{}; ///< 비우면 테이블 전체. 값이 있으면 이 접두어의 레코드만 훑는다
        int64                  _retentionMs{ 0 };
        ServiceRetentionTimeFn _pfnReadTime{ nullptr };
        string                 _cursor{}; ///< 다음 회차가 이어서 훑을 위치. 쓸기가 채운다
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

namespace sw
{
    /** @brief 보존 기간 쓸기입니다. 저장소 스레드(`IServiceStoreWork::run` 안)에서 씁니다. */
    struct SW_GF_API ServiceRetention
    {
        static constexpr int32 kPageSize = 64; ///< 한 트랜잭션의 쓰기 상한에 맞춘 한 쪽의 크기

        /**
         * @brief 규칙 하나를 한 회차 쓸어 보존 기간이 지난 레코드를 지웁니다.
         * @param maxScanCount 이번 회차에 훑는 레코드 상한
         * @param maxDeleteCount 이번 회차에 지우는 레코드 상한
         * @return 읽기나 쓰기가 Unavailable 이면 그 결과입니다. 그 밖에는 Ok 이며 Conflict 로 건너뛴 레코드는 지운 수에 넣지 않습니다.
         */
        [[nodiscard]] static ServiceStoreResult sweep( IServiceStoreConnection& connection, ServiceRetentionRule& inoutRule, int64 nowMs, int32 maxScanCount, int32 maxDeleteCount,
                                                       ServiceRetentionReport& outReport );
    };
} // namespace sw
