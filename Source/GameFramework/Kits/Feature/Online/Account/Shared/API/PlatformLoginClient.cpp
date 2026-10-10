#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Shared/API/PlatformLoginClient.h"

namespace sw
{
    FakePlatformLoginClient::FakePlatformLoginClient()
        : _listPreset{}
        , _listPending{}
        , _listDone{}
        , _nextRequestId{ 1 }
    {
    }

    void FakePlatformLoginClient::setTicket( string_view provider, string_view ticketText )
    {
        for ( Preset& preset : _listPreset )
        {
            if ( preset._provider == provider )
            {
                preset._ticketText = string( ticketText );
                return;
            }
        }
        _listPreset.push_back( Preset{ string( provider ), string( ticketText ) } );
    }

    uint64 FakePlatformLoginClient::beginLogin( string_view provider, int64 nowMs )
    {
        (void)nowMs;
        PlatformLoginClientResult& result = _listPending.emplace_back();
        result._requestId                 = _nextRequestId++;
        result._provider                  = string( provider );
        return result._requestId;
    }

    void FakePlatformLoginClient::tick( int64 nowMs )
    {
        (void)nowMs;
        for ( PlatformLoginClientResult& result : _listPending )
        {
            const Preset* pPreset = nullptr;
            for ( const Preset& preset : _listPreset )
            {
                if ( preset._provider == result._provider )
                    pPreset = &preset;
            }
            if ( pPreset == nullptr )
            {
                result._failureText = "no ticket for this provider";
            }
            else
            {
                result._ticket.assign( pPreset->_ticketText.begin(), pPreset->_ticketText.end() );
                result._bSucceeded = SW_TRUE;
            }
            _listDone.push_back( std::move( result ) );
        }
        _listPending.clear();
    }

    int32 FakePlatformLoginClient::pollResults( vector<PlatformLoginClientResult>& outListResult )
    {
        const int32 count = static_cast<int32>( _listDone.size() );
        for ( PlatformLoginClientResult& result : _listDone )
        {
            outListResult.push_back( std::move( result ) );
        }
        _listDone.clear();
        return count;
    }
} // namespace sw
