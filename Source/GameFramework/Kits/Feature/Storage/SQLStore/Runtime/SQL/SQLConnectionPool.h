/**
 * @file SQLConnectionPool.h
 * @brief SQL 연결 풀입니다. 전용 워커 스레드 N개가 각자 연결 하나를 열어 작업 큐를 비우고, 끝난 작업은 완료 큐에 쌓습니다. `pollCompletions` 를 부른 스레드가 complete 를 실행합니다.
 * @details DB 를 기다리는 작업이라 TaskManager 워커가 아니라 전용 스레드를 쓴다. 로그, 파일 감시, 네트워크 스레드와 같은 규칙이다.
 *          연결이 끊기면 그 워커가 다음 작업 전에 다시 연다. 이때 지수 백오프를 쓰며 최대 5초까지 늘어난다.
 *          다시 여는 동안의 작업은 닫힌 연결로 실행되어 `ConnectionLost` 를 받으며, 멈춰서 기다리지 않는다.
 */
#pragma once
#include "Core/Concurrency/OnceInit.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Concurrency/thread.h"
#include "Core/Container/deque.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"

#include <condition_variable>
#include <thread>

namespace sw
{
    class ISQLConnection;
    class ISQLDriver;

    /**
     * @class ISQLJob
     * @brief 풀의 작업 하나입니다. `run` 은 워커에서, `complete` 는 완료를 회수하는 스레드에서 실행합니다.
     */
    class ISQLJob
    {
    public:
        ISQLJob()          = default;
        virtual ~ISQLJob() = default;

        ISQLJob( const ISQLJob& )            = delete;
        ISQLJob& operator=( const ISQLJob& ) = delete;

        virtual void run( ISQLConnection& connection ) = 0;
        virtual void complete()                        = 0;
        /**
         * @brief 풀이 종료 중이라 이 작업을 닫힌 연결로 끝내려 할 때 `run` 앞에서 부릅니다. 작업이 취소와 연결 고장을 가르고 싶을 때 겹쳐 씁니다.
         * @details 기본은 아무것도 하지 않는다(풀 자체 테스트의 작업처럼 가르지 않는 작업).
         */
        virtual void cancel() {}
    };
} // namespace sw

namespace sw
{
    /** @brief 풀 설정입니다. */
    struct SQLConnectionPoolConfig
    {
        string _connection{};     ///< 드라이버 고유의 접속 문자열이다. 비밀은 포함하지 않는다
        string _secret{};         ///< 비밀번호이다. 연결을 다시 열 때도 쓰므로 풀이 보관한다
        int32  _workerCount{ 2 }; ///< 연결 수이다. SQLite 는 쓰기가 하나뿐이므로 읽기 하나와 쓰기 하나, 즉 2면 충분하다
    };
} // namespace sw

namespace sw
{
    /**
     * @class SQLConnectionPool
     * @brief 연결 풀입니다. `submit` 은 어느 스레드에서나 부를 수 있고, `pollCompletions` 는 작업을 맡긴 서비스 스레드 하나에서만 부릅니다.
     */
    class SW_KIT_API SQLConnectionPool
    {
    public:
        SQLConnectionPool();
        ~SQLConnectionPool();

        SQLConnectionPool( const SQLConnectionPool& )            = delete;
        SQLConnectionPool& operator=( const SQLConnectionPool& ) = delete;

        /** @brief 이 스레드에서 연결 하나를 먼저 열어 보고(기동 오류를 바로 알린다) 워커를 시작합니다. 열지 못하면 false 와 실패 이유를 반환하며 폴백은 없습니다. */
        [[nodiscard]] bool initialize( ISQLDriver* pDriver, const SQLConnectionPoolConfig& settings, string& outError );
        /** @brief 새 작업을 막고, 남은 작업은 닫힌 연결로 실행해 완료 큐에 넣은 뒤 워커를 합류시킵니다. 그 뒤 `pollCompletions` 한 번이 모두 회수합니다. */
        void shutdown();

        void  submit( unique_ptr<ISQLJob> job );
        int32 pollCompletions();
        /** @brief 맡았지만 아직 회수하지 않은 작업의 수입니다. */
        int32 getPendingCount() const;

    private:
        void runWorker( int32 workerIndex );

        vector<sw::thread>                         _listWorker;
        deque<unique_ptr<ISQLJob>> _listQueuedJob  SW_GUARDED_BY( _mutex );
        vector<unique_ptr<ISQLJob>> _listCompleted SW_GUARDED_BY( _mutex );
        OnceInit<SQLConnectionPoolConfig>          _settings;
        mutable mutex                              _mutex;
        std::condition_variable_any                _jobReady;
        OnceInit<ISQLDriver*>                      _pDriver;
        int32 _pendingCount                        SW_GUARDED_BY( _mutex );
        uint8                                      _bStopping;
    };
} // namespace sw
