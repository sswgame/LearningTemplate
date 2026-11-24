#include "pch.h"

#include "Engine/Net/FileServe/FileServeContentSource.h"

#include "Core/Concurrency/ScopedLock.h"
#include "Core/File/AsyncFileIO.h"
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

    FileServeContentSource::FileServeContentSource( IHTTPClient& client, string_view baseURL, string_view mountRoot, const AbsolutePath& mirrorRoot )
        : _client{ client }
        , _baseURL{ FileUtil::trimTrailingSlashes( baseURL ) }
        , _mountRoot{ FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( mountRoot ) ) }
        , _mirrorRoot{ mirrorRoot.join( {} ) }
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
        // 엔진이 만든 경로에도 서버 목록에서 온 이름이 섞인다 — 성분마다 거울 폴더 한 칸이어야 한다(`..` 로 거울 밖에 쓰지 않는다).
        if ( outRelative.empty() )
            return true;
        for ( const string_view component : StringUtil::split( outRelative, "/" ) )
        {
            if ( FileServeContentSourceInternal::isPlainFileName( component ) == false )
                return false;
        }
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

    FileServeContentSource::RemoteStat FileServeContentSource::requestRemoteStat( const string& relative ) const
    {
        RemoteStat                               stat{};
        vector<uint8>                            body;
        bool                                     bMissing = false;
        StringBuilder<constant::buffer::kMax512> url;
        url.appendFormat( "%#/stat?path=%#", _baseURL, encodeQueryValue( relative ) );
        if ( fetch( string( url.view() ), body, bMissing ) )
        {
            const vector<string_view> listToken = StringUtil::split( StringUtil::trim( FileServeContentSourceInternal::toText( body ) ), " " );
            if ( listToken.size() == 1 && listToken[0] == "folder" )
                stat._kind = RemoteKind::Folder;
            else if ( listToken.size() == 3 && listToken[0] == "file" && StringUtil::parseUint64( listToken[1], stat._sizeBytes ) &&
                      StringUtil::parseUint64( listToken[2], stat._modifiedNanos ) && stat._sizeBytes <= kMaxFileBytes )
                stat._kind = RemoteKind::File;
            else
                SW_LOG_WARNING_ID( "FileServe.BadStat", "Fileserve answered '%#' for '%#' - treated as missing", FileServeContentSourceInternal::toText( body ), relative );
        }
        return stat;
    }

    bool FileServeContentSource::downloadFile( const string& relative, uint64 sizeBytes, const AbsolutePath& mirrorPath ) const
    {
        AtomicFileWriter   writer;
        const AbsolutePath mirrorDiskPath = mirrorPath;
        if ( FileUtil::ensureParentDirectoryExists( mirrorDiskPath ) == false || writer.open( mirrorDiskPath ).has_value() == false )
            return false;
        vector<uint8> chunk;
        for ( uint64 offset = 0; offset < sizeBytes; offset += kChunkBytes )
        {
            const uint64                             size     = sizeBytes - offset < kChunkBytes ? sizeBytes - offset : kChunkBytes;
            bool                                     bMissing = false;
            StringBuilder<constant::buffer::kMax512> url;
            url.appendFormat( "%#/file?path=%#&offset=%#&size=%#", _baseURL, encodeQueryValue( relative ), offset, size );
            if ( fetch( string( url.view() ), chunk, bMissing ) == false || chunk.size() != size || writer.append( chunk.data(), chunk.size() ).has_value() == false )
                return false; // writer 가 소멸하며 임시 파일을 지운다 — 반쯤 받은 거울은 남지 않는다
            _receivedChunkCount.add();
        }
        return writer.commit().has_value();
    }

    bool FileServeContentSource::ensureMirrored( string_view path, AbsolutePath& outMirrorPath ) const
    {
        string relative;
        if ( tryMapToRelative( path, relative ) == false || relative.empty() )
            return false;
        {
            UniqueLock<mutex> lock{ _mutex };
            while ( _uniqueMirroring.contains( relative ) )
            {
                _mirrorCv.wait( lock );
            }
            _uniqueMirroring.insert( relative );
        }
        const AbsolutePath mirrorPath = _mirrorRoot.join( relative );
        const bool         bMirrored  = refreshMirror( relative, mirrorPath );
        {
            ScopedLock<mutex> lock{ _mutex };
            _uniqueMirroring.erase( relative );
        }
        _mirrorCv.notify_all();
        if ( bMirrored )
            outMirrorPath = mirrorPath;
        return bMirrored;
    }

    bool FileServeContentSource::refreshMirror( const string& relative, const AbsolutePath& mirrorPath ) const
    {
        const RemoteStat stat = requestRemoteStat( relative );
        if ( stat._kind != RemoteKind::File )
            return false;
        const AbsolutePath stampPath = mirrorPath.appendSuffix( kStampExtension );
        string             stampText;
        StringUtil::appendNumber( stampText, stat._sizeBytes );
        stampText += " ";
        StringUtil::appendNumber( stampText, stat._modifiedNanos );
        string     existingStamp;
        const bool bFresh = FileUtil::readOptionalTextFile( stampPath, existingStamp ) == OptionalFileRead::Read && existingStamp == stampText && FileUtil::isRegularFile( mirrorPath );
        if ( bFresh )
            return true;
        // 받는 사이 PC 가 다시 구우면 구간마다 다른 판이 섞인다 — 다 받은 뒤의 상태가 처음과 같을 때만 도장을 찍는다(아니면 다음 열기가 다시 받는다).
        if ( downloadFile( relative, stat._sizeBytes, mirrorPath ) && requestRemoteStat( relative ) == stat && FileUtil::writeTextFile( stampPath, stampText ) )
            return true;
        SW_LOG_WARNING_ID( "FileServe.DownloadFailed", "Fileserve could not mirror '%#' to '%#' - treated as missing", relative, mirrorPath.str() );
        return false;
    }

    AsyncFileHandle FileServeContentSource::open( string_view path ) const
    {
        AbsolutePath mirrorPath;
        return ensureMirrored( path, mirrorPath ) ? _disk.open( mirrorPath.str() ) : AsyncFileHandle{};
    }

    bool FileServeContentSource::isFile( string_view path ) const
    {
        string relative;
        return tryMapToRelative( path, relative ) && relative.empty() == false && requestRemoteStat( relative )._kind == RemoteKind::File;
    }

    bool FileServeContentSource::isFolder( string_view path ) const
    {
        string relative;
        return tryMapToRelative( path, relative ) && requestRemoteStat( relative )._kind == RemoteKind::Folder;
    }

    bool FileServeContentSource::collectFiles( string_view folder, vector<string>& outListPath ) const
    {
        string relative;
        if ( tryMapToRelative( folder, relative ) == false )
            return false;
        vector<uint8>                            body;
        bool                                     bMissing = false;
        StringBuilder<constant::buffer::kMax512> url;
        url.appendFormat( "%#/list?path=%#", _baseURL, encodeQueryValue( relative ) );
        if ( fetch( string( url.view() ), body, bMissing ) == false )
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
            StringBuilder<constant::buffer::kMax512> path;
            path.appendFormat( "%#/%#", folderKey, name );
            listPath.emplace_back( path.view() );
        }
        outListPath.insert( outListPath.end(), listPath.begin(), listPath.end() );
        return true;
    }

    bool FileServeContentSource::readFile( string_view path, vector<uint8>& outBytes ) const
    {
        AbsolutePath mirrorPath;
        return ensureMirrored( path, mirrorPath ) && _disk.readFile( mirrorPath.str(), outBytes );
    }

    bool FileServeContentSource::readTextFile( string_view path, string& outText ) const
    {
        AbsolutePath mirrorPath;
        return ensureMirrored( path, mirrorPath ) && _disk.readTextFile( mirrorPath.str(), outText );
    }

    AsyncReadHandle FileServeContentSource::readFileAsync( AsyncFileIO& io, string_view path, AsyncIOPriority priority, const AsyncReadCompleteDelegate& onComplete ) const
    {
        return readRangeAsync( io, path, 0, AsyncFileIO::kWholeFile, priority, onComplete );
    }

    AsyncReadHandle FileServeContentSource::readRangeAsync( AsyncFileIO& io, string_view path, uint64 offset, uint64 size, AsyncIOPriority priority,
                                                            const AsyncReadCompleteDelegate& onComplete ) const
    {
        // 받기는 망을 기다린다 — 부른 스레드(게임 · 렌더)가 아니라 여는 백엔드 스레드에서 한다. 소스는 매니저보다 오래 산다(`AndroidMain`).
        const auto onOpen = SW_DELEGATE_LAMBDA( AsyncFileOpenDelegate, [this, pathText = string( path )]()
        {
            return open( pathText );
        } );
        return io.openAndReadRange( onOpen, offset, size, priority, onComplete );
    }
} // namespace sw
