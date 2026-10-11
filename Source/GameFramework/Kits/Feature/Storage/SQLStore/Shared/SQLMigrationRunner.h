/**
 * @file SQLMigrationRunner.h
 * @brief 마이그레이션 적용기입니다. 번호가 붙은 SQL 을 차례로 적용하고 `sw_schema_migration` 에 기록합니다.
 * @details 스키마는 SQL 에만 있으므로 이 키트에 둔다. 메모리 구현과 캐시에는 스키마가 없다.
 *          파일 이름은 `NNNN_이름.sql`(공통) 또는 `NNNN_이름.<드라이버>.sql`(방언 분기)이다. 방언 분기 파일은 같은 번호의 공통 파일 대신 쓴다. 번호는 1부터 빈틈없이 매긴다.
 *          토큰 `{{blob}}`, `{{keytext}}`, `{{autoid}}` 를 방언에 맞게 치환한다.
 *          이미 적용된 파일의 체크섬이 지금 파일과 다르면 거절한다. 적용된 파일은 고치지 말고 새 번호를 쓴다.
 *          모두 한 트랜잭션으로 처리하므로 중간에 실패하면 아무것도 적용되지 않는다.
 *          서버 여러 대가 동시에 시작해도 방언의 테이블 락으로 하나만 적용한다.
 *          체크섬은 줄 끝을 LF 로 맞춘 원문으로 계산한다. 그래서 Windows 와 리눅스 체크아웃이 같은 DB 를 쓸 수 있다. Flyway 와 Rails 마이그레이션과 같은 방식이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct SQLDialect;

    class ISQLConnection;

    /** @brief 마이그레이션 하나입니다. 번호, 이름, 원문(토큰 치환 전, 줄 끝은 LF)을 가집니다. */
    struct SQLMigration
    {
        string _name{};
        string _sql{};
        int32  _version{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @struct SQLMigrationRunner
     * @brief 마이그레이션을 읽고 적용합니다.
     */
    struct SW_GF_API SQLMigrationRunner
    {
        /** @brief 디스크 폴더 @p folderPath 의 `.sql` 을 읽어 @p driverName 에 맞게 고릅니다. 번호가 겹치거나 빠졌거나 그 드라이버의 파일이 없으면 false 를 반환합니다. */
        [[nodiscard]] static bool loadMigrations( string_view folderPath, string_view driverName, vector<SQLMigration>& outListMigration, string& outError );
        /** @brief 아직 적용하지 않은 마이그레이션을 적용하고, 적용된 것은 체크섬을 확인합니다. 실패하면 아무것도 적용하지 않고 false 를 반환합니다. */
        [[nodiscard]] static bool apply( ISQLConnection& connection, const vector<SQLMigration>& listMigration, int64 nowMs, string& outError );
        static string             substituteTokens( string_view sql, const SQLDialect& dialect );
        /** @brief FNV-1a 64비트 해시를 16진 문자열로 반환합니다. 대소문자를 구분합니다. */
        static string computeChecksum( string_view sql );
    };
} // namespace sw
