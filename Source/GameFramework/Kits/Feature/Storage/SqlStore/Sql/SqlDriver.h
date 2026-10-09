/**
 * @file SqlDriver.h
 * @brief SQL 드라이버 계약 — 드라이버(이름 하나) · 연결(동기, 한 스레드) · 값 · 결과 행 · 방언 훅입니다. 비동기 · 풀은 `SqlConnectionPool` 이 이 위에 얹습니다.
 * @details - SQL 글의 자리표시자는 언제나 `?` 다(드라이버가 자기 형식으로 바꾸고 글마다 준비문을 캐시한다).
 *          - 연결은 그것을 연 스레드(풀 워커) 하나가 쓴다.
 *          - 제품 이름(SQLite · PostgreSQL)은 `Driver/<이름>/` 폴더에만 있다 — 이 계약과 그 위의 코드는 드라이버를 모른다.
 *          JDBC `Driver` · `Connection` · `ResultSet`, ADO.NET `DbProviderFactory` · `DbConnection` 과 같은 자리다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief SQL 호출의 결과입니다. */
    enum class SqlResult : uint8
    {
        Ok = 0,
        Constraint,           ///< 고유 · 기본 키 제약 위반
        SerializationFailure, ///< 직렬화 실패 · 교착 — 다시 하면 된다(서비스 저장소는 Conflict 로)
        Busy,                 ///< 잠금을 시한 안에 못 얻었다(SQLite busy_timeout)
        ConnectionLost,       ///< 연결이 끊겼다 — 트랜잭션 안이었으면 결과를 모른다
        Error                 ///< 문법 · 타입 · 그 밖(`getLastErrorText`)
    };

    /** @brief 값 하나의 종류입니다. */
    enum class SqlValueType : uint8
    {
        Null = 0,
        Int64,
        Text,
        Blob
    };
} // namespace sw

namespace sw
{
    /** @brief 매개변수 · 열 값 하나입니다. 글 · 바이트는 `_bytes` 에 든다. */
    struct SW_GF_API SqlValue
    {
        vector<uint8> _bytes{};
        int64         _integer{ 0 };
        SqlValueType  _type{ SqlValueType::Null };

        static SqlValue makeInt64( int64 value );
        static SqlValue makeText( string_view text );
        static SqlValue makeBlob( const uint8* pData, int32 size );
        string_view     getText() const { return string_view( reinterpret_cast<const utf8*>( _bytes.data() ), _bytes.size() ); }
    };
} // namespace sw

namespace sw
{
    /** @brief 결과 행들 + 바뀐 행 수입니다. */
    struct SqlRowSet
    {
        vector<vector<SqlValue>> _listRow{};
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
    /** @brief 드라이버가 갖는 방언 훅 — 이것 밖의 SQL 은 공통이다. */
    struct SqlDialect
    {
        const utf8* _pDriverName{ "" };         ///< "sqlite" · "postgres" — 마이그레이션 방언 갈래 파일 이름(`NNNN_이름.<드라이버>.sql`)
        const utf8* _pBlobType{ "BLOB" };       ///< {{blob}}
        const utf8* _pKeyTextType{ "TEXT" };    ///< {{keytext}} — 바이트 순서로 정렬되는 문자열
        const utf8* _pAutoIdColumn{ "" };       ///< {{autoid}}
        const utf8* _pBeginWrite{ "BEGIN" };    ///< 쓰기 트랜잭션 시작
        const utf8* _pNextCommitVersion{ "" };  ///< 다음 커밋 판 한 행 한 열을 돌려주는 문
        const utf8* _pLockMigrationTable{ "" }; ///< 마이그레이션 동안 다른 서버를 막는 문(없으면 빈 글)
    };
} // namespace sw

namespace sw
{
    /**
     * @class ISqlConnection
     * @brief 연결 하나 — 연 스레드 하나가 씁니다.
     */
    class SW_GF_API ISqlConnection
    {
    public:
        ISqlConnection()          = default;
        virtual ~ISqlConnection() = default;

        ISqlConnection( const ISqlConnection& )            = delete;
        ISqlConnection& operator=( const ISqlConnection& ) = delete;

        /** @brief 문 하나를 준비(글마다 캐시)해 돕니다. @p pOutRowSet 이 있으면 행 · 바뀐 행 수를 채운다. */
        [[nodiscard]] virtual SqlResult execute( string_view sql, const SqlValue* pParam, int32 paramCount, SqlRowSet* pOutRowSet ) = 0;
        /** @brief 매개변수 없는 문 여럿(마이그레이션 · BEGIN · COMMIT)을 돕니다. */
        [[nodiscard]] virtual SqlResult executeScript( string_view sql ) = 0;
        virtual const SqlDialect&       getDialect() const               = 0;
        /** @brief 마지막 실패의 까닭입니다(성공한 호출이 비운다). */
        virtual const utf8* getLastErrorText() const = 0;
        /** @brief 연결이 살아 있다고 믿을 수 있는가입니다(끊김을 본 뒤 false — 풀이 다시 연다). */
        virtual bool isAlive() const = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ISqlDriver
     * @brief 이름 하나의 드라이버입니다 — 상태 없는 공장이라 스레드 안전합니다(여러 워커가 `openConnection`).
     */
    class SW_GF_API ISqlDriver
    {
    public:
        ISqlDriver()          = default;
        virtual ~ISqlDriver() = default;

        ISqlDriver( const ISqlDriver& )            = delete;
        ISqlDriver& operator=( const ISqlDriver& ) = delete;

        /** @brief 서버 설정 `_driver` 의 값입니다 — "sqlite" · "postgres". */
        virtual const utf8* getName() const = 0;
        /**
         * @brief 연결을 엽니다. 실패하면 nullptr 과 까닭입니다.
         * @param connection 드라이버 고유 접속 글(SQLite: 파일 경로, PostgreSQL: libpq 접속 글 · URI) — 비밀은 들지 않는다
         * @param secret 비밀번호(서버 설정의 환경 변수에서 읽은 것, 없으면 빈 글)
         */
        virtual unique_ptr<ISqlConnection> openConnection( string_view connection, string_view secret, string& outError ) = 0;
    };
} // namespace sw
