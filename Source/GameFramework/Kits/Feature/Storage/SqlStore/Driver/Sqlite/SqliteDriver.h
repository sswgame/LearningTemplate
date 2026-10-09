/**
 * @file SqliteDriver.h
 * @brief SQLite 드라이버 — 이 키트 폴더(`Driver/Sqlite/`)만 sqlite3 를 압니다. 접속 글은 DB 파일 경로입니다(작업 폴더 기준 상대 경로 · 절대 경로).
 * @details 연결마다 WAL · synchronous FULL · busy_timeout 5 초. 준비문은 SQL 글마다 캐시한다. 비밀은 쓰지 않는다(파일 권한이 접근 제어).
 */
#pragma once
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Storage/SqlStore/Sql/SqlDriver.h"

namespace sw
{
    /**
     * @class SqliteDriver
     * @brief SQLite 드라이버입니다. 상태가 없어 하나를 모든 워커가 같이 씁니다.
     */
    class SW_GF_API SqliteDriver final : public ISqlDriver
    {
    public:
        static SqliteDriver& getInstance();

        const utf8*                getName() const override { return "sqlite"; }
        unique_ptr<ISqlConnection> openConnection( string_view connection, string_view secret, string& outError ) override;
    };
} // namespace sw
