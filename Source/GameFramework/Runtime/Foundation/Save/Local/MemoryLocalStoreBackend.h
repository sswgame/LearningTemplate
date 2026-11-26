/**
 * @file MemoryLocalStoreBackend.h
 * @brief 메모리 로컬 저장 백엔드입니다(테스트용). 슬롯별 봉투 바이트를 들고 "쓰기 도중 꺼짐" 을 주입합니다. 앞면은 `MemoryLocalStore` 입니다.
 */
#pragma once
#include "Core/Concurrency/mutex.h"
#include "Core/Container/map.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Save/Local/LocalStoreBackend.h"

namespace sw
{
    /**
     * @class MemoryLocalStoreBackend
     * @brief 슬롯별 봉투 바이트입니다. 잠금 하나로 직렬화합니다(프런트 여럿이 같이 쓴다).
     */
    class SW_GF_API MemoryLocalStoreBackend final : public ILocalStoreBackend
    {
    public:
        MemoryLocalStoreBackend();

        LocalStoreResult readSlot( const string& slot, vector<uint8>& outEnvelopeBytes ) override;
        /** @brief 직전 세대를 두지 않는다 — 메모리 쓰기는 잠금 안의 바꿔 넣기라 반쪽이 없다. 늘 NotFound 다. */
        LocalStoreResult readPreviousSlot( const string& slot, vector<uint8>& outEnvelopeBytes ) override;
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

        mutable mutex                   _mutex;
        map<string, SlotEntry> _mapSlot SW_GUARDED_BY( _mutex );
        uint8 _bFailNextWrite           SW_GUARDED_BY( _mutex );
    };
} // namespace sw
