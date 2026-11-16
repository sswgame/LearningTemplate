#include "pch.h"

#include "Core/File/BundledContentFileSource.h"

#include "Core/File/FileUtil.h"

namespace sw
{
    BundledContentFileSource::BundledContentFileSource( const IBundledFileReader& reader, string_view mountRoot, string_view bundledFolder )
        : _reader{ reader }
        , _mountRoot{ FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( mountRoot ) ) }
        , _bundledFolder{ FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( bundledFolder ) ) }
    {
    }

    bool BundledContentFileSource::tryMapToBundledName( string_view path, string_view mountRoot, string_view bundledFolder, string& outBundledName )
    {
        const string key  = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( path ) );
        const string root = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( mountRoot ) );
        if ( root.empty() || key.compare( 0, root.size(), root ) != 0 )
            return false;
        if ( key.size() == root.size() )
        {
            outBundledName.assign( bundledFolder.data(), bundledFolder.size() );
            return true;
        }
        if ( key[root.size()] != '/' )
            return false; // `<root>x` 는 다른 폴더다
        outBundledName.assign( bundledFolder.data(), bundledFolder.size() );
        outBundledName += key.substr( root.size() );
        return true;
    }

    AsyncFileHandle BundledContentFileSource::open( string_view path ) const
    {
        string bundledName;
        if ( tryMapToBundledName( path, _mountRoot, _bundledFolder, bundledName ) == false || bundledName == _bundledFolder )
            return AsyncFileHandle{};
        return _reader.open( bundledName );
    }

    bool BundledContentFileSource::isFile( string_view path ) const
    {
        return open( path ).isValid();
    }

    bool BundledContentFileSource::isFolder( string_view path ) const
    {
        string bundledName;
        if ( tryMapToBundledName( path, _mountRoot, _bundledFolder, bundledName ) == false )
            return false;
        if ( bundledName == _bundledFolder )
            return true; // 걸린 폴더 자체 — 묶음에 팩이 없어도 폴더는 있다(빈 목록)
        vector<string> listFileName;
        return _reader.collectFileNames( bundledName, listFileName ) && listFileName.empty() == false;
    }

    bool BundledContentFileSource::collectFiles( string_view folder, vector<string>& outListPath ) const
    {
        string bundledName;
        if ( tryMapToBundledName( folder, _mountRoot, _bundledFolder, bundledName ) == false )
            return false;
        vector<string> listFileName;
        if ( _reader.collectFileNames( bundledName, listFileName ) == false )
            return false;
        const string folderKey = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( folder ) );
        for ( const string& fileName : listFileName )
        {
            outListPath.push_back( folderKey + "/" + fileName );
        }
        return true;
    }

    bool BundledContentFileSource::readFile( string_view path, vector<uint8>& outBytes ) const
    {
        const AsyncFileHandle handle = open( path );
        if ( handle.isValid() == false )
            return false;
        // 곧 전부 읽어 덮으니 0 으로 채우지 않는다. 다 읽지 못하면 초기화하지 않은 바이트를 남기지 않게 비운다.
        outBytes.resizeForOverwrite( static_cast<size_t>( handle.getSize() ) );
        if ( handle.readAt( 0, outBytes.data(), outBytes.size() ) )
            return true;
        outBytes.clear();
        return false;
    }

    bool BundledContentFileSource::readTextFile( string_view path, string& outText ) const
    {
        vector<uint8> bytes;
        if ( readFile( path, bytes ) == false )
            return false;
        const string_view text = FileUtil::skipUTF8Bom( string_view{ reinterpret_cast<const utf8*>( bytes.data() ), bytes.size() } );
        outText.assign( text.data(), text.size() );
        return true;
    }

    AsyncReadHandle BundledContentFileSource::readFileAsync( AsyncFileIO& io, string_view path, AsyncIOPriority priority, const AsyncReadCompleteDelegate& onComplete ) const
    {
        return io.readRange( open( path ), 0, AsyncFileIO::kWholeFile, priority, onComplete );
    }
} // namespace sw
