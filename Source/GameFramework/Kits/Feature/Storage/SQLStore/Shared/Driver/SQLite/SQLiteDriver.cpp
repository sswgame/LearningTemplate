#include "pch.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Shared/Driver/SQLite/SQLiteDriver.h"

#include "Core/Container/unordered_map.h"

#include <sqlite3.h>

namespace sw
{
    namespace
    {
        struct SQLiteDriverInternal
        {
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
                switch ( code & 0xFF ) // 확장 코드의 기본 코드
                {
                    case SQLITE_OK:
                    case SQLITE_DONE:
                    case SQLITE_ROW:
                        return SQLResult::Ok;
                    case SQLITE_CONSTRAINT:
                        return SQLResult::Constraint;
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

            /** @brief 연결 하나 — 연 워커 스레드 하나가 쓴다(SQLITE_OPEN_NOMUTEX). */
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
                    sqlite3_stmt* pStatement = findOrPrepare( sql );
                    if ( pStatement == nullptr )
                        return fail( SQLResult::Error );
                    for ( int32 paramIndex = 0; paramIndex < paramCount; ++paramIndex )
                    {
                        bindValue( pStatement, paramIndex + 1, pParam[paramIndex] );
                    }
                    int32 code = sqlite3_step( pStatement );
                    while ( code == SQLITE_ROW )
                    {
                        if ( pOutRowSet != nullptr )
                            readRow( pStatement, pOutRowSet->_listRow.emplace_back() );
                        code = sqlite3_step( pStatement );
                    }
                    const SQLResult result = toSQLResult( code );
                    if ( result != SQLResult::Ok )
                        _lastError = sqlite3_errmsg( _pDatabase );
                    if ( pOutRowSet != nullptr )
                        pOutRowSet->_affectedRowCount = sqlite3_changes64( _pDatabase );
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
                sqlite3_stmt* findOrPrepare( string_view sql )
                {
                    const string key( sql );
                    const auto   statementIt = _mapStatement.find( key );
                    if ( statementIt != _mapStatement.end() )
                        return statementIt->second;
                    sqlite3_stmt* pStatement = nullptr;
                    if ( sqlite3_prepare_v3( _pDatabase, key.c_str(), static_cast<int32>( key.size() ), SQLITE_PREPARE_PERSISTENT, &pStatement, nullptr ) != SQLITE_OK )
                    {
                        _lastError = sqlite3_errmsg( _pDatabase );
                        return nullptr;
                    }
                    _mapStatement.emplace( key, pStatement );
                    return pStatement;
                }

                static void bindValue( sqlite3_stmt* pStatement, int32 position, const SQLValue& value )
                {
                    // 빈 글 · 빈 바이트도 NULL 이 아니다 — 자료 포인터가 nullptr 이면 sqlite3 가 NULL 로 묶으므로 따로 묶는다.
                    switch ( value._type )
                    {
                        case SQLValueType::Null:
                        {
                            sqlite3_bind_null( pStatement, position );
                            break;
                        }
                        case SQLValueType::Int64:
                        {
                            sqlite3_bind_int64( pStatement, position, value._integer );
                            break;
                        }
                        case SQLValueType::Text:
                        {
                            const utf8* pText = value._bytes.empty() ? "" : reinterpret_cast<const utf8*>( value._bytes.data() );
                            sqlite3_bind_text( pStatement, position, pText, static_cast<int32>( value._bytes.size() ), SQLITE_TRANSIENT );
                            break;
                        }
                        case SQLValueType::Blob:
                        {
                            if ( value._bytes.empty() )
                                sqlite3_bind_zeroblob( pStatement, position, 0 );
                            else
                                sqlite3_bind_blob( pStatement, position, value._bytes.data(), static_cast<int32>( value._bytes.size() ), SQLITE_TRANSIENT );
                            break;
                        }
                    }
                }

                static void readRow( sqlite3_stmt* pStatement, vector<SQLValue>& outListValue )
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
                            default:
                            {
                                value._type = SQLValueType::Null;
                                break;
                            }
                        }
                    }
                }

                SQLResult fail( SQLResult result )
                {
                    if ( _lastError.empty() )
                        _lastError = sqlite3_errmsg( _pDatabase );
                    if ( result == SQLResult::ConnectionLost )
                        _bAlive = SW_FALSE;
                    return result;
                }

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
        sqlite3*     pDatabase = nullptr;
        const int32  flags     = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX; // 연결은 한 스레드가 쓴다
        if ( sqlite3_open_v2( path.c_str(), &pDatabase, flags, nullptr ) != SQLITE_OK )
        {
            outError = "sqlite open '" + path + "' failed: " + ( pDatabase != nullptr ? sqlite3_errmsg( pDatabase ) : "out of memory" );
            sqlite3_close_v2( pDatabase );
            return nullptr;
        }
        // WAL — 읽기와 쓰기가 막지 않는다. synchronous FULL — 서비스 저장(거래)은 정전에도 커밋이 남아야 한다. busy_timeout — 다른 연결의 쓰기를 5 초까지 기다린다.
        utf8*       pError = nullptr;
        const int32 code   = sqlite3_exec( pDatabase, "PRAGMA journal_mode = WAL; PRAGMA synchronous = FULL; PRAGMA foreign_keys = ON; PRAGMA busy_timeout = 5000;",
                                           nullptr, nullptr, &pError );
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
