/**
 * @file FakeSQLDriver.h
 * @brief 외부 DB 없이 SQL 호출 순서와 실패를 시험하는 가짜 드라이버입니다(런타임 · 서버 키트 테스트가 같이 씁니다).
 * @details 받은 문장을 모두 적고, 문장 조각에 건 규칙대로 답한다(나중에 건 규칙이 먼저다). 규칙이 없으면 Ok 에 행 없음, 바뀐 행 1 이다.
 *          연결은 세대 번호로 산다 — `loseAllConnections` 나 `ConnectionLost` 답이 세대를 올리면 그 앞의 연결은 모두 `isAlive` 가 false 다.
 *          풀 워커와 테스트 스레드가 같이 부르므로 모든 칸을 잠금으로 지킨다.
 *          이름이 "fake" 이므로 `SQLDriverRegistration` 은 케이스 범위 안에서만 만든다 — 등록 표에 남으면 스키마 테스트가 이 이름의 방언도 찾는다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Kits/Feature/Storage/SQLStore/Runtime/SQL/SQLDriver.h"

namespace sw::test
{
    /**
     * @class FakeSQLDriver
     * @brief 파일 머리말을 참고하세요.
     */
    class FakeSQLDriver final : public ISQLDriver
    {
    public:
        static constexpr const utf8* kName              = "fake";
        static constexpr const utf8* kNextCommitVersion = "NEXT STORE VERSION"; ///< "COMMIT" 을 담지 않는다 - 커밋 문장을 세는 조각과 겹친다

        /** @brief 받은 문장 하나입니다. */
        struct Statement
        {
            vector<SQLValue> _listParam{};
            string           _sql{};
        };

        FakeSQLDriver()
            : _listRule{}
            , _listStatement{}
            , _mutex{}
            , _openCount{ 0 }
            , _allowedOpenCount{ -1 }
            , _generation{ 0 }
        {
        }

        /** @brief @p fragment 가 든 문장에 @p result 로 답합니다. @p useCount 번 쓰면 사라지고, -1 이면 계속입니다. */
        void addResult( string_view fragment, SQLResult result, int32 useCount = -1 ) { addRule( fragment, result, {}, 1, useCount ); }
        /** @brief @p fragment 가 든 문장에 행 @p listRow 와 바뀐 행 수로 답합니다. */
        void addRows( string_view fragment, vector<vector<SQLValue>> listRow, int64 affectedRowCount = 0, int32 useCount = -1 )
        {
            addRule( fragment, SQLResult::Ok, std::move( listRow ), affectedRowCount, useCount );
        }

        /** @brief 지금까지의 시도(실패 포함)를 넘는 열기를 @p allowedOpenCount 번까지만 허락합니다. -1 이면 끝이 없습니다. */
        void setAllowedOpenCount( int32 allowedOpenCount )
        {
            ScopedLock<mutex> lock{ _mutex };
            _allowedOpenCount = allowedOpenCount;
        }
        /** @brief 열린 연결을 모두 끊깁니다(`isAlive` 가 false 가 된다). */
        void loseAllConnections()
        {
            ScopedLock<mutex> lock{ _mutex };
            ++_generation;
        }
        int32 getOpenCount() const
        {
            ScopedLock<mutex> lock{ _mutex };
            return _openCount;
        }
        vector<Statement> getStatements() const
        {
            ScopedLock<mutex> lock{ _mutex };
            return _listStatement;
        }
        vector<string> getSQLLog() const
        {
            ScopedLock<mutex> lock{ _mutex };
            vector<string>    listSQL;
            for ( const Statement& statement : _listStatement )
            {
                listSQL.push_back( statement._sql );
            }
            return listSQL;
        }
        /** @brief @p fragment 가 든 문장의 수입니다. */
        int32 countSQL( string_view fragment ) const
        {
            ScopedLock<mutex> lock{ _mutex };
            int32             count = 0;
            for ( const Statement& statement : _listStatement )
            {
                if ( statement._sql.find( string( fragment ) ) != string::npos )
                    ++count;
            }
            return count;
        }
        void clearLog()
        {
            ScopedLock<mutex> lock{ _mutex };
            _listStatement.clear();
        }

        const utf8*                getName() const override { return kName; }
        unique_ptr<ISQLConnection> openConnection( string_view connection, string_view secret, string& outError ) override;

        static const SQLDialect& getDialect()
        {
            static const SQLDialect s_dialect{ kName, "BLOBTYPE", "KEYTEXT", "AUTOID", "BEGIN WRITE", kNextCommitVersion, "LOCK HISTORY" };
            return s_dialect;
        }

        /** @brief 연결이 받은 문장 하나에 답합니다. 연결만 부릅니다. */
        SQLResult answer( string_view sql, const SQLValue* pParam, int32 paramCount, SQLRowSet* pOutRowSet )
        {
            ScopedLock<mutex> lock{ _mutex };
            Statement&        statement = _listStatement.emplace_back();
            statement._sql              = string( sql );
            if ( paramCount > 0 )
                statement._listParam.assign( pParam, pParam + paramCount );
            if ( pOutRowSet != nullptr )
                pOutRowSet->clear();
            for ( Rule& rule : _listRule )
            {
                if ( rule._useCount == 0 || sql.find( rule._fragment ) == string_view::npos )
                    continue;
                if ( rule._useCount > 0 )
                    --rule._useCount;
                if ( rule._result == SQLResult::ConnectionLost )
                    ++_generation;
                if ( pOutRowSet != nullptr && rule._result == SQLResult::Ok )
                {
                    pOutRowSet->_listRow          = rule._listRow;
                    pOutRowSet->_affectedRowCount = rule._affectedRowCount;
                }
                return rule._result;
            }
            if ( pOutRowSet != nullptr )
                pOutRowSet->_affectedRowCount = 1;
            return SQLResult::Ok;
        }

        bool isGenerationAlive( uint32 generation ) const
        {
            ScopedLock<mutex> lock{ _mutex };
            return generation == _generation;
        }

    private:
        struct Rule
        {
            vector<vector<SQLValue>> _listRow{};
            string                   _fragment{};
            int64                    _affectedRowCount{ 0 };
            int32                    _useCount{ -1 };
            SQLResult                _result{ SQLResult::Ok };
        };

        void addRule( string_view fragment, SQLResult result, vector<vector<SQLValue>> listRow, int64 affectedRowCount, int32 useCount )
        {
            ScopedLock<mutex> lock{ _mutex };
            Rule&             rule = *_listRule.emplace( _listRule.begin() );
            rule._fragment         = string( fragment );
            rule._result           = result;
            rule._listRow          = std::move( listRow );
            rule._affectedRowCount = affectedRowCount;
            rule._useCount         = useCount;
        }

    private:
        vector<Rule>      _listRule;
        vector<Statement> _listStatement;
        mutable mutex     _mutex;
        int32             _openCount;
        int32             _allowedOpenCount;
        uint32            _generation;
    };
} // namespace sw::test

namespace sw::test
{
    /**
     * @class FakeSQLConnection
     * @brief `FakeSQLDriver` 가 여는 연결입니다. 드라이버보다 먼저 사라져야 합니다.
     */
    class FakeSQLConnection final : public ISQLConnection
    {
    public:
        FakeSQLConnection( FakeSQLDriver& driver, uint32 generation )
            : _pDriver{ &driver }
            , _generation{ generation }
            , _bLastFailed{ SW_FALSE }
        {
        }

        SQLResult execute( string_view sql, const SQLValue* pParam, int32 paramCount, SQLRowSet* pOutRowSet ) override
        {
            const SQLResult result = _pDriver->answer( sql, pParam, paramCount, pOutRowSet );
            _bLastFailed           = result != SQLResult::Ok ? SW_TRUE : SW_FALSE;
            return result;
        }
        SQLResult executeScript( string_view sql ) override { return execute( sql, nullptr, 0, nullptr ); }

        const SQLDialect& getDialect() const override { return FakeSQLDriver::getDialect(); }
        const utf8*       getLastErrorText() const override { return _bLastFailed == SW_TRUE ? "fake failure" : ""; }
        bool              isAlive() const override { return _pDriver->isGenerationAlive( _generation ); }

    private:
        FakeSQLDriver* _pDriver;
        uint32         _generation;
        uint8          _bLastFailed : 1;
    };

    inline unique_ptr<ISQLConnection> FakeSQLDriver::openConnection( string_view connection, string_view secret, string& outError )
    {
        (void)connection;
        (void)secret;
        uint32 generation = 0;
        {
            ScopedLock<mutex> lock{ _mutex };
            ++_openCount;
            if ( _allowedOpenCount == 0 )
            {
                outError = "fake driver refused the connection";
                return nullptr;
            }
            if ( _allowedOpenCount > 0 )
                --_allowedOpenCount;
            generation = _generation;
        }
        return sw::make_unique<FakeSQLConnection>( *this, generation );
    }
} // namespace sw::test
