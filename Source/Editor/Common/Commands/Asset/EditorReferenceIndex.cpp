#include "pch.h"

#include "Editor/Common/Commands/Asset/EditorReferenceIndex.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Workspace/EditorService.h"

namespace sw::editor
{
    namespace
    {
        struct EditorReferenceIndexInternal
        {
            /**
             * @brief 참조가 시작할 수 있는 리소스 도메인입니다(리소스 id 의 첫 조각).
             * @details `scratch/` 는 자동화 시나리오의 실행마다 새 사본 폴더(`AutomationScratch::kScratchDomain`)다 — 빠지면 사본 안의 참조(이름 바꾸기가 고칠 씬)를 세지 못한다.
             *          테스트 `EditorReferenceIndexTest.ScratchCopiesAreReferenceDomains` 가 두 이름이 같은지 본다.
             */
            static constexpr const utf8* kArrDomainPrefix[] = { "engine/", "common/", "game/", "editor/", "scratch/" };
            /** @brief 참조를 훑는 텍스트 에셋의 확장자입니다. 바이너리(dds, mesh, 셰이더 바이너리)는 글이 아니다. */
            static constexpr const utf8* kArrTextExtension[] = { ".xml", ".json", ".material", ".hlsl", ".hlsli" };
            /**
             * @brief 텍스트지만 에셋이 아닌 것입니다 — 자동화 시나리오는 테스트 입력이라 참조자로 세지 않는다.
             * @details 세면 이름 바꾸기가 도는 시나리오 파일까지 고쳐 쓴다. 시나리오가 적은 경로는 에셋이 옮겨지면 시나리오가 져서 알려야 한다.
             */
            static constexpr const utf8* kScenarioSuffix = ".scenario.xml";

            /** @brief 리소스 id 안에 올 수 있는 글자입니다(경로 조각 · 확장자). */
            static bool isPathChar( utf8 ch )
            {
                const utf8 lower = StringUtil::toLowerChar( ch );
                return ( 'a' <= lower && lower <= 'z' ) || ( '0' <= lower && lower <= '9' ) || lower == '_' || lower == '.' || lower == '/' || lower == '-';
            }

            /** @brief @p text 의 @p offset 에서 도메인 접두가 시작하면 true 입니다(대소문자 무시). */
            static bool startsWithDomain( string_view text, size_t offset )
            {
                for ( const utf8* pPrefix : kArrDomainPrefix )
                {
                    const string_view prefix{ pPrefix };
                    if ( offset + prefix.size() > text.size() )
                        continue;
                    bool bMatched = true;
                    for ( size_t charIndex = 0; charIndex < prefix.size(); ++charIndex )
                    {
                        if ( StringUtil::toLowerChar( text[offset + charIndex] ) != prefix[charIndex] )
                        {
                            bMatched = false;
                            break;
                        }
                    }
                    if ( bMatched )
                        return true;
                }
                return false;
            }

            static bool lessByTarget( const EditorAssetReference& left, const EditorAssetReference& right )
            {
                if ( left._targetPath != right._targetPath )
                    return left._targetPath < right._targetPath;
                if ( left._referrerPath != right._referrerPath )
                    return left._referrerPath < right._referrerPath;
                return left._line < right._line;
            }

            static bool isSameReference( const EditorAssetReference& left, const EditorAssetReference& right )
            {
                return left._targetPath == right._targetPath && left._referrerPath == right._referrerPath && left._line == right._line;
            }

            /** @brief 파일 하나를 읽어 참조를 더합니다. 읽지 못하면 아무것도 더하지 않는다. */
            static bool scanFile( string_view resourceRoot, string_view assetID, const vector<string>& listSortedKnownID,
                                  vector<EditorAssetReference>& outListReference )
            {
                // 목록을 모은 뒤 지워진 파일(에디터가 옮기는 중)은 읽지 않는다 — readTextFile 은 없는 파일을 오류로 남긴다.
                const string filePath = FileUtil::joinPath( resourceRoot, assetID );
                string       text;
                if ( FileUtil::exists( filePath ) == false || FileUtil::readTextFile( filePath, text ) == false )
                    return false;
                EditorReferenceIndex::extractReferences( assetID, text, listSortedKnownID, outListReference );
                return true;
            }

            /**
             * @brief 전체 스캔의 병렬 본문입니다. 텍스트 에셋 [start, end) 를 읽어 파일마다 **자기 칸**에만 쓴다.
             * @details 공유하는 것은 읽기 전용(`_listSortedKnownID` · 경로)뿐이고 칸은 서로 겹치지 않아 잠금이 없다. 합치는 것은 호출 스레드가 직렬로 한다.
             */
            struct ScanJob
            {
                string_view                   _resourceRoot;
                const vector<string>*         _pListSortedKnownID{ nullptr };
                const string* const*          _ppTextID{ nullptr };      ///< 텍스트 에셋 id 들(`_listSortedKnownID` 의 원소를 가리킨다)
                vector<EditorAssetReference>* _pArrReference{ nullptr }; ///< 파일마다 하나인 결과 칸
                uint8*                        _pArrScanned{ nullptr };   ///< 파일마다 하나인 "읽었다" 표시
                StopToken                     _stopToken{};              ///< 종료 신호(올라가면 남은 파일을 읽지 않는다). nullptr 이면 끝까지 읽는다

                void run( uint32 start, uint32 end )
                {
                    for ( uint32 index = start; index < end; ++index )
                    {
                        if ( _stopToken.isStopRequested() )
                            return; // 읽은 표시가 없는 칸은 합칠 때 건너뛴다 — 결과는 불완전하니 부른 쪽이 신호를 다시 읽고 버린다
                        if ( scanFile( _resourceRoot, *_ppTextID[index], *_pListSortedKnownID, _pArrReference[index] ) )
                            _pArrScanned[index] = SW_TRUE;
                    }
                }
            };

            /** @brief 파일 수가 이보다 적으면 나누지 않습니다. 파일 하나가 읽기 · 파싱이라 디스패치 비용보다 훨씬 큽니다. */
            static constexpr uint32 kSerialFileThreshold = 8;

            static string makeAssetID( string_view resourceRoot, string_view filePath )
            {
                string       relativePath;
                const string rootPath = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );
                if ( FileUtil::makeRelativePath( rootPath, FileUtil::normalizeSeparators( filePath ), relativePath ) == false )
                    return {};
                return FileUtil::normalizePath( relativePath );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorReferenceIndexData::EditorReferenceIndexData()
        : _listReference{}
        , _listKnownID{}
        , _scanMilliseconds{ 0 }
        , _scannedFileCount{ 0 }
    {
    }

    EditorReferenceIndex::EditorReferenceIndex()
        : _data{}
        , _bReady{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool EditorReferenceIndex::isTextAsset( string_view path )
    {
        const string lowerPath = FileUtil::normalizePath( path );
        if ( StringUtil::endsWith( lowerPath, EditorReferenceIndexInternal::kScenarioSuffix ) )
            return false;
        for ( const utf8* pExtension : EditorReferenceIndexInternal::kArrTextExtension )
        {
            if ( StringUtil::endsWith( lowerPath, pExtension ) )
                return true;
        }
        return false;
    }

    void EditorReferenceIndex::extractReferences( string_view referrerPath, string_view text, const vector<string>& listSortedKnownID,
                                                  vector<EditorAssetReference>& outListReference )
    {
        const string referrerID = FileUtil::normalizePath( referrerPath );
        uint32       line       = 1;
        size_t       offset     = 0;
        while ( offset < text.size() )
        {
            const utf8 ch = text[offset];
            if ( ch == '\n' )
            {
                ++line;
                ++offset;
                continue;
            }
            // 경로는 글자 경계에서만 시작한다 — `mygame/x` 의 `game/x` 를 세지 않게.
            const bool bAtBoundary = offset == 0 || EditorReferenceIndexInternal::isPathChar( text[offset - 1] ) == false;
            if ( bAtBoundary == false || EditorReferenceIndexInternal::startsWithDomain( text, offset ) == false )
            {
                ++offset;
                continue;
            }

            size_t endOffset = offset;
            while ( endOffset < text.size() && EditorReferenceIndexInternal::isPathChar( text[endOffset] ) )
            {
                ++endOffset;
            }
            const string candidate = FileUtil::normalizePath( text.substr( offset, endOffset - offset ) );
            offset                 = endOffset;
            if ( std::binary_search( listSortedKnownID.begin(), listSortedKnownID.end(), candidate ) == false )
                continue;
            EditorAssetReference reference{};
            reference._referrerPath = referrerID;
            reference._targetPath   = candidate;
            reference._line         = line;
            outListReference.push_back( std::move( reference ) );
        }
    }

    void EditorReferenceIndex::scan( string_view resourceRoot, EditorReferenceIndexData& outData, TaskManager* pTaskManager, const StopToken& stopToken )
    {
        const Stopwatch stopwatch;
        outData = EditorReferenceIndexData{};
        vector<string> listFile;
        if ( resourceRoot.empty() || FileUtil::collectFiles( resourceRoot, "", listFile, true ) == false )
            return;

        outData._listKnownID.reserve( listFile.size() );
        for ( const string& filePath : listFile )
        {
            string assetID = EditorReferenceIndexInternal::makeAssetID( resourceRoot, filePath );
            if ( assetID.empty() == false )
                outData._listKnownID.push_back( std::move( assetID ) );
        }
        std::sort( outData._listKnownID.begin(), outData._listKnownID.end() );

        vector<const string*> listTextID;
        for ( const string& assetID : outData._listKnownID )
        {
            if ( isTextAsset( assetID ) )
                listTextID.push_back( &assetID );
        }

        // 파일마다 자기 칸에 담고(병렬), 파일 순서대로 직렬로 합친다. 정렬 키가 (대상, 참조자, 줄)이고 같은 키는 지우므로 병렬이어도 결과가 직렬과 같다.
        const uint32                          fileCount = static_cast<uint32>( listTextID.size() );
        vector<vector<EditorAssetReference>>  listFileReference( fileCount );
        vector<uint8>                         listScanned( fileCount, SW_FALSE );
        EditorReferenceIndexInternal::ScanJob job{};
        job._resourceRoot       = resourceRoot;
        job._pListSortedKnownID = &outData._listKnownID;
        job._ppTextID           = listTextID.data();
        job._pArrReference      = listFileReference.data();
        job._pArrScanned        = listScanned.data();
        job._stopToken          = stopToken;
        if ( pTaskManager != nullptr )
        {
            // 호출 스레드가 워커여도 된다: `runParallel` 은 호출 스레드가 첫 청크부터 직접 돌고, 합류를 기다리는 동안 다른 일을 돕는다.
            pTaskManager->runParallel( fileCount, EditorReferenceIndexInternal::kSerialFileThreshold,
                                       SW_DELEGATE_METHOD( ParallelBlockDelegate, &EditorReferenceIndexInternal::ScanJob::run, &job ) );
        }
        else
        {
            job.run( 0, fileCount );
        }

        for ( uint32 fileIndex = 0; fileIndex < fileCount; ++fileIndex )
        {
            if ( listScanned[fileIndex] == SW_FALSE )
                continue;
            ++outData._scannedFileCount;
            outData._listReference.insert( outData._listReference.end(), std::make_move_iterator( listFileReference[fileIndex].begin() ),
                                           std::make_move_iterator( listFileReference[fileIndex].end() ) );
        }
        sortByTarget( outData._listReference );
        outData._scanMilliseconds = stopwatch.getElapsedMilliseconds();
    }

    void EditorReferenceIndex::assign( EditorReferenceIndexData&& data )
    {
        _data   = std::move( data );
        _bReady = SW_TRUE;
    }

    void EditorReferenceIndex::refreshFile( string_view resourceRoot, string_view assetPath )
    {
        const string assetID = FileUtil::normalizePath( assetPath );
        VectorUtil::removeIf( _data._listReference, [&assetID]( const EditorAssetReference& reference )
        { return reference._referrerPath == assetID; } );

        const bool bExists = FileUtil::exists( FileUtil::joinPath( resourceRoot, assetID ) );
        const auto itKnown = std::lower_bound( _data._listKnownID.begin(), _data._listKnownID.end(), assetID );
        const bool bKnown  = itKnown != _data._listKnownID.end() && *itKnown == assetID;
        if ( bExists && bKnown == false )
            _data._listKnownID.insert( itKnown, assetID );
        else if ( bExists == false && bKnown )
            _data._listKnownID.erase( itKnown );

        if ( bExists && isTextAsset( assetID ) )
            (void)EditorReferenceIndexInternal::scanFile( resourceRoot, assetID, _data._listKnownID, _data._listReference ); // 읽지 못한 파일은 참조가 없는 것과 같다
        sortByTarget( _data._listReference );
    }

    bool EditorReferenceIndex::containsFile( string_view assetID ) const
    {
        const string id = FileUtil::normalizePath( assetID );
        return std::binary_search( _data._listKnownID.begin(), _data._listKnownID.end(), id );
    }

    void EditorReferenceIndex::findReferrers( string_view targetPath, vector<EditorAssetReference>& outListReference ) const
    {
        outListReference.clear();
        EditorAssetReference key{};
        key._targetPath  = FileUtil::normalizePath( targetPath );
        auto itReference = std::lower_bound( _data._listReference.begin(), _data._listReference.end(), key,
                                             []( const EditorAssetReference& left, const EditorAssetReference& right )
        { return left._targetPath < right._targetPath; } );
        for ( ; itReference != _data._listReference.end() && itReference->_targetPath == key._targetPath; ++itReference )
        {
            outListReference.push_back( *itReference );
        }
    }

    void EditorReferenceIndex::findDependencies( string_view referrerPath, vector<EditorAssetReference>& outListReference ) const
    {
        outListReference.clear();
        const string referrerID = FileUtil::normalizePath( referrerPath );
        for ( const EditorAssetReference& reference : _data._listReference )
        {
            if ( reference._referrerPath == referrerID )
                outListReference.push_back( reference );
        }
        std::sort( outListReference.begin(), outListReference.end(),
                   []( const EditorAssetReference& left, const EditorAssetReference& right )
        { return left._line < right._line; } );
    }

    uint32 EditorReferenceIndex::countReferrerFiles( string_view targetPath ) const
    {
        vector<EditorAssetReference> listReference;
        findReferrers( targetPath, listReference );
        uint32        fileCount{ 0 };
        const string* pPrevious = nullptr;
        for ( const EditorAssetReference& reference : listReference )
        {
            if ( pPrevious == nullptr || *pPrevious != reference._referrerPath )
                ++fileCount;
            pPrevious = &reference._referrerPath;
        }
        return fileCount;
    }

    void EditorReferenceIndex::sortByTarget( vector<EditorAssetReference>& inoutListReference )
    {
        std::sort( inoutListReference.begin(), inoutListReference.end(), &EditorReferenceIndexInternal::lessByTarget );
        inoutListReference.erase( std::unique( inoutListReference.begin(), inoutListReference.end(), &EditorReferenceIndexInternal::isSameReference ),
                                  inoutListReference.end() );
    }
} // namespace sw::editor
