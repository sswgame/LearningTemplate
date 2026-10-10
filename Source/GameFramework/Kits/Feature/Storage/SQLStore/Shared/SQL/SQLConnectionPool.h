/**
 * @file SQLConnectionPool.h
 * @brief SQL 연결 풀 — 전용 워커 스레드 N 개가 각자 연결 하나를 열어 일 큐를 비우고, 끝난 일은 완료 큐에 쌓습니다(`pollCompletions` 를 부른 스레드가 complete).
 * @details DB 를 기다리는 일이라 TaskManager 워커가 아니라 전용 스레드다(로그 · 파일 감시 · 네트워크 스레드와 같은 규칙). 연결이 끊기면 그 워커가 다음 일 전에
 *          다시 연다(지수 물러남, 최대 5 초) — 다시 열 동안의 일은 닫힌 연결로 돌아 `ConnectionLost` 를 본다(멈춰 기다리지 않는다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
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
     * @brief 풀의 일 하나 — `run` 은 워커, `complete` 는 거두는 스레드입니다.
     */
    class SW_GF_API ISQLJob
    {
    public:
        ISQLJob()          = default;
        virtual ~ISQLJob() = default;

        ISQLJob( const ISQLJob& )            = delete;
        ISQLJob& operator=( const ISQLJob& ) = delete;

        virtual void run( ISQLConnection& connection ) = 0;
        virtual void complete()                        = 0;
    };
} // namespace sw

namespace sw
{
    /** @brief 풀 설정입니다. */
    struct SQLConnectionPoolSettings
    {
        string _connection{};     ///< 드라이버 고유 접속 글(비밀 없음)
        string _secret{};         ///< 비밀번호 — 연결을 다시 열 때도 쓰므로 풀이 들고 있다
        int32  _workerCount{ 2 }; ///< 연결 수(SQLite 는 쓰기가 하나뿐이라 2 로 충분 — 읽기 하나 · 쓰기 하나)
    };
} // namespace sw

namespace sw
{
    /**
     * @class SQLConnectionPool
     * @brief 연결 풀입니다. `submit` 은 아무 스레드, `pollCompletions` 는 맡긴 서비스 스레드 하나.
     */
    class SW_GF_API SQLConnectionPool
    {
    public:
        SQLConnectionPool();
        ~SQLConnectionPool();

        SQLConnectionPool( const SQLConnectionPool& )            = delete;
        SQLConnectionPool& operator=( const SQLConnectionPool& ) = delete;

        /** @brief 이 스레드에서 연결 하나를 열어 보고(기동 오류를 바로 알린다) 워커를 띄웁니다. 열지 못하면 false 와 까닭(폴백 없음). */
        [[nodiscard]] bool initialize( ISQLDriver* pDriver, const SQLConnectionPoolSettings& settings, string& outError );
        /** @brief 새 일을 막고, 남은 일은 닫힌 연결로 돌려 완료 큐에 넣은 뒤 워커를 합류합니다. 그 뒤 `pollCompletions` 한 번이 모두 거둔다. */
        void shutdown();

        void  submit( unique_ptr<ISQLJob> job );
        int32 pollCompletions();
        /** @brief 맡았지만 아직 거두지 않은 일의 수입니다. */
        int32 getPendingCount() const;

    private:
        void runWorker( int32 workerIndex );

        vector<std::thread>         _listWorker;
        deque<unique_ptr<ISQLJob>>  _listQueuedJob;
        vector<unique_ptr<ISQLJob>> _listCompleted;
        SQLConnectionPoolSettings   _settings;
        mutable mutex               _mutex;
        std::condition_variable_any _jobReady;
        ISQLDriver*                 _pDriver;
        int32                       _pendingCount;
        uint8                       _bStopping;
    };
} // namespace sw
