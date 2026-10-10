#include "pch.h"

#include "GameFramework/Kits/Feature/Online/LiveOps/Server/Push/Provider/Fake/FakePushProvider.h"

namespace sw
{
    FakePushProvider::FakePushProvider()
        : _listSent{}
        , _listPending{}
        , _mapTokenToScript{}
    {
    }

    void FakePushProvider::scriptResult( string_view deviceToken, PushDeliveryStatus status, int32 count, int64 retryAfterMs )
    {
        Script& script         = _mapTokenToScript[string( deviceToken )];
        script._status         = status;
        script._remainingCount = count;
        script._retryAfterMs   = retryAfterMs;
    }

    void FakePushProvider::send( uint64 deliveryID, const string& deviceToken, const string& locale, const PushNotificationMessage& message )
    {
        (void)locale;
        _listSent.push_back( SentRecord{ message, deviceToken, deliveryID } );
        PushDeliveryResult result;
        result._deliveryID  = deliveryID;
        const auto scriptIt = _mapTokenToScript.find( deviceToken );
        if ( scriptIt != _mapTokenToScript.end() && scriptIt->second._remainingCount > 0 )
        {
            result._status       = scriptIt->second._status;
            result._retryAfterMs = scriptIt->second._retryAfterMs;
            --scriptIt->second._remainingCount;
        }
        _listPending.push_back( result );
    }

    int32 FakePushProvider::pollResults( vector<PushDeliveryResult>& outListResult )
    {
        const int32 count = static_cast<int32>( _listPending.size() );
        outListResult.insert( outListResult.end(), _listPending.begin(), _listPending.end() );
        _listPending.clear();
        return count;
    }
} // namespace sw
