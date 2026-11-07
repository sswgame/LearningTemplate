#include "pch.h"

#include "Core/File/ContentFileSource.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

namespace sw
{
    namespace
    {
        const IContentFileSource* s_pContentFileSource = nullptr;

        struct ContentFileSourceInternal
        {
            /** @brief 메모리 소스의 폴더 키입니다(끝 구분자를 떼고 `/` 로 맞춘다). */
            static string makeFolderKey( string_view folder )
            {
                return FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( folder ) );
            }

            /** @brief @p key 가 @p folderKey 아래에 있으면 그 뒤 글을 돌려줍니다. 아니면 빈 글입니다. */
            static string_view findUnderFolder( string_view key, string_view folderKey )
            {
                if ( key.size() <= folderKey.size() + 1 || key.compare( 0, folderKey.size(), folderKey ) != 0 || key[folderKey.size()] != '/' )
                    return {};
                return key.substr( folderKey.size() + 1 );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    AsyncFileHandle DiskContentFileSource::open( string_view path ) const
    {
        return AsyncFileHandle::open( path );
    }

    bool DiskContentFileSource::isFile( string_view path ) const
    {
        return FileUtil::isRegularFile( path );
    }

    bool DiskContentFileSource::isFolder( string_view path ) const
    {
        return FileUtil::isDirectory( path );
    }

    bool DiskContentFileSource::collectFiles( string_view folder, vector<string>& outListPath ) const
    {
        return FileUtil::collectFiles( folder, {}, outListPath, false );
    }

    bool DiskContentFileSource::readFile( string_view path, vector<uint8>& outBytes ) const
    {
        return FileUtil::readFile( path, outBytes );
    }

    bool DiskContentFileSource::readTextFile( string_view path, string& outText ) const
    {
        return FileUtil::readTextFile( path, outText );
    }

    AsyncReadHandle DiskContentFileSource::readFileAsync( AsyncFileIO& io, string_view path, AsyncIOPriority priority, const AsyncReadCompleteDelegate& onComplete ) const
    {
        return io.readFile( path, priority, onComplete ); // 파일 열기는 백엔드 스레드가 한다
    }
} // namespace sw

namespace sw
{
    MemoryContentFileSource::MemoryContentFileSource()
        : _mapFile{}
    {
    }

    void MemoryContentFileSource::addFile( string_view path, vector<uint8> bytes )
    {
        _mapFile[FileUtil::normalizeSeparators( path )] = sw::make_shared<vector<uint8>>( std::move( bytes ) );
    }

    AsyncFileHandle MemoryContentFileSource::open( string_view path ) const
    {
        const string key = FileUtil::normalizeSeparators( path );
        const auto   it  = _mapFile.find( key );
        return it != _mapFile.end() ? AsyncFileHandle::openMemory( it->second, key ) : AsyncFileHandle{};
    }

    bool MemoryContentFileSource::isFile( string_view path ) const
    {
        return _mapFile.find( FileUtil::normalizeSeparators( path ) ) != _mapFile.end();
    }

    bool MemoryContentFileSource::isFolder( string_view path ) const
    {
        const string folderKey = ContentFileSourceInternal::makeFolderKey( path );
        for ( const auto& [key, pBytes] : _mapFile )
        {
            if ( ContentFileSourceInternal::findUnderFolder( key, folderKey ).empty() == false )
                return true;
        }
        return false;
    }

    bool MemoryContentFileSource::collectFiles( string_view folder, vector<string>& outListPath ) const
    {
        const string folderKey = ContentFileSourceInternal::makeFolderKey( folder );
        for ( const auto& [key, pBytes] : _mapFile )
        {
            const string_view rest = ContentFileSourceInternal::findUnderFolder( key, folderKey );
            if ( rest.empty() == false && rest.find( '/' ) == string_view::npos )
                outListPath.push_back( key );
        }
        VectorUtil::stableSort( outListPath, []( const string& lhs, const string& rhs )
        { return lhs < rhs; } ); // 해시 표 순서가 실행마다 달라도 마운트 순서는 같다
        return true;
    }

    bool MemoryContentFileSource::readFile( string_view path, vector<uint8>& outBytes ) const
    {
        const auto it = _mapFile.find( FileUtil::normalizeSeparators( path ) );
        if ( it == _mapFile.end() )
            return false;
        outBytes = *it->second;
        return true;
    }

    bool MemoryContentFileSource::readTextFile( string_view path, string& outText ) const
    {
        const auto it = _mapFile.find( FileUtil::normalizeSeparators( path ) );
        if ( it == _mapFile.end() )
            return false;
        const string_view text = FileUtil::skipUTF8Bom( string_view{ reinterpret_cast<const utf8*>( it->second->data() ), it->second->size() } );
        outText.assign( text.data(), text.size() );
        return true;
    }

    AsyncReadHandle MemoryContentFileSource::readFileAsync( AsyncFileIO& io, string_view path, AsyncIOPriority priority, const AsyncReadCompleteDelegate& onComplete ) const
    {
        return io.readRange( open( path ), 0, AsyncFileIO::kWholeFile, priority, onComplete );
    }
} // namespace sw

namespace sw
{
    const IContentFileSource& ContentFileSource::getCurrent()
    {
        if ( s_pContentFileSource != nullptr )
            return *s_pContentFileSource;
#if defined( SW_PLATFORM_WINDOWS ) || defined( SW_PLATFORM_LINUX )
        static const DiskContentFileSource s_diskSource;
        return s_diskSource;
#elif defined( SW_PLATFORM_ANDROID ) || defined( SW_PLATFORM_WEB )
        // 파일 경로로 열리지 않는 플랫폼이다 — 기동이 소스(AAsset · 내려받은 메모리)를 걸어야 한다. 디스크로 조용히 가지 않는다.
        SW_FATAL( "No content file source is bound on this platform" );
#else
    #error "Unsupported platform"
#endif
    }

    const IContentFileSource* ContentFileSource::bindCurrent( const IContentFileSource* pSource )
    {
        const IContentFileSource* const pPrevious = s_pContentFileSource;
        s_pContentFileSource                      = pSource;
        return pPrevious;
    }
} // namespace sw
