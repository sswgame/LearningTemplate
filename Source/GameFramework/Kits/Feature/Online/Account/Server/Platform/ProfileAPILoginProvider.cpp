#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/ProfileAPILoginProvider.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Runtime/Online/HTTP/HTTPClient.h"

namespace sw
{
    ProfileAPILoginProvider::ProfileAPILoginProvider( const PlatformLoginProviderConfig& settings, HTTPClient* pHTTPClient )
        : _settings{ settings }
        , _mapRequestToVerification{}
        , _listDone{}
        , _pHTTPClient{ pHTTPClient }
        , _verificationIDAllocator{}
    {
    }

    uint64 ProfileAPILoginProvider::submitVerification( const vector<uint8>& ticketBytes, UnixMilliseconds /*nowMs*/ )
    {
        const uint64      verificationID = _verificationIDAllocator.allocate();
        HTTPClientRequest request;
        request._url       = _settings._profileURL;
        request._timeoutMs = _settings._requestTimeoutMs;
        request._listHeader.push_back( HTTPHeader{ "Authorization", "Bearer " + string( reinterpret_cast<const utf8*>( ticketBytes.data() ), ticketBytes.size() ) } );
        request._listHeader.push_back( HTTPHeader{ "Accept", "application/json" } );
        _mapRequestToVerification[_pHTTPClient->submitRequest( request )] = verificationID;
        return verificationID;
    }

    int32 ProfileAPILoginProvider::pollVerifications( vector<PlatformLoginVerification>& outListVerification )
    {
        const int32 count = static_cast<int32>( _listDone.size() );
        for ( PlatformLoginVerification& verification : _listDone )
        {
            outListVerification.push_back( std::move( verification ) );
        }
        _listDone.clear();
        return count;
    }

    void ProfileAPILoginProvider::update( UnixMilliseconds /*nowMs*/ )
    {
        _pHTTPClient->update();
        vector<HTTPClientResponse> listResponse;
        (void)_pHTTPClient->pollResponses( listResponse );
        for ( const HTTPClientResponse& response : listResponse )
        {
            const auto requestIt = _mapRequestToVerification.find( response._requestID );
            if ( requestIt == _mapRequestToVerification.end() )
                continue;
            PlatformLoginVerification& verification = _listDone.emplace_back();
            verification._verificationID            = requestIt->second;
            _mapRequestToVerification.erase( requestIt );
            const bool bDenied = response._statusCode == HTTPConstant::kStatusUnauthorized || response._statusCode == HTTPConstant::kStatusForbidden;
            if ( bDenied )
            {
                verification._bRejected = SW_TRUE;
                continue;
            }
            JSONDocument document;
            if ( response.isSuccess() == false || document.tryParse( response.getBodyText() ) == false )
            {
                verification._bUnavailable = SW_TRUE;
                continue;
            }
            if ( document.getRoot().findPathText( _settings._subjectPath, verification._subject ) == false || verification._subject.empty() )
            {
                verification._subject.clear();
                verification._bRejected = SW_TRUE;
                continue;
            }
            if ( _settings._displayNamePath.empty() == false )
                (void)document.getRoot().findPathText( _settings._displayNamePath, verification._displayName ); // 선택 값이다 — 없으면 비워 둔다
        }
    }
} // namespace sw
