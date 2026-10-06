#include "pch.h"

#include "GameFramework/Base/Framework/SaveGame.h"

namespace sw
{
    uint64 SaveGame::saveToSlot( ILocalStore& store, string_view slot, const LocalStoreWriteOptions& options ) const
    {
        vector<uint8> bytes;
        if ( writeBytes( bytes ) == false )
        {
            SW_LOG_WARNING( "SaveGame could not serialize itself for slot %#", string( slot ).c_str() );
            return 0;
        }
        LocalStoreWriteOptions writeOptions = options;
        writeOptions._formatVersion         = getFormatVersion();
        return store.submitWrite( slot, std::move( bytes ), writeOptions );
    }

    bool SaveGame::loadFromCompletion( const LocalStoreCompletion& completion )
    {
        const bool bRead = completion._operation == LocalStoreOperation::Read && completion._result == LocalStoreResult::Ok;
        if ( bRead == false )
            return false;
        if ( completion._formatVersion != getFormatVersion() )
        {
            SW_LOG_WARNING( "SaveGame slot %# has format version %# (expected %#) — not read", completion._slot.c_str(), completion._formatVersion, getFormatVersion() );
            return false;
        }
        return readBytes( completion._bytes.data(), completion._bytes.size() );
    }
} // namespace sw
