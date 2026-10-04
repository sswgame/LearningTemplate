#include "pch.h"

#include "Engine/Telemetry/TelemetryUploader.h"

#include "Engine/Telemetry/HttpClient.h"

namespace sw
{
    SW_LOG_CALLER( "TelemetryUploader" );

    namespace
    {
        struct TelemetryUploaderInternal
        {
            static constexpr const utf8* kArrResultName[] = { "Sent", "Kept", "Failed" };
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( TelemetryUploadResult result )
    {
        return TelemetryUploaderInternal::kArrResultName[static_cast<uint32>( result )];
    }

    TelemetryUploadResult NullTelemetryUploader::upload( const TelemetryUploadBatch& batch )
    {
        (void)batch;
        return TelemetryUploadResult::Kept;
    }

    NullTelemetryUploader& NullTelemetryUploader::get()
    {
        static NullTelemetryUploader s_uploader;
        return s_uploader;
    }

    HttpTelemetryUploader::HttpTelemetryUploader( IHttpClient& client, string_view endpoint, string_view apiKey )
        : _endpoint{ endpoint }
        , _apiKey{ apiKey }
        , _pClient{ &client }
    {
    }

    TelemetryUploadResult HttpTelemetryUploader::upload( const TelemetryUploadBatch& batch )
    {
        HttpRequest request;
        request._method = "POST";
        request._url    = _endpoint;
        request._body   = batch._content;
        request._listHeader.push_back( { "Content-Type", "application/x-ndjson" } );
        request._listHeader.push_back( { "X-Telemetry-Session", batch._sessionId } );
        request._listHeader.push_back( { "X-Telemetry-Events", std::to_string( batch._eventCount ) } );
        if ( _apiKey.empty() == false )
            request._listHeader.push_back( { "X-Api-Key", _apiKey } );
        const HttpResponse response = _pClient->send( request );
        if ( response.isSuccess() )
            return TelemetryUploadResult::Sent;
        SW_LOG_INFO( "Telemetry upload of '%#' was not accepted (status %#, %#)", batch._filePath.c_str(), response._status, response._error.c_str() );
        return TelemetryUploadResult::Failed;
    }
} // namespace sw
