#include "pch.h"

#include "GameFramework/Kits/Storage/Server/SqlStore/Driver/Postgres/PostgresDriver.h"

#include "Core/Container/unordered_map.h"
#include "Core/String/StringBuilder.h"

#include <cstdlib>
#include <libpq-fe.h>

namespace sw
{
    namespace
    {
        struct PostgresDriverInternal
        {
            // PostgreSQL 형 OID(pg_type) — 이진 결과를 읽는 데 쓰는 것만.
            static constexpr Oid kBoolOid    = 16;
            static constexpr Oid kInt8Oid    = 20;
            static constexpr Oid kInt2Oid    = 21;
            static constexpr Oid kInt4Oid    = 23;
            static constexpr Oid kTextOid    = 25;
            static constexpr Oid kVarcharOid = 1043;

            static const SqlDialect& getDialect()
            {
                static const SqlDialect s_dialect{ "postgres",
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

            /** @brief `?` 를 `$1..$n` 으로 바꿉니다(작은따옴표 · 큰따옴표 안은 건너뛴다). @p outParamCount 는 자리 수. */
            static string convertPlaceholders( string_view sql, int32& outParamCount )
            {
                string text;
                text.reserve( sql.size() + 8 );
                outParamCount  = 0;
                utf8 quoteChar = '\0';
                for ( const utf8 ch : sql )
                {
                    if ( quoteChar != '\0' )
                    {
                        if ( ch == quoteChar )
                            quoteChar = '\0';
                        text.push_back( ch );
                        continue;
                    }
                    if ( ch == '\'' || ch == '"' )
                    {
                        quoteChar = ch;
                        text.push_back( ch );
                        continue;
                    }
                    if ( ch != '?' )
                    {
                        text.push_back( ch );
                        continue;
                    }
                    ++outParamCount;
                    StringBuilder<constant::kMaxBuffer16> placeholder;
                    placeholder.appendFormat( "$%#", outParamCount );
                    text.append( placeholder.c_str() );
                }
                return text;
            }

            static uint64 readBigEndian( const uint8* pData, int32 size )
            {
                uint64 value = 0;
                for ( int32 byteIndex = 0; byteIndex < size; ++byteIndex )
                {
                    value = ( value << 8 ) | pData[byteIndex];
                }
                return value;
            }

            static SqlResult toSqlResult( const PGresult* pResult, const PGconn* pConnection )
            {
                const ExecStatusType status = PQresultStatus( pResult );
                if ( status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK || status == PGRES_EMPTY_QUERY )
                    return SqlResult::Ok;
                if ( PQstatus( pConnection ) == CONNECTION_BAD )
                    return SqlResult::ConnectionLost;
                const utf8*       pState = PQresultErrorField( pResult, PG_DIAG_SQLSTATE );
                const string_view state  = pState != nullptr ? string_view( pState ) : string_view{};
                if ( state == "23505" )
                    return SqlResult::Constraint;
                if ( state == "40001" || state == "40P01" )
                    return SqlResult::SerializationFailure;
                if ( state.size() == 5 && state[0] == '0' && state[1] == '8' )
                    return SqlResult::ConnectionLost;
                if ( state == "55P03" )
                    return SqlResult::Busy; // lock_not_available
                return SqlResult::Error;
            }

            /** @brief 결과 하나를 거둬 내놓고 지웁니다. */
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

            /** @brief 연결 하나 — 연 워커 스레드 하나가 쓴다. */
            class PostgresConnection final : public ISqlConnection
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

                SqlResult execute( string_view sql, const SqlValue* pParam, int32 paramCount, SqlRowSet* pOutRowSet ) override
                {
                    _lastError.clear();
                    if ( pOutRowSet != nullptr )
                        pOutRowSet->clear();
                    const PreparedStatement* pStatement = findOrPrepare( sql );
                    if ( pStatement == nullptr )
                        return SqlResult::Error;
                    // 매개변수 — 정수 · 글은 글 형식(0, NUL 로 끝남), 바이트는 이진(1). 덜 주면 NULL 로 채운다.
                    const int32         placeholderCount = pStatement->_paramCount;
                    vector<string>      listText( static_cast<size_t>( placeholderCount ) );
                    vector<const utf8*> listValuePointer( static_cast<size_t>( placeholderCount ), nullptr );
                    vector<int32>       listLength( static_cast<size_t>( placeholderCount ), 0 );
                    vector<int32>       listFormat( static_cast<size_t>( placeholderCount ), 0 );
                    for ( int32 paramIndex = 0; paramIndex < placeholderCount && paramIndex < paramCount; ++paramIndex )
                    {
                        const SqlValue& value = pParam[paramIndex];
                        const size_t    index = static_cast<size_t>( paramIndex );
                        switch ( value._type )
                        {
                            case SqlValueType::Null:
                            {
                                break;
                            }
                            case SqlValueType::Int64:
                            {
                                StringBuilder<constant::kMaxBuffer32> number;
                                number.append( value._integer );
                                listText[index]         = string( number.view() );
                                listValuePointer[index] = listText[index].c_str();
                                break;
                            }
                            case SqlValueType::Text:
                            {
                                listText[index]         = string( value.getText() );
                                listValuePointer[index] = listText[index].c_str();
                                break;
                            }
                            case SqlValueType::Blob:
                            {
                                listValuePointer[index] = value._bytes.empty() ? "" : reinterpret_cast<const utf8*>( value._bytes.data() );
                                listLength[index]       = static_cast<int32>( value._bytes.size() );
                                listFormat[index]       = 1;
                                break;
                            }
                        }
                    }
                    ScopedResult    result{ PQexecPrepared( _pConnection, pStatement->_name.c_str(), placeholderCount, listValuePointer.data(), listLength.data(),
                                                            listFormat.data(), 1 ) };
                    const SqlResult sqlResult = toSqlResult( result.get(), _pConnection );
                    if ( sqlResult != SqlResult::Ok )
                        return fail( sqlResult, result.get() );
                    if ( pOutRowSet != nullptr )
                    {
                        readRows( result.get(), *pOutRowSet );
                        const utf8* pAffected         = PQcmdTuples( result.get() );
                        pOutRowSet->_affectedRowCount = ( pAffected != nullptr && pAffected[0] != '\0' ) ? std::atoll( pAffected ) : 0;
                    }
                    return SqlResult::Ok;
                }

                SqlResult executeScript( string_view sql ) override
                {
                    _lastError.clear();
                    if ( sql.empty() )
                        return SqlResult::Ok;
                    const string    text( sql );
                    ScopedResult    result{ PQexec( _pConnection, text.c_str() ) };
                    const SqlResult sqlResult = toSqlResult( result.get(), _pConnection );
                    return sqlResult == SqlResult::Ok ? sqlResult : fail( sqlResult, result.get() );
                }

                const SqlDialect& getDialect() const override { return PostgresDriverInternal::getDialect(); }
                const utf8*       getLastErrorText() const override { return _lastError.c_str(); }
                bool              isAlive() const override { return _bAlive == SW_TRUE && PQstatus( _pConnection ) == CONNECTION_OK; }

            private:
                struct PreparedStatement
                {
                    string _name{};
                    int32  _paramCount{ 0 };
                };

                const PreparedStatement* findOrPrepare( string_view sql )
                {
                    const string key( sql );
                    const auto   statementIt = _mapStatementName.find( key );
                    if ( statementIt != _mapStatementName.end() )
                        return &statementIt->second;
                    PreparedStatement                     statement;
                    const string                          converted = convertPlaceholders( sql, statement._paramCount );
                    StringBuilder<constant::kMaxBuffer32> name;
                    name.appendFormat( "sw_s%#", _nextStatementIndex++ );
                    statement._name = string( name.view() );
                    ScopedResult    result{ PQprepare( _pConnection, statement._name.c_str(), converted.c_str(), statement._paramCount, nullptr ) };
                    const SqlResult sqlResult = toSqlResult( result.get(), _pConnection );
                    if ( sqlResult != SqlResult::Ok )
                    {
                        (void)fail( sqlResult, result.get() );
                        return nullptr;
                    }
                    return &_mapStatementName.emplace( key, statement ).first->second;
                }

                static void readRows( const PGresult* pResult, SqlRowSet& outRowSet )
                {
                    const int32 rowCount    = PQntuples( pResult );
                    const int32 columnCount = PQnfields( pResult );
                    outRowSet._listRow.reserve( static_cast<size_t>( rowCount ) );
                    for ( int32 rowIndex = 0; rowIndex < rowCount; ++rowIndex )
                    {
                        vector<SqlValue>& listValue = outRowSet._listRow.emplace_back();
                        listValue.reserve( static_cast<size_t>( columnCount ) );
                        for ( int32 columnIndex = 0; columnIndex < columnCount; ++columnIndex )
                        {
                            SqlValue& value = listValue.emplace_back();
                            if ( PQgetisnull( pResult, rowIndex, columnIndex ) != 0 )
                                continue;
                            const uint8* pData    = reinterpret_cast<const uint8*>( PQgetvalue( pResult, rowIndex, columnIndex ) );
                            const int32  size     = PQgetlength( pResult, rowIndex, columnIndex );
                            const Oid    type     = PQftype( pResult, columnIndex );
                            const bool   bInteger = type == kInt8Oid || type == kInt4Oid || type == kInt2Oid || type == kBoolOid;
                            if ( bInteger )
                            {
                                // 이진 정수는 빅엔디언, 부호는 크기만큼에서 늘린다.
                                const uint64 raw   = readBigEndian( pData, size );
                                const int32  shift = 64 - size * 8;
                                value._type        = SqlValueType::Int64;
                                value._integer     = shift > 0 ? static_cast<int64>( raw << shift ) >> shift : static_cast<int64>( raw );
                                continue;
                            }
                            value._type = ( type == kTextOid || type == kVarcharOid ) ? SqlValueType::Text : SqlValueType::Blob;
                            if ( size > 0 )
                                value._bytes.assign( pData, pData + size );
                        }
                    }
                }

                SqlResult fail( SqlResult result, const PGresult* pResult )
                {
                    const utf8* pMessage = pResult != nullptr ? PQresultErrorMessage( pResult ) : nullptr;
                    _lastError           = ( pMessage != nullptr && pMessage[0] != '\0' ) ? pMessage : PQerrorMessage( _pConnection );
                    if ( result == SqlResult::ConnectionLost )
                        _bAlive = SW_FALSE;
                    return result;
                }

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
    PostgresDriver& PostgresDriver::getInstance()
    {
        static PostgresDriver s_driver;
        return s_driver;
    }

    unique_ptr<ISqlConnection> PostgresDriver::openConnection( string_view connection, string_view secret, string& outError )
    {
        // 접속 글을 dbname 자리에 펼치고(expand_dbname) 비밀번호는 따로 넘긴다 — 비밀이 접속 글 · 로그에 남지 않는다.
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
