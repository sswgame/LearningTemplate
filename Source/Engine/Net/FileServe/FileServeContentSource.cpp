#include "pch.h"

#include "Engine/Net/FileServe/FileServeContentSource.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/File/BundledContentFileSource.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"

#include "Engine/Net/HTTPClient.h"

namespace sw
{
    namespace
    {
        struct FileServeContentSourceInternal
        {
            /** @brief HTTP 성공 · 없음 상태입니다. */
            static constexpr int32 kStatusOK       = 200;
            static constexpr int32 kStatusNotFound = 404;
            /** @brief 퍼센트 인코딩의 16 진 한 자리가 담는 비트 수와 아래 자리 마스크입니다. */
            static constexpr uint32 kNibbleBits    = 4;
            static constexpr uint8  kLowNibbleMask = 0x0F;

            /** @brief 목록 이름이 거울 폴더 바로 아래 한 칸이면 true 입니다 — 서버 응답이 거울 밖으로 쓰게 하지 않는다. */
            static bool isPlainFileName( string_view name )
            {
                return name.empty() == false && name != "." && name != ".." && name.find_first_of( "/\\:" ) == string_view::npos && name.find( '\0' ) == string_view::npos;
            }

            static string_view toText( const vector<uint8>& bytes ) { return string_view{ reinterpret_cast<const char*>( bytes.data() ), bytes.size() }; }
        };
    } // namespace

    FileServeContentSource::FileServeContentSource( IHTTPClient& client, string_view baseURL, string_view mountRoot, string_view mirrorRoot )
        : _client{ client }
        , _baseURL{ FileUtil::trimTrailingSlashes( baseURL ) }
        , _mountRoot{ FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( mountRoot ) ) }
        , _mirrorRoot{ FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( mirrorRoot ) ) }
        , _disk{}
    {
    }

    string FileServeContentSource::encodeQueryValue( string_view text )
    {
        static constexpr const utf8* kHexDigits = "0123456789ABCDEF";
        string                       encoded;
        encoded.reserve( text.size() );
        for ( const char character : text )
        {
            const auto byte        = static_cast<uint8>( character );
            const bool bUnreserved = ( byte >= 'a' && byte <= 'z' ) || ( byte >= 'A' && byte <= 'Z' ) || ( byte >= '0' && byte <= '9' ) || byte == '-' || byte == '_' || byte == '.' ||
                                     byte == '~' || byte == '/';
            if ( bUnreserved )
            {
                encoded.push_back( character );
                continue;
            }
            encoded.push_back( '%' );
            encoded.push_back( kHexDigits[byte >> FileServeContentSourceInternal::kNibbleBits] );
            encoded.push_back( kHexDigits[byte & FileServeContentSourceInternal::kLowNibbleMask] );
        }
        return encoded;
    }

    bool FileServeContentSource::tryMapToRelative( string_view path, string& outRelative ) const
    {
        // 묶음 폴더 이름을 빈 글로 주면 걸린 폴더 자체는 빈 글, 그 아래는 `/<나머지>` 다 — 앞 구분자를 떼면 서버 상대 경로다.
        if ( BundledContentFileSource::tryMapToBundledName( path, _mountRoot, string_view{}, outRelative ) == false )
            return false;
        if ( outRelative.empty() == false && outRelative.front() == '/' )
            outRelative.erase( 0, 1 );
        return true;
    }

    bool FileServeContentSource::fetch( const string& url, vector<uint8>& outBody, bool& outbMissing ) const
    {
        outbMissing = false;
        HTTPRequest request;
        request._method       = "GET";
        request._url          = url;
        HTTPResponse response = _client.send( request );
        if ( response._status == FileServeContentSourceInternal::kStatusOK )
        {
            outBody = std::move( response._body );
            return true;
        }
        outbMissing = response._status == FileServeContentSourceInternal::kStatusNotFound;
        if ( outbMissing == false )
            SW_LOG_WARNING_ID( "FileServe.RequestFailed", "Fileserve request '%#' failed (status %#: %#)", url, response._status, response._error );
        return false;
    }

    FileServeContentSource::RemoteStat FileServeContentSource::findRemoteStat( const string& relative ) const
    {
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        it = _mapRemoteStat.find( relative );
            if ( it != _mapRemoteStat.end() )
                return it->second;
        }
        RemoteStat    stat{};
        vector<uint8> body;
        bool          bMissing = false;
        if ( fetch( _baseURL + "/stat?path=" + encodeQueryValue( relative ), body, bMissing ) )
        {
            const vector<string_view> listToken = StringUtil::split( StringUtil::trim( FileServeContentSourceInternal::toText( body ) ), " " );
            if ( listToken.size() == 1 && listToken[0] == "folder" )
                stat._kind = RemoteKind::Folder;
            else if ( listToken.size() == 3 && listToken[0] == "file" && StringUtil::parseUint64( listToken[1], stat._sizeBytes ) &&
                      StringUtil::parseUint64( listToken[2], stat._modifiedNanos ) )
                stat._kind = RemoteKind::File;
            else
                SW_LOG_WARNING_ID( "FileServe.BadStat", "Fileserve answered '%#' for '%#' - treated as missing", FileServeContentSourceInternal::toText( body ), relative );
        }
        ScopedLock<mutex> lock{ _mutex };
        _mapRemoteStat[relative] = stat;
        return stat;
    }

    bool FileServeContentSource::downloadFile( const string& relative, uint64 sizeBytes, const string& mirrorPath ) const
    {
        AtomicFileWriter writer;
        if ( FileUtil::ensureParentDirectoryExists( mirrorPath ) == false || writer.open( mirrorPath ).has_value() == false )
            return false;
        vector<uint8> chunk;
        for ( uint64 offset = 0; offset < sizeBytes; offset += kChunkBytes )
        {
            const uint64 size     = sizeBytes - offset < kChunkBytes ? sizeBytes - offset : kChunkBytes;
            bool         bMissing = false;
            string       url      = _baseURL + "/file?path=" + encodeQueryValue( relative ) + "&offset=";
            StringUtil::appendNumber( url, offset );
            url += "&size=";
            StringUtil::appendNumber( url, size );
            if ( fetch( url, chunk, bMissing ) == false || chunk.size() != size || writer.append( chunk.data(), chunk.size() ).has_value() == false )
                return false; // writer 가 소멸하며 임시 파일을 지운다 — 반쯤 받은 거울은 남지 않는다
            _receivedChunkCount.fetch_add( 1 );
        }
        return writer.commit().has_value();
    }

    bool FileServeContentSource::ensureMirrored( string_view path, string& outMirrorPath ) const
    {
        string relative;
        if ( tryMapToRelative( path, relative ) == false || relative.empty() )
            return false;
        {
            ScopedLock<mutex> lock{ _mutex };
            const auto        it = _mapMirrored.find( relative );
            if ( it != _mapMirrored.end() )
            {
                outMirrorPath = it->second;
                return true;
            }
        }
        const RemoteStat stat = findRemoteStat( relative );
        if ( stat._kind != RemoteKind::File )
            return false;
        // shortcut: 같은 파일을 두 스레드가 처음 함께 열면 둘 다 받는다(원자 교체라 결과는 같다) — 받는 중 표를 두는 것은 겹침이 측정에 보일 때.
        const string mirrorPath = _mirrorRoot + "/" + relative;
        const string stampPath  = mirrorPath + kStampExtension;
        string       stampText;
        StringUtil::appendNumber( stampText, stat._sizeBytes );
        stampText += " ";
        StringUtil::appendNumber( stampText, stat._modifiedNanos );
        string     existingStamp;
        const bool bFresh = FileUtil::readOptionalTextFile( stampPath, existingStamp ) == OptionalFileRead::Read && existingStamp == stampText && FileUtil::isRegularFile( mirrorPath );
        if ( bFresh == false )
        {
            if ( downloadFile( relative, stat._sizeBytes, mirrorPath ) == false || FileUtil::writeTextFile( stampPath, stampText ) == false )
            {
                SW_LOG_WARNING_ID( "FileServe.DownloadFailed", "Fileserve could not mirror '%#' to '%#' - treated as missing", relative, mirrorPath );
                return false;
            }
        }
        ScopedLock<mutex> lock{ _mutex };
        _mapMirrored[relative] = mirrorPath;
        outMirrorPath          = mirrorPath;
        return true;
    }

    AsyncFileHandle FileServeContentSource::open( string_view path ) const
    {
        string mirrorPath;
        return ensureMirrored( path, mirrorPath ) ? _disk.open( mirrorPath ) : AsyncFileHandle{};
    }

    bool FileServeContentSource::isFile( string_view path ) const
    {
        string relative;
        return tryMapToRelative( path, relative ) && relative.empty() == false && findRemoteStat( relative )._kind == RemoteKind::File;
    }

    bool FileServeContentSource::isFolder( string_view path ) const
    {
        string relative;
        return tryMapToRelative( path, relative ) && findRemoteStat( relative )._kind == RemoteKind::Folder;
    }

    bool FileServeContentSource::collectFiles( string_view folder, vector<string>& outListPath ) const
    {
        string relative;
        if ( tryMapToRelative( folder, relative ) == false )
            return false;
        vector<uint8> body;
        bool          bMissing = false;
        if ( fetch( _baseURL + "/list?path=" + encodeQueryValue( relative ), body, bMissing ) == false )
            return false;
        const string              folderKey = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( folder ) );
        const vector<string_view> listName  = StringUtil::split( FileServeContentSourceInternal::toText( body ), "\n" );
        vector<string>            listPath;
        for ( const string_view name : listName )
        {
            if ( name.empty() )
                continue;
            if ( FileServeContentSourceInternal::isPlainFileName( name ) == false )
            {
                SW_LOG_WARNING_ID( "FileServe.BadListName", "Fileserve listed '%#' under '%#' - the listing is refused", name, folderKey );
                return false;
            }
            listPath.push_back( folderKey + "/" + string( name ) );
        }
        outListPath.insert( outListPath.end(), listPath.begin(), listPath.end() );
        return true;
    }

    bool FileServeContentSource::readFile( string_view path, vector<uint8>& outBytes ) const
    {
        string mirrorPath;
        return ensureMirrored( path, mirrorPath ) && _disk.readFile( mirrorPath, outBytes );
    }

    bool FileServeContentSource::readTextFile( string_view path, string& outText ) const
    {
        string mirrorPath;
        return ensureMirrored( path, mirrorPath ) && _disk.readTextFile( mirrorPath, outText );
    }

    AsyncReadHandle FileServeContentSource::readFileAsync( AsyncFileIO& io, string_view path, AsyncIOPriority priority, const AsyncReadCompleteDelegate& onComplete ) const
    {
        // shortcut: 처음 받는 파일은 부른 스레드에서 받는다(개발 전용 — 팩은 기동 때 대부분 열린다). 막는 시간이 보이면 받기를 AsyncFileIO 워커로 옮긴다.
        string mirrorPath;
        if ( ensureMirrored( path, mirrorPath ) == false )
            mirrorPath = _mirrorRoot + "/.fileserve-missing"; // 디스크 소스가 없는 파일로 끝낸다 — 완료 콜백은 늘 한 번 온다
        return _disk.readFileAsync( io, mirrorPath, priority, onComplete );
    }

    AsyncReadHandle FileServeContentSource::readRangeAsync( AsyncFileIO& io, string_view path, uint64 offset, uint64 size, AsyncIOPriority priority,
                                                            const AsyncReadCompleteDelegate& onComplete ) const
    {
        string mirrorPath;
        if ( ensureMirrored( path, mirrorPath ) == false )
            mirrorPath = _mirrorRoot + "/.fileserve-missing";
        return _disk.readRangeAsync( io, mirrorPath, offset, size, priority, onComplete );
    }
} // namespace sw
