/**
 * @file PostgresTestSchema.h
 * @brief PostgreSQL 시험의 무작위 스키마 — 실행 · 픽스처마다 `sw_test_<16 진 8 자리>` 스키마를 만들고 접속 글에 search_path 를 붙여, 끝나면 `DROP SCHEMA … CASCADE`.
 * @details 서버 주소는 환경 변수 `SW_TEST_POSTGRES_URL`(libpq 키워드 글 또는 URI)이다. 로컬에서 돌리는 법:
 *          `docker run -e POSTGRES_PASSWORD=sw -p 5432:5432 postgres:17` 뒤 `set SW_TEST_POSTGRES_URL=postgresql://postgres:sw@127.0.0.1:5432/postgres`.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/MonotonicClock.h"

#include "GameFramework/Kits/Feature/Storage/SqlStore/Server/Driver/Postgres/PostgresDriver.h"

#include "TestFramework/TestFramework.h"

namespace test
{
    /** @brief 시험 스키마 하나 — 만들 때 스키마를 만들고, 없앨 때 지운다. */
    class PostgresTestSchema
    {
    public:
        static constexpr const utf8* kEnvironmentName = "SW_TEST_POSTGRES_URL";

        PostgresTestSchema()
            : _baseConnection{ getEnvironmentValue( kEnvironmentName ) }
            , _schemaName{}
            , _connection{}
            , _admin{}
        {
            static uint32                                 s_sequence = 0;
            const uint64                                  seed       = static_cast<uint64>( sw::MonotonicClock::nowNanoseconds() ) ^ ( static_cast<uint64>( ++s_sequence ) << 40 );
            sw::StringBuilder<sw::constant::kMaxBuffer64> name;
            name.appendFormat( "sw_test_%#", static_cast<uint32>( seed ^ ( seed >> 32 ) ) );
            _schemaName = sw::string( name.view() );
            sw::string error;
            _admin = sw::PostgresDriver::getInstance().openConnection( _baseConnection, "", error );
            if ( _admin == nullptr || _admin->executeScript( "CREATE SCHEMA " + _schemaName ) != sw::SqlResult::Ok )
            {
                _admin.reset();
                return;
            }
            // 키워드 글이면 options 를 덧붙이고, URI 면 질의 인자로 붙인다.
            const bool bUri = _baseConnection.find( "://" ) != sw::string::npos;
            if ( bUri )
                _connection = _baseConnection + ( _baseConnection.find( '?' ) == sw::string::npos ? "?" : "&" ) + "options=-csearch_path%3D" + _schemaName;
            else
                _connection = _baseConnection + " options='-csearch_path=" + _schemaName + "'";
        }

        ~PostgresTestSchema()
        {
            if ( _admin != nullptr )
                (void)_admin->executeScript( "DROP SCHEMA " + _schemaName + " CASCADE" );
        }

        PostgresTestSchema( const PostgresTestSchema& )            = delete;
        PostgresTestSchema& operator=( const PostgresTestSchema& ) = delete;

        bool              isReady() const { return _admin != nullptr; }
        const sw::string& getConnection() const { return _connection; }

    private:
        sw::string                         _baseConnection;
        sw::string                         _schemaName;
        sw::string                         _connection;
        sw::unique_ptr<sw::ISqlConnection> _admin;
    };
} // namespace test
