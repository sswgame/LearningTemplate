/**
 * @file PostgresDriver.cpp
 * @brief PostgreSQL 드라이버(libpq)입니다. 이 폴더(`Driver/Postgres/`)만 libpq 를 알며 서버 전용입니다(GF_Server_SQLStore).
 * @details 문장 · 값 · SQLSTATE 변환은 libpq 없이 테스트하도록 `PostgresCodec` 에 있다.
 *          접속 문자열은 libpq 의 키워드 형식(`host=127.0.0.1 port=5432 dbname=game user=game`)이나 URI(`postgresql://user@host:5432/db?sslmode=require`)를 쓴다.
 *          비밀번호는 접속 문자열이 아니라 `secret` 으로 따로 받는다. 서버 설정의 환경 변수에서 읽은 값이다.
 *          자리표시자 `?` 는 SQL 문마다 한 번 `$1..$n` 으로 바꿔 준비된 문장으로 캐시한다.
 *          결과는 이진 형식(int8, int4, int2, bytea, text)으로 받고, bytea 매개변수도 이진으로 보낸다.
 *          SQLSTATE 로 제약 위반(23505), 직렬화 실패(40001, 40P01), 연결 끊김(08xxx)을 구분한다.
 *          연결마다 `SET TIME ZONE 'UTC'` 를 실행하고 서버 알림(NOTICE)은 버린다.
 *          드라이버는 레지스트리 등록(`SQLDriverRegistration`)으로만 닿고 이름으로 부르는 곳이 없어 헤더를 두지 않는다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/StringBuilder.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriver.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriverRegistry.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Server/Driver/Postgres/PostgresCodec.h"

#include <cstdlib>
#include <libpq-fe.h>

namespace sw
{
    namespace
    {
        struct PostgresDriverInternal
        {
            static const SQLDialect& getDialect()
            {
                static const SQLDialect s_dialect{ "postgres",
                                                   "BYTEA",
                                                   "TEXT COLLATE \"C\"",
                                                   "BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY",
                                                   "BEGIN ISOLATION LEVEL SERIALIZABLE",
                                                   "SELECT nextval( 'sw_commit_version' )",
                                                   "LOCK TABLE sw_schema_migration IN EXCLUSIVE MODE" };
                return s_dialect;
            }

            static void ignoreNotice( void* pArgument, const utf8* pMessage )
            {
                (void)pArgument;
                (void)pMessage;
            }

            static SQLResult toSQLResult( const PGresult* pResult, const PGconn* pConnection )
            {
                const ExecStatusType status           = PQresultStatus( pResult );
                const bool           bStatusSucceeded = status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK || status == PGRES_EMPTY_QUERY;
                if ( bStatusSucceeded )
                    return SQLResult::Ok;
                if ( PQstatus( pConnection ) == CONNECTION_BAD )
                    return SQLResult::ConnectionLost;
                const utf8* pState = PQresultErrorField( pResult, PG_DIAG_SQLSTATE );
                return PostgresCodec::classifySQLState( pState != nullptr ? string_view( pState ) : string_view{} );
            }

            /** @brief 결과 하나를 꺼내 반환하고 지웁니다. */
            class ScopedResult
            {
            public:
                explicit ScopedResult( PGresult* pResult )
                    : _pResult{ pResult }
                {
                }

                ~ScopedResult() { PQclear( _pResult ); }

                ScopedResult( const ScopedResult& )            = delete;
                ScopedResult& operator=( const ScopedResult& ) = delete;

                PGresult* get() const { return _pResult; }

            private:
                PGresult* _pResult;
            };

            /** @brief 연결 하나입니다. 연 워커 스레드 하나만 씁니다. */
            class PostgresConnection final : public ISQLConnection
            {
            public:
                explicit PostgresConnection( PGconn* pConnection )
                    : _mapStatementName{}
                    , _lastError{}
                    , _pConnection{ pConnection }
                    , _nextStatementIndex{ 0 }
                    , _bAlive{ SW_TRUE }
                {
                }

                ~PostgresConnection() override { PQfinish( _pConnection ); }

                SQLResult execute( string_view sql, const SQLValue* pParam, int32 paramCount, SQLRowSet* pOutRowSet ) override
                {
                    _lastError.clear();
                    if ( pOutRowSet != nullptr )
                        pOutRowSet->clear();
                    SQLResult                prepareResult = SQLResult::Ok;
                    const PreparedStatement* pStatement    = findOrPrepare( sql, prepareResult );
                    if ( pStatement == nullptr )
                        return prepareResult;
                    PostgresParameters parameters;
                    if ( PostgresCodec::encodeParameters( pParam, paramCount, pStatement->_paramCount, parameters, _lastError ) == false )
                        return SQLResult::Error;
                    ScopedResult    result{ PQexecPrepared( _pConnection, pStatement->_name.c_str(), pStatement->_paramCount, parameters._listValue.data(), parameters._listLength.data(),
                                                            parameters._listFormat.data(), 1 ) };
                    const SQLResult sqlResult = toSQLResult( result.get(), _pConnection );
                    if ( sqlResult != SQLResult::Ok )
                        return fail( sqlResult, result.get() );
                    if ( pOutRowSet != nullptr )
                    {
                        if ( readRows( result.get(), *pOutRowSet, _lastError ) == false )
                        {
                            pOutRowSet->clear();
                            return SQLResult::Error;
                        }
                        // PQcmdTuples 는 행 수가 없는 명령이면 빈 글을 준다 — 그때만 0 이고, 숫자가 아닌 글은 드라이버 오류다.
                        const utf8* pAffected    = PQcmdTuples( result.get() );
                        const bool  bHasRowCount = pAffected != nullptr && pAffected[0] != '\0';
                        if ( bHasRowCount && StringUtil::parseInt64( pAffected, pOutRowSet->_affectedRowCount ) == false )
                        {
                            _lastError = "PQcmdTuples returned a non-numeric row count";
                            return SQLResult::Error;
                        }
                    }
                    return SQLResult::Ok;
                }

                SQLResult executeScript( string_view sql ) override
                {
                    _lastError.clear();
                    if ( sql.empty() )
                        return SQLResult::Ok;
                    const string    text( sql );
                    ScopedResult    result{ PQexec( _pConnection, text.c_str() ) };
                    const SQLResult sqlResult = toSQLResult( result.get(), _pConnection );
                    return sqlResult == SQLResult::Ok ? sqlResult : fail( sqlResult, result.get() );
                }

                const SQLDialect& getDialect() const override { return PostgresDriverInternal::getDialect(); }
                const utf8*       getLastErrorText() const override { return _lastError.c_str(); }
                bool              isAlive() const override { return _bAlive == SW_TRUE && PQstatus( _pConnection ) == CONNECTION_OK; }

            private:
                struct PreparedStatement
                {
                    string _name{};
                    int32  _paramCount{ 0 };
                };

                /** @brief 준비된 문장을 찾거나 만듭니다. 못 만들면 nullptr 과 까닭(@p outFailure)입니다 - 예전에는 까닭을 버려 끊김 · 직렬화 실패가 호출한 쪽에는 Error 로 보였다. */
                const PreparedStatement* findOrPrepare( string_view sql, SQLResult& outFailure )
                {
                    const string key( sql );
                    const auto   statementIt = _mapStatementName.find( key );
                    if ( statementIt != _mapStatementName.end() )
                        return &statementIt->second;
                    PreparedStatement                       statement;
                    const string                            converted = PostgresCodec::convertPlaceholders( sql, statement._paramCount );
                    StringBuilder<constant::buffer::kMax32> name;
                    name.appendFormat( "sw_s%#", _nextStatementIndex++ );
                    statement._name = string( name.view() );
                    ScopedResult    result{ PQprepare( _pConnection, statement._name.c_str(), converted.c_str(), statement._paramCount, nullptr ) };
                    const SQLResult sqlResult = toSQLResult( result.get(), _pConnection );
                    if ( sqlResult != SQLResult::Ok )
                    {
                        outFailure = fail( sqlResult, result.get() );
                        return nullptr;
                    }
                    return &_mapStatementName.emplace( key, statement ).first->second;
                }

                /** @brief 결과 행을 읽습니다. 담을 수 없는 칸(모르는 타입 · 틀린 정수 길이)이 있으면 false 와 까닭입니다 - 예전에는 모르는 타입의 이진 표현을 바이트로 그대로 건넸다. */
                [[nodiscard]] static bool readRows( const PGresult* pResult, SQLRowSet& outRowSet, string& outError )
                {
                    const int32 rowCount    = PQntuples( pResult );
                    const int32 columnCount = PQnfields( pResult );
                    outRowSet._listRow.reserve( static_cast<size_t>( rowCount ) );
                    for ( int32 rowIndex = 0; rowIndex < rowCount; ++rowIndex )
                    {
                        vector<SQLValue>& listValue = outRowSet._listRow.emplace_back();
                        listValue.reserve( static_cast<size_t>( columnCount ) );
                        for ( int32 columnIndex = 0; columnIndex < columnCount; ++columnIndex )
                        {
                            SQLValue& value = listValue.emplace_back();
                            if ( PQgetisnull( pResult, rowIndex, columnIndex ) != 0 )
                                continue;
                            const uint8* pData = reinterpret_cast<const uint8*>( PQgetvalue( pResult, rowIndex, columnIndex ) );
                            const int32  size  = PQgetlength( pResult, rowIndex, columnIndex );
                            if ( PostgresCodec::decodeColumn( static_cast<uint32>( PQftype( pResult, columnIndex ) ), pData, size, value, outError ) == false )
                                return false;
                        }
                    }
                    return true;
                }

                SQLResult fail( SQLResult result, const PGresult* pResult )
                {
                    const utf8* pMessage = pResult != nullptr ? PQresultErrorMessage( pResult ) : nullptr;
                    _lastError           = ( pMessage != nullptr && pMessage[0] != '\0' ) ? pMessage : PQerrorMessage( _pConnection );
                    if ( result == SQLResult::ConnectionLost )
                        _bAlive = SW_FALSE;
                    return result;
                }

            private:
                unordered_map<string, PreparedStatement> _mapStatementName;
                string                                   _lastError;
                PGconn*                                  _pConnection;
                int32                                    _nextStatementIndex;
                uint8                                    _bAlive;
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @class PostgresDriver
     * @brief PostgreSQL 드라이버입니다. 상태가 없어 하나를 모든 워커가 같이 씁니다.
     */
    class PostgresDriver final : public ISQLDriver
    {
    public:
        static PostgresDriver& getInstance();

        const utf8*                getName() const override { return "postgres"; }
        unique_ptr<ISQLConnection> openConnection( string_view connection, string_view secret, string& outError ) override;
    };
} // namespace sw

namespace sw
{
    PostgresDriver& PostgresDriver::getInstance()
    {
        static PostgresDriver s_driver;
        return s_driver;
    }

    unique_ptr<ISQLConnection> PostgresDriver::openConnection( string_view connection, string_view secret, string& outError )
    {
        // 빈 접속 문자열을 libpq 에 넘기면 환경 변수 · 기본값(localhost, OS 사용자)으로 어딘가에 붙는다 - SQLite 드라이버처럼 거절한다.
        if ( connection.empty() )
        {
            outError = "postgres connection needs a connection string";
            return nullptr;
        }
        // 접속 문자열은 dbname 에 펼쳐 넘기고(expand_dbname) 비밀번호는 따로 넘긴다. 그래서 비밀이 접속 문자열이나 로그에 남지 않는다.
        const string connectionText( connection );
        const string secretText( secret );
        const utf8*  arrKeyword[] = { "dbname", secretText.empty() ? nullptr : "password", nullptr };
        const utf8*  arrValue[]   = { connectionText.c_str(), secretText.empty() ? nullptr : secretText.c_str(), nullptr };
        PGconn*      pConnection  = PQconnectdbParams( arrKeyword, arrValue, 1 );
        if ( pConnection == nullptr )
        {
            outError = "postgres connection failed: out of memory";
            return nullptr;
        }
        if ( PQstatus( pConnection ) != CONNECTION_OK )
        {
            outError = string( "postgres connection failed: " ) + PQerrorMessage( pConnection );
            PQfinish( pConnection );
            return nullptr;
        }
        PQsetNoticeProcessor( pConnection, &PostgresDriverInternal::ignoreNotice, nullptr );
        PGresult*  pResult = PQexec( pConnection, "SET TIME ZONE 'UTC'" );
        const bool bOk     = PQresultStatus( pResult ) == PGRES_COMMAND_OK;
        PQclear( pResult );
        if ( bOk == false )
        {
            outError = string( "postgres session setup failed: " ) + PQerrorMessage( pConnection );
            PQfinish( pConnection );
            return nullptr;
        }
        return make_unique<PostgresDriverInternal::PostgresConnection>( pConnection );
    }
} // namespace sw

namespace sw
{
    // 모듈이 로드되면 이 드라이버를 레지스트리에 올리고, 언로드되면 내린다.
    const SQLDriverRegistration s_postgresDriverRegistration{ PostgresDriver::getInstance() };
} // namespace sw
