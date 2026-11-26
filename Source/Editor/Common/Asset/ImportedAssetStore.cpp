#include "pch.h"

#include "Editor/Common/Asset/ImportedAssetStore.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Asset/AssetImportStamp.h"
#include "Editor/Common/Asset/EditorContentScan.h"
#include "Editor/Common/Asset/ModelImport/ModelImportUtil.h"
#include "Editor/Common/Asset/ModelImport/ModelImporter.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Content/AssetGUID.h"
#include "Engine/Content/AssetPaths.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 공유 저장소의 TU 로컬 상태 · 도우미입니다. */
        struct ImportedAssetStoreInternal
        {
            /** @brief 글에서 찾는 저장소 구간입니다(`/models/_shared/`). */
            static string makeMarker()
            {
                StringBuilder<constant::buffer::kMax64> markerBuilder;
                markerBuilder.appendFormat( "/%#/%#/", ImportedAssetStore::kModelFolderName, ImportedAssetStore::kStoreFolderName );
                return string( markerBuilder.view() );
            }
        };
    } // namespace

    string ImportedAssetStore::makeKey( const void* pBytes, size_t byteCount, string_view settingsText )
    {
        uint64 hash = HashUtil::computeStringHash64( static_cast<const utf8*>( pBytes ), byteCount, false );
        hash        = HashUtil::computeStringHash64( settingsText.data(), settingsText.size(), false, hash );
        StringBuilder<constant::buffer::kMax32> text;
        text.appendFormat( "%#", Fmt( hash, Format( 16, Format::Padding::Zero ).hex() ) );
        return string( text.c_str(), text.size() );
    }

    bool ImportedAssetStore::writeResultText( const AbsolutePath& diskPath, string_view text )
    {
        return FileUtil::writeTextFile( diskPath, text );
    }

    AbsolutePath ImportedAssetStore::findDomainFolder( const AbsolutePath& sourcePath )
    {
        const string&                           normalized = sourcePath.str();
        StringBuilder<constant::buffer::kMax64> segmentBuilder;
        segmentBuilder.appendFormat( "/%#/", ModelImporter::kRawFolder );
        const size_t at = normalized.find( segmentBuilder.view() );
        return at == string::npos ? AbsolutePath{} : FileUtil::toDiskPath( string_view( normalized ).substr( 0, at ) );
    }

    AbsolutePath ImportedAssetStore::makeStorePath( const AbsolutePath& domainFolder, string_view kind, string_view key, string_view extension )
    {
        return domainFolder.join( kModelFolderName ).join( kStoreFolderName ).join( kind ).join( key ).appendSuffix( extension );
    }

    bool ImportedAssetStore::writeIfAbsent( const AbsolutePath& storePath, const void* pBytes, size_t byteCount )
    {
        // 잠그지 않는다: 병렬 임포트의 두 원본이 같은 키를 써도 키가 내용 지문이라 같은 바이트이고, `FileUtil::writeFile` 은 저마다의 임시 파일에 쓴 뒤
        // 바꿔 끼운다(언리얼 DDC 의 내용 주소 쓰기와 같다) — 프로세스 전역 잠금이 지킬 상태가 없다.
        const AbsolutePath path{ storePath };
        if ( FileUtil::exists( path ) )
        {
            vector<uint8> existing;
            const bool    bSame = FileUtil::readFile( path, existing ) && existing.size() == byteCount && ( byteCount == 0 || Memory::compare( existing.data(), pBytes, byteCount ) == 0 );
            if ( bSame == false )
                SW_LOG_ERROR_ID( "ImportedAssetStore.ContentMismatch", "Shared import result '%#' exists with other bytes (a hash collision or a hand edit) - delete it and import again", path.c_str() );
            return bSame;
        }
        if ( FileUtil::writeFile( path, static_cast<const uint8*>( pBytes ), byteCount ) == false )
        {
            SW_LOG_ERROR_ID( "ImportedAssetStore.WriteFailed", "Failed to write shared import result '%#'", path.c_str() );
            return false;
        }
        return true;
    }

    bool ImportedAssetStore::writeSourceIfAbsent( const AssetPaths& assetPaths, const AbsolutePath& sourcePath, const void* pBytes, size_t byteCount )
    {
        if ( writeIfAbsent( sourcePath, pBytes, byteCount ) == false )
            return false;
        const string       assetID  = assetPaths.toAssetID( sourcePath );
        const AbsolutePath metaPath = AssetMeta::makePath( sourcePath );
        if ( assetID.empty() || FileUtil::isRegularFile( metaPath ) )
            return true;
        return AssetMeta::saveBeside( sourcePath, AssetGUID::fromName( assetID ) );
    }

    void ImportedAssetStore::collectReferences( string_view text, vector<string>& outListPath )
    {
        const string marker = ImportedAssetStoreInternal::makeMarker();
        for ( size_t at = text.find( marker ); at != string_view::npos; at = text.find( marker, at + marker.size() ) )
        {
            const size_t begin = text.rfind( '"', at );
            const size_t end   = text.find( '"', at );
            if ( begin == string_view::npos || end == string_view::npos )
                continue;
            outListPath.push_back( string( text.substr( begin + 1, end - begin - 1 ) ) );
        }
    }

    bool ImportedAssetStore::isReferrer( string_view path )
    {
        return StringUtil::endsWith( path, ".material" ) || StringUtil::endsWith( path, ".animset.xml" ) || StringUtil::endsWith( path, ".prefab.xml" );
    }

    uint64 ImportedAssetStore::mixReferencedFiles( const AbsolutePath& importedPath, string_view text, uint64 hash )
    {
        vector<string> listReference;
        collectReferences( text, listReference );
        VectorUtil::sort( listReference );
        const string& normalized = importedPath.str();
        const string  marker     = ImportedAssetStoreInternal::makeMarker();
        for ( const string& reference : listReference )
        {
            // 리소스 경로의 도메인(`common` · `game/x`)을 디스크 경로에서 찾아 리소스 루트를 얻는다.
            const string                             domain = reference.substr( 0, reference.find( marker ) );
            StringBuilder<constant::buffer::kMax256> rootBuilder;
            rootBuilder.appendFormat( "/%#/%#/", domain, kModelFolderName );
            const size_t rootEnd  = normalized.find( rootBuilder.view() );
            uint64       fileHash = 0;
            if ( rootEnd != string::npos )
            {
                const AbsolutePath resourceRoot = FileUtil::toDiskPath( string_view( normalized ).substr( 0, rootEnd ) );
                fileHash                        = AssetImportStampUtil::computeFileHash( resourceRoot.join( reference ) );
            }
            hash = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( &fileHash ), sizeof( fileHash ), false, hash );
        }
        return hash;
    }

    void ImportedAssetStore::collectReferencedFiles( const AbsolutePath& importedPath, string_view text, vector<AbsolutePath>& outListPath )
    {
        vector<string> listReference;
        collectReferences( text, listReference );
        VectorUtil::sort( listReference );
        const string& normalized = importedPath.str();
        const string  marker     = ImportedAssetStoreInternal::makeMarker();
        for ( const string& reference : listReference )
        {
            const string                             domain = reference.substr( 0, reference.find( marker ) );
            StringBuilder<constant::buffer::kMax256> rootBuilder;
            rootBuilder.appendFormat( "/%#/%#/", domain, kModelFolderName );
            const size_t rootEnd = normalized.find( rootBuilder.view() );
            if ( rootEnd != string::npos )
            {
                const AbsolutePath resourceRoot = FileUtil::toDiskPath( string_view( normalized ).substr( 0, rootEnd ) );
                outListPath.push_back( resourceRoot.join( reference ) );
            }
        }
    }

    uint32 ImportedAssetStore::removeUnreferenced( const vector<AbsolutePath>& listDomainFolder, vector<string>& inoutListProblem )
    {
        uint32 removedCount = 0;
        for ( const AbsolutePath& domainFolder : listDomainFolder )
        {
            const AbsolutePath store = domainFolder.join( kModelFolderName ).join( kStoreFolderName );
            if ( FileUtil::isDirectory( store ) == false )
                continue;
            // 같은 도메인의 임포트 결과(`<x>/models/**`)가 가리키는 파일만 남긴다. 손 데이터는 저장소를 직접 가리키지 않는다(키가 내용과 함께 바뀐다) — 셋의 이름으로 찾는다.
            vector<AbsolutePath> listResult;
            vector<AbsolutePath> listStored;
            const bool           bListed = EditorContentScan::collectFiles( store.getParent(), "", listResult, true ) && EditorContentScan::collectFiles( store, "", listStored, true );
            if ( bListed == false )
            {
                // 다 훑지 못한 목록으로 "가리키지 않는다" 를 판정하면 쓰는 파일을 지운다.
                StringBuilder<constant::buffer::kMax512> problemBuilder;
                problemBuilder.appendFormat( "%#: could not list the imported results - nothing is removed from it", store.str() );
                inoutListProblem.push_back( string( problemBuilder.view() ) );
                continue;
            }
            unordered_set<string> setReferenced;
            for ( const AbsolutePath& result : listResult )
            {
                if ( isReferrer( result.str() ) == false )
                    continue;
                string text;
                if ( FileUtil::readTextFile( result, text ) == false )
                {
                    StringBuilder<constant::buffer::kMax512> problemBuilder;
                    problemBuilder.appendFormat( "%#: could not read it to find its shared results - nothing is removed from %#", result.str(), store.str() );
                    inoutListProblem.push_back( string( problemBuilder.view() ) );
                    return removedCount;
                }
                vector<string> listReference;
                collectReferences( text, listReference );
                for ( const string& reference : listReference )
                {
                    setReferenced.insert( FileUtil::getFileNamePart( reference ) );
                }
            }
            for ( const AbsolutePath& stored : listStored )
            {
                const string fileName = FileUtil::getFileNamePart( FileUtil::normalizeSeparators( stored.str() ) );
                if ( setReferenced.find( fileName ) != setReferenced.end() )
                    continue;
                // 키가 내용 해시라 파일 이름만으로 가른다. 저장소 안 파일끼리는 가리키지 않는다.
                if ( FileUtil::removeFile( stored ) )
                    ++removedCount;
                else
                {
                    StringBuilder<constant::buffer::kMax512> problemBuilder;
                    problemBuilder.appendFormat( "%#: could not remove the unreferenced shared result", stored.str() );
                    inoutListProblem.push_back( string( problemBuilder.view() ) );
                }
            }
        }
        return removedCount;
    }
} // namespace sw::editor
