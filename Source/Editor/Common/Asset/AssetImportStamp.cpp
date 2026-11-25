#include "pch.h"

#include "Editor/Common/Asset/AssetImportStamp.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/map.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Encoding/HexUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Asset/AssetSourceLocator.h"
#include "Editor/Common/Asset/EditorContentScan.h"
#include "Editor/Common/Asset/ImportedAssetStore.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Asset/Cook/CookRecord.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/ContentHash.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @struct AssetImportStampInternal
         * @brief `import.stamp` 읽기 · 쓰기 · 대조와 원본 폴더 하나의 일괄 임포트입니다(파일은 병렬로 임포트하고 합치기는 직렬입니다).
         */
        struct AssetImportStampInternal
        {
            /** @brief 스탬프 한 줄입니다. */
            struct StampEntry
            {
                uint64 _sourceHash{ 0 };
                uint64 _importedHash{ 0 };
            };

            /** @brief 경로를 `.../<rawFolderName>` 과 그 아래 상대 경로로 나눕니다. 폴더 이름은 경로 구간 경계에서만 찾습니다. */
            static bool splitRawPath( string_view path, string_view rawFolderName, string& outRawFolder, string& outRelativePath )
            {
                const string      normalized = FileUtil::normalizeSeparators( path );
                const string_view normalizedView{ normalized };
                for ( size_t pos = normalizedView.find( rawFolderName ); pos != string_view::npos; pos = normalizedView.find( rawFolderName, pos + 1 ) )
                {
                    const size_t endPos         = pos + rawFolderName.size();
                    const bool   bStartsSegment = pos == 0 || normalized[pos - 1] == '/';
                    const bool   bEndsSegment   = endPos + 1 < normalized.size() && normalized[endPos] == '/';
                    if ( bStartsSegment && bEndsSegment )
                    {
                        outRawFolder    = normalized.substr( 0, endPos );
                        outRelativePath = normalized.substr( endPos + 1 );
                        return true;
                    }
                }
                return false;
            }

            static string formatHash( uint64 hash )
            {
                StringBuilder<constant::buffer::kMax32> text;
                text.appendFormat( "%#", Fmt( hash, Format( 16, Format::Padding::Zero ).hex() ) );
                return string( text.c_str(), text.size() );
            }

            /** @brief 스탬프를 읽습니다. 없거나 머리 줄이 다르면 빈 표입니다(= 모든 원본이 어긋남). 형식이 아닌 줄은 건너뜁니다. */
            static void readStamp( const AbsolutePath& stampPath, string_view header, map<string, StampEntry>& outMapEntry )
            {
                outMapEntry.clear();
                string text;
                if ( FileUtil::readOptionalTextFile( stampPath, text ) != OptionalFileRead::Read )
                    return;

                bool bHeaderSeen = false;
                StringUtil::forEachLine( text, [&]( string_view rawLine )
                {
                    const string_view line = StringUtil::trim( rawLine );
                    if ( bHeaderSeen == false )
                    {
                        bHeaderSeen = true;
                        return line == header; // 머리 줄이 다르면 빈 표
                    }

                    const size_t firstSpace  = line.find( ' ' );
                    const size_t secondSpace = firstSpace == string_view::npos ? string_view::npos : line.find( ' ', firstSpace + 1 );
                    if ( secondSpace == string_view::npos || secondSpace + 1 >= line.size() )
                        return true;

                    StampEntry entry;
                    const bool bSourceParsed   = StringUtil::parseUint64( line.substr( 0, firstSpace ), entry._sourceHash, 16 );
                    const bool bImportedParsed = StringUtil::parseUint64( line.substr( firstSpace + 1, secondSpace - firstSpace - 1 ), entry._importedHash, 16 );
                    if ( bSourceParsed && bImportedParsed )
                        outMapEntry[string( line.substr( secondSpace + 1 ) )] = entry;
                    return true;
                } );
            }

            /** @brief 스탬프 본문입니다. 줄은 경로 순이라 같은 테이블은 같은 바이트입니다. */
            static string makeStampText( string_view header, const map<string, StampEntry>& mapEntry )
            {
                StringBuilder<constant::buffer::kMax4096> text;
                text.appendFormat( "%#\n", header );
                for ( const auto& [relativePath, entry] : mapEntry )
                {
                    text.appendFormat( "%# %# %#\n", formatHash( entry._sourceHash ), formatHash( entry._importedHash ), relativePath );
                }
                return string( text.view() );
            }

            /** @brief 원본 하나가 스탬프와 어긋난 이유입니다. 맞으면 빈 문자열입니다. */
            static string findDriftReason( const StampEntry* pStamped, const StampEntry& current, string_view importedLabel )
            {
                if ( current._sourceHash == 0 )
                    return "원본을 읽지 못했습니다";
                StringBuilder<constant::buffer::kMax256> reasonBuilder;
                if ( current._importedHash == 0 )
                {
                    reasonBuilder.appendFormat( "%# 가 없습니다", importedLabel );
                    return string( reasonBuilder.view() );
                }
                if ( pStamped == nullptr )
                    return "스탬프에 없습니다 (임포트된 적이 없습니다)";
                if ( pStamped->_sourceHash != current._sourceHash )
                    return "원본이나 규칙이 바뀌었는데 다시 임포트하지 않았습니다";
                if ( pStamped->_importedHash != current._importedHash )
                {
                    reasonBuilder.appendFormat( "%# 가 임포트된 결과와 다릅니다 (손으로 바꿨습니다)", importedLabel );
                    return string( reasonBuilder.view() );
                }
                return {};
            }

            /** @brief 스탬프를 씁니다. 내용이 같으면 쓰지 않습니다 — 맞는 트리에서 임포트를 돌려도 작업 트리가 더러워지지 않습니다. */
            static void writeStampIfChanged( const AbsolutePath& stampPath, string_view header, const map<string, StampEntry>& mapCurrent, AssetImportSummary& inoutSummary )
            {
                const bool bStampExists = FileUtil::exists( stampPath );
                if ( mapCurrent.empty() && bStampExists == false )
                    return;
                const string stampText = makeStampText( header, mapCurrent );
                string       existingText;
                if ( bStampExists && FileUtil::readTextFile( stampPath, existingText ) )
                {
                    // 체크아웃이 줄 끝을 CRLF 로 바꿔 둘 수 있으므로 CR 은 빼고 비교한다.
                    VectorUtil::removeValue( existingText, '\r' );
                    if ( existingText == stampText )
                        return;
                }
                if ( FileUtil::writeTextFile( stampPath, stampText ) == false )
                {
                    StringBuilder<constant::buffer::kMax512> problemBuilder;
                    problemBuilder.appendFormat( "스탬프를 쓰지 못했습니다: %#", stampPath.str() );
                    inoutSummary._listProblem.push_back( string( problemBuilder.view() ) );
                }
            }

            /** @brief 원본 파일 하나의 입력입니다. 준비 단계(직렬)가 채우고, 병렬 구간에서는 읽기만 합니다. */
            struct SourceFile
            {
                AbsolutePath _sourcePath;
                string       _relativePath; ///< 원본 폴더 기준
                string       _assetPath;    ///< 리소스 루트 기준
                AbsolutePath _importedPath;
                uint64       _byteCount{ 0 };   ///< 원본 크기 — 무거운 원본부터 임포트하는 순서에만 쓴다
                uint32       _folderIndex{ 0 }; ///< 이 원본이 든 `RawFolder` 번호
            };

            /** @brief 원본 폴더 하나의 준비 결과입니다 — 스탬프와, 원본 목록(모든 폴더가 한 목록)에서 이 폴더의 구간. */
            struct RawFolder
            {
                AbsolutePath                  _path;
                map<string, StampEntry>       _mapStamped;
                unordered_map<string, string> _mapImportedOwner; ///< 결과 경로(소문자) → 먼저 나온 원본
                vector<string>                _listProblem;      ///< 준비에서 나온 문제(두 원본이 한 결과에 쓴다)
                unordered_set<string>         _uniqueRejected;   ///< 준비에서 거절한 원본(폴더 기준 상대 경로) — 스탬프 줄을 남기지 않는다
                size_t                        _firstFile{ 0 };
                size_t                        _fileCount{ 0 };
            };

            /** @brief 원본 파일 하나의 결과입니다. 파일마다 자기 칸에만 쓰고, 요약과 스탬프 표에 합치는 일은 파일 순서대로 직렬로 합니다. */
            struct FileResult
            {
                string     _problem;            ///< 비어 있지 않으면 `AssetImportSummary::_listProblem` 에 더할 한 줄
                StampEntry _current;            ///< `_bStamped` 일 때 스탬프에 쓸 줄
                bool       _bStamped{ false };  ///< 맞거나 방금 임포트해 스탬프 표에 넣는다
                bool       _bImported{ false }; ///< 이번에 임포트했다
                bool       _bDeferred{ false }; ///< 다른 원본의 결과를 읽는다(`hasOrderDependency`) — 나머지가 모두 끝난 둘째 단계에서 임포트한다
                bool       _bSkipped{ false };  ///< 취소로 보지 않았다 — 스탬프 줄을 그대로 둔다
            };

            /** @brief DDC 키 서술입니다(`DerivedDataCache` 가 항목 머리에 적어 키 충돌을 미스로 만든다). 원본 해시가 원본 바이트 · 규칙 · 임포터 판 · 대상 형식을 이미 섞는다. */
            static string makeCacheKeyDescription( const IRawAssetImporter& importer, const SourceFile& file, uint64 sourceHash, uint64 codeFingerprint )
            {
                string text( "import=" );
                text.append( importer.getStampHeader() ).append( "\ncode=" ).append( formatHash( codeFingerprint ) ).append( "\nsource=" ).append( formatHash( sourceHash ) );
                if ( importer.isImportedPathEmbedded() )
                    text.append( "\npath=" ).append( file._assetPath );
                else
                    text.append( "\nnames=stem" ); // 덩어리의 이름이 원본 줄기를 뺀 꼴이다 — 줄기를 박아 넣던 옛 항목은 미스가 된다
                text.push_back( '\n' );
                return text;
            }

            /** @brief 결과 파일 이름의 기준 폴더(끝에 `/`)입니다. 경로가 박히는 종류는 리소스 루트, 아니면 결과 파일의 폴더입니다(`isImportedPathEmbedded`). */
            static string makeCacheNameBase( const IRawAssetImporter& importer, const SourceFile& file )
            {
                string base = importer.isImportedPathEmbedded() ? file._sourcePath.str().substr( 0, file._sourcePath.str().size() - file._assetPath.size() )
                                                                : FileUtil::getDirectoryPart( FileUtil::normalizeSeparators( file._importedPath.str() ) );
                if ( base.empty() == false && base.back() != '/' )
                    base.push_back( '/' );
                return base;
            }

            /** @brief 결과 파일 이름의 원본 줄기(`<폴더>/x.dds` 의 `x`)입니다. 경로가 박히지 않는 종류는 키가 내용만이라 같은 바이트의 다른 원본이 한 항목을 받는다 — 이름에서 이것을 빼 두고 받는 원본의 줄기로 다시 붙인다. */
            static string makeCacheStem( const IRawAssetImporter& importer, const SourceFile& file )
            {
                if ( importer.isImportedPathEmbedded() )
                    return {};
                const string name = FileUtil::getFileNamePart( FileUtil::normalizeSeparators( file._importedPath.str() ) );
                return name.substr( 0, name.find( '.' ) );
            }

            /** @brief 결과 이름(@p name, 기준 폴더 뒤)에서 줄기를 `*` 로 바꿉니다. 줄기로 시작하지 않으면(다른 원본 이름의 결과) false — 그 원본은 DDC 에 넣지 않는다. */
            [[nodiscard]] static bool replaceStem( string& inoutName, const string& stem )
            {
                if ( stem.empty() )
                    return true;
                const bool bHasStem = StringUtil::startsWith( inoutName, stem ) && inoutName.size() > stem.size() && ( inoutName[stem.size()] == '.' || inoutName[stem.size()] == '/' );
                if ( bHasStem == false )
                    return false;
                inoutName.replace( 0, stem.size(), "*" );
                return true;
            }

            /** @brief 저장소 공유 파일(`<x>/models/_shared/…`)인지 봅니다 — 다른 원본도 쓰는 파일이라 지우지 않고 `writeIfAbsent` 로만 쓴다. */
            static bool isSharedStorePath( string_view path )
            {
                string marker( "/" );
                marker.append( ImportedAssetStore::kModelFolderName ).append( "/" ).append( ImportedAssetStore::kStoreFolderName ).append( "/" );
                return FileUtil::normalizeSeparators( path ).find( marker ) != string::npos;
            }

            /**
             * @brief 원본 하나의 결과 파일을 (이름, 바이트) 덩어리로 묶습니다. 이름 순서이고 한 파일은 `<이름>\n<바이트 수 hex16>\n<바이트>` 입니다.
             * @return 결과가 없거나 못 읽었거나 기준 폴더 밖 파일이 있으면 false 입니다(그 원본은 DDC 에 넣지 않는다).
             */
            [[nodiscard]] static bool packImportedFiles( const IRawAssetImporter& importer, const SourceFile& file, vector<uint8>& outPayload )
            {
                vector<AbsolutePath> listPath;
                importer.collectImportedFiles( file._importedPath, listPath );
                if ( listPath.empty() )
                    return false;
                const string base = makeCacheNameBase( importer, file );
                const string stem = makeCacheStem( importer, file );
                for ( const AbsolutePath& path : listPath )
                {
                    if ( path.str().size() <= base.size() || StringUtil::startsWith( path.str(), base ) == false )
                        return false;
                }
                VectorUtil::sort( listPath );
                outPayload.clear();
                for ( const AbsolutePath& path : listPath )
                {
                    vector<uint8> bytes;
                    if ( FileUtil::readFile( path, bytes ) == false )
                        return false;
                    string head( string_view( path.str() ).substr( base.size() ) );
                    if ( replaceStem( head, stem ) == false )
                        return false;
                    head.push_back( '\n' );
                    HexUtil::appendUint64( head, bytes.size() );
                    head.push_back( '\n' );
                    outPayload.insert( outPayload.end(), head.begin(), head.end() );
                    outPayload.insert( outPayload.end(), bytes.begin(), bytes.end() );
                }
                return true;
            }

            /**
             * @brief `packImportedFiles` 의 덩어리를 결과 자리에 씁니다. 지금 결과 파일(공유 파일 빼고)은 먼저 지운다 — 옛 임포트가 남긴 클립이 섞이지 않게.
             * @details 이름은 신뢰 경계다(공유 층은 다른 머신이 쓴 바이트) — 빈 이름 · 절대 경로 · `..` 구간이 든 이름은 덩어리 전체를 거절한다.
             */
            [[nodiscard]] static bool unpackImportedFiles( const IRawAssetImporter& importer, const SourceFile& file, const vector<uint8>& payload )
            {
                struct Entry
                {
                    string _name;
                    size_t _offset{ 0 };
                    size_t _size{ 0 };
                };
                vector<Entry>     listEntry;
                const string_view text{ reinterpret_cast<const char*>( payload.data() ), payload.size() };
                for ( size_t cursor = 0; cursor < text.size(); )
                {
                    const size_t nameEnd = text.find( '\n', cursor );
                    const size_t sizeEnd = nameEnd == string_view::npos ? string_view::npos : text.find( '\n', nameEnd + 1 );
                    uint64       size    = 0;
                    if ( sizeEnd == string_view::npos || sizeEnd - nameEnd - 1 != HexDigitUtil::kMaxDigitCount ||
                         HexDigitUtil::tryParseFixed( text.substr( nameEnd + 1, HexDigitUtil::kMaxDigitCount ), size ) == false || size > text.size() - sizeEnd - 1 )
                        return false;
                    Entry entry;
                    entry._name            = string( text.substr( cursor, nameEnd - cursor ) );
                    entry._offset          = sizeEnd + 1;
                    entry._size            = static_cast<size_t>( size );
                    const bool bUnsafeName = entry._name.empty() || entry._name.front() == '/' || entry._name.find( ':' ) != string::npos || entry._name.find( '\\' ) != string::npos ||
                                             entry._name == ".." || StringUtil::startsWith( entry._name, "../" ) || entry._name.find( "/../" ) != string::npos;
                    if ( bUnsafeName )
                        return false;
                    cursor = entry._offset + entry._size;
                    listEntry.push_back( std::move( entry ) );
                }
                if ( listEntry.empty() )
                    return false;

                vector<AbsolutePath> listOld;
                importer.collectImportedFiles( file._importedPath, listOld );
                for ( const AbsolutePath& oldPath : listOld )
                {
                    if ( isSharedStorePath( oldPath.str() ) == false && FileUtil::removeFile( oldPath ) == false )
                        return false;
                }
                const string base = makeCacheNameBase( importer, file );
                const string stem = makeCacheStem( importer, file );
                for ( const Entry& entry : listEntry )
                {
                    const bool bHasStem = entry._name.front() == '*';
                    if ( bHasStem != ( stem.empty() == false ) )
                        return false; // 줄기 자리가 없는 이름(옛 항목)이거나 경로가 박히는 종류인데 줄기 자리가 있다
                    StringBuilder<constant::buffer::kMax256> pathBuilder;
                    if ( bHasStem )
                        pathBuilder.appendFormat( "%#%#%#", base, stem, string_view( entry._name ).substr( 1 ) );
                    else
                        pathBuilder.appendFormat( "%#%#", base, entry._name );
                    const AbsolutePath path( FileUtil::toDiskPath( pathBuilder.view() ) );
                    const uint8*       pBytes = payload.data() + entry._offset;
                    const bool         bWritten =
                        isSharedStorePath( path.str() ) ? ImportedAssetStore::writeIfAbsent( path, pBytes, entry._size )
                                                                : FileUtil::writeFile( path, pBytes, entry._size );
                    if ( bWritten == false )
                        return false;
                }
                return true;
            }

            /** @brief DDC 에서 원본 하나의 결과를 찾아 씁니다. 미스 · 못 쓴 것은 false 라 부르는 쪽이 임포트한다. */
            [[nodiscard]] static bool serveFromCache( const IRawAssetImporter& importer, const DerivedDataCache& derivedDataCache, const SourceFile& file, const string& keyDescription )
            {
                vector<uint8> payload;
                const uint64  key = HashUtil::computeStringHash64( keyDescription.c_str(), keyDescription.size(), false );
                if ( derivedDataCache.find( key, keyDescription, payload ) == false )
                    return false;
                if ( unpackImportedFiles( importer, file, payload ) == false )
                {
                    SW_LOG_WARNING_ID( "AssetImportStamp.CacheEntryNotRestored", "Import: the DDC entry of '%#' could not be written back - imported again", file._assetPath );
                    return false;
                }
                return true;
            }

            /** @brief 방금 임포트한 원본 하나의 결과를 DDC 에 넣습니다. 못 넣어도 임포트는 성공이다(다음 임포트가 다시 넣는다). */
            static void storeInCache( const IRawAssetImporter& importer, const DerivedDataCache& derivedDataCache, const SourceFile& file, const string& keyDescription )
            {
                vector<uint8> payload;
                if ( packImportedFiles( importer, file, payload ) == false )
                {
                    SW_LOG_INFO_ID( "AssetImportStamp.CacheEntrySkipped", "Import: '%#' is not stored in the DDC - its results could not be read or lie outside its folder", file._assetPath );
                    return;
                }
                const uint64 key = HashUtil::computeStringHash64( keyDescription.c_str(), keyDescription.size(), false );
                if ( derivedDataCache.store( key, keyDescription, payload ) == false )
                    SW_LOG_WARNING_ID( "AssetImportStamp.CacheStoreFailed", "Import: could not store '%#' in the DDC", file._assetPath );
            }

            /**
             * @brief 모든 원본 폴더의 파일들을 임포트하는 일입니다. 병렬 구간에서 입력은 읽기만 하고 파일마다 자기 결과 칸(`_pListFileResult[파일 번호]`)에만 씁니다.
             * @details 병렬 단위는 **사슬**(같은 결과 자리에 쓰는 파일의 묶음, `makeChains`)이라 두 스레드가 한 결과 경로를 쓰거나 지우지 않습니다.
             *          스레드는 원자 카운터에서 사슬을 하나씩 가져옵니다(무거운 원본 하나가 청크 끝을 늦추지 않게 동적으로 나눈다). 임포터 함수는 모두 스레드 안전해야 합니다(`IRawAssetImporter`).
             *          단계는 둘이다 — 첫 단계가 다른 원본의 결과를 읽지 않는 파일을 모두 임포트하고, 둘째 단계가 읽는 파일(`hasOrderDependency`)을 임포트한다. 단계 안은 둘 다 병렬이다.
             */
            struct ImportJob
            {
                const IRawAssetImporter*      _pImporter{ nullptr };
                const vector<RawFolder>*      _pListFolder{ nullptr };
                const vector<SourceFile>*     _pListFile{ nullptr };
                const vector<vector<uint32>>* _pListChain{ nullptr };
                const vector<uint32>*         _pListRunChain{ nullptr };   ///< 이번 단계에서 돌 사슬 번호
                FileResult*                   _pListFileResult{ nullptr }; ///< 파일 수만큼
                atomic<uint32>                _nextSlot{ 0 };              ///< `_pListRunChain` 에서 다음에 가져갈 칸
                AssetImportMode               _mode{ AssetImportMode::ImportStale };
                bool                          _bDependentPhase{ false };     ///< 둘째 단계(미룬 파일만)
                TaskManager*                  _pTaskManager{ nullptr };      ///< 사슬을 나눠 돌 작업 시스템(빌림). nullptr 이면 직렬
                const DerivedDataCache*       _pDerivedDataCache{ nullptr }; ///< 임포트 전에 찾고 임포트 뒤에 넣는 DDC(빌림). nullptr 이면 쓰지 않는다(CheckOnly)
                const AssetImportControl*     _pControl{ nullptr };          ///< 취소 · 진행(빌림). nullptr 이면 끝까지 돈다

                /** @brief 원본 하나를 대조하고, 어긋났고 ImportStale 이면 임포트합니다. 결과는 @p outResult 에만 씁니다. */
                void importFile( const SourceFile& file, FileResult& outResult ) const
                {
                    const utf8* const pUnsupportedReason = _pImporter->findUnsupportedReason( file._sourcePath );
                    if ( pUnsupportedReason != nullptr )
                    {
                        StringBuilder<constant::buffer::kMax512> problemBuilder;
                        problemBuilder.appendFormat( "%#: %#", file._assetPath, pUnsupportedReason );
                        outResult._problem = string( problemBuilder.view() );
                        return;
                    }

                    StampEntry current;
                    current._sourceHash   = _pImporter->computeSourceHash( file._sourcePath, file._assetPath );
                    current._importedHash = _pImporter->computeImportedHash( file._importedPath );

                    const map<string, StampEntry>& mapStamped = ( *_pListFolder )[file._folderIndex]._mapStamped;
                    const auto                     itStamped  = mapStamped.find( file._relativePath );
                    const string                   reason     = findDriftReason( itStamped != mapStamped.end() ? &itStamped->second : nullptr, current, _pImporter->getImportedLabel() );
                    if ( reason.empty() )
                    {
                        outResult._current  = current;
                        outResult._bStamped = true;
                        return;
                    }

                    if ( _mode == AssetImportMode::CheckOnly )
                    {
                        StringBuilder<constant::buffer::kMax512> problemBuilder;
                        problemBuilder.appendFormat( "%#: %#", file._assetPath, reason );
                        outResult._problem = string( problemBuilder.view() );
                        return;
                    }

                    string cacheKeyDescription;
                    bool   bServedFromCache = false;
                    if ( _pDerivedDataCache != nullptr && current._sourceHash != 0 )
                    {
                        const uint64 codeFingerprint = _pImporter->findCodeFingerprint();
                        if ( codeFingerprint == 0 )
                        {
                            StringBuilder<constant::buffer::kMax512> problemBuilder;
                            problemBuilder.appendFormat( "%#: 임포터 코드 지문이 없습니다 - Config/Dev/CookCodeInputs.json 에 임포터를 적습니다", file._assetPath );
                            outResult._problem = string( problemBuilder.view() );
                            return;
                        }
                        cacheKeyDescription = makeCacheKeyDescription( *_pImporter, file, current._sourceHash, codeFingerprint );
                        bServedFromCache    = serveFromCache( *_pImporter, *_pDerivedDataCache, file, cacheKeyDescription );
                    }
                    if ( bServedFromCache == false )
                    {
                        if ( _pImporter->importSource( file._sourcePath, file._importedPath, file._assetPath ) == false )
                        {
                            StringBuilder<constant::buffer::kMax512> problemBuilder;
                            problemBuilder.appendFormat( "%#: 임포트하지 못했습니다", file._assetPath );
                            outResult._problem = string( problemBuilder.view() );
                            return;
                        }
                        // 임포트하는 동안 원본이 바뀌었으면 이 결과는 앞 원본 해시의 것이 아니다 — 적으면 스탬프 · DDC 가 앞 내용의 자리에 새 내용의 결과를 남겨,
                        // 원본을 되돌렸을 때 다른 결과를 "맞다" 로 답한다(유니티도 임포트 뒤 원본 해시를 다시 견준다).
                        if ( _pImporter->computeSourceHash( file._sourcePath, file._assetPath ) != current._sourceHash )
                        {
                            StringBuilder<constant::buffer::kMax512> problemBuilder;
                            problemBuilder.appendFormat( "%#: 임포트하는 동안 원본이 바뀌었습니다 - 다음 임포트가 다시 만듭니다", file._assetPath );
                            outResult._problem = string( problemBuilder.view() );
                            return;
                        }
                        if ( cacheKeyDescription.empty() == false )
                            storeInCache( *_pImporter, *_pDerivedDataCache, file, cacheKeyDescription );
                    }

                    current._importedHash = _pImporter->computeImportedHash( file._importedPath );
                    outResult._current    = current;
                    outResult._bStamped   = true;
                    outResult._bImported  = true;
                }

                /** @brief 사슬 하나의 이번 단계 파일을 파일 순서대로 임포트합니다. 첫 단계는 다른 원본의 결과를 읽는 파일을 둘째 단계로 미룬다(CheckOnly 는 임포트하지 않으므로 미루지 않는다). */
                void importChain( uint32 chainIndex ) const
                {
                    for ( const uint32 fileIndex : ( *_pListChain )[chainIndex] )
                    {
                        const SourceFile& file   = ( *_pListFile )[fileIndex];
                        FileResult&       result = _pListFileResult[fileIndex];
                        if ( _bDependentPhase )
                        {
                            if ( result._bDeferred == false )
                                continue;
                        }
                        else if ( _mode == AssetImportMode::ImportStale && _pImporter->hasOrderDependency( file._sourcePath, file._assetPath ) )
                        {
                            result._bDeferred = true;
                            continue;
                        }
                        if ( _pControl != nullptr && _pControl->isCanceled() )
                        {
                            result._bSkipped = true;
                            continue;
                        }
                        importFile( file, result );
                        if ( _pControl != nullptr )
                            _pControl->recordFinished();
                    }
                }

                /** @brief `_pListRunChain` 의 사슬을 다 떨어질 때까지 가져와 임포트합니다. 구간 인자는 쓰지 않습니다(어느 스레드가 몇 번 불리든 사슬은 한 번씩만 돕니다). */
                void flushPending( [[maybe_unused]] uint32 start, [[maybe_unused]] uint32 end )
                {
                    const uint32 slotCount = static_cast<uint32>( _pListRunChain->size() );
                    for ( ;; )
                    {
                        const uint32 slot = _nextSlot.fetch_add( 1, std::memory_order_relaxed );
                        if ( slot >= slotCount )
                            return;
                        importChain( ( *_pListRunChain )[slot] );
                    }
                }

                /**
                 * @brief @p listRunChain 의 사슬을 임포트합니다. 작업 시스템이 있으면 워커와 나눠 돌고, 아니면 이 스레드가 목록 순서대로 돕니다(결과는 같다).
                 * @details 텍스처 임포트의 밴드 압축(`TextureImporter`)이 이 안에서 다시 `runParallel` 을 부른다. 기다리는 스레드가 다른 일을 도우므로(`TaskManager::runParallelRange`) 중첩해도 막히지 않는다.
                 *          동시에 열린 원본은 참여자 수(워커 + 1)를 넘지 않는다 — 원본 사이 병렬의 메모리 상한이다.
                 */
                void importChains( const vector<uint32>& listRunChain )
                {
                    _pListRunChain          = &listRunChain;
                    const uint32 chainCount = static_cast<uint32>( listRunChain.size() );
                    _nextSlot.store( 0 );
                    if ( _pTaskManager == nullptr || chainCount <= 1 )
                    {
                        flushPending( 0, chainCount );
                        return;
                    }
                    // 참여자(워커 + 이 스레드)마다 청크 하나라 `flushPending` 이 청크 수만큼 불리고, 먼저 끝난 스레드가 남은 사슬을 이어서 가져간다.
                    const uint32 participantCount = MathUtil::min( chainCount, _pTaskManager->getWorkerCount() + 1 );
                    _pTaskManager->runParallel( participantCount, 1, SW_DELEGATE_METHOD( ParallelBlockDelegate, &ImportJob::flushPending, this ) );
                }

                /** @brief 두 단계를 차례로 돕니다. 사슬은 원본 크기가 큰 것부터 가져간다 — 무거운 원본이 끝에 혼자 남아 워커가 쉬지 않게(LPT 스케줄링). */
                void runPhases()
                {
                    vector<uint32> listRunChain( _pListChain->size() );
                    vector<uint64> listChainBytes( _pListChain->size(), 0 );
                    for ( uint32 chainIndex = 0; chainIndex < static_cast<uint32>( _pListChain->size() ); ++chainIndex )
                    {
                        listRunChain[chainIndex] = chainIndex;
                        for ( const uint32 fileIndex : ( *_pListChain )[chainIndex] )
                            listChainBytes[chainIndex] += ( *_pListFile )[fileIndex]._byteCount;
                    }
                    const auto isHeavier = [&listChainBytes]( uint32 lhs, uint32 rhs )
                    { return listChainBytes[lhs] != listChainBytes[rhs] ? listChainBytes[lhs] > listChainBytes[rhs] : lhs < rhs; };
                    VectorUtil::sort( listRunChain, isHeavier );
                    importChains( listRunChain );

                    const auto hasNoDeferred = [this]( uint32 chainIndex )
                    {
                        for ( const uint32 fileIndex : ( *_pListChain )[chainIndex] )
                        {
                            if ( _pListFileResult[fileIndex]._bDeferred )
                                return false;
                        }
                        return true;
                    };
                    VectorUtil::removeIf( listRunChain, hasNoDeferred );
                    _bDependentPhase = true;
                    importChains( listRunChain );
                }
            };

            /** @brief 결과 경로에서 확장자를 뗀 소문자 경로입니다. 임포트가 쓰는 자리(결과 파일과 같은 이름의 옆 폴더 이름)이고, 비어 있으면 결과 경로를 모릅니다. */
            static string makeFootprintKey( const string& importedPath )
            {
                if ( importedPath.empty() )
                    return {};
                return StringUtil::toLower( FileUtil::normalizeSeparators( FileUtil::removeExtension( importedPath ) ).c_str() );
            }

            /**
             * @brief 같은 자리에 쓰는 원본들을 한 사슬로 묶어 @p outListChain 에 담습니다. 사슬 안은 파일 순서이고 사슬은 첫 파일 순서입니다.
             * @details 한 원본의 임포트는 자기 결과 파일과 같은 이름의 옆 폴더를 지우고 다시 씁니다. 그래서 확장자만 다른 결과를 쓰거나(`x.png` → `x.heightfield` · `x_splat.png` → `x.layerweights`),
             *          한 쪽의 자리가 다른 쪽의 옆 폴더 아래이면(`x.glb` 의 `x/` 아래 `x/y.glb` 의 `y.mesh`) 병렬로 돌리면 서로 지우고 씁니다. 그 원본들을 한 사슬로 묶는다.
             */
            static void makeChains( const vector<SourceFile>& listFile, vector<vector<uint32>>& outListChain )
            {
                outListChain.clear();
                vector<string>        listKey;
                unordered_set<string> uniqueKey;
                for ( const SourceFile& file : listFile )
                {
                    listKey.push_back( makeFootprintKey( file._importedPath.str() ) );
                    if ( listKey.back().empty() == false )
                        uniqueKey.insert( listKey.back() );
                }

                unordered_map<string, uint32> mapRootToChain;
                for ( uint32 fileIndex = 0; fileIndex < static_cast<uint32>( listFile.size() ); ++fileIndex )
                {
                    const string& key = listKey[fileIndex];
                    if ( key.empty() )
                    {
                        outListChain.push_back( vector<uint32>{ fileIndex } );
                        continue;
                    }

                    // 가장 바깥의 조상 자리(경로 구간 경계)가 사슬의 뿌리다.
                    string      root = key;
                    string_view parent{ key };
                    for ( size_t slash = parent.rfind( '/' ); slash != string_view::npos; slash = parent.rfind( '/' ) )
                    {
                        parent = parent.substr( 0, slash );
                        if ( uniqueKey.find( string( parent ) ) != uniqueKey.end() )
                            root = string( parent );
                    }

                    const auto itChain = mapRootToChain.find( root );
                    if ( itChain == mapRootToChain.end() )
                    {
                        mapRootToChain.emplace( root, static_cast<uint32>( outListChain.size() ) );
                        outListChain.push_back( vector<uint32>{ fileIndex } );
                    }
                    else
                    {
                        outListChain[itChain->second].push_back( fileIndex );
                    }
                }
            }

            /** @brief 원본 폴더 하나(파일 @p listContentFile)의 스탬프를 읽고 임포트할 원본을 @p inoutListFile 끝에 더합니다(직렬 준비). @p pScope 밖의 원본은 읽지도 해시하지도 않는다. */
            static void collectRawFolder( const string& rootDir, const AbsolutePath& rawFolder, const vector<EditorContentFile>& listContentFile, const IRawAssetImporter& importer,
                                          const unordered_set<string>* pScope, vector<RawFolder>& inoutListFolder, vector<SourceFile>& inoutListFile )
            {
                RawFolder folder;
                folder._path      = rawFolder;
                folder._firstFile = inoutListFile.size();
                readStamp( rawFolder.join( importer.getStampFileName() ), importer.getStampHeader(), folder._mapStamped );

                unordered_map<string, uint64> mapByteCount;
                vector<AbsolutePath>          listPath;
                for ( const EditorContentFile& contentFile : listContentFile )
                {
                    listPath.push_back( FileUtil::toDiskPath( contentFile._path ) );
                    mapByteCount.emplace( listPath.back().str(), contentFile._fileStamp._sizeBytes );
                }
                AssetSourceLocator::collectStoredSources( rawFolder, listPath );
                VectorUtil::sort( listPath );

                for ( const AbsolutePath& path : listPath )
                {
                    SourceFile file;
                    file._sourcePath           = path;
                    const bool bSkipSourceFile = importer.isSourceFile( file._sourcePath.str() ) == false || file._sourcePath.str().size() <= rawFolder.str().size() + 1 || file._sourcePath.str().size() <= rootDir.size() + 1;
                    if ( bSkipSourceFile )
                        continue;

                    file._relativePath = file._sourcePath.str().substr( rawFolder.str().size() + 1 );
                    file._assetPath    = file._sourcePath.str().substr( rootDir.size() + 1 );
                    file._importedPath = importer.makeImportedPath( file._sourcePath );
                    file._folderIndex  = static_cast<uint32>( inoutListFolder.size() );
                    const auto itBytes = mapByteCount.find( file._sourcePath.str() );
                    file._byteCount    = itBytes != mapByteCount.end() ? itBytes->second : 0; // 저장소 주소만 적은 원본은 크기를 모른다 — 순서에만 쓰는 값이다
                    // 두 원본이 한 결과에 쓰면(`a.jpg` · `a.png` → `a.dds`) 돌 때마다 서로 덮어 둘 다 영원히 어긋난다 — 뒤의 것을 문제로 알리고 임포트하지 않는다.
                    const auto owner       = folder._mapImportedOwner.emplace( makeImportedKey( file._importedPath.str() ), file._assetPath );
                    const bool bOutOfScope = pScope != nullptr && pScope->find( makeScopeKey( file._assetPath ) ) == pScope->end();
                    if ( bOutOfScope )
                        continue;
                    if ( file._importedPath.empty() == false && owner.second == false )
                    {
                        StringBuilder<constant::buffer::kMax512> problemBuilder;
                        problemBuilder.appendFormat( "%#: '%#' 와 같은 결과에 씁니다 - 이름을 바꾸거나 하나를 지웁니다", file._assetPath, owner.first->second );
                        folder._listProblem.push_back( string( problemBuilder.view() ) );
                        folder._uniqueRejected.insert( file._relativePath );
                        continue;
                    }
                    inoutListFile.push_back( std::move( file ) );
                }
                folder._fileCount = inoutListFile.size() - folder._firstFile;
                inoutListFolder.push_back( std::move( folder ) );
            }

            /**
             * @brief 원본 폴더 하나의 결과를 요약에 합치고, ImportStale 이면 사라진 원본의 결과를 지우고 스탬프를 다시 씁니다(직렬, 파일 순서).
             * @details 스탬프는 쓰기 직전에 다시 읽어 이번에 본 줄만 바꾼다 — 그 사이 다른 실행(에디터 핫 리로드와 `App --import-*`)이 적은 줄을 지우지 않는다. 지워져도 다음 임포트가 DDC 에서 되살리므로
             *          파일 잠금은 두지 않는다.
             */
            static void mergeRawFolder( const string& rootDir, const RawFolder& folder, const vector<SourceFile>& listFile, const vector<FileResult>& listFileResult, const IRawAssetImporter& importer,
                                        AssetImportMode mode, const unordered_set<string>* pScope, AssetImportSummary& inoutSummary )
            {
                const AbsolutePath& rawFolder = folder._path;
                inoutSummary._sourceCount += static_cast<uint32>( folder._listProblem.size() );
                inoutSummary._listProblem.insert( inoutSummary._listProblem.end(), folder._listProblem.begin(), folder._listProblem.end() );

                map<string, StampEntry> mapCurrent;
                unordered_set<string>   uniqueChecked = folder._uniqueRejected; // 이번에 판정한 원본 — 이 줄들은 판정대로 바꾼다
                for ( size_t fileIndex = folder._firstFile; fileIndex < folder._firstFile + folder._fileCount; ++fileIndex )
                {
                    const FileResult& fileResult = listFileResult[fileIndex];
                    ++inoutSummary._sourceCount;
                    if ( fileResult._bSkipped )
                    {
                        inoutSummary._listSkippedPath.push_back( listFile[fileIndex]._assetPath );
                        continue;
                    }
                    uniqueChecked.insert( listFile[fileIndex]._relativePath );
                    if ( fileResult._problem.empty() == false )
                        inoutSummary._listProblem.push_back( fileResult._problem );
                    if ( fileResult._bStamped )
                        mapCurrent[listFile[fileIndex]._relativePath] = fileResult._current;
                    if ( fileResult._bImported )
                        ++inoutSummary._importedCount;
                }

                // 이번에 보지 않은 줄 — 범위 밖 · 취소한 원본, 훑은 뒤 다른 실행이 더한 원본. 지우면 다음 전체 대조가 그 원본들을 모두 다시 임포트한다.
                const AbsolutePath      stampPath = rawFolder.join( importer.getStampFileName() );
                map<string, StampEntry> mapOnDisk;
                if ( mode == AssetImportMode::ImportStale )
                    readStamp( stampPath, importer.getStampHeader(), mapOnDisk );
                for ( const auto& [relativePath, entry] : mapOnDisk )
                {
                    const bool bKeep = uniqueChecked.contains( relativePath ) == false && ( pScope != nullptr || FileUtil::exists( rawFolder.join( relativePath ) ) );
                    if ( bKeep )
                        mapCurrent.emplace( relativePath, entry );
                }

                // 원본이 사라진 줄 — 그 결과(임포터가 답한 파일)도 지운다. 결과는 생성물이라 깨끗한 클론에는 없다. 남겨 두면 지운 원본을 아직 가리키는 코드 · 데이터가
                // 이 PC 에서만 그려져 숨는다(2026-11-02 — 지운 원본의 `nilecity/models/tree_large.mesh` 를 C++ 표가 계속 읽었다). 옆 폴더 아래에 남은 원본의 자리가 있으면
                // (`x.glb` 를 지웠는데 `x/y.glb` 는 있다) 옆 폴더는 두고 결과 파일만 지운다.
                // 남은 원본의 자리마다 조상 자리를 한 번 색인한다 — 지울 결과마다 남은 원본 전체를 훑으면 지운 수 × 남은 수다. 지울 것이 있을 때만 만든다.
                unordered_set<string> uniqueLiveAncestor;
                bool                  bLiveAncestorBuilt = false;
                for ( const auto& [relativePath, entry] : folder._mapStamped )
                {
                    if ( pScope != nullptr || mapCurrent.find( relativePath ) != mapCurrent.end() || uniqueChecked.contains( relativePath ) )
                        continue;
                    const AbsolutePath sourcePath = rawFolder.join( relativePath );
                    if ( FileUtil::exists( sourcePath ) )
                        continue; // 원본은 있다 — 위에서 어긋남 · 실패로 이미 보고했다
                    if ( mode == AssetImportMode::CheckOnly )
                    {
                        StringBuilder<constant::buffer::kMax512> problemBuilder;
                        problemBuilder.appendFormat( "%#: 원본이 없는데 스탬프에 남아 있습니다", string_view( sourcePath.str() ).substr( rootDir.size() + 1 ) );
                        inoutSummary._listProblem.push_back( string( problemBuilder.view() ) );
                        continue;
                    }
                    const AbsolutePath importedPath = importer.makeImportedPath( sourcePath );
                    if ( folder._mapImportedOwner.contains( makeImportedKey( importedPath.str() ) ) )
                        continue; // 남은 원본이 그 결과의 주인이다(`a.png` 를 지우고 `a.jpg` 를 두었다)
                    if ( bLiveAncestorBuilt == false )
                    {
                        for ( const auto& [importedKey, ownerPath] : folder._mapImportedOwner )
                            collectFootprintAncestors( importedKey, uniqueLiveAncestor );
                        bLiveAncestorBuilt = true;
                    }
                    removeOrphanedResult( importer, importedPath, uniqueLiveAncestor, inoutSummary );
                }

                if ( mode == AssetImportMode::CheckOnly )
                    return;
                writeStampIfChanged( stampPath, importer.getStampHeader(), mapCurrent, inoutSummary );
            }

            /** @brief 결과 경로의 비교 열쇠입니다(구분자를 `/` 로 맞춘 소문자 — 파일 이름의 대소문자는 OS 가 구분하지 않는다). */
            static string makeImportedKey( const string& importedPath ) { return StringUtil::toLower( FileUtil::normalizeSeparators( importedPath ).c_str() ); }

            /** @brief 남은 원본의 결과 @p importedPath 의 자리(`makeFootprintKey`)에서 그 조상 자리(`a/b/c` → `a/b` · `a`)를 모읍니다. */
            static void collectFootprintAncestors( const string& importedPath, unordered_set<string>& outUniqueAncestor )
            {
                const string key = makeFootprintKey( importedPath );
                for ( size_t slash = key.rfind( '/' ); slash != string::npos && slash > 0; slash = key.rfind( '/', slash - 1 ) )
                    outUniqueAncestor.insert( key.substr( 0, slash ) );
            }

            /**
             * @brief 원본이 사라진 결과를 지웁니다 — 임포터가 자기 결과라고 답한 파일(`collectImportedFiles`)만 지우고, 비게 된 옆 폴더를 거둡니다. 못 지우면 문제로 남깁니다.
             * @details 이름이 같다고 옆 폴더를 통째로 지우지 않는다 — 텍스처처럼 옆 폴더를 만들지 않는 종류의 같은 이름 폴더(`textures/rock/`)에는 커밋한 파일이 있을 수 있다.
             *          공유 저장소 파일은 다른 원본도 쓰므로 두고(`ImportedAssetStore::removeUnreferenced` 가 거둔다), 옆 폴더 아래에 남은 원본의 결과 자리가 있으면
             *          (@p uniqueLiveAncestor — 남은 자리의 조상 `collectFootprintAncestors`) 옆 폴더 안은 두고 나머지만 지운다.
             */
            static void removeOrphanedResult( const IRawAssetImporter& importer, const AbsolutePath& importedPath, const unordered_set<string>& uniqueLiveAncestor, AssetImportSummary& inoutSummary )
            {
                const string footprint = makeFootprintKey( importedPath.str() );
                if ( footprint.empty() )
                    return;
                const AbsolutePath sideFolder = importedPath.removeExtension();
                const bool         bSideInUse = uniqueLiveAncestor.contains( footprint );

                vector<AbsolutePath> listResult;
                importer.collectImportedFiles( importedPath, listResult );
                for ( const AbsolutePath& result : listResult )
                {
                    const bool bInSideFolder = FileUtil::isPathAtOrUnderRoot( sideFolder.str(), FileUtil::normalizeSeparators( result.str() ) );
                    if ( isSharedStorePath( result.str() ) || ( bSideInUse && bInSideFolder ) || FileUtil::removeFile( result ) )
                        continue;
                    StringBuilder<constant::buffer::kMax512> problemBuilder;
                    problemBuilder.appendFormat( "%#: 원본이 사라진 결과를 지우지 못했습니다", result.str() );
                    inoutSummary._listProblem.push_back( string( problemBuilder.view() ) );
                }

                vector<AbsolutePath> listRemaining;
                const bool           bSideFolder = FileUtil::isDirectory( sideFolder );
                const bool           bSideEmpty  = bSideFolder && EditorContentScan::collectFiles( sideFolder, "", listRemaining, true ) && listRemaining.empty();
                if ( bSideEmpty && FileUtil::removeDirectory( sideFolder ) == false )
                {
                    StringBuilder<constant::buffer::kMax512> problemBuilder;
                    problemBuilder.appendFormat( "%#: 원본이 사라진 결과의 옆 폴더를 지우지 못했습니다", sideFolder.str() );
                    inoutSummary._listProblem.push_back( string( problemBuilder.view() ) );
                }
                SW_LOG_INFO_ID( "AssetImportStamp.RemovedOrphanedResult", "Removed '%#' - its source is gone", importedPath.str() );
            }

            /** @brief 범위 표의 열쇠입니다 — 구분자를 `/` 로 맞춘 소문자 리소스 경로(파일 이름의 대소문자는 OS 가 구분하지 않는다). */
            static string makeScopeKey( string_view assetPath ) { return StringUtil::toLower( FileUtil::normalizeSeparators( assetPath ).c_str() ); }

            /** @brief @p pScope 의 원본이 든 원본 폴더(절대 경로)를 모읍니다 — 리소스 트리를 훑지 않는다(비용이 바뀐 파일 수에 비례한다). */
            static void collectScopeFolders( const AbsolutePath& rootDir, const vector<string>& listAssetPath, string_view rawFolderName, vector<AbsolutePath>& outListFolder )
            {
                unordered_set<string> uniqueFolder;
                for ( const string& assetPath : listAssetPath )
                {
                    string rawFolder;
                    string relativePath;
                    if ( splitRawPath( assetPath, rawFolderName, rawFolder, relativePath ) == false || uniqueFolder.insert( makeScopeKey( rawFolder ) ).second == false )
                        continue;
                    const AbsolutePath folder = rootDir.join( rawFolder );
                    if ( FileUtil::isDirectory( folder ) )
                        outListFolder.push_back( folder );
                }
            }

            /** @brief 리소스 루트를 한 번 훑어 원본 폴더마다 파일을 모읍니다(폴더 경로 순). 원본 폴더를 먼저 찾고 다시 훑지 않는다. 옛 작업 도메인 자리의 찌꺼기는 `EditorContentScan` 이 뺀다. 훑지 못하면 false 입니다. */
            [[nodiscard]] static bool collectRawFolders( const AbsolutePath& rootDir, string_view rawFolderName, map<string, vector<EditorContentFile>>& outMapFolder )
            {
                vector<EditorContentFile> listTreeFile;
                if ( rootDir.empty() || EditorContentScan::collectTreeFiles( rootDir, AbsolutePath{}, listTreeFile ) == false )
                    return false;
                for ( EditorContentFile& file : listTreeFile )
                {
                    string rawFolder;
                    string relativePath;
                    if ( splitRawPath( file._path, rawFolderName, rawFolder, relativePath ) )
                        outMapFolder[rawFolder].push_back( std::move( file ) );
                }
                return true;
            }

            /**
             * @brief 리소스 루트 아래 원본 폴더들을 스탬프와 대조하고, ImportStale 이면 어긋난 것을 임포트합니다. @p pListScopePath 가 있으면 그 원본(리소스 경로)만 봅니다.
             * @details 단계는 준비(직렬: 폴더마다 스탬프 · 파일 목록, 모든 폴더에 걸친 사슬) → 병렬 임포트(다른 원본의 결과를 읽지 않는 원본, 그 뒤 읽는 원본) → 합치기(직렬, 폴더 · 파일 순서)입니다.
             *          폴더마다 끝을 기다리지 않는다 — 폴더 하나의 무거운 원본이 도는 동안 다른 폴더의 원본이 워커를 채운다.
             *          파일마다 결과가 자기 칸에 담기고 요약 · 스탬프 표는 폴더 · 파일 순서대로만 만들어지므로, 문제 목록의 순서와 스탬프의 내용은 직렬로 돌 때와 같습니다.
             */
            static AssetImportSummary importFolders( const AbsolutePath& resourceRoot, const IRawAssetImporter& importer, AssetImportMode mode, const vector<string>* pListScopePath,
                                                     TaskManager* pTaskManager, const DerivedDataCache* pDerivedDataCache, const AssetImportControl* pControl )
            {
                AssetImportSummary                     summary;
                const AbsolutePath                     rootDir = resourceRoot.join( {} );
                map<string, vector<EditorContentFile>> mapRawFolder;
                unordered_set<string>                  uniqueScope;
                if ( pListScopePath != nullptr )
                {
                    for ( const string& assetPath : *pListScopePath )
                        uniqueScope.insert( makeScopeKey( assetPath ) );
                    vector<AbsolutePath> listScopeFolder;
                    collectScopeFolders( rootDir, *pListScopePath, importer.getRawFolderName(), listScopeFolder );
                    for ( const AbsolutePath& rawFolder : listScopeFolder )
                        (void)EditorContentScan::collectTreeFiles( rawFolder, AbsolutePath{}, mapRawFolder[rawFolder.str()] ); // 폴더가 있는 것을 방금 보았다 — 도중에 멈춘 목록은 훑기가 알린다
                }
                else if ( collectRawFolders( rootDir, importer.getRawFolderName(), mapRawFolder ) == false )
                {
                    StringBuilder<constant::buffer::kMax512> problemBuilder;
                    problemBuilder.appendFormat( "리소스 루트를 훑지 못했습니다: %#", rootDir.str() );
                    summary._listProblem.push_back( string( problemBuilder.view() ) );
                    return summary;
                }

                const unordered_set<string>* const pScope = pListScopePath != nullptr ? &uniqueScope : nullptr;
                vector<RawFolder>                  listFolder;
                vector<SourceFile>                 listFile;
                for ( const auto& [rawFolder, listContentFile] : mapRawFolder )
                    collectRawFolder( rootDir.str(), FileUtil::toDiskPath( rawFolder ), listContentFile, importer, pScope, listFolder, listFile );

                vector<vector<uint32>> listChain;
                makeChains( listFile, listChain );
                vector<FileResult> listFileResult( listFile.size() );
                ImportJob          job;
                job._pImporter         = &importer;
                job._pListFolder       = &listFolder;
                job._pListFile         = &listFile;
                job._pListChain        = &listChain;
                job._pListFileResult   = listFileResult.data();
                job._mode              = mode;
                job._pTaskManager      = pTaskManager;
                job._pDerivedDataCache = mode == AssetImportMode::ImportStale ? pDerivedDataCache : nullptr;
                job._pControl          = pControl;
                job.runPhases();

                for ( const RawFolder& folder : listFolder )
                {
                    mergeRawFolder( rootDir.str(), folder, listFile, listFileResult, importer, mode, pScope, summary );
                    for ( const string& rejectedPath : folder._uniqueRejected )
                        summary._listSourcePath.push_back( folder._path.join( rejectedPath ).str().substr( rootDir.str().size() + 1 ) );
                }
                for ( const SourceFile& file : listFile )
                    summary._listSourcePath.push_back( file._assetPath );
                VectorUtil::sort( summary._listSourcePath );
                return summary;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    uint64 IRawAssetImporter::computeImportedHash( const AbsolutePath& importedPath ) const
    {
        return AssetImportStampUtil::computeFileHash( importedPath );
    }

    void IRawAssetImporter::collectImportedFiles( const AbsolutePath& importedPath, vector<AbsolutePath>& outListPath ) const
    {
        if ( FileUtil::isRegularFile( importedPath ) )
            outListPath.push_back( importedPath );
    }

    AssetImportSummary AssetImportStampUtil::importAll( const AbsolutePath& resourceRoot, const IRawAssetImporter& importer, AssetImportMode mode, TaskManager* pTaskManager,
                                                        const DerivedDataCache& derivedDataCache )
    {
        return AssetImportStampInternal::importFolders( resourceRoot, importer, mode, nullptr, pTaskManager, &derivedDataCache, nullptr );
    }

    AssetImportSummary AssetImportStampUtil::importSources( const AbsolutePath& resourceRoot, const IRawAssetImporter& importer, const vector<string>& listAssetPath,
                                                            TaskManager* pTaskManager, const DerivedDataCache& derivedDataCache, const AssetImportControl* pControl )
    {
        return AssetImportStampInternal::importFolders( resourceRoot, importer, AssetImportMode::ImportStale, &listAssetPath, pTaskManager, &derivedDataCache, pControl );
    }

    void AssetImportStampUtil::importChangedSources( AssetSourceBatch& batch, const vector<AssetSourceBatchEntry>& listEntry, const IRawAssetImporter& importer )
    {
        using Internal = AssetImportStampInternal;
        if ( listEntry.empty() )
            return;
        vector<AbsolutePath> listRoot;
        for ( const AssetSourceBatchEntry& entry : listEntry )
        {
            AssetSourceBatchItem& item = batch._listItem[entry._itemIndex];
            item._bRan                 = true;
            item._bHandled             = true;
            VectorUtil::addUnique( listRoot, batch._pAssetPaths->getRootFolderPathFor( item._relativePath ) );
        }
        const expected<DerivedDataCache> derivedDataCache = DerivedDataCache::open( DerivedDataBucket::Import );
        if ( derivedDataCache.has_value() == false )
        {
            SW_LOG_ERROR_ID( "AssetImportStamp.DerivedDataConfigUnreadable", "Import of '%#' skipped: %# (%#)", importer.getRawFolderName(), derivedDataCache.error().message(), derivedDataCache.error().detail() );
            return;
        }
        for ( const AbsolutePath& root : listRoot )
        {
            vector<string> listSourcePath;
            for ( const AssetSourceBatchEntry& entry : listEntry )
            {
                if ( batch._pAssetPaths->getRootFolderPathFor( batch._listItem[entry._itemIndex]._relativePath ) == root )
                    listSourcePath.insert( listSourcePath.end(), entry._listSourcePath.begin(), entry._listSourcePath.end() );
            }
            const AssetImportSummary summary = importSources( root, importer, listSourcePath, batch._pTaskManager, *derivedDataCache, &batch._control );
            for ( const string& problem : summary._listProblem )
            {
                SW_LOG_ERROR_ID( "AssetImportStamp.ChangedSourceNotImported", "Import failed: %#", problem.c_str() );
            }
            unordered_set<string> uniqueSkipped;
            for ( const string& skippedPath : summary._listSkippedPath )
                uniqueSkipped.insert( Internal::makeScopeKey( skippedPath ) );
            for ( const AssetSourceBatchEntry& entry : listEntry )
            {
                AssetSourceBatchItem& item = batch._listItem[entry._itemIndex];
                for ( const string& sourcePath : entry._listSourcePath )
                {
                    if ( uniqueSkipped.contains( Internal::makeScopeKey( sourcePath ) ) == false )
                        continue;
                    item._bRan     = false;
                    item._bHandled = false;
                }
            }
            SW_LOG_INFO_ID( "AssetImportStamp.ChangedSourcesImported", "Import of '%#': %# changed -> %# imported", importer.getRawFolderName(), static_cast<uint32>( listSourcePath.size() ), summary._importedCount );
        }
    }

    bool AssetImportStampUtil::isUnderRawFolder( string_view path, string_view rawFolderName )
    {
        string rawFolder;
        string relativePath;
        return AssetImportStampInternal::splitRawPath( path, rawFolderName, rawFolder, relativePath );
    }

    string AssetImportStampUtil::makeImportedPath( string_view rawPath, string_view rawFolderName, string_view importedFolderName, string_view importedExtension )
    {
        string rawFolder;
        string relativePath;
        if ( AssetImportStampInternal::splitRawPath( rawPath, rawFolderName, rawFolder, relativePath ) == false )
            return {};

        const string domainFolder   = FileUtil::getDirectoryPart( rawFolder );
        const string importedFolder = domainFolder.empty() ? string( importedFolderName ) : FileUtil::joinPath( domainFolder, importedFolderName );
        return FileUtil::replaceExtension( FileUtil::joinPath( importedFolder, relativePath ), importedExtension );
    }

    AbsolutePath AssetImportStampUtil::makeImportedPath( const AbsolutePath& rawPath, string_view rawFolderName, string_view importedFolderName, string_view importedExtension )
    {
        string rawFolder;
        string relativePath;
        if ( AssetImportStampInternal::splitRawPath( rawPath.str(), rawFolderName, rawFolder, relativePath ) == false )
            return {};
        return FileUtil::toDiskPath( rawFolder ).getParent().join( importedFolderName ).join( relativePath ).removeExtension().appendSuffix( importedExtension );
    }

    uint64 AssetImportStampUtil::computeFileHash( const AbsolutePath& path )
    {
        FileStamp  stamp{};
        const bool bMissingOrEmpty = FileUtil::getFileStamp( path, stamp ) == false || stamp._sizeBytes == 0;
        return bMissingOrEmpty ? 0 : ContentHash::computeFile( path );
    }
} // namespace sw::editor
