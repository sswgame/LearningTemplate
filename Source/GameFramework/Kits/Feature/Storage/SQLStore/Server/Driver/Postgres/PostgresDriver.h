/**
 * @file PostgresDriver.h
 * @brief PostgreSQL 드라이버(libpq) — 이 폴더(`Driver/Postgres/`)만 libpq 를 압니다. 서버 전용(GF_Server_SQLStore).
 * @details 접속 글은 libpq 의 키워드 글(`host=127.0.0.1 port=5432 dbname=game user=game`) 또는 URI(`postgresql://user@host:5432/db?sslmode=require`)이고,
 *          비밀번호는 접속 글이 아니라 `secret` 으로 따로 받는다(서버 설정의 환경 변수). 자리표시자 `?` 는 글마다 한 번 `$1..$n` 으로 바꿔 준비문으로 캐시한다.
 *          결과는 이진(int8 · int4 · int2 · bytea · text), bytea 매개변수는 이진으로 보낸다. SQLSTATE 로 제약(23505) · 직렬화 실패(40001 · 40P01) · 끊김(08xxx)을 가른다.
 *          연결마다 `SET TIME ZONE 'UTC'`, 서버 알림(NOTICE)은 버린다.
 */
#pragma once
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/SQL/SQLDriver.h"

namespace sw
{
    /**
     * @class PostgresDriver
     * @brief PostgreSQL 드라이버입니다. 상태가 없어 하나를 모든 워커가 같이 씁니다.
     */
    class SW_GF_API PostgresDriver final : public ISQLDriver
    {
    public:
        static PostgresDriver& getInstance();

        const utf8*                getName() const override { return "postgres"; }
        unique_ptr<ISQLConnection> openConnection( string_view connection, string_view secret, string& outError ) override;
    };
} // namespace sw
