#include "pch.h"

#include "Editor/Common/Asset/AssetSourceLocator.h"

#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Log/Logger.h"
#include "Core/Process/ProcessEnvironment.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Asset/EditorContentScan.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    namespace
    {
        struct AssetSourceLocatorInternal
        {
            /** @brief 원본 @p sourcePath 곁 서술이 적은 저장소 주소입니다. 서술이 없거나 읽지 못했거나 곁의 파일을 적었으면 없음입니다. */
            static optional<AssetSourceAddress> findStoredAddress( const DiskPath& sourcePath )
            {
                const DiskPath metaPath = AssetMeta::makePath( sourcePath );
                string         text;
                AssetMeta      meta;
                if ( FileUtil::readOptionalTextFile( metaPath, text ) != OptionalFileRead::Read || meta.parseXML( text, metaPath.str() ).has_value() == false )
                    return nullopt;
                const optional<AssetSourceAddress> address = AssetSourceAddress::tryParse( meta._source );
                return address.has_value() && address->isStored() ? address : nullopt;
            }

            /** @brief 원본의 바이트가 있는 파일입니다. 저장소 주소를 풀지 못한 까닭은 오류로 남기고, 없는 파일은 부르는 쪽이 알린다. */
            static optional<DiskPath> resolveOrReport( const DiskPath& sourcePath )
            {
                const expected<DiskPath> contentPath = AssetSourceLocator::resolve( sourcePath );
                if ( contentPath.has_value() )
                    return *contentPath;
                if ( findStoredAddress( sourcePath ).has_value() )
                    SW_LOG_ERROR_ID( "AssetSourceLocator.StoredSourceNotFound", "Source '%#': %# '%#'", sourcePath.str(), contentPath.error().message(), contentPath.error().detail() );
                return nullopt;
            }
        };
    } // namespace

    expected<DiskPath> AssetSourceLocator::resolve( const DiskPath& sourcePath )
    {
        const DiskPath diskPath = sourcePath;
        if ( FileUtil::isRegularFile( diskPath ) )
            return diskPath;
        const expected<AbsolutePath> storeRoot = loadStoreRoot();
        if ( storeRoot.has_value() == false )
            return unexpected( storeRoot.error() );
        return resolveInStore( sourcePath, *storeRoot );
    }

    expected<DiskPath> AssetSourceLocator::resolveInStore( const DiskPath& sourcePath, const AbsolutePath& storeRoot )
    {
        const DiskPath diskPath = sourcePath;
        if ( FileUtil::isRegularFile( diskPath ) )
            return diskPath;
        const optional<AssetSourceAddress> address = AssetSourceLocatorInternal::findStoredAddress( diskPath );
        if ( address.has_value() == false )
            return unexpected( Error{ ErrorKind::NotFound, "the source file is missing and its .meta names no stored source", sourcePath.str() } );
        AbsolutePath storePath = makeStorePath( storeRoot, *address );
        if ( FileUtil::isRegularFile( storePath ) )
            return storePath;
        return unexpected( Error{ ErrorKind::NotFound, "the source is not in the local source store - fetch it first", storePath.str() } );
    }

    bool AssetSourceLocator::readBytes( const DiskPath& sourcePath, vector<uint8>& outBytes )
    {
        const optional<DiskPath> contentPath = AssetSourceLocatorInternal::resolveOrReport( sourcePath );
        return contentPath.has_value() && FileUtil::readFile( *contentPath, outBytes ) && outBytes.empty() == false;
    }

    uint64 AssetSourceLocator::computeHash( const DiskPath& sourcePath )
    {
        const optional<DiskPath> contentPath = AssetSourceLocatorInternal::resolveOrReport( sourcePath );
        return contentPath.has_value() ? AssetImportStampUtil::computeFileHash( *contentPath ) : 0;
    }

    void AssetSourceLocator::collectStoredSources( const AbsolutePath& folder, vector<AbsolutePath>& inoutListSourcePath )
    {
        unordered_set<string> uniqueListed;
        for ( const AbsolutePath& path : inoutListSourcePath )
            uniqueListed.insert( path.str() );
        vector<AbsolutePath> listMetaPath;
        (void)EditorContentScan::collectSidecarFiles( folder, listMetaPath ); // 폴더를 훑지 못한 까닭은 FileUtil 이 남긴다
        for ( const AbsolutePath& metaPath : listMetaPath )
        {
            const AbsolutePath sourcePath = metaPath.removeExtension(); // `.meta` 를 뗀 원본
            if ( uniqueListed.find( sourcePath.str() ) != uniqueListed.end() || FileUtil::isRegularFile( sourcePath ) )
                continue; // 파일이 있다(거의 모든 원본 — 서술을 열지 않는다)
            if ( AssetSourceLocatorInternal::findStoredAddress( sourcePath ).has_value() )
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
        const expected<AssetSourceStoreConfig> config = ConfigManager::readConfig( AssetSourceStoreConfig::kConfigFile );
        if ( config.has_value() == false )
            return unexpected( config.error() );
        if ( config->_root.empty() == false )
            return ProcessRoots::get()._derivedData.join( config->_root );
        string environmentRoot;
        if ( ProcessEnvironment::tryReadNonEmpty( config->_rootEnvironment, environmentRoot ) == false )
            return unexpected( Error{ ErrorKind::NotFound, "the source store environment variable is not set", config->_rootEnvironment } );
        const optional<AbsolutePath> environmentPath = FileUtil::requireAbsolutePath( environmentRoot );
        if ( environmentPath.has_value() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the source store environment variable is not an absolute path", environmentRoot } );
        return *environmentPath;
    }
} // namespace sw::editor
