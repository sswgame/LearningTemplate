#include "pch.h"

#include "Engine/Observability/HTTPClient.h"

#include "Core/Container/StringUtil.h"

namespace sw
{
    string_view HTTPRequest::findHeader( string_view name ) const
    {
        for ( const pair<string, string>& header : _listHeader )
        {
            if ( StringUtil::equals( string_view( header.first ), name, true ) )
                return header.second;
        }
        return {};
    }

    HTTPResponse NullHTTPClient::send( const HTTPRequest& request )
    {
        (void)request;
        HTTPResponse response;
        response._status = 0;
        response._error  = "network disabled";
        return response;
    }

    NullHTTPClient& NullHTTPClient::get()
    {
        static NullHTTPClient s_client;
        return s_client;
    }
} // namespace sw
