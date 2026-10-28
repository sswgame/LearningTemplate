/**
 * @file SQLDriver.h
 * @brief SQL 드라이버 계약입니다. 드라이버(이름 하나), 연결(동기, 한 스레드), 값, 결과 행, 방언 훅을 정의합니다.
 * @details 비동기 처리와 풀은 `SQLConnectionPool` 이 이 계약 위에 구현한다.
 *          - SQL 문의 자리표시자는 항상 `?` 이다. 드라이버가 자기 형식으로 바꾸고 SQL 문마다 준비된 문장을 캐시한다.
 *          - 연결은 그것을 연 스레드(풀 워커) 하나만 쓴다.
 *          - 제품 이름(SQLite, PostgreSQL)은 `Driver/<이름>/` 폴더에만 있다. 이 계약과 그 위의 코드는 드라이버를 모른다.
 *
 *          JDBC 의 `Driver`, `Connection`, `ResultSet` 과 ADO.NET 의 `DbProviderFactory`, `DbConnection` 에 해당한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief SQL 호출의 결과입니다. */
    enum class SQLResult : uint8
    {
        Ok = 0,
        Constraint,           ///< 고유 키나 기본 키 제약을 위반했다
        SerializationFailure, ///< 직렬화 실패나 교착이다. 다시 하면 되며 서비스 저장소는 Conflict 로 처리한다
        Busy,                 ///< 락을 시한 안에 얻지 못했다(SQLite busy_timeout)
        ConnectionLost,       ///< 연결이 끊겼다. 트랜잭션 안이었으면 결과를 알 수 없다
        Error                 ///< 문법, 타입, 그 밖의 오류이다(`getLastErrorText`)
    };

    /** @brief 값 하나의 종류입니다. */
    enum class SQLValueType : uint8
    {
        Null = 0,
        Int64,
        Text,
        Blob
    };
} // namespace sw

namespace sw
{
    /** @brief 매개변수나 열의 값 하나입니다. 문자열과 바이트는 `_bytes` 에 담깁니다. */
    struct SW_KIT_API SQLValue
    {
        vector<uint8> _bytes{};
        int64         _integer{ 0 };
        SQLValueType  _type{ SQLValueType::Null };

        static SQLValue makeInt64( int64 value );
        static SQLValue makeText( string_view text );
        static SQLValue makeBlob( const uint8* pData, int32 size );
        string_view     getText() const { return string_view( reinterpret_cast<const utf8*>( _bytes.data() ), _bytes.size() ); }
    };
} // namespace sw

namespace sw
{
    /** @brief 결과 행 목록과 바뀐 행 수입니다. */
    struct SQLRowSet
    {
        vector<vector<SQLValue>> _listRow{};
        int64                    _affectedRowCount{ 0 };

        void clear()
        {
            _listRow.clear();
            _affectedRowCount = 0;
        }
    };
} // namespace sw

namespace sw
{
    /** @brief 드라이버가 제공하는 방언 훅입니다. 이 밖의 SQL 은 모든 드라이버에 공통입니다. */
    struct SQLDialect
    {
        const utf8* _pDriverName{ "" };         ///< "sqlite" 나 "postgres" 이다. 마이그레이션 방언 분기 파일 이름(`NNNN_이름.<드라이버>.sql`)에 쓴다
        const utf8* _pBlobType{ "BLOB" };       ///< {{blob}}
        const utf8* _pKeyTextType{ "TEXT" };    ///< {{keytext}} 로 치환된다. 바이트 순서로 정렬되는 문자열이다
        const utf8* _pAutoIDColumn{ "" };       ///< {{autoid}}
        const utf8* _pBeginWrite{ "BEGIN" };    ///< 쓰기 트랜잭션을 시작하는 문이다
        const utf8* _pNextCommitVersion{ "" };  ///< 다음 커밋 버전을 한 행 한 열로 돌려주는 문이다
        const utf8* _pLockMigrationTable{ "" }; ///< 마이그레이션 동안 다른 서버를 막는 문이다. 없으면 빈 문자열이다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ISQLConnection
     * @brief 연결 하나입니다. 연 스레드 하나만 씁니다.
     */
    class ISQLConnection
    {
    public:
        ISQLConnection()          = default;
        virtual ~ISQLConnection() = default;

        ISQLConnection( const ISQLConnection& )            = delete;
        ISQLConnection& operator=( const ISQLConnection& ) = delete;

        /** @brief 문 하나를 준비하고(SQL 문마다 캐시) 실행합니다. @p pOutRowSet 이 있으면 행과 바뀐 행 수를 채웁니다. */
        [[nodiscard]] virtual SQLResult execute( string_view sql, const SQLValue* pParam, int32 paramCount, SQLRowSet* pOutRowSet ) = 0;
        /** @brief 매개변수 없는 문 여러 개를 실행합니다. 마이그레이션, BEGIN, COMMIT 에 씁니다. */
        [[nodiscard]] virtual SQLResult executeScript( string_view sql ) = 0;
        virtual const SQLDialect&       getDialect() const               = 0;
        /** @brief 마지막 실패의 이유입니다. 성공한 호출이 비웁니다. */
        virtual const utf8* getLastErrorText() const = 0;
        /** @brief 연결이 살아 있다고 믿을 수 있으면 true 입니다. 끊김을 본 뒤에는 false 이며 풀이 다시 엽니다. */
        virtual bool isAlive() const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ISQLDriver
     * @brief 이름 하나에 해당하는 드라이버입니다. 상태 없는 팩토리이므로 스레드 안전하며, 여러 워커가 `openConnection` 을 부를 수 있습니다.
     */
    class ISQLDriver
    {
    public:
        ISQLDriver()          = default;
        virtual ~ISQLDriver() = default;

        ISQLDriver( const ISQLDriver& )            = delete;
        ISQLDriver& operator=( const ISQLDriver& ) = delete;

        /** @brief 서버 설정 `_driver` 의 값입니다. "sqlite" 나 "postgres" 입니다. */
        virtual const utf8* getName() const = 0;
        /**
         * @brief 연결을 엽니다. 실패하면 nullptr 과 실패 이유를 반환합니다.
         * @param connection 드라이버 고유의 접속 문자열입니다. SQLite 는 파일 경로이고 PostgreSQL 은 libpq 접속 문자열이나 URI 이며, 비밀은 포함하지 않습니다.
         * @param secret 비밀번호입니다. 서버 설정의 환경 변수에서 읽은 값이며 없으면 빈 문자열입니다.
         */
        virtual unique_ptr<ISQLConnection> openConnection( string_view connection, string_view secret, string& outError ) = 0;
    };
} // namespace sw
