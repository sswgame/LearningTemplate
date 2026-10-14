/**
 * @file SQLiteDriver.h
 * @brief SQLite 드라이버입니다. 이 키트 폴더(`Driver/SQLite/`)만 sqlite3 를 압니다. 접속 문자열은 DB 파일 경로이며 작업 폴더 기준 상대 경로와 절대 경로를 모두 쓸 수 있습니다.
 * @details 연결마다 WAL, synchronous FULL, busy_timeout 5초를 설정한다. 준비된 문장은 SQL 문마다 캐시한다. 비밀은 쓰지 않으며 접근 제어는 파일 권한이 맡는다.
 */
#pragma once
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLDriver.h"

namespace sw
{
    /**
     * @class SQLiteDriver
     * @brief SQLite 드라이버입니다. 상태가 없으므로 모든 워커가 하나를 같이 씁니다.
     */
    class SW_KIT_API SQLiteDriver final : public ISQLDriver
    {
    public:
        static SQLiteDriver& getInstance();

        const utf8*                getName() const override { return "sqlite"; }
        unique_ptr<ISQLConnection> openConnection( string_view connection, string_view secret, string& outError ) override;
    };
} // namespace sw
