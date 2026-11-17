#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/Local/LocalSlotStorage.h"

namespace sw
{
    bool LocalStoreRequest::makeInvalidCompletion( const LocalStoreRequest& request, LocalStoreCompletion& outCompletion )
    {
        bool bValid = false;
        switch ( request._operation )
        {
            case LocalStoreOperation::Read:
            case LocalStoreOperation::Erase:
            {
                bValid = ILocalStore::isValidSlotName( request._slot );
                break;
            }
            case LocalStoreOperation::Write:
            {
                bValid = ILocalStore::isValidSlotName( request._slot ) && static_cast<int64>( request._bytes.size() ) <= ILocalStore::kMaxSlotSize;
                break;
            }
            case LocalStoreOperation::List:
            {
                bValid = ILocalStore::isValidGroupPrefix( request._slot );
                break;
            }
        }
        if ( bValid )
            return false;
        outCompletion = makeCompletion( request, LocalStoreResult::Invalid );
        return true;
    }

    LocalStoreCompletion LocalStoreRequest::execute( LocalStoreRequest& request, ILocalSlotStorage& storage, const LocalSealContext& sealContext )
    {
        LocalStoreCompletion completion;
        if ( makeInvalidCompletion( request, completion ) )
            return completion;
        completion = makeCompletion( request, LocalStoreResult::Ok );
        switch ( request._operation )
        {
            case LocalStoreOperation::Read:
            {
                vector<uint8> envelopeBytes;
                completion._result = storage.readSlot( request._slot, envelopeBytes );
                if ( completion._result == LocalStoreResult::Ok )
                    completion._result = LocalSealContext::open( sealContext, envelopeBytes.data(), envelopeBytes.size(), completion._bytes, completion._formatVersion );
                if ( completion._result == LocalStoreResult::Corrupt )
                {
                    // 지금 봉투가 깨졌다(장치 결함 · 밖에서 고친 파일) — 바닥이 남긴 직전 세대로 돌아간다. 그것도 지면 Corrupt 그대로다.
                    vector<uint8> previousBytes;
                    vector<uint8> previousBody;
                    uint32        previousVersion = 0;
                    const bool    bRecovered      = storage.readPreviousSlot( request._slot, previousBytes ) == LocalStoreResult::Ok &&
                                            LocalSealContext::open( sealContext, previousBytes.data(), previousBytes.size(), previousBody, previousVersion ) == LocalStoreResult::Ok;
                    if ( bRecovered )
                    {
                        SW_LOG_WARNING_ID( "LocalStore.ReadPreviousGeneration", "Local store slot '%#' is corrupt - read its previous generation instead (the last write is lost)",
                                           request._slot.c_str() );
                        completion._result        = LocalStoreResult::Ok;
                        completion._bytes         = std::move( previousBody );
                        completion._formatVersion = previousVersion;
                    }
                }
                break;
            }
            case LocalStoreOperation::Write:
            {
                vector<uint8> envelopeBytes;
                completion._result = LocalSealContext::seal( sealContext, request._bytes.data(), request._bytes.size(), request._options, envelopeBytes );
                if ( completion._result == LocalStoreResult::Ok )
                    completion._result = storage.writeSlot( request._slot, envelopeBytes );
                break;
            }
            case LocalStoreOperation::Erase:
            {
                completion._result = storage.eraseSlot( request._slot );
                break;
            }
            case LocalStoreOperation::List:
            {
                completion._result = storage.listSlots( request._slot, completion._listSlotInfo );
                break;
            }
        }
        return completion;
    }

    LocalStoreCompletion LocalStoreRequest::makeCompletion( const LocalStoreRequest& request, LocalStoreResult result )
    {
        LocalStoreCompletion completion;
        completion._slot      = request._slot;
        completion._requestID = request._requestID;
        completion._result    = result;
        completion._operation = request._operation;
        return completion;
    }
} // namespace sw
