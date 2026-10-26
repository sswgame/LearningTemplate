/**
 * @file ThreadedLocalStore.h
 * @brief 디스크 작업을 전용 스레드 하나에서 하는 로컬 저장 프런트입니다. 백엔드(파일, SQLite)를 소유하고 요청을 받은 순서대로 실행합니다.
 * @details 게임 스레드는 요청하고(`submit*`) 회수만 한다(`pollCompletions`). 디스크를 기다리는 일이라 TaskManager 워커가 아니라 전용 스레드다
 *          (로그, 파일 감시, SQL 풀과 같은 규칙). `shutdown` 은 남은 쓰기와 지우기를 끝까지 마치고 합류한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/IDAllocator.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/DedicatedWorker.h"

#include "GameFramework/Base/Online/Local/LocalSlotStorage.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class ThreadedLocalStore
     * @brief 전용 스레드 하나 + 슬롯 바닥입니다.
     */
    class SW_GF_API ThreadedLocalStore final : public ILocalStore
    {
    public:
        /** @brief 백엔드는 이 프런트가 소유하고, 봉인 인터페이스는 빌려 씁니다. 스레드를 바로 시작합니다. */
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
        /** @brief 작업 스레드에서 요청 하나를 실행해 완료를 쌓습니다. 종료 중에도 쓰기 · 지우기는 끝까지 하고, 읽기 · 나열은 받을 사람이 없어 실패로 끝냅니다. */
        void processRequest( LocalStoreRequest& request, bool bStopping );

        vector<LocalStoreCompletion>       _listCompletion;
        LocalSealContext                   _sealContext;
        unique_ptr<ILocalSlotStorage>      _storage;
        DedicatedWorker<LocalStoreRequest> _worker;
        mutable mutex                      _mutex; ///< 완료 목록과 대기 수를 보호한다
        IDAllocator<uint64>                _requestIDAllocator;
        int32                              _pendingCount;
    };
} // namespace sw
