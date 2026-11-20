#include "pch.h"

#include "Engine/Telemetry/TelemetryUploader.h"

#include "Core/String/StringUtil.h"

#include "Engine/Net/HTTPClient.h"

namespace sw
{
} // namespace sw

namespace sw
{
    TelemetryUploadResult NullTelemetryUploader::upload( const TelemetryUploadBatch& batch )
    {
        (void)batch;
        return TelemetryUploadResult::Kept;
    }

    HTTPTelemetryUploader::HTTPTelemetryUploader( IHTTPClient& client, string_view endpoint, string_view apiKey )
        : _endpoint{ endpoint }
        , _apiKey{ apiKey }
        , _pClient{ &client }
    {
    }

    TelemetryUploadResult HTTPTelemetryUploader::upload( const TelemetryUploadBatch& batch )
    {
        HTTPRequest request;
        request._method = "POST";
        request._url    = _endpoint;
        request._body   = batch._content;
        request._listHeader.push_back( { "Content-Type", "application/x-ndjson" } );
        request._listHeader.push_back( { "X-Telemetry-Session", batch._sessionID } );
        string eventCount;
        StringUtil::appendNumber( eventCount, batch._eventCount );
        request._listHeader.push_back( { "X-Telemetry-Events", std::move( eventCount ) } );
        if ( _apiKey.empty() == false )
            request._listHeader.push_back( { "X-Api-Key", _apiKey } );
        const HTTPResponse response = _pClient->send( request );
        if ( response.isSuccess() )
            return TelemetryUploadResult::Sent;
        SW_LOG_INFO_ID( "TelemetryUploader.TelemetryUploadNotAccepted", "Telemetry upload of '%#' was not accepted (status %#, %#)", batch._filePath.c_str(), response._status, response._error.c_str() );
        return TelemetryUploadResult::Failed;
    }
} // namespace sw
