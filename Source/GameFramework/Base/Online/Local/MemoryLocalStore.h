/**
 * @file MemoryLocalStore.h
 * @brief 메모리 로컬 저장소입니다. 테스트용입니다.
 *        데이터(`MemoryLocalDatabase`: 슬롯별 봉투 바이트, "쓰기 도중 꺼짐" 주입)와 프런트(`MemoryLocalStore`: 요청 즉시 실행)가 나뉩니다.
 * @details 프런트를 없애고 새 프런트를 같은 데이터에 붙이면 "다시 켠 게임" 이다. 꺼짐 주입은 다음 쓰기를 적지 않고 IOError 로 끝낸다. 다시 켜면 옛 내용이다(원자 쓰기의 계약).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/IDAllocator.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Local/LocalSlotStorage.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class MemoryLocalDatabase
     * @brief 슬롯별 봉투 바이트입니다. 잠금 하나로 직렬화합니다(프런트 여럿이 같이 쓴다).
     */
    class SW_GF_API MemoryLocalDatabase final : public ILocalSlotStorage
    {
    public:
        MemoryLocalDatabase();

        LocalStoreResult readSlot( const string& slot, vector<uint8>& outEnvelopeBytes ) override;
        LocalStoreResult writeSlot( const string& slot, const vector<uint8>& envelopeBytes ) override;
        LocalStoreResult eraseSlot( const string& slot ) override;
        LocalStoreResult listSlots( const string& groupPrefix, vector<LocalSlotInfo>& outListSlotInfo ) override;

        /** @brief 다음 쓰기 하나를 도중에 꺼진 것처럼 만듭니다. 적지 않고 IOError 로 끝냅니다. */
        void failNextWrite();
        /** @brief 봉투 바이트를 직접 고칩니다(변조 테스트). 없는 슬롯이면 false. */
        [[nodiscard]] bool modifyEnvelopeByte( const string& slot, size_t offset, uint8 xorMask );
        /** @brief 봉투 바이트를 그대로 봅니다(평문이 보이지 않는지 테스트). */
        [[nodiscard]] bool findEnvelope( const string& slot, vector<uint8>& outEnvelopeBytes ) const;

    private:
        struct SlotEntry
        {
            vector<uint8> _envelope{};
            int64         _writtenAtMs{ 0 };
        };

        mutable mutex          _mutex;
        map<string, SlotEntry> _mapSlot;
        uint8                  _bFailNextWrite;
    };
} // namespace sw

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
        MemoryLocalStore( MemoryLocalDatabase* pDatabase, const LocalSealContext& sealContext );

        uint64 submitRead( string_view slot ) override;
        uint64 submitWrite( string_view slot, vector<uint8> bytes, const LocalStoreWriteOptions& options ) override;
        uint64 submitErase( string_view slot ) override;
        uint64 submitList( string_view groupPrefix ) override;
        int32  pollCompletions( vector<LocalStoreCompletion>& outListCompletion ) override;
        int32  getPendingCount() const override { return static_cast<int32>( _listCompletion.size() ); }
        void   shutdown() override;

    private:
        uint64 executeRequest( LocalStoreRequest& request );

        vector<LocalStoreCompletion> _listCompletion;
        LocalSealContext             _sealContext;
        MemoryLocalDatabase*         _pDatabase;
        IDAllocator<uint64>          _requestIDAllocator;
        uint8                        _bShutdown;
    };
} // namespace sw
