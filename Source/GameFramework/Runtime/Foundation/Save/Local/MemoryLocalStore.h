/**
 * @file MemoryLocalStore.h
 * @brief 메모리 로컬 저장소의 프런트(`MemoryLocalStore`: 요청 즉시 실행)입니다. 테스트용이고, 데이터는 `MemoryLocalStoreBackend` 가 듭니다.
 * @details 프런트를 없애고 새 프런트를 같은 데이터에 붙이면 "다시 켠 게임" 이다. 꺼짐 주입은 다음 쓰기를 적지 않고 IOError 로 끝낸다. 다시 켜면 옛 내용이다(원자 쓰기의 계약).
 */
#pragma once
#include "Core/Container/IDAllocator.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Save/Local/LocalStoreBackend.h"
#include "GameFramework/Runtime/Foundation/Save/Local/MemoryLocalStoreBackend.h"

namespace sw
{
    /**
     * @class MemoryLocalStore
     * @brief 메모리 데이터의 프런트입니다. 요청 즉시 실행하고 완료는 `pollCompletions` 까지 쌓습니다.
     */
    class SW_GF_API MemoryLocalStore final : public ILocalStore
    {
    public:
        /** @brief @p pDatabase 는 빌려 쓰므로 프런트보다 오래 살아야 합니다. 봉인 인터페이스도 빌려 씁니다. */
        MemoryLocalStore( MemoryLocalStoreBackend* pDatabase, const LocalSealContext& sealContext );

        uint64 submitRead( string_view slot ) override;
        uint64 submitWrite( string_view slot, vector<uint8> bytes, const LocalStoreWriteOptions& options ) override;
        uint64 submitErase( string_view slot ) override;
        uint64 submitList( string_view groupPrefix ) override;
        int32  pollCompletions( vector<LocalStoreCompletion>& outListCompletion ) override;
        int32  getPendingCount() const override { return static_cast<int32>( _completionBuffer.getCount() ); }
        void   shutdown() override;

    private:
        uint64 executeRequest( LocalStoreRequest& request );

    private:
        EventBuffer<LocalStoreCompletion> _completionBuffer; ///< 끝난 요청 — `pollCompletions` 가 가져간다
        LocalSealContext                  _sealContext;
        MemoryLocalStoreBackend*          _pDatabase;
        IDAllocator<uint64>               _requestIDAllocator;
        uint8                             _bShutdown;
    };
} // namespace sw
