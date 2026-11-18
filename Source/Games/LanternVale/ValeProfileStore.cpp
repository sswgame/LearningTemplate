#include "pch.h"

#include "Games/LanternVale/ValeProfileStore.h"

#include "Core/Serialization/BitStream.h"
#include "Core/String/StringBuilder.h"

namespace sw
{
    ValeProfileStore::ValeProfileStore()
        : _mapPendingRead{}
        , _mapPendingWrite{}
        , _mapStoredBytes{}
        , _setReleasedAccount{}
        , _listCompletionScratch{}
        , _pStore{ nullptr }
        , _savedCount{ 0 }
        , _skippedSaveCount{ 0 }
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
        (void)_setReleasedAccount.erase( accountID ); // 다시 들어왔다 — 읽기가 기억을 다시 채운다
        _mapPendingRead[_pStore->submitRead( makeSlotName( accountID ) )] = accountID;
    }

    void ValeProfileStore::releaseAccount( uint64 accountID )
    {
        if ( hasPendingWrite( accountID ) )
            (void)_setReleasedAccount.insert( accountID );
        else
            (void)_mapStoredBytes.erase( accountID );
    }

    bool ValeProfileStore::hasPendingWrite( uint64 accountID ) const
    {
        for ( const auto& entry : _mapPendingWrite )
        {
            if ( entry.second == accountID )
                return true;
        }
        return false;
    }

    bool ValeProfileStore::requestSave( uint64 accountID, const ValeProfile& profile )
    {
        if ( _pStore == nullptr )
            return false;
        BitWriter writer;
        profile.write( writer );
        vector<uint8> listBytes = writer.releaseBytes();
        // 견주기는 직렬화한 바이트로 한다 — 진행에 칸이 늘어도 `write` 가 싣는 한 놓치지 않는다.
        vector<uint8>& listStored = _mapStoredBytes[accountID];
        if ( listStored == listBytes )
        {
            ++_skippedSaveCount;
            return false;
        }
        listStored = listBytes;
        LocalStoreWriteOptions options;
        options._formatVersion                                                                               = ValeProfile::kFormatVersion;
        _mapPendingWrite[_pStore->submitWrite( makeSlotName( accountID ), std::move( listBytes ), options )] = accountID;
        return true;
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
                {
                    _mapStoredBytes.erase( accountID ); // 디스크 바이트를 모른다 — 다음 저장이 다시 쓴다
                    SW_LOG_ERROR_ID( "LanternVale.ProfileSaveFailed", "[LanternVale] profile of account %# was not saved (local store result %#)", accountID, static_cast<int32>( completion._result ) );
                }
                if ( _setReleasedAccount.count( accountID ) != 0 && hasPendingWrite( accountID ) == false )
                {
                    (void)_setReleasedAccount.erase( accountID );
                    (void)_mapStoredBytes.erase( accountID ); // 세션이 끝난 계정의 마지막 쓰기가 끝났다
                }
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
                if ( load._bOk == SW_TRUE )
                    _mapStoredBytes[load._accountID] = completion._bytes; // 그대로인 진행은 다음 자동 저장이 다시 쓰지 않는다
            }
            if ( load._bOk == SW_FALSE )
                SW_LOG_ERROR_ID( "LanternVale.ProfileLoadFailed", "[LanternVale] profile of account %# could not be read (local store result %#, format %#) - the login is refused so the stored progress is not overwritten",
                                 load._accountID, static_cast<int32>( completion._result ), completion._formatVersion );
            outListLoad.push_back( load );
        }
    }
} // namespace sw
