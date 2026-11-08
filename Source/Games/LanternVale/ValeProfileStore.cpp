#include "pch.h"

#include "Games/LanternVale/ValeProfileStore.h"

#include "Core/Serialization/BitStream.h"
#include "Core/String/StringBuilder.h"

namespace sw
{
    ValeProfileStore::ValeProfileStore()
        : _mapPendingRead{}
        , _mapPendingWrite{}
        , _listCompletionScratch{}
        , _pStore{ nullptr }
        , _savedCount{ 0 }
    {
    }

    string ValeProfileStore::makeSlotName( uint64 accountID )
    {
        StringBuilder<constant::buffer::kMax256> resultBuilder;
        resultBuilder.appendFormat( "vale/a%#", accountID );
        return string( resultBuilder.view() );
    }

    void ValeProfileStore::requestLoad( uint64 accountID )
    {
        if ( _pStore == nullptr )
            return;
        _mapPendingRead[_pStore->submitRead( makeSlotName( accountID ) )] = accountID;
    }

    void ValeProfileStore::requestSave( uint64 accountID, const ValeProfile& profile )
    {
        if ( _pStore == nullptr )
            return;
        BitWriter writer;
        profile.write( writer );
        LocalStoreWriteOptions options;
        options._formatVersion                                                                              = ValeProfile::kFormatVersion;
        _mapPendingWrite[_pStore->submitWrite( makeSlotName( accountID ), writer.releaseBytes(), options )] = accountID;
    }

    void ValeProfileStore::poll( vector<ValeProfileLoad>& outListLoad )
    {
        if ( _pStore == nullptr )
            return;
        _listCompletionScratch.clear();
        (void)_pStore->pollCompletions( _listCompletionScratch );
        for ( const LocalStoreCompletion& completion : _listCompletionScratch )
        {
            if ( completion._operation == LocalStoreOperation::Write )
            {
                const auto   iter      = _mapPendingWrite.find( completion._requestID );
                const uint64 accountID = iter != _mapPendingWrite.end() ? iter->second : 0;
                if ( iter != _mapPendingWrite.end() )
                    _mapPendingWrite.erase( iter );
                if ( completion._result == LocalStoreResult::Ok )
                {
                    ++_savedCount;
                    SW_LOG_INFO_ID( "LanternVale.ProfileSaved", "[LanternVale] profile of account %# saved", accountID );
                }
                else
                    SW_LOG_ERROR_ID( "LanternVale.ProfileSaveFailed", "[LanternVale] profile of account %# was not saved (local store result %#)", accountID, static_cast<int32>( completion._result ) );
                continue;
            }
            const auto iter = _mapPendingRead.find( completion._requestID );
            if ( completion._operation != LocalStoreOperation::Read || iter == _mapPendingRead.end() )
                continue;
            ValeProfileLoad load;
            load._accountID = iter->second;
            _mapPendingRead.erase( iter );
            if ( completion._result == LocalStoreResult::NotFound )
                load._bOk = SW_TRUE; // 새 계정 — 기본 진행
            else if ( completion._result == LocalStoreResult::Ok && completion._formatVersion == ValeProfile::kFormatVersion )
            {
                BitReader reader( completion._bytes.data(), static_cast<int32>( completion._bytes.size() ) );
                load._bOk    = load._profile.read( reader ) ? SW_TRUE : SW_FALSE;
                load._bFound = load._bOk;
            }
            if ( load._bOk == SW_FALSE )
                SW_LOG_ERROR_ID( "LanternVale.ProfileLoadFailed", "[LanternVale] profile of account %# could not be read (local store result %#, format %#) - the login is refused so the stored progress is not overwritten",
                                 load._accountID, static_cast<int32>( completion._result ), completion._formatVersion );
            outListLoad.push_back( load );
        }
    }
} // namespace sw
