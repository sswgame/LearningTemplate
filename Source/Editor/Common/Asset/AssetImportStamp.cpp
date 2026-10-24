#include "pch.h"

#include "Editor/Common/Asset/AssetImportStamp.h"

#include "Core/Common/HashUtil.h"
#include "Core/Common/StdHeaders.h"
#include "Core/Concurrency/atomic.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/map.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/Delegate/Delegate.h"
#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Task/TaskManager.h"

#include "Editor/Common/Workspace/EditorService.h"

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
            /** @brief 원본 폴더마다 하나 두는 스탬프 파일 이름입니다. */
            static constexpr string_view kImportStampFileName = "import.stamp";

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
                StringBuilder<constant::kMaxBuffer32> text;
                text.appendFormat( "%#", Fmt( hash, Format( 16, Format::Padding::Zero ).hex() ) );
                return string( text.c_str(), text.size() );
            }

            /** @brief 스탬프를 읽습니다. 없거나 머리 줄이 다르면 빈 표입니다(= 모든 원본이 어긋남). 형식이 아닌 줄은 건너뜁니다. */
            static void readStamp( const string& stampPath, string_view header, map<string, StampEntry>& outMapEntry )
            {
                outMapEntry.clear();
                string text;
                if ( FileUtil::exists( stampPath ) == false || FileUtil::readTextFile( stampPath, text ) == false )
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
                string text = string( header ) + "\n";
                for ( const auto& [relativePath, entry] : mapEntry )
                {
                    text += formatHash( entry._sourceHash );
                    text += ' ';
                    text += formatHash( entry._importedHash );
                    text += ' ';
                    text += relativePath;
                    text += '\n';
                }
                return text;
            }

            /** @brief 원본 하나가 스탬프와 어긋난 이유입니다. 맞으면 빈 문자열입니다. */
            static string findDriftReason( const StampEntry* pStamped, const StampEntry& current, string_view importedLabel )
            {
                if ( current._sourceHash == 0 )
                    return "원본을 읽지 못했습니다";
                if ( current._importedHash == 0 )
                    return string( importedLabel ) + " 가 없습니다";
                if ( pStamped == nullptr )
                    return "스탬프에 없습니다 (임포트된 적이 없습니다)";
                if ( pStamped->_sourceHash != current._sourceHash )
                    return "원본이나 규칙이 바뀌었는데 다시 임포트하지 않았습니다";
                if ( pStamped->_importedHash != current._importedHash )
                    return string( importedLabel ) + " 가 임포트된 결과와 다릅니다 (손으로 바꿨습니다)";
                return {};
            }

            /** @brief 스탬프를 씁니다. 내용이 같으면 쓰지 않습니다 — 맞는 트리에서 임포트를 돌려도 작업 트리가 더러워지지 않습니다. */
            static void writeStampIfChanged( const string& stampPath, string_view header, const map<string, StampEntry>& mapCurrent, AssetImportSummary& inoutSummary )
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
                    inoutSummary._listProblem.push_back( "스탬프를 쓰지 못했습니다: " + stampPath );
            }

            /** @brief 원본 파일 하나의 입력입니다. 준비 단계(직렬)가 채우고, 병렬 구간에서는 읽기만 합니다. */
            struct SourceFile
            {
                string _sourcePath;
                string _relativePath; ///< 원본 폴더 기준
                string _resourcePath; ///< 리소스 루트 기준
                string _importedPath;
            };

            /** @brief 원본 파일 하나의 결과입니다. 파일마다 자기 칸에만 쓰고, 요약과 스탬프 표에 합치는 일은 파일 순서대로 직렬로 합니다. */
            struct FileResult
            {
                string     _problem;            ///< 비어 있지 않으면 `AssetImportSummary::_listProblem` 에 더할 한 줄
                StampEntry _current;            ///< `_bStamped` 일 때 스탬프에 쓸 줄
                bool       _bStamped{ false };  ///< 맞거나 방금 임포트해 스탬프 표에 넣는다
                bool       _bImported{ false }; ///< 이번에 임포트했다
            };

            /**
             * @brief 원본 폴더 하나의 파일들을 임포트하는 일입니다. 병렬 구간에서 입력은 읽기만 하고 파일마다 자기 결과 칸(`_pListFileResult[파일 번호]`)에만 씁니다.
             * @details 병렬 단위는 **사슬**(같은 결과 자리에 쓰는 파일의 묶음, `makeChains`)이라 두 스레드가 한 결과 경로를 쓰거나 지우지 않습니다.
             *          스레드는 원자 카운터에서 사슬을 하나씩 가져옵니다(무거운 원본 하나가 청크 끝을 늦추지 않게 동적으로 나눈다). 임포터 함수는 모두 스레드 안전해야 합니다(`IRawAssetImporter`).
             */
            struct ImportJob
            {
                const IRawAssetImporter*       _pImporter{ nullptr };
                const map<string, StampEntry>* _pMapStamped{ nullptr };
                const vector<SourceFile>*      _pListFile{ nullptr };
                const vector<vector<uint32>>*  _pListChain{ nullptr };
                const vector<uint32>*          _pListRunChain{ nullptr };   ///< 이번 구간에서 돌 사슬 번호
                FileResult*                    _pListFileResult{ nullptr }; ///< 파일 수만큼
                atomic<uint32>                 _nextSlot{ 0 };              ///< `_pListRunChain` 에서 다음에 가져갈 칸
                AssetImportMode                _mode{ AssetImportMode::ImportStale };

                /** @brief 원본 하나를 대조하고, 어긋났고 ImportStale 이면 임포트합니다. 결과는 @p outResult 에만 씁니다. */
                void importFile( const SourceFile& file, FileResult& outResult ) const
                {
                    const utf8* const pUnsupportedReason = _pImporter->findUnsupportedReason( file._sourcePath );
                    if ( pUnsupportedReason != nullptr )
                    {
                        outResult._problem = file._resourcePath + ": " + pUnsupportedReason;
                        return;
                    }

                    StampEntry current;
                    current._sourceHash   = _pImporter->computeSourceHash( file._sourcePath, file._resourcePath );
                    current._importedHash = _pImporter->computeImportedHash( file._importedPath );

                    const auto   itStamped = _pMapStamped->find( file._relativePath );
                    const string reason    = findDriftReason( itStamped != _pMapStamped->end() ? &itStamped->second : nullptr, current, _pImporter->getImportedLabel() );
                    if ( reason.empty() )
                    {
                        outResult._current  = current;
                        outResult._bStamped = true;
                        return;
                    }

                    if ( _mode == AssetImportMode::CheckOnly )
                    {
                        outResult._problem = file._resourcePath + ": " + reason;
                        return;
                    }

                    if ( _pImporter->importSource( file._sourcePath, file._importedPath, file._resourcePath ) == false )
                    {
                        outResult._problem = file._resourcePath + ": 임포트하지 못했습니다";
                        return;
                    }

                    current._importedHash = _pImporter->computeImportedHash( file._importedPath );
                    outResult._current    = current;
                    outResult._bStamped   = true;
                    outResult._bImported  = true;
                }

                /** @brief 사슬 하나를 파일 순서대로 임포트합니다. */
                void importChain( uint32 chainIndex ) const
                {
                    for ( const uint32 fileIndex : ( *_pListChain )[chainIndex] )
                    {
                        importFile( ( *_pListFile )[fileIndex], _pListFileResult[fileIndex] );
                    }
                }

                /** @brief `_pListRunChain` 의 사슬을 다 떨어질 때까지 가져와 임포트합니다. 구간 인자는 쓰지 않습니다(어느 스레드가 몇 번 불리든 사슬은 한 번씩만 돕니다). */
                void drain( [[maybe_unused]] uint32 start, [[maybe_unused]] uint32 end )
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
                 * @brief @p listRunChain 의 사슬을 임포트합니다. @p bParallel 이고 작업 시스템이 있으면 워커와 나눠 돌고, 아니면 이 스레드가 사슬 순서대로 돕니다(결과는 같다).
                 * @details 텍스처 임포트의 밴드 압축(`TextureImporter`)이 이 안에서 다시 `runParallel` 을 부른다. 기다리는 스레드가 다른 일을 도우므로(`TaskManager::runParallelRange`) 중첩해도 막히지 않는다.
                 */
                void importChains( const vector<uint32>& listRunChain, bool bParallel )
                {
                    _pListRunChain          = &listRunChain;
                    const uint32 chainCount = static_cast<uint32>( listRunChain.size() );
                    _nextSlot.store( 0 );
                    TaskManager* const pTaskManager = bParallel && chainCount > 1 ? editor::findSubsystem<TaskManager>() : nullptr;
                    if ( pTaskManager == nullptr )
                    {
                        drain( 0, chainCount );
                        return;
                    }
                    // 참여자(워커 + 이 스레드)마다 청크 하나라 `drain` 이 청크 수만큼 불리고, 먼저 끝난 스레드가 남은 사슬을 이어서 가져간다.
                    const uint32 participantCount = MathUtil::min( chainCount, pTaskManager->getWorkerCount() + 1 );
                    pTaskManager->runParallel( participantCount, 1, SW_DELEGATE_METHOD( ParallelBlockDelegate, &ImportJob::drain, this ) );
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
             * @details 한 원본의 임포트는 자기 결과 파일과 같은 이름의 옆 폴더를 지우고 다시 씁니다. 그래서 결과 경로가 같거나(`a.glb` · `a.gltf` → `a.mesh`),
             *          한 쪽의 자리가 다른 쪽의 옆 폴더 아래이면(`x.glb` 의 `x/` 아래 `x/y.glb` 의 `y.mesh`) 병렬로 돌리면 서로 지우고 씁니다. 그 원본들을 한 사슬로 묶는다.
             */
            static void makeChains( const vector<SourceFile>& listFile, vector<vector<uint32>>& outListChain )
            {
                outListChain.clear();
                vector<string>        listKey;
                unordered_set<string> uniqueKey;
                for ( const SourceFile& file : listFile )
                {
                    listKey.push_back( makeFootprintKey( file._importedPath ) );
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

            /**
             * @brief 원본 폴더 하나를 그 폴더의 스탬프와 대조하고, ImportStale 이면 어긋난 것을 임포트하고 스탬프를 다시 씁니다.
             * @details 단계는 준비(직렬: 파일 목록 · 사슬) → 병렬 임포트(의존 없는 사슬) → 직렬 임포트(`hasOrderDependency` 인 원본이 든 사슬) → 합치기(직렬, 파일 순서)입니다.
             *          파일마다 결과가 자기 칸에 담기고 요약 · 스탬프 표는 파일 순서대로만 만들어지므로, 문제 목록의 순서와 스탬프의 내용은 직렬로 돌 때와 같습니다.
             */
            static void importRawFolder( const string& rootDir, const string& rawFolder, const IRawAssetImporter& importer, AssetImportMode mode,
                                         AssetImportSummary& inoutSummary )
            {
                const string            stampPath = FileUtil::joinPath( rawFolder, kImportStampFileName );
                map<string, StampEntry> mapStamped;
                readStamp( stampPath, importer.getStampHeader(), mapStamped );

                vector<string> listPath;
                (void)FileUtil::collectFiles( rawFolder, "", listPath, true ); // 폴더는 방금 훑어서 찾았다
                std::sort( listPath.begin(), listPath.end() );

                vector<SourceFile> listFile;
                for ( const string& path : listPath )
                {
                    SourceFile file;
                    file._sourcePath           = FileUtil::normalizeSeparators( path );
                    const bool bSkipSourceFile = importer.isSourceFile( file._sourcePath ) == false || file._sourcePath.size() <= rawFolder.size() + 1 || file._sourcePath.size() <= rootDir.size() + 1;
                    if ( bSkipSourceFile )
                        continue;

                    file._relativePath = file._sourcePath.substr( rawFolder.size() + 1 );
                    file._resourcePath = file._sourcePath.substr( rootDir.size() + 1 );
                    file._importedPath = importer.makeImportedPath( file._sourcePath );
                    listFile.push_back( std::move( file ) );
                }

                vector<vector<uint32>> listChain;
                makeChains( listFile, listChain );
                // 다른 원본의 결과를 읽는 원본이 든 사슬은 의존이 없는 사슬이 모두 끝난 뒤 직렬로 임포트한다. CheckOnly 는 아무것도 임포트하지 않으므로 나눌 이유가 없다.
                vector<uint32> listParallelChain;
                vector<uint32> listSerialChain;
                for ( uint32 chainIndex = 0; chainIndex < static_cast<uint32>( listChain.size() ); ++chainIndex )
                {
                    bool bOrderDependent = false;
                    for ( const uint32 fileIndex : listChain[chainIndex] )
                    {
                        bOrderDependent = bOrderDependent || ( mode == AssetImportMode::ImportStale && importer.hasOrderDependency( listFile[fileIndex]._resourcePath ) );
                    }
                    if ( bOrderDependent )
                        listSerialChain.push_back( chainIndex );
                    else
                        listParallelChain.push_back( chainIndex );
                }

                vector<FileResult> listFileResult( listFile.size() );
                ImportJob          job;
                job._pImporter       = &importer;
                job._pMapStamped     = &mapStamped;
                job._pListFile       = &listFile;
                job._pListChain      = &listChain;
                job._pListFileResult = listFileResult.data();
                job._mode            = mode;
                job.importChains( listParallelChain, true );
                job.importChains( listSerialChain, false );

                map<string, StampEntry> mapCurrent;
                for ( uint32 fileIndex = 0; fileIndex < static_cast<uint32>( listFile.size() ); ++fileIndex )
                {
                    const FileResult& fileResult = listFileResult[fileIndex];
                    ++inoutSummary._sourceCount;
                    if ( fileResult._problem.empty() == false )
                        inoutSummary._listProblem.push_back( fileResult._problem );
                    if ( fileResult._bStamped )
                        mapCurrent[listFile[fileIndex]._relativePath] = fileResult._current;
                    if ( fileResult._bImported )
                        ++inoutSummary._importedCount;
                }

                // 원본이 사라진 줄. 임포트된 결과는 다른 것이 참조할 수 있어 지우지 않는다 — 줄만 지운다.
                for ( const auto& [relativePath, entry] : mapStamped )
                {
                    if ( mapCurrent.find( relativePath ) != mapCurrent.end() )
                        continue;
                    const string sourcePath = FileUtil::joinPath( rawFolder, relativePath );
                    if ( FileUtil::exists( sourcePath ) )
                        continue; // 원본은 있다 — 위에서 어긋남 · 실패로 이미 보고했다
                    if ( mode == AssetImportMode::CheckOnly )
                        inoutSummary._listProblem.push_back( sourcePath.substr( rootDir.size() + 1 ) + ": 원본이 없는데 스탬프에 남아 있습니다" );
                }

                if ( mode == AssetImportMode::CheckOnly )
                    return;
                writeStampIfChanged( stampPath, importer.getStampHeader(), mapCurrent, inoutSummary );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    uint64 IRawAssetImporter::computeImportedHash( string_view importedPath ) const
    {
        return AssetImportStampUtil::computeFileHash( importedPath );
    }

    AssetImportSummary AssetImportStampUtil::importAll( string_view resourceRoot, const IRawAssetImporter& importer, AssetImportMode mode )
    {
        AssetImportSummary summary;
        const string       rootDir = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );

        vector<string> listFolder;
        if ( rootDir.empty() || FileUtil::collectFolders( rootDir, listFolder, true ) == false )
        {
            summary._listProblem.push_back( "리소스 루트를 훑지 못했습니다: " + rootDir );
            return summary;
        }

        std::sort( listFolder.begin(), listFolder.end() );
        for ( const string& folder : listFolder )
        {
            const string normalizedFolder = FileUtil::normalizeSeparators( folder );
            if ( FileUtil::getFileNamePart( normalizedFolder ) == importer.getRawFolderName() )
                AssetImportStampInternal::importRawFolder( rootDir, normalizedFolder, importer, mode, summary );
        }
        return summary;
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

    uint64 AssetImportStampUtil::computeFileHash( string_view path )
    {
        vector<uint8> bytes;
        const bool    bUnreadableFile = FileUtil::exists( path ) == false || FileUtil::readFile( path, bytes ) == false || bytes.empty();
        if ( bUnreadableFile )
            return 0;
        return HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( bytes.data() ), bytes.size(), false );
    }
} // namespace sw::editor
