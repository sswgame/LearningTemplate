/**
 * @file SQLiteDriver.cpp
 * @brief SQLite 드라이버입니다. 이 키트 폴더(`Driver/SQLite/`)만 sqlite3 를 압니다. 접속 문자열은 DB 파일 경로이며 작업 폴더 기준 상대 경로와 절대 경로를 모두 쓸 수 있습니다.
 * @details 연결마다 WAL, synchronous FULL, busy_timeout 5초를 설정한다. 준비된 문장은 SQL 문마다 캐시한다. 비밀은 쓰지 않으며 접근 제어는 파일 권한이 맡는다.
 *          드라이버는 레지스트리 등록(`SQLDriverRegistration`)으로만 닿고 이름으로 부르는 곳이 없어 헤더를 두지 않는다.
 */
#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriver.h"
#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriverRegistry.h"

#include <sqlite3.h>

namespace sw
{
    namespace
    {
        struct SQLiteDriverInternal
        {
            /** @brief 다른 연결의 잠금을 기다리는 시한입니다. `sqlite3_busy_timeout` 으로 **열자마자** 건다 - 설정 문장에 섞어 적으면 순서에 기대게 된다. */
            static constexpr int32 kBusyTimeoutMilliseconds = 5000;

            /** @brief 파일이 아닌 메모리 DB 의 연결 이름입니다(SQLite 규약). */
            static constexpr string_view kMemoryDatabase = ":memory:";

            /** @brief 연결마다 한 번 도는 설정 문장입니다(잠금 시한은 이미 걸려 있어 이 문장들의 순서와 무관하게 지켜진다). WAL 은 읽기와 쓰기가 서로 막지 않게 하고, synchronous FULL 은 거래의 커밋이 정전에도 남게 한다. */
            static constexpr const utf8* kSetupSQL = "PRAGMA journal_mode = WAL; PRAGMA synchronous = FULL; PRAGMA foreign_keys = ON;";

            static const SQLDialect& getDialect()
            {
                static const SQLDialect s_dialect{ "sqlite",
                                                   "BLOB",
                                                   "TEXT",
                                                   "INTEGER PRIMARY KEY AUTOINCREMENT",
                                                   "BEGIN IMMEDIATE",
                                                   "UPDATE sw_store_counter SET value = value + 1 WHERE name = 'commit_version' RETURNING value",
                                                   "" };
                return s_dialect;
            }

            static SQLResult toSQLResult( int32 code )
            {
                // 고유 키 · 기본 키만 Constraint 다(`SQLResult::Constraint` · PostgreSQL 23505 와 같다). NOT NULL · CHECK · 외래 키는 다시 해도 같은 오류인데 Conflict 로 올라가 재시도를 돌게 했다.
                if ( code == SQLITE_CONSTRAINT_UNIQUE || code == SQLITE_CONSTRAINT_PRIMARYKEY || code == SQLITE_CONSTRAINT_ROWID )
                    return SQLResult::Constraint;
                switch ( code & 0xFF ) // 확장 코드에서 기본 코드를 뽑는다
                {
                    case SQLITE_OK:
                    case SQLITE_DONE:
                    case SQLITE_ROW:
                        return SQLResult::Ok;
                    case SQLITE_BUSY:
                    case SQLITE_LOCKED:
                        return SQLResult::Busy;
                    case SQLITE_IOERR:
                    case SQLITE_CANTOPEN:
                    case SQLITE_CORRUPT:
                    case SQLITE_NOTADB:
                        return SQLResult::ConnectionLost;
                    default:
                        return SQLResult::Error;
                }
            }

            /** @brief 연결 하나입니다. 연 워커 스레드 하나만 쓰며 SQLITE_OPEN_NOMUTEX 로 엽니다. */
            class SQLiteConnection final : public ISQLConnection
            {
            public:
                explicit SQLiteConnection( sqlite3* pDatabase )
                    : _mapStatement{}
                    , _lastError{}
                    , _pDatabase{ pDatabase }
                    , _bAlive{ SW_TRUE }
                {
                }

                ~SQLiteConnection() override
                {
                    for ( auto& [sql, pStatement] : _mapStatement )
                    {
                        sqlite3_finalize( pStatement );
                    }
                    sqlite3_close_v2( _pDatabase );
                }

                SQLResult execute( string_view sql, const SQLValue* pParam, int32 paramCount, SQLRowSet* pOutRowSet ) override
                {
                    _lastError.clear();
                    if ( pOutRowSet != nullptr )
                        pOutRowSet->clear();
                    SQLResult     prepareResult = SQLResult::Ok;
                    sqlite3_stmt* pStatement    = findOrPrepare( sql, prepareResult );
                    if ( pStatement == nullptr )
                        return fail( prepareResult );
                    // 자리 수가 다른 호출은 고칠 수 없는 잘못이다 - 예전에는 모자란 칸이 NULL 로, 남는 값은 버려져 다른 문장이 말없이 돌았다.
                    if ( sqlite3_bind_parameter_count( pStatement ) != paramCount )
                    {
                        _lastError = "the statement and the parameter list differ in length";
                        return fail( SQLResult::Error );
                    }
                    for ( int32 paramIndex = 0; paramIndex < paramCount; ++paramIndex )
                    {
                        const int32 bindCode = bindValue( pStatement, paramIndex + 1, pParam[paramIndex] );
                        if ( bindCode != SQLITE_OK )
                        {
                            _lastError = sqlite3_errstr( bindCode );
                            sqlite3_clear_bindings( pStatement );
                            return fail( toSQLResult( bindCode ) );
                        }
                    }
                    bool  bColumnsReadable = true;
                    int32 code             = sqlite3_step( pStatement );
                    while ( code == SQLITE_ROW )
                    {
                        if ( pOutRowSet != nullptr && bColumnsReadable )
                            bColumnsReadable = readRow( pStatement, pOutRowSet->_listRow.emplace_back() );
                        code = sqlite3_step( pStatement );
                    }
                    SQLResult result = toSQLResult( code );
                    if ( result != SQLResult::Ok )
                    {
                        _lastError = sqlite3_errmsg( _pDatabase );
                    }
                    else if ( bColumnsReadable == false )
                    {
                        result     = SQLResult::Error;
                        _lastError = "a column holds a REAL value and SQLValue has no floating type";
                        pOutRowSet->clear();
                    }
                    // 읽기 문은 행을 바꾸지 않는다 - `sqlite3_changes64` 는 앞선 쓰기의 수를 그대로 돌려주어 SELECT 의 바뀐 행 수가 낡은 값이었다.
                    if ( pOutRowSet != nullptr && result == SQLResult::Ok )
                        pOutRowSet->_affectedRowCount = sqlite3_stmt_readonly( pStatement ) != 0 ? 0 : sqlite3_changes64( _pDatabase );
                    sqlite3_reset( pStatement );
                    sqlite3_clear_bindings( pStatement );
                    return result == SQLResult::Ok ? result : fail( result );
                }

                SQLResult executeScript( string_view sql ) override
                {
                    _lastError.clear();
                    if ( sql.empty() )
                        return SQLResult::Ok;
                    utf8*        pError = nullptr;
                    const string text( sql );
                    const int32  code = sqlite3_exec( _pDatabase, text.c_str(), nullptr, nullptr, &pError );
                    if ( pError != nullptr )
                    {
                        _lastError = pError;
                        sqlite3_free( pError );
                    }
                    const SQLResult result = toSQLResult( code );
                    return result == SQLResult::Ok ? result : fail( result );
                }

                const SQLDialect& getDialect() const override { return SQLiteDriverInternal::getDialect(); }
                const utf8*       getLastErrorText() const override { return _lastError.c_str(); }
                bool              isAlive() const override { return _bAlive == SW_TRUE; }

            private:
                /** @brief 준비된 문장을 찾거나 만듭니다. 못 만들면 nullptr 과 까닭(@p outFailure)입니다 - 끊김 · 잠금을 문법 오류로 뭉개면 풀이 다시 열지 못한다. */
                sqlite3_stmt* findOrPrepare( string_view sql, SQLResult& outFailure )
                {
                    const string key( sql );
                    const auto   statementIt = _mapStatement.find( key );
                    if ( statementIt != _mapStatement.end() )
                        return statementIt->second;
                    sqlite3_stmt* pStatement = nullptr;
                    const utf8*   pTail      = nullptr;
                    const int32   code       = sqlite3_prepare_v3( _pDatabase, key.c_str(), static_cast<int32>( key.size() ), SQLITE_PREPARE_PERSISTENT, &pStatement, &pTail );
                    if ( code != SQLITE_OK )
                    {
                        _lastError = sqlite3_errmsg( _pDatabase );
                        outFailure = toSQLResult( code );
                        return nullptr;
                    }
                    // 문 하나만 받는다 - 예전에는 첫 문 뒤를 말없이 버려 적은 SQL 과 다른 일을 했다(PostgreSQL 의 PQprepare 는 거절한다). 여러 문은 `executeScript` 다.
                    const bool bSingleStatement = pStatement != nullptr && StringUtil::trim( string_view( pTail ) ).empty();
                    if ( bSingleStatement == false )
                    {
                        sqlite3_finalize( pStatement );
                        _lastError = "execute takes exactly one statement";
                        outFailure = SQLResult::Error;
                        return nullptr;
                    }
                    _mapStatement.emplace( key, pStatement );
                    return pStatement;
                }

                [[nodiscard]] static int32 bindValue( sqlite3_stmt* pStatement, int32 position, const SQLValue& value )
                {
                    // 빈 문자열과 빈 바이트도 NULL 이 아니다. 데이터 포인터가 nullptr 이면 sqlite3 가 NULL 로 바인딩하므로 따로 바인딩한다.
                    switch ( value._type )
                    {
                        case SQLValueType::Null:
                        {
                            return sqlite3_bind_null( pStatement, position );
                        }
                        case SQLValueType::Int64:
                        {
                            return sqlite3_bind_int64( pStatement, position, value._integer );
                        }
                        case SQLValueType::Text:
                        {
                            const utf8* pText = value._bytes.empty() ? "" : reinterpret_cast<const utf8*>( value._bytes.data() );
                            return sqlite3_bind_text( pStatement, position, pText, static_cast<int32>( value._bytes.size() ), SQLITE_TRANSIENT );
                        }
                        case SQLValueType::Blob:
                        {
                            if ( value._bytes.empty() )
                                return sqlite3_bind_zeroblob( pStatement, position, 0 );
                            return sqlite3_bind_blob( pStatement, position, value._bytes.data(), static_cast<int32>( value._bytes.size() ), SQLITE_TRANSIENT );
                        }
                    }
                    return SQLITE_MISUSE;
                }

                /** @brief 행 하나를 읽습니다. `SQLValue` 로 담을 수 없는 칸(REAL)이 있으면 false 입니다 - 예전에는 NULL 로 읽혀 값이 말없이 사라졌다. */
                [[nodiscard]] static bool readRow( sqlite3_stmt* pStatement, vector<SQLValue>& outListValue )
                {
                    const int32 columnCount = sqlite3_column_count( pStatement );
                    for ( int32 columnIndex = 0; columnIndex < columnCount; ++columnIndex )
                    {
                        SQLValue&   value      = outListValue.emplace_back();
                        const int32 columnType = sqlite3_column_type( pStatement, columnIndex );
                        switch ( columnType )
                        {
                            case SQLITE_INTEGER:
                            {
                                value._type    = SQLValueType::Int64;
                                value._integer = sqlite3_column_int64( pStatement, columnIndex );
                                break;
                            }
                            case SQLITE_TEXT:
                            case SQLITE_BLOB:
                            {
                                value._type        = columnType == SQLITE_TEXT ? SQLValueType::Text : SQLValueType::Blob;
                                const uint8* pData = static_cast<const uint8*>( sqlite3_column_blob( pStatement, columnIndex ) );
                                const int32  size  = sqlite3_column_bytes( pStatement, columnIndex );
                                if ( pData != nullptr && size > 0 )
                                    value._bytes.assign( pData, pData + size );
                                break;
                            }
                            case SQLITE_FLOAT:
                            {
                                return false;
                            }
                            default:
                            {
                                value._type = SQLValueType::Null;
                                break;
                            }
                        }
                    }
                    return true;
                }

                SQLResult fail( SQLResult result )
                {
                    if ( _lastError.empty() )
                        _lastError = sqlite3_errmsg( _pDatabase );
                    if ( result == SQLResult::ConnectionLost )
                        _bAlive = SW_FALSE;
                    return result;
                }

            private:
                unordered_map<string, sqlite3_stmt*> _mapStatement;
                string                               _lastError;
                sqlite3*                             _pDatabase;
                uint8                                _bAlive;
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @class SQLiteDriver
     * @brief SQLite 드라이버입니다. 상태가 없으므로 모든 워커가 하나를 같이 씁니다.
     */
    class SQLiteDriver final : public ISQLDriver
    {
    public:
        static SQLiteDriver& getInstance();

        const utf8*                getName() const override { return "sqlite"; }
        unique_ptr<ISQLConnection> openConnection( string_view connection, string_view secret, string& outError ) override;
    };
} // namespace sw

namespace sw
{
    SQLiteDriver& SQLiteDriver::getInstance()
    {
        static SQLiteDriver s_driver;
        return s_driver;
    }

    unique_ptr<ISQLConnection> SQLiteDriver::openConnection( string_view connection, string_view secret, string& outError )
    {
        (void)secret;
        if ( connection.empty() )
        {
            outError = "sqlite connection needs a database file path";
            return nullptr;
        }
        const string path( connection );
        // SQLite 가 경로를 스스로 열어 만들고 쓴다 — `FileUtil` 의 쓰기 입구를 지나지 않으므로 게이트를 직접 묻는다(메모리 DB 는 파일이 아니다).
        if ( string_view( path ) != SQLiteDriverInternal::kMemoryDatabase && FileUtil::isWriteAllowed( path ) == false )
        {
            outError = "sqlite open '" + path + "' was refused by the file write gate";
            return nullptr;
        }
        sqlite3*    pDatabase = nullptr;
        const int32 flags     = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX; // 연결은 한 스레드만 쓴다
        if ( sqlite3_open_v2( path.c_str(), &pDatabase, flags, nullptr ) != SQLITE_OK )
        {
            outError = "sqlite open '" + path + "' failed: " + ( pDatabase != nullptr ? sqlite3_errmsg( pDatabase ) : "out of memory" );
            sqlite3_close_v2( pDatabase );
            return nullptr;
        }
        // 잠금 시한을 어떤 문장보다 먼저 건다 - 새 파일을 여러 작업자가 동시에 열면 WAL 로 바꾸는 첫 문장이 서로 잠가("database is locked"), 늦게 건 시한은 그 문장을 지키지 못한다.
        // API 로 걸어 설정 문장의 순서에서 떼어 놓았다(SQLite 를 여는 곳은 이 함수 하나다).
        sqlite3_busy_timeout( pDatabase, SQLiteDriverInternal::kBusyTimeoutMilliseconds );
        sqlite3_extended_result_codes( pDatabase, 1 ); // 제약 종류를 가른다(`toSQLResult`)
        utf8*       pError = nullptr;
        const int32 code   = sqlite3_exec( pDatabase, SQLiteDriverInternal::kSetupSQL, nullptr, nullptr, &pError );
        if ( code != SQLITE_OK )
        {
            outError = "sqlite setup of '" + path + "' failed: " + ( pError != nullptr ? pError : sqlite3_errmsg( pDatabase ) );
            sqlite3_free( pError );
            sqlite3_close_v2( pDatabase );
            return nullptr;
        }
        return make_unique<SQLiteDriverInternal::SQLiteConnection>( pDatabase );
    }
} // namespace sw

namespace sw
{
    // 모듈이 로드되면 이 드라이버를 레지스트리에 올리고, 언로드되면 내린다.
    const SQLDriverRegistration s_sqliteDriverRegistration{ SQLiteDriver::getInstance() };
} // namespace sw
