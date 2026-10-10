#include "pch.h"

#include "GameFramework/Base/Online/Local/LocalSlotStorage.h"

namespace sw
{
    bool LocalStoreRequestUtil::makeInvalidCompletion( const LocalStoreRequest& request, LocalStoreCompletion& outCompletion )
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

    LocalStoreCompletion LocalStoreRequestUtil::execute( LocalStoreRequest& request, ILocalSlotStorage& storage, const LocalSealContext& sealContext )
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
                    completion._result = LocalSlotEnvelope::open( sealContext, envelopeBytes.data(), envelopeBytes.size(), completion._bytes, completion._formatVersion );
                break;
            }
            case LocalStoreOperation::Write:
            {
                vector<uint8> envelopeBytes;
                completion._result = LocalSlotEnvelope::seal( sealContext, request._bytes.data(), request._bytes.size(), request._options, envelopeBytes );
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

    LocalStoreCompletion LocalStoreRequestUtil::makeCompletion( const LocalStoreRequest& request, LocalStoreResult result )
    {
        LocalStoreCompletion completion;
        completion._slot      = request._slot;
        completion._requestID = request._requestID;
        completion._result    = result;
        completion._operation = request._operation;
        return completion;
    }
} // namespace sw
