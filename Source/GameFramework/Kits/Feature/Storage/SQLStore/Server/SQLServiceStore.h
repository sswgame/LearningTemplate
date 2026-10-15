/**
 * @file SQLServiceStore.h
 * @brief `IServiceStore` 의 SQL 구현입니다. `sw_record`( tbl, rkey, bytes, version ) 테이블 하나를 쓰며, 커밋 버전은 방언의 시퀀스로 받고, 조건부 쓰기는 영향받은 행 수로 판정합니다.
 * @details 커밋은 쓰기 트랜잭션(SQLite 는 `BEGIN IMMEDIATE`, PostgreSQL 은 `SERIALIZABLE`) 안에서 버전 하나를 받고, 쓰기마다 다음을 실행한다.
 *          - put 조건 없음: `INSERT … ON CONFLICT ( tbl, rkey ) DO UPDATE SET bytes = excluded.bytes, version = excluded.version`
 *          - put 없어야 함: `INSERT … ON CONFLICT DO NOTHING`. 바뀐 행이 0이면 Conflict 이다. 멱등 기록의 고유 제약이 이것이다.
 *          - put 같아야 함: `UPDATE … WHERE tbl = ? AND rkey = ? AND version = ?`. 바뀐 행이 0이면 Conflict 이다.
 *          - erase: `DELETE … [AND version = ?]`. 조건이 있고 바뀐 행이 0이면 Conflict 이다.
 *          - require, erase 없어야 함: `SELECT version …` 으로 비교한다. PostgreSQL 은 SERIALIZABLE 이 읽은 행의 동시 변경을 직렬화 실패로 막는다.
 *
 *          하나라도 어긋나면 ROLLBACK 한다. 직렬화 실패는 Conflict(버전 -1)이고, 연결 끊김과 COMMIT 실패는 Unavailable 이다. Unavailable 은 적용됐는지 알 수 없다.
 *          메모리 구현과 같은 계약 테스트(`ServiceStoreContract.h`)를 같은 결과로 통과한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLConnectionPool.h"

namespace sw
{
    /**
     * @class SQLServiceStore
     * @brief SQL 서비스 저장소입니다. 작업은 풀 워커에서 실행하고, 완료 콜백은 `pollCompletions` 를 부른 스레드에서 실행합니다.
     */
    class SW_KIT_SERVER_API SQLServiceStore final : public IServiceStore
    {
    public:
        /** @brief 서비스 저장소 마이그레이션의 리소스 폴더입니다. */
        static constexpr const utf8* kMigrationFolder = "common/sql/servicestore";

        SQLServiceStore();
        ~SQLServiceStore() override;

        /**
         * @brief 드라이버를 찾고, 마이그레이션을 적용하고, 풀을 시작합니다. 실패하면 false 와 실패 이유를 반환하며 기동 오류입니다. 폴백은 없습니다.
         * @param migrationFolder 리소스 경로(`common/sql/servicestore`)나 디스크 폴더입니다.
         * @param nowMs 마이그레이션 기록에 남길 벽시계 시각(밀리초)입니다.
         */
        [[nodiscard]] bool initialize( string_view driverName, const SQLConnectionPoolConfig& settings, string_view migrationFolder, int64 nowMs, string& outError );

        void  submit( unique_ptr<IServiceStoreWork> work ) override;
        int32 pollCompletions() override { return _pool.pollCompletions(); }
        int32 getPendingCount() const override { return _pool.getPendingCount(); }
        void  shutdown() override { _pool.shutdown(); }

    private:
        SQLConnectionPool _pool;
    };
} // namespace sw
