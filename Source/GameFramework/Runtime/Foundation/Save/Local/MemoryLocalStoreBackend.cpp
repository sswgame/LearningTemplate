#include "pch.h"

#include "GameFramework/Runtime/Foundation/Save/Local/MemoryLocalStoreBackend.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    MemoryLocalStoreBackend::MemoryLocalStoreBackend()
        : _mutex{}
        , _mapSlot{}
        , _bFailNextWrite{ SW_FALSE }
    {
    }

    LocalStoreResult MemoryLocalStoreBackend::readPreviousSlot( const string& slot, vector<uint8>& outEnvelopeBytes )
    {
        (void)slot;
        (void)outEnvelopeBytes;
        return LocalStoreResult::NotFound;
    }

    LocalStoreResult MemoryLocalStoreBackend::readSlot( const string& slot, vector<uint8>& outEnvelopeBytes )
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        slotIt = _mapSlot.find( slot );
        if ( slotIt == _mapSlot.end() )
            return LocalStoreResult::NotFound;
        outEnvelopeBytes = slotIt->second._envelope;
        return LocalStoreResult::Ok;
    }

    LocalStoreResult MemoryLocalStoreBackend::writeSlot( const string& slot, const vector<uint8>& envelopeBytes )
    {
        ScopedLock<mutex> lock{ _mutex };
        if ( _bFailNextWrite == SW_TRUE )
        {
            _bFailNextWrite = SW_FALSE;
            return LocalStoreResult::IOError;
        }
        SlotEntry& entry   = _mapSlot[slot];
        entry._envelope    = envelopeBytes;
        entry._writtenAtMs = FileUtil::getCurrentFileWriteTime() / ( FileUtil::kFileTimeTicksPerSecond / constant::time::kMillisecondsPerSecond );
        return LocalStoreResult::Ok;
    }

    LocalStoreResult MemoryLocalStoreBackend::eraseSlot( const string& slot )
    {
        ScopedLock<mutex> lock{ _mutex };
        return _mapSlot.erase( slot ) > 0 ? LocalStoreResult::Ok : LocalStoreResult::NotFound;
    }

    LocalStoreResult MemoryLocalStoreBackend::listSlots( const string& groupPrefix, vector<LocalSlotInfo>& outListSlotInfo )
    {
        ScopedLock<mutex> lock{ _mutex };
        for ( const auto& [slot, entry] : _mapSlot )
        {
            if ( StringUtil::startsWith( slot, groupPrefix ) == false )
                continue;
            LocalSlotInfo& slotInfo = outListSlotInfo.emplace_back();
            slotInfo._slot          = slot;
            slotInfo._byteCount     = static_cast<int64>( entry._envelope.size() );
            slotInfo._writtenAtMs   = entry._writtenAtMs;
        }
        return LocalStoreResult::Ok;
    }

    void MemoryLocalStoreBackend::failNextWrite()
    {
        ScopedLock<mutex> lock{ _mutex };
        _bFailNextWrite = SW_TRUE;
    }

    bool MemoryLocalStoreBackend::modifyEnvelopeByte( const string& slot, size_t offset, uint8 xorMask )
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        slotIt = _mapSlot.find( slot );
        if ( slotIt == _mapSlot.end() || offset >= slotIt->second._envelope.size() )
            return false;
        slotIt->second._envelope[offset] ^= xorMask;
        return true;
    }

    bool MemoryLocalStoreBackend::findEnvelope( const string& slot, vector<uint8>& outEnvelopeBytes ) const
    {
        ScopedLock<mutex> lock{ _mutex };
        const auto        slotIt = _mapSlot.find( slot );
        if ( slotIt == _mapSlot.end() )
            return false;
        outEnvelopeBytes = slotIt->second._envelope;
        return true;
    }
} // namespace sw
