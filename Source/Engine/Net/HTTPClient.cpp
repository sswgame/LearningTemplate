#include "pch.h"

#include "Engine/Net/HTTPClient.h"

#include "Core/String/StringUtil.h"

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
} // namespace sw
