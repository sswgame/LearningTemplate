/**
 * @file SqlServiceStore.h
 * @brief `IServiceStore` 의 SQL 구현 — `sw_record`( tbl, rkey, bytes, version ) 표 하나, 커밋 판은 방언의 시퀀스, 조건부 쓰기는 영향 받은 행 수로 판정합니다.
 * @details 커밋 = 쓰기 트랜잭션(SQLite `BEGIN IMMEDIATE` · PostgreSQL `SERIALIZABLE`) 안에서 판 하나를 받고 쓰기마다:
 *          - put 조건 없음: `INSERT … ON CONFLICT ( tbl, rkey ) DO UPDATE SET bytes = excluded.bytes, version = excluded.version`
 *          - put 없어야 함: `INSERT … ON CONFLICT DO NOTHING` — 바뀐 행 0 이면 Conflict(멱등 기록의 고유 제약이 이것이다)
 *          - put 같아야 함: `UPDATE … WHERE tbl = ? AND rkey = ? AND version = ?` — 0 이면 Conflict
 *          - erase: `DELETE … [AND version = ?]` — 조건이 있고 0 이면 Conflict
 *          - require · erase 없어야 함: `SELECT version …` 비교(PostgreSQL 은 SERIALIZABLE 이 읽은 행의 동시 변경을 직렬화 실패로 막는다)
 *          하나라도 어긋나면 ROLLBACK. 직렬화 실패는 Conflict(번호 −1), 연결 끊김 · COMMIT 실패는 Unavailable(적용됐는지 모른다).
 *          메모리 구현과 같은 계약 시험(`ServiceStoreContract.h`)을 같은 결과로 통과한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/Base/Online/Store/ServiceStore.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Storage/SqlStore/Sql/SqlConnectionPool.h"

namespace sw
{
    /**
     * @class SqlServiceStore
     * @brief SQL 서비스 저장소의 앞입니다 — 일은 풀 워커에서 돌고, 완료는 `pollCompletions` 를 부른 스레드에서 돈다.
     */
    class SW_GF_API SqlServiceStore final : public IServiceStore
    {
    public:
        /** @brief 서비스 저장소 마이그레이션의 리소스 폴더입니다. */
        static constexpr const utf8* kMigrationFolder = "common/sql/servicestore";

        SqlServiceStore();
        ~SqlServiceStore() override;

        /**
         * @brief 드라이버를 찾고 · 마이그레이션을 적용하고 · 풀을 띄웁니다. 실패하면 false 와 까닭 — 기동 오류다(폴백 없음).
         * @param migrationFolder 리소스 경로(`common/sql/servicestore`) 또는 디스크 폴더
         * @param nowMs 마이그레이션 기록에 남길 벽시계 밀리초
         */
        [[nodiscard]] bool initialize( string_view driverName, const SqlConnectionPoolSettings& settings, string_view migrationFolder, int64 nowMs, string& outError );

        void  submit( unique_ptr<IServiceStoreWork> work ) override;
        int32 pollCompletions() override { return _pool.pollCompletions(); }
        int32 getPendingCount() const override { return _pool.getPendingCount(); }
        void  shutdown() override { _pool.shutdown(); }

    private:
        SqlConnectionPool _pool;
    };
} // namespace sw
