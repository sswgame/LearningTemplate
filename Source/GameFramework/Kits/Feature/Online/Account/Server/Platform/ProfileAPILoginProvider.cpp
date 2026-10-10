#include "pch.h"

#include "GameFramework/Kits/Feature/Online/Account/Server/Platform/ProfileAPILoginProvider.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

#include "GameFramework/Base/Online/HTTP/HTTPClient.h"

namespace sw
{
    ProfileAPILoginProvider::ProfileAPILoginProvider( const PlatformLoginProviderSettings& settings, HTTPClient* pHTTPClient )
        : _settings{ settings }
        , _mapRequestToVerification{}
        , _listDone{}
        , _pHTTPClient{ pHTTPClient }
        , _nextVerificationId{ 1 }
    {
    }

    uint64 ProfileAPILoginProvider::submitVerification( const vector<uint8>& ticketBytes, int64 nowMs )
    {
        const uint64      verificationId = _nextVerificationId++;
        HTTPClientRequest request;
        request._url       = _settings._profileUrl;
        request._timeoutMs = _settings._requestTimeoutMs;
        request._listHeader.push_back( HTTPHeader{ "Authorization", "Bearer " + string( reinterpret_cast<const utf8*>( ticketBytes.data() ), ticketBytes.size() ) } );
        request._listHeader.push_back( HTTPHeader{ "Accept", "application/json" } );
        _mapRequestToVerification[_pHTTPClient->submitRequest( request, nowMs )] = verificationId;
        return verificationId;
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

    void ProfileAPILoginProvider::tick( int64 nowMs )
    {
        _pHTTPClient->tick( nowMs );
        vector<HTTPClientResponse> listResponse;
        (void)_pHTTPClient->pollResponses( listResponse );
        for ( const HTTPClientResponse& response : listResponse )
        {
            const auto requestIt = _mapRequestToVerification.find( response._requestId );
            if ( requestIt == _mapRequestToVerification.end() )
                continue;
            PlatformLoginVerification& verification = _listDone.emplace_back();
            verification._verificationId            = requestIt->second;
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
            if ( findPathText( document.getRoot(), _settings._subjectPath, verification._subject ) == false || verification._subject.empty() )
            {
                verification._subject.clear();
                verification._bRejected = SW_TRUE;
                continue;
            }
            if ( _settings._displayNamePath.empty() == false )
                (void)findPathText( document.getRoot(), _settings._displayNamePath, verification._displayName );
        }
    }

    bool ProfileAPILoginProvider::findPathText( const JSONValue& root, string_view path, string& outText )
    {
        JSONValue current = root;
        while ( path.empty() == false )
        {
            const size_t      dot  = path.find( '.' );
            const string_view part = path.substr( 0, dot );
            path                   = dot == string_view::npos ? string_view{} : path.substr( dot + 1 );
            if ( current.isObject() == false )
                return false;
            current = current.get( part, false );
            if ( current.isValid() == false )
                return false;
        }
        if ( current.isString() == false && current.isNumber() == false )
            return false;
        outText = current.asString();
        return true;
    }
} // namespace sw
