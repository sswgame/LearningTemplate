#include "pch.h"

#include "GameFramework/Kits/Storage/SqlStore/Sql/SqlConnectionPool.h"

#include "Core/Concurrency/ThreadName.h"
#include "Core/String/StringBuilder.h"
#include "Core/Time/MonotonicClock.h"

#include "GameFramework/Kits/Storage/SqlStore/Sql/SqlDriver.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "SqlConnectionPool" );

    namespace
    {
        struct SqlConnectionPoolInternal
        {
            static constexpr int64 kFirstRetryDelayMs = 100;
            static constexpr int64 kMaxRetryDelayMs   = 5000;

            /** @brief 닫힌 연결 — 모든 호출이 ConnectionLost 입니다(다시 여는 중 · 내리는 중의 일). */
            class ClosedConnection final : public ISqlConnection
            {
            public:
                SqlResult execute( string_view sql, const SqlValue* pParam, int32 paramCount, SqlRowSet* pOutRowSet ) override
                {
                    (void)sql;
                    (void)pParam;
                    (void)paramCount;
                    if ( pOutRowSet != nullptr )
                        pOutRowSet->clear();
                    return SqlResult::ConnectionLost;
                }

                SqlResult executeScript( string_view sql ) override
                {
                    (void)sql;
                    return SqlResult::ConnectionLost;
                }

                const SqlDialect& getDialect() const override
                {
                    static const SqlDialect s_dialect{};
                    return s_dialect;
                }

                const utf8* getLastErrorText() const override { return "connection is closed"; }
                bool        isAlive() const override { return false; }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    SqlConnectionPool::SqlConnectionPool()
        : _listWorker{}
        , _listQueuedJob{}
        , _listCompleted{}
        , _settings{}
        , _mutex{}
        , _jobReady{}
        , _pDriver{ nullptr }
        , _pendingCount{ 0 }
        , _bStopping{ SW_FALSE }
    {
    }

    SqlConnectionPool::~SqlConnectionPool()
    {
        shutdown();
        // 거두지 않은 일은 complete 없이 버린다 — 맡긴 서비스가 먼저 내려가 complete 가 가리킬 곳이 없다(메모리 구현과 같다).
        std::scoped_lock<mutex> lock{ _mutex };
        _listCompleted.clear();
    }

    bool SqlConnectionPool::initialize( ISqlDriver* pDriver, const SqlConnectionPoolSettings& settings, string& outError )
    {
        if ( pDriver == nullptr )
        {
            outError = "SQL connection pool has no driver";
            return false;
        }
        unique_ptr<ISqlConnection> probe = pDriver->openConnection( settings._connection, settings._secret, outError );
        if ( probe == nullptr )
            return false;
        probe.reset();
        _pDriver                = pDriver;
        _settings               = settings;
        _bStopping              = SW_FALSE;
        const int32 workerCount = std::max( settings._workerCount, 1 );
        _listWorker.reserve( static_cast<size_t>( workerCount ) );
        for ( int32 workerIndex = 0; workerIndex < workerCount; ++workerIndex )
            _listWorker.emplace_back( &SqlConnectionPool::runWorker, this, workerIndex );
        return true;
    }

    void SqlConnectionPool::shutdown()
    {
        {
            std::scoped_lock<mutex> lock{ _mutex };
            _bStopping = SW_TRUE;
        }
        _jobReady.notify_all();
        for ( std::thread& worker : _listWorker )
        {
            if ( worker.joinable() )
                worker.join();
        }
        _listWorker.clear();
        // 워커가 없거나(초기화 전) 남긴 일 — 닫힌 연결로 돌려 완료 큐에.
        SqlConnectionPoolInternal::ClosedConnection closed;
        for ( ;; )
        {
            unique_ptr<ISqlJob> job;
            {
                std::scoped_lock<mutex> lock{ _mutex };
                if ( _listQueuedJob.empty() )
                    break;
                job = std::move( _listQueuedJob.front() );
                _listQueuedJob.pop_front();
            }
            job->run( closed );
            std::scoped_lock<mutex> lock{ _mutex };
            _listCompleted.push_back( std::move( job ) );
        }
        _settings._secret.clear();
    }

    void SqlConnectionPool::submit( unique_ptr<ISqlJob> job )
    {
        bool bStopping = false;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            ++_pendingCount;
            bStopping = _bStopping == SW_TRUE || _listWorker.empty();
            if ( bStopping == false )
                _listQueuedJob.push_back( std::move( job ) );
        }
        if ( bStopping == false )
        {
            _jobReady.notify_one();
            return;
        }
        SqlConnectionPoolInternal::ClosedConnection closed;
        job->run( closed );
        std::scoped_lock<mutex> lock{ _mutex };
        _listCompleted.push_back( std::move( job ) );
    }

    int32 SqlConnectionPool::pollCompletions()
    {
        // complete 안에서 새 일을 맡길 수 있다 — 꺼낸 목록을 잠금 밖에서 돈다(메모리 구현과 같은 순서 약속).
        vector<unique_ptr<ISqlJob>> listReady;
        {
            std::scoped_lock<mutex> lock{ _mutex };
            listReady.swap( _listCompleted );
            _pendingCount -= static_cast<int32>( listReady.size() );
        }
        for ( unique_ptr<ISqlJob>& job : listReady )
            job->complete();
        return static_cast<int32>( listReady.size() );
    }

    int32 SqlConnectionPool::getPendingCount() const
    {
        std::scoped_lock<mutex> lock{ _mutex };
        return _pendingCount;
    }

    void SqlConnectionPool::runWorker( int32 workerIndex )
    {
        StringBuilder<constant::kMaxBuffer32> threadName;
        threadName.appendFormat( "SqlWorker%#", workerIndex );
        ThreadName::setCurrentThreadName( threadName.c_str() );

        SqlConnectionPoolInternal::ClosedConnection closed;
        string                                      error;
        unique_ptr<ISqlConnection>                  connection   = _pDriver->openConnection( _settings._connection, _settings._secret, error );
        int64                                       retryDelayMs = SqlConnectionPoolInternal::kFirstRetryDelayMs;
        Deadline                                    retryAt      = Deadline::afterMilliseconds( 0 );
        if ( connection == nullptr )
            SW_LOG_WARNING( "SQL worker %# could not connect: %#", workerIndex, error.c_str() );
        for ( ;; )
        {
            unique_ptr<ISqlJob> job;
            bool                bStopping = false;
            {
                std::unique_lock<mutex> lock{ _mutex };
                _jobReady.wait( lock, [this]()
                { return _bStopping == SW_TRUE || _listQueuedJob.empty() == false; } );
                if ( _listQueuedJob.empty() )
                    return; // 멈추는 중이고 남은 일이 없다
                job = std::move( _listQueuedJob.front() );
                _listQueuedJob.pop_front();
                bStopping = _bStopping == SW_TRUE;
            }
            const bool bNeedsReconnect = connection == nullptr || connection->isAlive() == false;
            if ( bNeedsReconnect && bStopping == false && retryAt.isExpired() )
            {
                connection = _pDriver->openConnection( _settings._connection, _settings._secret, error );
                if ( connection == nullptr )
                {
                    SW_LOG_WARNING( "SQL worker %# could not reconnect: %#", workerIndex, error.c_str() );
                    retryDelayMs = std::min( retryDelayMs * 2, SqlConnectionPoolInternal::kMaxRetryDelayMs );
                    retryAt      = Deadline::afterMilliseconds( retryDelayMs );
                }
                else
                {
                    retryDelayMs = SqlConnectionPoolInternal::kFirstRetryDelayMs;
                }
            }
            const bool bUsable = connection != nullptr && connection->isAlive() && bStopping == false;
            if ( bUsable )
                job->run( *connection );
            else
                job->run( closed );
            std::scoped_lock<mutex> lock{ _mutex };
            _listCompleted.push_back( std::move( job ) );
        }
    }
} // namespace sw
