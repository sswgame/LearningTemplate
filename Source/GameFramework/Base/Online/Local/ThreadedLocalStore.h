/**
 * @file ThreadedLocalStore.h
 * @brief 디스크 일을 전용 스레드 하나에서 하는 로컬 저장 앞 — 바닥(파일 · SQLite)을 갖고 요청을 맡긴 순서대로 실행합니다.
 * @details 게임 스레드는 맡기고(`submit*`) 거두기만 한다(`pollCompletions`). 디스크를 기다리는 일이라 TaskManager 워커가 아니라 전용 스레드다
 *          (로그 · 파일 감시 · SQL 풀과 같은 규칙). `shutdown` 은 남은 쓰기 · 지우기를 끝까지 마치고 합류한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/deque.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/Base/Online/Local/LocalSlotStorage.h"
#include "GameFramework/GameFrameworkExports.h"

#include <condition_variable>
#include <thread>

namespace sw
{
    /**
     * @class ThreadedLocalStore
     * @brief 전용 스레드 하나 + 슬롯 바닥입니다.
     */
    class SW_GF_API ThreadedLocalStore final : public ILocalStore
    {
    public:
        /** @brief 바닥은 이 앞이 가지고, 봉인 창구는 빌려 쓴다. 스레드를 바로 띄운다. */
        ThreadedLocalStore( unique_ptr<ILocalSlotStorage> storage, const LocalSealContext& sealContext );
        ~ThreadedLocalStore() override;

        uint64 submitRead( string_view slot ) override;
        uint64 submitWrite( string_view slot, vector<uint8> bytes, const LocalStoreWriteOptions& options ) override;
        uint64 submitErase( string_view slot ) override;
        uint64 submitList( string_view groupPrefix ) override;
        int32  pollCompletions( vector<LocalStoreCompletion>& outListCompletion ) override;
        int32  getPendingCount() const override;
        void   shutdown() override;

    private:
        uint64 enqueueRequest( LocalStoreRequest request );
        void   runWorker();

        deque<LocalStoreRequest>      _listQueuedRequest;
        vector<LocalStoreCompletion>  _listCompletion;
        LocalSealContext              _sealContext;
        unique_ptr<ILocalSlotStorage> _storage;
        std::thread                   _worker;
        mutable mutex                 _mutex;
        std::condition_variable_any   _requestReady;
        uint64                        _nextRequestID;
        int32                         _pendingCount;
        uint8                         _bStopping;
    };
} // namespace sw
