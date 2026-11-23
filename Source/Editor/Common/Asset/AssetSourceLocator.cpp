#include "pch.h"

#include "Editor/Common/Asset/AssetSourceLocator.h"

#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Process/ProcessEnvironment.h"

#include "Editor/Common/Asset/EditorContentScan.h"

#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    namespace
    {
        struct AssetSourceLocatorInternal
        {
            /** @brief 원본 @p sourcePath 곁 서술이 적은 저장소 주소입니다. 서술이 없거나 읽지 못했거나 곁의 파일을 적었으면 없음입니다. */
            static optional<AssetSourceAddress> findStoredAddress( const AbsolutePath& sourcePath )
            {
                const AbsolutePath metaPath = AssetMeta::makePath( sourcePath );
                string             text;
                AssetMeta          meta;
                if ( FileUtil::readOptionalTextFile( metaPath, text ) != OptionalFileRead::Read || meta.parseXML( text, metaPath.str() ).has_value() == false )
                    return nullopt;
                const optional<AssetSourceAddress> address = AssetSourceAddress::tryParse( meta._source );
                return address.has_value() && address->isStored() ? address : nullopt;
            }
        };
    } // namespace

    expected<AbsolutePath> AssetSourceLocator::resolve( string_view sourcePath )
    {
        const AbsolutePath diskPath = FileUtil::toDiskPath( sourcePath );
        if ( FileUtil::isRegularFile( diskPath ) )
            return diskPath;
        const expected<AbsolutePath> storeRoot = loadStoreRoot();
        if ( storeRoot.has_value() == false )
            return unexpected( storeRoot.error() );
        return resolveInStore( sourcePath, *storeRoot );
    }

    expected<AbsolutePath> AssetSourceLocator::resolveInStore( string_view sourcePath, const AbsolutePath& storeRoot )
    {
        const AbsolutePath diskPath = FileUtil::toDiskPath( sourcePath );
        if ( FileUtil::isRegularFile( diskPath ) )
            return diskPath;
        const optional<AssetSourceAddress> address = AssetSourceLocatorInternal::findStoredAddress( diskPath );
        if ( address.has_value() == false )
            return unexpected( Error{ ErrorKind::NotFound, "the source file is missing and its .meta names no stored source", sourcePath } );
        AbsolutePath storePath = makeStorePath( storeRoot, *address );
        if ( FileUtil::isRegularFile( storePath ) )
            return storePath;
        return unexpected( Error{ ErrorKind::NotFound, "the source is not in the local source store - fetch it first", storePath.str() } );
    }

    bool AssetSourceLocator::readBytes( string_view sourcePath, vector<uint8>& outBytes )
    {
        const expected<AbsolutePath> contentPath = resolve( sourcePath );
        if ( contentPath.has_value() == false )
        {
            if ( AssetSourceLocatorInternal::findStoredAddress( FileUtil::toDiskPath( sourcePath ) ).has_value() )
                SW_LOG_ERROR_ID( "AssetSourceLocator.StoredSourceNotFound", "Source '%#': %# '%#'", sourcePath, contentPath.error().message(), contentPath.error().detail() );
            return false;
        }
        return FileUtil::readFile( *contentPath, outBytes ) && outBytes.empty() == false;
    }

    void AssetSourceLocator::collectStoredSources( string_view folder, vector<string>& inoutListSourcePath )
    {
        unordered_set<string> uniqueListed;
        for ( const string& path : inoutListSourcePath )
            uniqueListed.insert( FileUtil::normalizeSeparators( path ) );
        vector<string> listMetaPath;
        (void)EditorContentScan::collectSidecarFiles( folder, listMetaPath ); // 폴더를 훑지 못한 까닭은 FileUtil 이 남긴다
        for ( const string& metaPath : listMetaPath )
        {
            const string       sourcePath = FileUtil::normalizeSeparators( string_view( metaPath ).substr( 0, metaPath.size() - AssetMeta::kExtension.size() ) );
            const AbsolutePath diskPath   = FileUtil::toDiskPath( sourcePath );
            if ( uniqueListed.find( sourcePath ) != uniqueListed.end() || FileUtil::isRegularFile( diskPath ) )
                continue; // 파일이 있다(거의 모든 원본 — 서술을 열지 않는다)
            if ( AssetSourceLocatorInternal::findStoredAddress( diskPath ).has_value() )
                inoutListSourcePath.push_back( sourcePath );
        }
    }

    AbsolutePath AssetSourceLocator::makeStorePath( const AbsolutePath& storeRoot, const AssetSourceAddress& address )
    {
        const string_view hash = address._contentHash;
        return storeRoot.join( hash.substr( 0, 2 ) ).join( hash.substr( 2, 2 ) ).join( hash );
    }

    expected<AbsolutePath> AssetSourceLocator::loadStoreRoot()
    {
        const AbsolutePath path = ProcessRoots::get()._project.join( kConfigFile );
        JSONDocument       document;
        if ( document.loadFromFile( path.str() ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "the source store config could not be read", path.str() } );
        const optional<string> root        = document.getRoot().readRequired<string>( "_root" );
        const optional<string> environment = document.getRoot().readRequired<string>( "_rootEnvironment" );
        // 폴더와 환경 변수 중 정확히 하나 — 둘 다 적으면 어느 쪽이 이기는지가 숨은 규칙이 된다(DDC 층과 같은 규칙).
        if ( root.has_value() == false || environment.has_value() == false || root->empty() == environment->empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the source store config needs exactly one of _root or _rootEnvironment", path.str() } );
        if ( root->empty() == false )
            return ProcessRoots::get()._derivedData.join( *root );
        string environmentRoot;
        if ( ProcessEnvironment::tryReadNonEmpty( *environment, environmentRoot ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "the source store environment variable is not set", *environment } );
        const optional<AbsolutePath> environmentPath = FileUtil::requireAbsolutePath( environmentRoot );
        if ( environmentPath.has_value() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the source store environment variable is not an absolute path", environmentRoot } );
        return *environmentPath;
    }
} // namespace sw::editor
