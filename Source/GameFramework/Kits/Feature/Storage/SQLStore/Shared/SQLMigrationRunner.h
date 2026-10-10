/**
 * @file SQLMigrationRunner.h
 * @brief 마이그레이션 적용기 — 번호 붙은 SQL 을 차례로 적용하고 `sw_schema_migration` 에 적습니다. 스키마는 SQL 에만 있어 이 키트에 둡니다(메모리 · 캐시는 스키마가 없다).
 * @details 파일 이름 `NNNN_이름.sql`(공통) 또는 `NNNN_이름.<드라이버>.sql`(방언 갈래 — 같은 번호의 공통 파일 대신). 번호는 1 부터 빈틈없이.
 *          토큰 `{{blob}}` · `{{keytext}}` · `{{autoid}}` 를 방언으로 바꾼다. 적용된 것의 체크섬이 지금 파일과 다르면 거절한다(적용된 파일은 고치지 않고 새 번호를 쓴다).
 *          모두 한 트랜잭션 — 중간에 실패하면 아무것도 적용되지 않는다. 서버 여럿이 동시에 띄워져도 표 잠금(방언)으로 하나만 적용한다.
 *          체크섬은 줄 끝을 LF 로 맞춘 원문으로 낸다(Windows · 리눅스 체크아웃이 같은 DB 를 쓴다). Flyway · Rails 마이그레이션과 같은 모양이다.
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

    /** @brief 마이그레이션 하나입니다 — 번호 · 이름 · 원문(토큰 치환 전, 줄 끝 LF). */
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
        /** @brief 디스크 폴더 @p folderPath 의 `.sql` 을 읽어 @p driverName 에 맞게 고릅니다. 번호가 겹치거나 빠지거나 그 드라이버 파일이 없으면 false. */
        [[nodiscard]] static bool loadMigrations( string_view folderPath, string_view driverName, vector<SQLMigration>& outListMigration, string& outError );
        /** @brief 안 된 것을 적용하고 된 것은 체크섬을 봅니다. 실패하면 아무것도 적용하지 않고 false. */
        [[nodiscard]] static bool apply( ISQLConnection& connection, const vector<SQLMigration>& listMigration, int64 nowMs, string& outError );
        static string             substituteTokens( string_view sql, const SQLDialect& dialect );
        /** @brief FNV-1a 64 비트 16 진(대소문자 구분)입니다. */
        static string computeChecksum( string_view sql );
    };
} // namespace sw
