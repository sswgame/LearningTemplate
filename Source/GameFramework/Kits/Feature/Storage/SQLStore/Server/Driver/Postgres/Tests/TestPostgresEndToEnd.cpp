#include "pch.h"

#include "Core/Concurrency/mutex.h"
#include "Core/Container/map.h"
#include "Core/Container/unordered_map.h"
#include "Core/Network/Transport/IStreamTransport.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Server/Driver/Postgres/PostgresCodec.h"
#include "GameFramework/Runtime/Online/Server/OnlineServerKit.h"
#include "GameFramework/Runtime/Online/Server/Tests/OnlineEndpointTestUtil.h"
#include "GameFramework/Runtime/Online/Server/Tests/OnlineRuntimeTestUtil.h"
#include "GameFramework/Runtime/Online/Service/Server/Tests/OnlineHostTestUtil.h"
#include "GameFramework/Runtime/Online/Store/Server/ServiceStore.h"

namespace
{
    /** @brief 실패의 문장과 대상을 이은 글입니다(성공이면 빈 글). 오류 글을 확인하는 테스트가 씁니다. */
    template <typename T>
    sw::string describeError( const sw::expected<T>& result )
    {
        return result.has_value() ? sw::string{} : sw::string( result.error().message() ) + " " + sw::string( result.error().detail() );
    }
} // namespace

// PostgresEndToEndTest — 서버 설정이 고른 `postgres` 저장소: 조립(`OnlineServerRuntime`)이 드라이버 키트로 저장소를 만들고, 실제 libpq 가 실제 TCP 로 로컬 가짜 서버에 붙어 비밀번호 인증(환경 변수) · 마이그레이션 · 준비된 문장 · 이진 결과를 끝까지 돈다.
// 가짜 서버는 PostgreSQL 와이어 프로토콜 3.0 을 말하고 `SQLServiceStore` · `SQLMigrationRunner` 가 쓰는 문장만 값으로 흉내 낸다. 모르는 문장은 SQL 오류로 답해 케이스가 진다 - 문장을 더하면 여기에도 더한다.
// 실제 서버의 SQL 해석 · 직렬화 격리는 보지 않는다 - 그것은 실제 Postgres 를 가리키는 `online-local run --server-config` 의 몫이다(nogpu).

using namespace sw;

namespace
{
    constexpr int64       kNowMs     = 1700000000000ll;
    constexpr const utf8* kSecretEnv = "SW_TEST_POSTGRES_END_TO_END_SECRET";
    constexpr const utf8* kSecret    = "db-secret";

    /** @brief 와이어 메시지 하나를 쌓습니다. 정수는 빅 엔디언입니다. */
    struct PgMessageWriter
    {
        vector<uint8> _bytes{};

        void addByte( uint8 value ) { _bytes.push_back( value ); }
        void addInt16( int32 value )
        {
            _bytes.push_back( static_cast<uint8>( value >> 8 ) );
            _bytes.push_back( static_cast<uint8>( value ) );
        }
        void addInt32( int32 value )
        {
            addInt16( value >> 16 );
            addInt16( value & 0xFFFF );
        }
        void addText( string_view text )
        {
            _bytes.insert( _bytes.end(), text.begin(), text.end() );
            _bytes.push_back( 0 );
        }
        void addRaw( string_view text ) { _bytes.insert( _bytes.end(), text.begin(), text.end() ); }
    };

    /** @brief 받은 메시지 본문을 앞에서부터 읽습니다. 본문이 모자라면 0 · 빈 글이고 `_bFailed` 가 켜집니다. */
    struct PgMessageReader
    {
        const uint8* _pData{ nullptr };
        size_t       _size{ 0 };
        size_t       _offset{ 0 };
        uint8        _bFailed{ SW_FALSE };

        int32 readInt16()
        {
            if ( _offset + 2 > _size )
            {
                _bFailed = SW_TRUE;
                return 0;
            }
            const int32 value = static_cast<int16>( ( _pData[_offset] << 8 ) | _pData[_offset + 1] );
            _offset += 2;
            return value;
        }
        int32 readInt32()
        {
            if ( _offset + 4 > _size )
            {
                _bFailed = SW_TRUE;
                return 0;
            }
            const uint32 value = ( static_cast<uint32>( _pData[_offset] ) << 24 ) | ( static_cast<uint32>( _pData[_offset + 1] ) << 16 ) | ( static_cast<uint32>( _pData[_offset + 2] ) << 8 ) | _pData[_offset + 3];
            _offset += 4;
            return static_cast<int32>( value );
        }
        string readText()
        {
            string text;
            while ( _offset < _size && _pData[_offset] != 0 )
            {
                text.push_back( static_cast<utf8>( _pData[_offset++] ) );
            }
            if ( _offset >= _size )
                _bFailed = SW_TRUE;
            ++_offset;
            return text;
        }
        string readBytes( int32 size )
        {
            if ( size < 0 || _offset + static_cast<size_t>( size ) > _size )
            {
                _bFailed = SW_TRUE;
                return {};
            }
            string text( reinterpret_cast<const utf8*>( _pData + _offset ), static_cast<size_t>( size ) );
            _offset += static_cast<size_t>( size );
            return text;
        }
    };

    /**
     * @brief PostgreSQL 와이어 프로토콜 3.0 을 말하는 가짜 서버입니다. 전용 I/O 스레드에서 답합니다 - libpq 는 답을 막고 기다린다.
     * @details 연결을 나눠도 데이터는 하나이고 테스트 조립은 워커 하나(`_storeWorkerCount`)라 트랜잭션이 겹치지 않습니다. ROLLBACK 은 BEGIN 때의 사본으로 되돌립니다.
     */
    class FakePostgresServer final : public IStreamHandler
    {
    public:
        struct Row
        {
            string _bytes{};
            int64  _version{ 0 };
        };

        ~FakePostgresServer() override { stop(); }

        void start( string_view password )
        {
            _password  = string( password );
            _transport = StreamTransportFactory::createPlatformTransport();
            SW_REQUIRE_TRUE( _transport != nullptr );
            SW_REQUIRE_TRUE( _transport->initialize( this, StreamTransportConfig{} ) );
            SW_REQUIRE_TRUE( _transport->listen( NetAddress::makeLoopback( 0 ) ) );
        }

        void stop()
        {
            if ( _transport != nullptr )
                _transport->shutdown();
        }

        uint16 getPort() const { return _transport->getListenPort(); }

        // 테스트 스레드가 잠금을 지나 봅니다.
        int32 getAuthenticatedCount() const
        {
            ScopedLock<mutex> lock{ _mutex };
            return _authenticatedCount;
        }
        int32 getMigrationCount() const
        {
            ScopedLock<mutex> lock{ _mutex };
            return static_cast<int32>( _mapMigration.size() );
        }
        bool hasScript( string_view fragment ) const
        {
            ScopedLock<mutex> lock{ _mutex };
            for ( const string& script : _listScript )
            {
                if ( string_view{ script }.find( fragment ) != string_view::npos )
                    return true;
            }
            return false;
        }
        bool findRow( string_view table, string_view key, Row& outRow ) const
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        rowIt = _mapRow.find( makeRowKey( table, key ) );
            if ( rowIt == _mapRow.end() )
                return false;
            outRow = rowIt->second;
            return true;
        }
        vector<string> getUnknownStatements() const
        {
            ScopedLock<mutex> lock{ _mutex };
            return _listUnknown;
        }

        void onStreamOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
        {
            (void)remote;
            (void)bAccepted;
            ScopedLock<mutex> lock{ _mutex };
            _mapPeer[handle.packed()] = make_unique<Peer>();
        }

        void onStreamReceived( StreamConnectionHandle handle, const uint8* pData, int32 size ) override
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        peerIt = _mapPeer.find( handle.packed() );
            if ( peerIt == _mapPeer.end() )
                return;
            Peer& peer = *peerIt->second;
            peer._inBytes.insert( peer._inBytes.end(), pData, pData + size );
            vector<uint8> outBytes;
            while ( consumeOne( peer, outBytes ) )
            {
            }
            if ( outBytes.empty() == false )
                (void)_transport->send( handle, outBytes.data(), static_cast<int32>( outBytes.size() ) );
        }

        void onStreamClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
        {
            (void)reason;
            ScopedLock<mutex> lock{ _mutex };
            _mapPeer.erase( handle.packed() );
        }

    private:
        struct Column
        {
            const utf8* _pName{ nullptr };
            uint32      _typeOid{ 0 };
        };

        struct Result
        {
            vector<Column>         _listColumn{};
            vector<vector<string>> _listRow{};
            string                 _tag{};
            string                 _errorState{}; ///< 비어 있지 않으면 SQL 오류다
            string                 _errorText{};
        };

        struct Peer
        {
            vector<uint8>                 _inBytes{};
            unordered_map<string, string> _mapStatement{}; ///< 준비된 문장 이름 → SQL
            vector<string>                _listParam{};    ///< 지금 묶인 매개변수(NULL 은 쓰지 않는다)
            string                        _boundSQL{};
            uint8                         _bStarted{ SW_FALSE };
            uint8                         _bInTransaction{ SW_FALSE };
            uint8                         _bFailedUntilSync{ SW_FALSE };
        };

        static string makeRowKey( string_view table, string_view key )
        {
            string rowKey{ table };
            rowKey.push_back( '\n' ); // 표 이름과 키에 줄바꿈은 없다(키 규칙)
            rowKey.append( key );
            return rowKey;
        }

        static void appendMessage( vector<uint8>& outBytes, utf8 type, const PgMessageWriter& body )
        {
            outBytes.push_back( static_cast<uint8>( type ) );
            PgMessageWriter header;
            header.addInt32( static_cast<int32>( body._bytes.size() ) + 4 );
            outBytes.insert( outBytes.end(), header._bytes.begin(), header._bytes.end() );
            outBytes.insert( outBytes.end(), body._bytes.begin(), body._bytes.end() );
        }

        static void appendReady( vector<uint8>& outBytes, const Peer& peer )
        {
            PgMessageWriter body;
            body.addByte( peer._bInTransaction == SW_TRUE ? 'T' : 'I' );
            appendMessage( outBytes, 'Z', body );
        }

        static void appendError( vector<uint8>& outBytes, string_view state, string_view text )
        {
            PgMessageWriter body;
            body.addByte( 'S' );
            body.addText( "ERROR" );
            body.addByte( 'C' );
            body.addText( state );
            body.addByte( 'M' );
            body.addText( text );
            body.addByte( 0 );
            appendMessage( outBytes, 'E', body );
        }

        static void appendParameterStatus( vector<uint8>& outBytes, string_view name, string_view value )
        {
            PgMessageWriter body;
            body.addText( name );
            body.addText( value );
            appendMessage( outBytes, 'S', body );
        }

        /** @brief 메시지 하나를 처리합니다. 다 오지 않았으면 false 입니다. */
        bool consumeOne( Peer& peer, vector<uint8>& outBytes )
        {
            const bool   bStartup   = peer._bStarted == SW_FALSE;
            const size_t headerSize = bStartup ? 4 : 5;
            if ( peer._inBytes.size() < headerSize )
                return false;
            PgMessageReader lengthReader{ peer._inBytes.data() + ( bStartup ? 0 : 1 ), 4 };
            const int32     length = lengthReader.readInt32();
            const size_t    total  = static_cast<size_t>( length ) + ( bStartup ? 0 : 1 );
            if ( peer._inBytes.size() < total )
                return false;
            PgMessageReader body{ peer._inBytes.data() + headerSize, total - headerSize };
            if ( bStartup )
                handleStartup( peer, body, outBytes );
            else
                handleMessage( peer, static_cast<utf8>( peer._inBytes[0] ), body, outBytes );
            peer._inBytes.erase( peer._inBytes.begin(), peer._inBytes.begin() + static_cast<ptrdiff_t>( total ) );
            return true;
        }

        void handleStartup( Peer& peer, PgMessageReader& body, vector<uint8>& outBytes )
        {
            const int32 code = body.readInt32();
            if ( ( code >> 16 ) != 3 )
            {
                outBytes.push_back( 'N' ); // SSLRequest · GSSENCRequest 는 거절한다 - 평문으로 이어 온다
                return;
            }
            peer._bStarted = SW_TRUE;
            PgMessageWriter request;
            request.addInt32( 3 ); // AuthenticationCleartextPassword
            appendMessage( outBytes, 'R', request );
        }

        void handleMessage( Peer& peer, utf8 type, PgMessageReader& body, vector<uint8>& outBytes )
        {
            if ( type == 'S' ) // Sync
            {
                peer._bFailedUntilSync = SW_FALSE;
                appendReady( outBytes, peer );
                return;
            }
            if ( peer._bFailedUntilSync == SW_TRUE )
                return; // 서버는 오류 뒤 Sync 까지 메시지를 버린다
            switch ( type )
            {
                case 'p':
                {
                    handlePassword( peer, body, outBytes );
                    break;
                }
                case 'Q':
                {
                    handleSimpleQuery( peer, body, outBytes );
                    break;
                }
                case 'P':
                {
                    const string name        = body.readText();
                    peer._mapStatement[name] = body.readText();
                    appendMessage( outBytes, '1', PgMessageWriter{} );
                    break;
                }
                case 'B':
                {
                    handleBind( peer, body, outBytes );
                    break;
                }
                case 'D':
                {
                    handleDescribe( peer, outBytes );
                    break;
                }
                case 'E':
                {
                    handleExecute( peer, outBytes );
                    break;
                }
                default:
                {
                    break; // Terminate('X') · Close · Flush 는 답이 없다
                }
            }
        }

        void handlePassword( Peer& peer, PgMessageReader& body, vector<uint8>& outBytes )
        {
            if ( body.readText() != _password )
            {
                appendError( outBytes, "28P01", "password authentication failed" );
                return;
            }
            ++_authenticatedCount;
            PgMessageWriter ok;
            ok.addInt32( 0 ); // AuthenticationOk
            appendMessage( outBytes, 'R', ok );
            appendParameterStatus( outBytes, "server_version", "16.0" );
            appendParameterStatus( outBytes, "server_encoding", "UTF8" );
            appendParameterStatus( outBytes, "client_encoding", "UTF8" );
            appendParameterStatus( outBytes, "standard_conforming_strings", "on" );
            appendParameterStatus( outBytes, "integer_datetimes", "on" );
            PgMessageWriter key;
            key.addInt32( 1 );
            key.addInt32( 2 );
            appendMessage( outBytes, 'K', key );
            appendReady( outBytes, peer );
        }

        /** @brief 스크립트 하나를 돕니다(마이그레이션 · BEGIN · COMMIT · ROLLBACK). 여러 문장이어도 끝난 답 하나로 충분합니다 - PQexec 는 마지막 결과를 돌려준다. */
        void handleSimpleQuery( Peer& peer, PgMessageReader& body, vector<uint8>& outBytes )
        {
            const string script = body.readText();
            _listScript.push_back( script );
            string tag = "OK";
            if ( StringUtil::startsWith( script, "BEGIN" ) )
            {
                peer._bInTransaction = SW_TRUE;
                _mapRowAtBegin       = _mapRow;
                _mapMigrationAtBegin = _mapMigration;
                tag                  = "BEGIN";
            }
            else if ( script == "COMMIT" )
            {
                peer._bInTransaction = SW_FALSE;
                tag                  = "COMMIT";
            }
            else if ( script == "ROLLBACK" )
            {
                if ( peer._bInTransaction == SW_TRUE )
                {
                    _mapRow       = _mapRowAtBegin;
                    _mapMigration = _mapMigrationAtBegin;
                }
                peer._bInTransaction = SW_FALSE;
                tag                  = "ROLLBACK";
            }
            PgMessageWriter complete;
            complete.addText( tag );
            appendMessage( outBytes, 'C', complete );
            appendReady( outBytes, peer );
        }

        void handleBind( Peer& peer, PgMessageReader& body, vector<uint8>& outBytes )
        {
            (void)body.readText(); // 포털 이름
            const string statement   = body.readText();
            const int32  formatCount = body.readInt16();
            for ( int32 index = 0; index < formatCount; ++index )
            {
                (void)body.readInt16(); // 글과 바이트 모두 그대로 담는다
            }
            const int32 paramCount = body.readInt16();
            peer._listParam.clear();
            for ( int32 index = 0; index < paramCount; ++index )
            {
                peer._listParam.push_back( body.readBytes( body.readInt32() ) );
            }
            const auto statementIt = peer._mapStatement.find( statement );
            peer._boundSQL         = statementIt != peer._mapStatement.end() ? statementIt->second : string{};
            appendMessage( outBytes, '2', PgMessageWriter{} );
        }

        /** @brief 포털의 열 모양을 답합니다. 실행은 Execute 가 하지만 열은 문장이 정합니다. */
        void handleDescribe( const Peer& peer, vector<uint8>& outBytes )
        {
            const vector<Column> listColumn = describeColumns( peer._boundSQL );
            if ( listColumn.empty() )
            {
                appendMessage( outBytes, 'n', PgMessageWriter{} );
                return;
            }
            PgMessageWriter body;
            body.addInt16( static_cast<int32>( listColumn.size() ) );
            for ( const Column& column : listColumn )
            {
                body.addText( column._pName );
                body.addInt32( 0 );
                body.addInt16( 0 );
                body.addInt32( static_cast<int32>( column._typeOid ) );
                body.addInt16( -1 );
                body.addInt32( -1 );
                body.addInt16( 1 ); // 이진
            }
            appendMessage( outBytes, 'T', body );
        }

        void handleExecute( Peer& peer, vector<uint8>& outBytes )
        {
            const Result result = runStatement( peer._boundSQL, peer._listParam );
            if ( result._errorState.empty() == false )
            {
                appendError( outBytes, result._errorState, result._errorText );
                peer._bFailedUntilSync = SW_TRUE;
                return;
            }
            for ( const vector<string>& row : result._listRow )
            {
                PgMessageWriter body;
                body.addInt16( static_cast<int32>( row.size() ) );
                for ( const string& value : row )
                {
                    body.addInt32( static_cast<int32>( value.size() ) );
                    body.addRaw( value );
                }
                appendMessage( outBytes, 'D', body );
            }
            PgMessageWriter complete;
            complete.addText( result._tag );
            appendMessage( outBytes, 'C', complete );
        }

        static bool isSelectOf( string_view sql, string_view columns ) { return StringUtil::startsWith( sql, "SELECT " ) && sql.find( columns ) != string_view::npos; }

        static vector<Column> describeColumns( string_view sql )
        {
            if ( sql.find( "nextval" ) != string_view::npos )
                return {
                    { "nextval", PostgresCodec::kInt8Oid }
                };
            if ( isSelectOf( sql, "version, checksum" ) )
                return {
                    { "version", PostgresCodec::kInt4Oid},
                    {"checksum", PostgresCodec::kTextOid}
                };
            if ( isSelectOf( sql, "rkey, bytes, version" ) )
                return {
                    {   "rkey",  PostgresCodec::kTextOid},
                    {  "bytes", PostgresCodec::kByteaOid},
                    {"version",  PostgresCodec::kInt8Oid}
                };
            if ( isSelectOf( sql, "bytes, version" ) )
                return {
                    {  "bytes", PostgresCodec::kByteaOid},
                    {"version",  PostgresCodec::kInt8Oid}
                };
            if ( isSelectOf( sql, "version FROM sw_record" ) )
                return {
                    { "version", PostgresCodec::kInt8Oid }
                };
            return {};
        }

        static string encodeInt( int64 value, int32 size )
        {
            string bytes( static_cast<size_t>( size ), '\0' );
            for ( int32 index = 0; index < size; ++index )
            {
                bytes[static_cast<size_t>( index )] = static_cast<utf8>( static_cast<uint64>( value ) >> ( ( size - 1 - index ) * 8 ) );
            }
            return bytes;
        }

        static int64 parseInt( const string& text )
        {
            int64 value = 0;
            (void)StringUtil::parseInt64( text, value );
            return value;
        }

        /** @brief `SQLServiceStore` · `SQLMigrationRunner` 의 문장을 값으로 돕니다. 모르는 문장은 SQL 오류입니다. */
        Result runStatement( const string& sql, const vector<string>& listParam )
        {
            Result result;
            if ( sql.find( "nextval" ) != string::npos )
            {
                result._listRow.push_back( { encodeInt( ++_commitVersion, 8 ) } );
                result._tag = "SELECT 1";
            }
            else if ( isSelectOf( sql, "version, checksum" ) )
            {
                for ( const auto& [version, checksum] : _mapMigration )
                {
                    result._listRow.push_back( { encodeInt( version, 4 ), checksum } );
                }
                StringBuilder<constant::buffer::kMax256> tagBuilder;
                tagBuilder.appendFormat( "SELECT %#", result._listRow.size() );
                result._tag = string( tagBuilder.view() );
            }
            else if ( StringUtil::startsWith( sql, "INSERT INTO sw_schema_migration" ) )
            {
                _mapMigration[static_cast<int32>( parseInt( listParam[0] ) )] = listParam[2];
                result._tag                                                   = "INSERT 0 1";
            }
            else if ( sql.find( "sw_record" ) != string::npos )
            {
                runRecordStatement( sql, listParam, result );
            }
            else
            {
                _listUnknown.push_back( sql );
                result._errorState = "42601";
                result._errorText  = "the fake server does not know this statement";
            }
            return result;
        }

        void runRecordStatement( const string& sql, const vector<string>& listParam, Result& outResult )
        {
            if ( isSelectOf( sql, "rkey, bytes, version" ) )
            {
                runList( sql, listParam, outResult );
                return;
            }
            if ( isSelectOf( sql, "bytes, version" ) || isSelectOf( sql, "version FROM sw_record" ) )
            {
                const bool bBytes = sql.find( "bytes" ) != string::npos;
                const auto rowIt  = _mapRow.find( makeRowKey( listParam[0], listParam[1] ) );
                if ( rowIt != _mapRow.end() )
                {
                    if ( bBytes )
                        outResult._listRow.push_back( { rowIt->second._bytes, encodeInt( rowIt->second._version, 8 ) } );
                    else
                        outResult._listRow.push_back( { encodeInt( rowIt->second._version, 8 ) } );
                }
                StringBuilder<constant::buffer::kMax256> tagBuilder;
                tagBuilder.appendFormat( "SELECT %#", outResult._listRow.size() );
                outResult._tag = string( tagBuilder.view() );
                return;
            }
            if ( StringUtil::startsWith( sql, "INSERT INTO sw_record" ) )
            {
                const string rowKey    = makeRowKey( listParam[0], listParam[1] );
                const bool   bUpsert   = sql.find( "DO UPDATE" ) != string::npos;
                const bool   bInserted = bUpsert || _mapRow.find( rowKey ) == _mapRow.end();
                if ( bInserted )
                    _mapRow[rowKey] = Row{ listParam[2], parseInt( listParam[3] ) };
                outResult._tag = bInserted ? "INSERT 0 1" : "INSERT 0 0";
                return;
            }
            if ( StringUtil::startsWith( sql, "UPDATE sw_record" ) ) // bytes, version, tbl, rkey, 옛 version
            {
                const auto rowIt    = _mapRow.find( makeRowKey( listParam[2], listParam[3] ) );
                const bool bMatched = rowIt != _mapRow.end() && rowIt->second._version == parseInt( listParam[4] );
                if ( bMatched )
                    rowIt->second = Row{ listParam[0], parseInt( listParam[1] ) };
                outResult._tag = bMatched ? "UPDATE 1" : "UPDATE 0";
                return;
            }
            if ( StringUtil::startsWith( sql, "DELETE FROM sw_record" ) ) // tbl, rkey[, 옛 version]
            {
                const auto rowIt    = _mapRow.find( makeRowKey( listParam[0], listParam[1] ) );
                const bool bMatched = rowIt != _mapRow.end() && ( listParam.size() < 3 || rowIt->second._version == parseInt( listParam[2] ) );
                if ( bMatched )
                    _mapRow.erase( rowIt );
                outResult._tag = bMatched ? "DELETE 1" : "DELETE 0";
                return;
            }
            _listUnknown.push_back( sql );
            outResult._errorState = "42601";
            outResult._errorText  = "the fake server does not know this statement";
        }

        /** @brief 범위 목록입니다. 오름차순 `tbl, >=, <, >커서, LIMIT` · 내림차순 `tbl, >=, <, LIMIT` 입니다. */
        void runList( const string& sql, const vector<string>& listParam, Result& outResult ) const
        {
            const bool                                  bDescending = sql.find( "DESC" ) != string::npos;
            const string                                first       = makeRowKey( listParam[0], listParam[1] );
            const string                                last        = makeRowKey( listParam[0], listParam[2] );
            const string                                cursor      = bDescending ? string{} : makeRowKey( listParam[0], listParam[3] );
            const int64                                 limit       = parseInt( bDescending ? listParam[3] : listParam[4] );
            vector<const map<string, Row>::value_type*> listMatch;
            for ( const auto& entry : _mapRow )
            {
                const bool bInRange = entry.first >= first && entry.first < last && ( bDescending || entry.first > cursor );
                if ( bInRange )
                    listMatch.push_back( &entry );
            }
            const size_t count = static_cast<size_t>( limit ) < listMatch.size() ? static_cast<size_t>( limit ) : listMatch.size();
            for ( size_t index = 0; index < count; ++index )
            {
                const map<string, Row>::value_type& entry = *listMatch[bDescending ? listMatch.size() - 1 - index : index];
                outResult._listRow.push_back( { entry.first.substr( listParam[0].size() + 1 ), entry.second._bytes, encodeInt( entry.second._version, 8 ) } );
            }
            StringBuilder<constant::buffer::kMax256> tagBuilder;
            tagBuilder.appendFormat( "SELECT %#", outResult._listRow.size() );
            outResult._tag = string( tagBuilder.view() );
        }

    private:
        mutable mutex                           _mutex;
        unordered_map<uint64, unique_ptr<Peer>> _mapPeer{};
        map<string, Row>                        _mapRow{}; ///< 키 순서가 목록 순서다
        map<string, Row>                        _mapRowAtBegin{};
        map<int32, string>                      _mapMigration{};
        map<int32, string>                      _mapMigrationAtBegin{};
        vector<string>                          _listScript{};
        vector<string>                          _listUnknown{};
        string                                  _password{};
        unique_ptr<IStreamTransport>            _transport{};
        int64                                   _commitVersion{ 0 };
        int32                                   _authenticatedCount{ 0 };
    };

    /** @brief 저장소 일 하나를 함수 하나로 감쌉니다. `run` 은 저장소 스레드, `complete` 는 호스트 틱입니다. */
    template <typename TFunction>
    class PostgresTestJob final : public IServiceStoreJob
    {
    public:
        PostgresTestJob( TFunction function, int32& outCompleteCount )
            : _function{ std::move( function ) }
            , _outCompleteCount{ outCompleteCount }
        {
        }

        void run( IServiceStoreConnection& connection ) override { _function( connection ); }
        void complete() override { ++_outCompleteCount; }

    private:
        TFunction _function;
        int32&    _outCompleteCount;
    };

    struct PostgresEndToEndTestUtil
    {
        static string makeServerJSON( uint16 port )
        {
            StringBuilder<constant::buffer::kMax256> resultBuilder;
            resultBuilder.appendFormat( R"({ "_listStore": [ { "_name": "main", "_driver": "postgres", "_connection": "host=127.0.0.1 port=%# dbname=game user=game sslmode=disable gssencmode=disable", "_secretEnvironment": "%#" } ], "_listCache": [ { "_name": "local", "_driver": "memory" } ] })",
                                        port, kSecretEnv );
            return string( resultBuilder.view() );
        }

        static constexpr const utf8* kOnlineJSON = R"({ "_storeName": "main", "_cacheName": "local", "_storeWorkerCount": 1, "_listKit": [ { "_name": "testLogin" } ] })";

        /** @brief 일을 맡기고 끝날 때까지 조립을 돌립니다. */
        template <typename TFunction>
        [[nodiscard]] static bool run( test::OnlineRuntimeFixture& rig, TFunction function )
        {
            int32 completeCount = 0;
            rig._runtime.getContext()._pStore->submit( sw::make_unique<PostgresTestJob<TFunction>>( std::move( function ), completeCount ) );
            return test::pumpUntil( [&rig]()
            { rig.updateAll( kNowMs, 1 ); }, [&completeCount]()
            { return completeCount == 1; } );
        }
    };
} // namespace

/** @brief [PostgresEndToEndTest] 설정이 고른 `postgres` 로 조립이 서고, libpq 가 환경 변수의 비밀로 인증해 마이그레이션을 올린 뒤, 조건부 쓰기 · 충돌 · 읽기 · 목록 · 지우기가 끝점까지 닿는다 */
SW_TEST_CASE( PostgresEndToEndTest, ConfigSelectsPostgresAndReachesTheEndpoint )
{
    FakePostgresServer server;
    server.start( kSecret );
    const test::ScopedEnvironmentVariable secret{ kSecretEnv, kSecret };

    test::OnlineRuntimeFixture rig;
    string                     error;
    {
        const auto result = rig.fromConfig( PostgresEndToEndTestUtil::makeServerJSON( server.getPort() ), PostgresEndToEndTestUtil::kOnlineJSON, kNowMs );
        SW_REQUIRE_TRUE_MSG( result.has_value(), result.has_value() ? std::string_view{} : result.error().message() );
    }
    SW_EXPECT_TRUE( server.getMigrationCount() >= 2 );
    SW_EXPECT_TRUE( server.hasScript( "CREATE SEQUENCE sw_commit_version" ) ); // 방언 분기 파일(`.postgres.sql`)이 골렸다
    SW_EXPECT_TRUE( server.hasScript( "LOCK TABLE sw_schema_migration" ) );

    const hashed_string table{ "e2e" };
    ServiceStoreResult  firstResult    = ServiceStoreResult::Unavailable;
    ServiceStoreResult  conflictResult = ServiceStoreResult::Unavailable;
    uint64              firstVersion   = 0;
    SW_REQUIRE_TRUE( PostgresEndToEndTestUtil::run( rig, [&]( IServiceStoreConnection& connection )
    {
        ServiceTransaction first;
        first.put( table, "a/1", vector<uint8>{ 0, 1, 2, 0xFF }, ServiceRecord::kAbsentVersion ); // NUL 이 든 바이트 - bytea 는 이진으로 간다
        ServiceCommitInfo info;
        firstResult  = connection.commit( first, &info );
        firstVersion = info._commitVersion;
        ServiceTransaction again;
        again.put( table, "a/1", vector<uint8>{ 9 }, ServiceRecord::kAbsentVersion );
        conflictResult = connection.commit( again );
    } ) );
    SW_EXPECT_TRUE( server.getAuthenticatedCount() >= 2 ); // 마이그레이션 연결과 워커 연결
    SW_EXPECT_TRUE( firstResult == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( conflictResult == ServiceStoreResult::Conflict );
    FakePostgresServer::Row row;
    SW_REQUIRE_TRUE( server.findRow( "e2e", "a/1", row ) );
    SW_EXPECT_EQUAL( size_t( 4 ), row._bytes.size() );
    SW_EXPECT_EQUAL( static_cast<int64>( firstVersion ), row._version );

    ServiceRecord         record;
    vector<ServiceRecord> listRecord;
    ServiceStoreResult    readResult  = ServiceStoreResult::Unavailable;
    ServiceStoreResult    missResult  = ServiceStoreResult::Unavailable;
    ServiceStoreResult    eraseResult = ServiceStoreResult::Unavailable;
    SW_REQUIRE_TRUE( PostgresEndToEndTestUtil::run( rig, [&]( IServiceStoreConnection& connection )
    {
        readResult = connection.readRecord( table, "a/1", record );
        ServiceRecord missing;
        missResult = connection.readRecord( table, "a/none", missing );
        (void)connection.listRecords( table, "a/", "", 10, false, listRecord );
        ServiceTransaction erase;
        erase.erase( table, "a/1", firstVersion );
        eraseResult = connection.commit( erase );
    } ) );
    SW_EXPECT_TRUE( readResult == ServiceStoreResult::Ok );
    SW_EXPECT_TRUE( record._bytes == ( vector<uint8>{ 0, 1, 2, 0xFF } ) );
    SW_EXPECT_EQUAL( firstVersion, record._version );
    SW_EXPECT_TRUE( missResult == ServiceStoreResult::NotFound );
    SW_REQUIRE_EQUAL( size_t( 1 ), listRecord.size() );
    SW_EXPECT_EQUAL( string( "a/1" ), listRecord[0]._key );
    SW_EXPECT_TRUE( eraseResult == ServiceStoreResult::Ok );
    SW_EXPECT_FALSE( server.findRow( "e2e", "a/1", row ) );
    SW_EXPECT_TRUE( server.getUnknownStatements().empty() );
}

/** @brief [PostgresEndToEndTest] 비밀번호가 틀리면 메모리로 물러나지 않고 기동이 멈춘다 */
SW_TEST_CASE( PostgresEndToEndTest, WrongPasswordStopsStartup )
{
    FakePostgresServer server;
    server.start( kSecret );
    const test::ScopedEnvironmentVariable secret{ kSecretEnv, "not-the-secret" };

    test::OnlineRuntimeFixture rig;
    string                     error;
    error = describeError( rig.fromConfig( PostgresEndToEndTestUtil::makeServerJSON( server.getPort() ), PostgresEndToEndTestUtil::kOnlineJSON, kNowMs ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.find( "postgres connection failed" ) != string::npos );
    SW_EXPECT_EQUAL( 0, server.getAuthenticatedCount() );
}

/** @brief [PostgresEndToEndTest] 비밀 환경 변수가 없으면 비밀 없이 붙지 않고 기동이 멈춘다 */
SW_TEST_CASE( PostgresEndToEndTest, MissingSecretStopsStartup )
{
    SW_EXPECT_LOG( "Server.SecretNotSet" );
    test::OnlineRuntimeFixture rig;
    string                     error;
    error = describeError( rig.fromConfig( PostgresEndToEndTestUtil::makeServerJSON( 5432 ), PostgresEndToEndTestUtil::kOnlineJSON, kNowMs ) );
    SW_EXPECT_FALSE( error.empty() );
    SW_EXPECT_TRUE( error.find( "main" ) != string::npos );
}
