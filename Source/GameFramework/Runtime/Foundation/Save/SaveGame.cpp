#include "pch.h"

#include "GameFramework/Runtime/Foundation/Save/SaveGame.h"

namespace sw
{
    uint64 SaveGame::saveToSlot( ILocalStore& store, string_view slot, const LocalStoreWriteOptions& options ) const
    {
        vector<uint8> bytes;
        if ( writeBytes( bytes ) == false )
        {
            SW_LOG_WARNING_ID( "SaveGame.SaveGameCouldNotSerialize", "SaveGame could not serialize itself for slot %#", string( slot ).c_str() );
            return 0;
        }
        LocalStoreWriteOptions writeOptions = options;
        writeOptions._formatVersion         = getFormat()._version;
        return store.submitWrite( slot, std::move( bytes ), writeOptions );
    }

    bool SaveGame::loadFromCompletion( const LocalStoreCompletion& completion )
    {
        const bool bRead = completion._operation == LocalStoreOperation::Read && completion._result == LocalStoreResult::Ok;
        if ( bRead == false )
            return false;
        // 옛 판: 사본을 형식의 단계로 올린 뒤 현재 리더가 읽는다(언리얼 FCustomVersion 자리). 새 판 · 단계 없는 판은 읽지 않는다.
        vector<uint8> bytes = completion._bytes;
        if ( getFormat().raise( completion._formatVersion, bytes ) == false )
        {
            SW_LOG_WARNING_ID( "SaveGame.SaveGameSlotFormatVersion", "SaveGame slot %# has format version %# that could not be upgraded — not read", completion._slot.c_str(), completion._formatVersion );
            return false;
        }
        return readBytes( bytes.data(), bytes.size() );
    }
} // namespace sw
