/**
 * @file PostgresDriver.h
 * @brief PostgreSQL 드라이버(libpq)입니다. 이 폴더(`Driver/Postgres/`)만 libpq 를 알며 서버 전용입니다(GF_Server_SQLStore).
 * @details 접속 문자열은 libpq 의 키워드 형식(`host=127.0.0.1 port=5432 dbname=game user=game`)이나 URI(`postgresql://user@host:5432/db?sslmode=require`)를 쓴다.
 *          비밀번호는 접속 문자열이 아니라 `secret` 으로 따로 받는다. 서버 설정의 환경 변수에서 읽은 값이다.
 *          자리표시자 `?` 는 SQL 문마다 한 번 `$1..$n` 으로 바꿔 준비된 문장으로 캐시한다.
 *          결과는 이진 형식(int8, int4, int2, bytea, text)으로 받고, bytea 매개변수도 이진으로 보낸다.
 *          SQLSTATE 로 제약 위반(23505), 직렬화 실패(40001, 40P01), 연결 끊김(08xxx)을 구분한다.
 *          연결마다 `SET TIME ZONE 'UTC'` 를 실행하고 서버 알림(NOTICE)은 버린다.
 */
#pragma once
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriver.h"

namespace sw
{
    /**
     * @class PostgresDriver
     * @brief PostgreSQL 드라이버입니다. 상태가 없어 하나를 모든 워커가 같이 씁니다.
     */
    class SW_KIT_SERVER_API PostgresDriver final : public ISQLDriver
    {
    public:
        static PostgresDriver& getInstance();

        const utf8*                getName() const override { return "postgres"; }
        unique_ptr<ISQLConnection> openConnection( string_view connection, string_view secret, string& outError ) override;
    };
} // namespace sw
