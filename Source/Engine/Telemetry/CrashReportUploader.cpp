#include "pch.h"

#include "Engine/Telemetry/CrashReportUploader.h"

#include "Core/File/FileUtil.h"

#include "Engine/Observability/HttpClient.h"

namespace sw
{
    SW_LOG_CALLER( "CrashReportUploader" );

    namespace
    {
        struct CrashReportUploaderInternal
        {
            static constexpr const utf8* kArrResultName[] = { "Sent", "Kept", "Failed" };

            static void appendPartHeader( string& inoutBody, string_view fieldName, string_view fileName, string_view contentType )
            {
                inoutBody += "--";
                inoutBody += HttpCrashReportUploader::kBoundary;
                inoutBody += "\r\nContent-Disposition: form-data; name=\"";
                inoutBody += fieldName;
                inoutBody += "\"";
                if ( fileName.empty() == false )
                {
                    inoutBody += "; filename=\"";
                    inoutBody += fileName;
                    inoutBody += "\"";
                }
                inoutBody += "\r\nContent-Type: ";
                inoutBody += contentType;
                inoutBody += "\r\n\r\n";
            }

            /** @brief 파일의 multipart 필드 이름 — 덤프는 미니덤프 끝점들이 받는 이름이다. */
            static string makeFieldName( string_view fileName )
            {
                if ( FileUtil::hasExtension( fileName, ".dmp" ) )
                    return "upload_file_minidump";
                string field( "attachment_" );
                field += fileName;
                return field;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    const utf8* toString( CrashReportUploadResult result )
    {
        return CrashReportUploaderInternal::kArrResultName[static_cast<uint32>( result )];
    }

    CrashReportUploadResult NullCrashReportUploader::upload( const CrashReportUploadBundle& bundle )
    {
        (void)bundle;
        return CrashReportUploadResult::Kept;
    }

    NullCrashReportUploader& NullCrashReportUploader::get()
    {
        static NullCrashReportUploader s_uploader;
        return s_uploader;
    }

    HttpCrashReportUploader::HttpCrashReportUploader( IHttpClient& client, string_view endpoint )
        : _endpoint{ endpoint }
        , _pClient{ &client }
    {
    }

    CrashReportUploadResult HttpCrashReportUploader::upload( const CrashReportUploadBundle& bundle )
    {
        using Internal = CrashReportUploaderInternal;
        HttpRequest request;
        request._method = "POST";
        request._url    = _endpoint;
        request._listHeader.push_back( { "Content-Type", string( "multipart/form-data; boundary=" ) + kBoundary } );
        request._listHeader.push_back( { "X-Crash-Session", bundle._sessionId } );
        Internal::appendPartHeader( request._body, "manifest", "", "application/json" );
        request._body += bundle._manifest;
        request._body += "\r\n";
        for ( const string& path : bundle._listFilePath )
        {
            vector<uint8> bytes;
            if ( FileUtil::readFile( path, bytes ) == false )
                continue;
            const string fileName = FileUtil::getFileNamePart( path );
            Internal::appendPartHeader( request._body, Internal::makeFieldName( fileName ), fileName, "application/octet-stream" );
            request._body.append( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() );
            request._body += "\r\n";
        }
        request._body += "--";
        request._body += kBoundary;
        request._body += "--\r\n";
        const HttpResponse response = _pClient->send( request );
        if ( response.isSuccess() )
            return CrashReportUploadResult::Sent;
        SW_LOG_INFO( "Crash report '%#' was not accepted (status %#, %#)", bundle._sessionId.c_str(), response._status, response._error.c_str() );
        return CrashReportUploadResult::Failed;
    }
} // namespace sw
