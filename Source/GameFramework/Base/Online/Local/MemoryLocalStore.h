/**
 * @file MemoryLocalStore.h
 * @brief 메모리 로컬 저장 — 시험용입니다. 데이터(`MemoryLocalDatabase` — 슬롯 → 봉투 바이트, "쓰기 도중 꺼짐" 주입)와 앞(`MemoryLocalStore` — 맡기는 자리에서 실행)이 나뉩니다.
 * @details 앞을 없애고 새 앞을 같은 데이터에 붙이면 "다시 켠 게임" 이다. 꺼짐 주입은 다음 쓰기를 적지 않고 IOError 로 끝낸다 — 다시 켜면 옛 내용이다(원자 쓰기의 약속).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Local/LocalSlotStorage.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class MemoryLocalDatabase
     * @brief 슬롯 → 봉투 바이트입니다. 잠금 하나로 줄 세운다(앞 여럿이 같이 쓴다).
     */
    class SW_GF_API MemoryLocalDatabase final : public ILocalSlotStorage
    {
    public:
        MemoryLocalDatabase();

        LocalStoreResult readSlot( const string& slot, vector<uint8>& outEnvelopeBytes ) override;
        LocalStoreResult writeSlot( const string& slot, const vector<uint8>& envelopeBytes ) override;
        LocalStoreResult eraseSlot( const string& slot ) override;
        LocalStoreResult listSlots( const string& groupPrefix, vector<LocalSlotInfo>& outListSlotInfo ) override;

        /** @brief 다음 쓰기 하나가 도중에 꺼진 것처럼 — 적지 않고 IOError 입니다. */
        void failNextWrite();
        /** @brief 봉투 바이트를 직접 고칩니다(변조 시험). 없는 슬롯이면 false. */
        [[nodiscard]] bool modifyEnvelopeByte( const string& slot, size_t offset, uint8 xorMask );
        /** @brief 봉투 바이트를 그대로 봅니다(평문이 보이지 않는지 시험). */
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
     * @brief 메모리 데이터의 앞입니다. 맡기는 자리에서 실행하고 완료는 `pollCompletions` 까지 쌓는다.
     */
    class SW_GF_API MemoryLocalStore final : public ILocalStore
    {
    public:
        /** @brief @p pDatabase 는 빌려 쓴다(앞보다 오래 산다). 봉인 창구도 빌려 쓴다. */
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
        uint64                       _nextRequestId;
        uint8                        _bShutdown;
    };
} // namespace sw
