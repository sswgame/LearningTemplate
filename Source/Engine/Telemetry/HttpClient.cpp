#include "pch.h"

#include "Engine/Telemetry/HttpClient.h"

#include "Core/Container/StringUtil.h"

namespace sw
{
    string_view HttpRequest::findHeader( string_view name ) const
    {
        for ( const pair<string, string>& header : _listHeader )
        {
            if ( StringUtil::equals( string_view( header.first ), name, true ) )
                return header.second;
        }
        return {};
    }

    HttpResponse NullHttpClient::send( const HttpRequest& request )
    {
        (void)request;
        HttpResponse response;
        response._status = 0;
        response._error  = "network disabled";
        return response;
    }

    NullHttpClient& NullHttpClient::get()
    {
        static NullHttpClient s_client;
        return s_client;
    }
} // namespace sw
