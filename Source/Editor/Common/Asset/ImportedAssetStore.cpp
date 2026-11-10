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
#include "Editor/Common/Asset/Model/ModelImportUtil.h"
#include "Editor/Common/Commands/Asset/EditorContentScan.h"

namespace sw::editor
{
    namespace
    {
        /** @brief 공유 저장소의 TU 로컬 상태 · 도우미입니다. */
        struct ImportedAssetStoreInternal
        {
            /** @brief 글에서 찾는 저장소 구간입니다(`/models/_shared/`). */
            static string makeMarker() { return "/" + string( ImportedAssetStore::kModelFolderName ) + "/" + string( ImportedAssetStore::kStoreFolderName ) + "/"; }
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

    string ImportedAssetStore::findDomainFolder( string_view sourcePath )
    {
        const string normalized = FileUtil::normalizeSeparators( sourcePath );
        const size_t at         = normalized.find( "/" + string( ModelImportUtil::kRawModelFolder ) + "/" );
        return at == string::npos ? string{} : normalized.substr( 0, at );
    }

    string ImportedAssetStore::makeStorePath( string_view domainFolder, string_view kind, string_view key, string_view extension )
    {
        return string( domainFolder ) + "/" + string( kModelFolderName ) + "/" + string( kStoreFolderName ) + "/" + string( kind ) + "/" + string( key ) + string( extension );
    }

    bool ImportedAssetStore::writeIfAbsent( string_view storePath, const void* pBytes, size_t byteCount )
    {
        // 잠그지 않는다: 병렬 임포트의 두 원본이 같은 키를 써도 키가 내용 지문이라 같은 바이트이고, `FileUtil::writeFile` 은 저마다의 임시 파일에 쓴 뒤
        // 바꿔 끼운다(언리얼 DDC 의 내용 주소 쓰기와 같다) — 프로세스 전역 잠금이 지킬 상태가 없다.
        const string path{ storePath };
        if ( FileUtil::exists( path ) )
        {
            vector<uint8> existing;
            const bool    bSame = FileUtil::readFile( path, existing ) && existing.size() == byteCount && ( byteCount == 0 || Memory::compare( existing.data(), pBytes, byteCount ) == 0 );
            if ( bSame == false )
                SW_LOG_ERROR_ID( "ImportedAssetStore.ContentMismatch", "Shared import result '%#' exists with other bytes (a hash collision or a hand edit) - delete it and import again", path.c_str() );
            return bSame;
        }
        if ( FileUtil::ensureParentDirectoryExists( path ) == false || FileUtil::writeFile( path, static_cast<const uint8*>( pBytes ), byteCount ) == false )
        {
            SW_LOG_ERROR_ID( "ImportedAssetStore.WriteFailed", "Failed to write shared import result '%#'", path.c_str() );
            return false;
        }
        return true;
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

    uint64 ImportedAssetStore::mixReferencedFiles( string_view importedPath, string_view text, uint64 hash )
    {
        vector<string> listReference;
        collectReferences( text, listReference );
        VectorUtil::sort( listReference );
        const string normalized = FileUtil::normalizeSeparators( importedPath );
        const string marker     = ImportedAssetStoreInternal::makeMarker();
        for ( const string& reference : listReference )
        {
            // 리소스 경로의 도메인(`common` · `game/x`)을 디스크 경로에서 찾아 리소스 루트를 얻는다.
            const string domain   = reference.substr( 0, reference.find( marker ) );
            const size_t rootEnd  = normalized.find( "/" + domain + "/" + string( kModelFolderName ) + "/" );
            const uint64 fileHash = rootEnd == string::npos ? 0 : AssetImportStampUtil::computeFileHash( normalized.substr( 0, rootEnd + 1 ) + reference );
            hash                  = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( &fileHash ), sizeof( fileHash ), false, hash );
        }
        return hash;
    }

    uint32 ImportedAssetStore::removeUnreferenced( string_view resourceRoot, vector<string>& inoutListProblem )
    {
        const string   root = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );
        vector<string> listFolder;
        // 남의 시나리오 사본의 공유 결과는 지우지 않는다(그 프로세스가 쓰는 중일 수 있다 — EditorContentScan 이 뺀다)
        if ( root.empty() || EditorContentScan::collectFolders( root, root, listFolder, true ) == false )
            return 0;
        uint32 removedCount = 0;
        for ( const string& folder : listFolder )
        {
            const string store = FileUtil::normalizeSeparators( folder );
            if ( FileUtil::getFileNamePart( store ) != kStoreFolderName || FileUtil::getFileNamePart( FileUtil::getDirectoryPart( store ) ) != kModelFolderName )
                continue;
            // 같은 도메인의 임포트 결과(`<x>/models/**`)가 가리키는 파일만 남긴다. 손 데이터는 저장소를 직접 가리키지 않는다(키가 내용과 함께 바뀐다) — 셋의 이름으로 찾는다.
            vector<string> listResult;
            (void)EditorContentScan::collectFiles( root, FileUtil::getDirectoryPart( store ), "", listResult, true );
            unordered_set<string> setReferenced;
            for ( const string& result : listResult )
            {
                if ( isReferrer( result ) == false )
                    continue;
                string text;
                if ( FileUtil::readTextFile( result, text ) == false )
                {
                    inoutListProblem.push_back( result + ": could not read it to find its shared results - nothing is removed from " + store );
                    return removedCount;
                }
                vector<string> listReference;
                collectReferences( text, listReference );
                for ( const string& reference : listReference )
                {
                    setReferenced.insert( FileUtil::getFileNamePart( reference ) );
                }
            }
            vector<string> listStored;
            (void)EditorContentScan::collectFiles( root, store, "", listStored, true );
            for ( const string& stored : listStored )
            {
                const string fileName = FileUtil::getFileNamePart( FileUtil::normalizeSeparators( stored ) );
                if ( setReferenced.find( fileName ) != setReferenced.end() )
                    continue;
                // 키가 내용 해시라 파일 이름만으로 가른다. 저장소 안 파일끼리는 가리키지 않는다.
                if ( FileUtil::removeFile( stored ) )
                    ++removedCount;
                else
                    inoutListProblem.push_back( stored + ": could not remove the unreferenced shared result" );
            }
        }
        return removedCount;
    }
} // namespace sw::editor
