#include "pch.h"

#include "Editor/Common/Asset/EditorAssetIndex.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Asset/EditorContentScan.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Serialization/Inherit/AssetRedirect.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"

namespace sw::editor
{
    namespace
    {
        struct EditorAssetIndexInternal
        {
            /**
             * @brief 경로 참조가 시작할 수 있는 리소스 도메인입니다(리소스 id 의 첫 조각).
             * @details `scratch/` 는 자동화 시나리오의 실행마다 새 사본 폴더(`AutomationScratch::kScratchDomain`)다 — 빠지면 사본 안의 참조(이름 바꾸기가 고칠 씬)를 세지 못한다.
             */
            static constexpr const utf8* kArrDomainPrefix[] = { "engine/", "common/", "game/", "editor/", "scratch/" };
            /** @brief 참조를 훑는 텍스트 에셋의 확장자입니다. 바이너리(dds, mesh, 셰이더 바이너리)는 글이 아니다. */
            static constexpr const utf8* kArrTextExtension[] = { ".xml", ".json", ".material", ".hlsl", ".hlsli" };
            /** @brief 텍스트지만 에셋이 아닌 것입니다 — 자동화 시나리오는 테스트 입력이라 참조자로 세지 않는다(세면 이름 바꾸기가 시나리오를 고쳐 쓴다). */
            static constexpr const utf8* kScenarioSuffix = ".scenario.xml";
            /** @brief `.meta` 의 GUID 칸 머리입니다(`AssetMeta::_guid`). 서술의 엄격한 읽기 · 결함 알림은 `asset-ids` · `AssetDatabase` 가 한다 — 여기는 줄만 찾는다. */
            static constexpr string_view kMetaGUIDKey = "_guid=\"";
            /** @brief 파일 수가 이보다 적으면 나누지 않습니다. 파일 하나가 읽기 · 파싱이라 디스패치 비용보다 훨씬 큽니다. */
            static constexpr uint32 kSerialFileThreshold = 8;

            /** @brief 리소스 id 안에 올 수 있는 글자입니다(경로 조각 · 확장자). */
            static bool isPathChar( utf8 ch )
            {
                const utf8 lower = StringUtil::toLowerChar( ch );
                return ( 'a' <= lower && lower <= 'z' ) || ( '0' <= lower && lower <= '9' ) || lower == '_' || lower == '.' || lower == '/' || lower == '-';
            }

            /** @brief @p text 의 @p offset 에서 @p prefix 가 시작하면 true 입니다(대소문자 무시, @p prefix 는 소문자). */
            static bool startsWithLower( string_view text, size_t offset, string_view prefix )
            {
                if ( offset + prefix.size() > text.size() )
                    return false;
                for ( size_t charIndex = 0; charIndex < prefix.size(); ++charIndex )
                {
                    if ( StringUtil::toLowerChar( text[offset + charIndex] ) != prefix[charIndex] )
                        return false;
                }
                return true;
            }

            static bool startsWithDomain( string_view text, size_t offset )
            {
                return std::any_of( std::begin( kArrDomainPrefix ), std::end( kArrDomainPrefix ), [&]( const utf8* pPrefix )
                { return startsWithLower( text, offset, pPrefix ); } );
            }

            /**
             * @brief @p offset 에서 시작하는 값이 `_base="` 칸의 값이면 true 입니다.
             * @details 항목 상속의 `_base="game/x/items.xml#id"` 도 경로 글자가 `#` 에서 끊겨 파일만 대상이 된다. XML 주석에 적은 `_base="…"` 는 참조가 아니다.
             */
            static bool isBaseAttributeValue( string_view text, size_t offset )
            {
                if ( offset == 0 || ( text[offset - 1] != '"' && text[offset - 1] != '\'' ) )
                    return false;
                const size_t commentOpen = text.rfind( "<!--", offset );
                if ( commentOpen != string_view::npos && text.find( "-->", commentOpen ) > offset )
                    return false;
                const string_view name{ DataInheritance::kBaseAttribute };
                size_t            end = offset - 1;
                while ( end > 0 && ( text[end - 1] == '=' || text[end - 1] == ' ' || text[end - 1] == '\t' ) )
                {
                    --end;
                }
                if ( end < name.size() || text.substr( end - name.size(), name.size() ) != name )
                    return false;
                return end == name.size() || isPathChar( text[end - name.size() - 1] ) == false;
            }

            static bool isMetaFile( string_view assetID ) { return StringUtil::endsWith( assetID, AssetMeta::kExtension, true ); }

            /** @brief `.meta` 글의 GUID 입니다. 칸이 없거나 글 표기가 아니면 없음입니다. */
            static optional<AssetGUID> readMetaGUID( string_view text )
            {
                const size_t keyAt = text.find( kMetaGUIDKey );
                if ( keyAt == string_view::npos )
                    return nullopt;
                const size_t valueAt  = keyAt + kMetaGUIDKey.size();
                const size_t valueEnd = text.find( '"', valueAt );
                return valueEnd == string_view::npos ? nullopt : AssetGUID::tryParse( text.substr( valueAt, valueEnd - valueAt ) );
            }

            static string makeAssetID( string_view resourceRoot, string_view filePath )
            {
                string       relativePath;
                const string rootPath = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );
                if ( FileUtil::makeRelativePath( rootPath, FileUtil::normalizeSeparators( filePath ), relativePath ) == false )
                    return {};
                return FileUtil::normalizePath( relativePath ).str();
            }

            static bool lessByReferrerLine( const EditorAssetReference& left, const EditorAssetReference& right )
            {
                if ( left._referrerPath != right._referrerPath )
                    return left._referrerPath < right._referrerPath;
                return left._line < right._line;
            }

            /** @brief 파일 하나의 읽기 결과입니다 — 워커가 자기 칸에 쓰고 호출 스레드가 차례로 합친다. */
            struct FileRead
            {
                FileStamp _fileStamp{};
                string    _text;
                uint8     _bRead{ SW_FALSE }; ///< 글을 읽었다(텍스트 에셋 · `.meta`)
            };

            /** @brief 전체 스캔의 병렬 본문입니다. 파일마다 **자기 칸**에만 쓰므로 잠금이 없다. */
            struct ScanJob
            {
                string_view          _resourceRoot;
                const string* const* _ppAssetID{ nullptr };
                FileRead*            _pArrRead{ nullptr };
                StopToken            _stopToken{};

                void run( uint32 start, uint32 end )
                {
                    for ( uint32 index = start; index < end; ++index )
                    {
                        if ( _stopToken.isStopRequested() )
                            return; // 결과는 불완전하니 부른 쪽이 신호를 다시 읽고 버린다
                        const string& assetID = *_ppAssetID[index];
                        const string  absPath = FileUtil::joinPath( _resourceRoot, assetID );
                        FileRead&     read    = _pArrRead[index];
                        (void)FileUtil::getFileStamp( absPath, read._fileStamp ); // 목록 뒤에 사라진 파일은 시각이 비어 다음 바뀜에서 다시 읽는다
                        // 목록을 모은 뒤 지워진 파일(에디터가 옮기는 중)은 읽지 않는다 — readTextFile 은 없는 파일을 오류로 남긴다.
                        if ( ( isTextAsset( assetID ) || isMetaFile( assetID ) ) && FileUtil::readOptionalTextFile( absPath, read._text ) == OptionalFileRead::Read )
                            read._bRead = SW_TRUE;
                    }
                }

                static bool isTextAsset( string_view assetID ) { return EditorAssetIndex::isTextAsset( assetID ); }
            };
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    EditorAssetIndex::EditorAssetIndex()
        : _data{}
        , _bReady{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    bool EditorAssetIndex::isTextAsset( string_view path )
    {
        const string lowerPath = FileUtil::normalizePath( path ).str();
        if ( StringUtil::endsWith( lowerPath, EditorAssetIndexInternal::kScenarioSuffix ) )
            return false;
        // 리다이렉트 표는 옛 id 와 새 id 를 적는 글이라 참조자가 아니다(엔진 규칙 `AssetReferenceFields::isReferrer` 와 같다).
        if ( AssetRedirect::isTable( lowerPath ) )
            return false;
        return std::any_of( std::begin( EditorAssetIndexInternal::kArrTextExtension ), std::end( EditorAssetIndexInternal::kArrTextExtension ),
                            [&lowerPath]( const utf8* pExtension )
        { return StringUtil::endsWith( lowerPath, pExtension ); } );
    }

    void EditorAssetIndex::extractLinks( string_view text, vector<EditorAssetLink>& outListLink )
    {
        using Internal                   = EditorAssetIndexInternal;
        const string_view guidPrefix     = AssetRegistry::kReferencePrefix;
        const size_t      guidTextLength = guidPrefix.size() + AssetGUID::kTextLength;
        uint32            line           = 1;
        size_t            offset         = 0;
        while ( offset < text.size() )
        {
            if ( text[offset] == '\n' )
            {
                ++line;
                ++offset;
                continue;
            }
            // 참조는 글자 경계에서만 시작한다 — `mygame/x` 의 `game/x` 를 세지 않게.
            const bool bAtBoundary = offset == 0 || Internal::isPathChar( text[offset - 1] ) == false;
            if ( bAtBoundary && Internal::startsWithLower( text, offset, guidPrefix ) && offset + guidTextLength <= text.size() )
            {
                const string_view         guidText = text.substr( offset + guidPrefix.size(), guidTextLength - guidPrefix.size() );
                const optional<AssetGUID> guid     = AssetGUID::tryParse( guidText );
                if ( guid.has_value() )
                {
                    outListLink.push_back( EditorAssetLink{ AssetRegistry::makeReferenceText( *guid ), line, Internal::isBaseAttributeValue( text, offset ) } );
                    offset += guidTextLength;
                    continue;
                }
            }
            if ( bAtBoundary == false || Internal::startsWithDomain( text, offset ) == false )
            {
                ++offset;
                continue;
            }
            size_t endOffset = offset;
            while ( endOffset < text.size() && Internal::isPathChar( text[endOffset] ) )
            {
                ++endOffset;
            }
            outListLink.push_back( EditorAssetLink{ FileUtil::normalizePath( text.substr( offset, endOffset - offset ) ).str(), line, Internal::isBaseAttributeValue( text, offset ) } );
            offset = endOffset;
        }
    }

    void EditorAssetIndex::removeEntry( EditorAssetIndexData& inoutData, const string& assetID )
    {
        const auto itEntry = inoutData._mapEntry.find( assetID );
        if ( itEntry != inoutData._mapEntry.end() )
        {
            for ( const EditorAssetLink& link : itEntry->second._listLink )
            {
                const auto itReferrer = inoutData._mapReferrer.find( link._target );
                if ( itReferrer == inoutData._mapReferrer.end() )
                    continue; // 같은 대상을 두 번 적은 파일 — 앞 줄이 이미 뺐다
                VectorUtil::removeIf( itReferrer->second, [&assetID]( const string& referrer )
                { return referrer == assetID; } );
                if ( itReferrer->second.empty() )
                    inoutData._mapReferrer.erase( itReferrer );
            }
            inoutData._mapEntry.erase( itEntry );
        }
        // 정체성 줄 — 머리 글은 자기 id, `.meta` 는 원본 id 의 줄이다. GUID 쪽은 아직 이 경로를 가리킬 때만 뺀다(옮긴 새 자리가 먼저 들어왔을 수 있다).
        const string identityID = EditorAssetIndexInternal::isMetaFile( assetID ) ? assetID.substr( 0, assetID.size() - AssetMeta::kExtension.size() ) : assetID;
        const auto   itGUID     = inoutData._mapPathToGUID.find( identityID );
        if ( itGUID == inoutData._mapPathToGUID.end() )
            return;
        const auto itPath = inoutData._mapGUIDToPath.find( itGUID->second );
        if ( itPath != inoutData._mapGUIDToPath.end() && itPath->second == identityID )
            inoutData._mapGUIDToPath.erase( itPath );
        inoutData._mapPathToGUID.erase( itGUID );
    }

    void EditorAssetIndex::insertEntry( EditorAssetIndexData& inoutData, const string& assetID, const FileStamp& fileStamp, const string* pText )
    {
        removeEntry( inoutData, assetID );
        EditorAssetEntry& entry = inoutData._mapEntry[assetID];
        entry._fileStamp        = fileStamp;
        if ( pText == nullptr )
            return;

        optional<AssetGUID> guid;
        string              identityID = assetID;
        if ( EditorAssetIndexInternal::isMetaFile( assetID ) )
        {
            guid       = EditorAssetIndexInternal::readMetaGUID( *pText );
            identityID = assetID.substr( 0, assetID.size() - AssetMeta::kExtension.size() );
        }
        else
        {
            guid = AssetGUIDHeader::read( *pText )._guid;
            extractLinks( *pText, entry._listLink );
            for ( size_t linkIndex = 0; linkIndex < entry._listLink.size(); ++linkIndex )
            {
                vector<string>& listReferrer = inoutData._mapReferrer[entry._listLink[linkIndex]._target];
                if ( listReferrer.empty() || listReferrer.back() != assetID ) // 이 파일의 줄은 방금 넣은 것이 맨 뒤다 — 같은 대상을 여러 번 적어도 한 번만
                    listReferrer.push_back( assetID );
            }
        }
        if ( guid.has_value() )
        {
            inoutData._mapPathToGUID[identityID] = *guid;
            inoutData._mapGUIDToPath[*guid]      = identityID;
        }
    }

    void EditorAssetIndex::refreshFromDisk( EditorAssetIndexData& inoutData, string_view resourceRoot, const string& assetID )
    {
        const string absPath = FileUtil::joinPath( resourceRoot, assetID );
        FileStamp    stamp{};
        if ( FileUtil::getFileStamp( absPath, stamp ) == false )
        {
            removeEntry( inoutData, assetID );
            return;
        }
        const auto itEntry = inoutData._mapEntry.find( assetID );
        if ( itEntry != inoutData._mapEntry.end() && itEntry->second._fileStamp == stamp )
            return; // 크기 · 시각이 같다 — 열지 않는다
        string     text;
        const bool bWantsText = isTextAsset( assetID ) || EditorAssetIndexInternal::isMetaFile( assetID );
        const bool bRead      = bWantsText && FileUtil::readOptionalTextFile( absPath, text ) == OptionalFileRead::Read;
        insertEntry( inoutData, assetID, stamp, bRead ? &text : nullptr ); // 읽지 못한 글은 참조가 없는 것과 같다
    }

    void EditorAssetIndex::scan( string_view resourceRoot, EditorAssetIndexData& outData, TaskManager* pTaskManager, const StopToken& stopToken )
    {
        const Stopwatch stopwatch;
        outData = EditorAssetIndexData{};
        vector<string> listFile;
        // 남의 시나리오 사본은 이 에디터의 에셋이 아니다(EditorContentScan 이 뺀다)
        if ( resourceRoot.empty() || EditorContentScan::collectFiles( resourceRoot, resourceRoot, "", listFile, true, stopToken ) == false )
            return;

        vector<string> listAssetID;
        listAssetID.reserve( listFile.size() );
        for ( const string& filePath : listFile )
        {
            string assetID = EditorAssetIndexInternal::makeAssetID( resourceRoot, filePath );
            if ( assetID.empty() == false )
                listAssetID.push_back( std::move( assetID ) );
        }
        VectorUtil::sort( listAssetID ); // 같은 GUID 를 든 두 파일(복사본)의 승자가 실행마다 같게 — 파일 순서로 합친다

        vector<const string*> listAssetIDPointer;
        listAssetIDPointer.reserve( listAssetID.size() );
        for ( const string& assetID : listAssetID )
        {
            listAssetIDPointer.push_back( &assetID );
        }
        const uint32                               fileCount = static_cast<uint32>( listAssetID.size() );
        vector<EditorAssetIndexInternal::FileRead> listRead( fileCount );
        EditorAssetIndexInternal::ScanJob          job{};
        job._resourceRoot = resourceRoot;
        job._ppAssetID    = listAssetIDPointer.data();
        job._pArrRead     = listRead.data();
        job._stopToken    = stopToken;
        if ( pTaskManager != nullptr )
        {
            // 호출 스레드가 워커여도 된다: `runParallel` 은 호출 스레드가 첫 청크부터 직접 돌고, 합류를 기다리는 동안 다른 일을 돕는다.
            pTaskManager->runParallel( fileCount, EditorAssetIndexInternal::kSerialFileThreshold,
                                       SW_DELEGATE_METHOD( ParallelBlockDelegate, &EditorAssetIndexInternal::ScanJob::run, &job ) );
        }
        else
        {
            job.run( 0, fileCount );
        }

        outData._mapEntry.reserve( fileCount );
        for ( uint32 fileIndex = 0; fileIndex < fileCount; ++fileIndex )
        {
            const EditorAssetIndexInternal::FileRead& read = listRead[fileIndex];
            if ( read._bRead == SW_TRUE && isTextAsset( listAssetID[fileIndex] ) )
                ++outData._scannedFileCount;
            insertEntry( outData, listAssetID[fileIndex], read._fileStamp, read._bRead == SW_TRUE ? &read._text : nullptr );
        }
        outData._scanMilliseconds = stopwatch.getElapsedMilliseconds();
    }

    void EditorAssetIndex::assign( EditorAssetIndexData&& data )
    {
        _data   = std::move( data );
        _bReady = SW_TRUE;
    }

    void EditorAssetIndex::applyChanges( string_view resourceRoot, const vector<string>& listChangedID )
    {
        for ( const string& changed : listChangedID )
        {
            const string assetID = FileUtil::normalizePath( changed ).str();
            const string absPath = FileUtil::joinPath( resourceRoot, assetID );
            if ( FileUtil::isDirectory( absPath ) )
            {
                // 폴더가 생겼다(옮겨 왔다) — 그 아래 파일을 하나씩 넣는다. 남의 시나리오 사본은 빠진다(EditorContentScan).
                vector<string> listFile;
                (void)EditorContentScan::collectFiles( resourceRoot, absPath, "", listFile, true );
                for ( const string& filePath : listFile )
                {
                    const string fileID = EditorAssetIndexInternal::makeAssetID( resourceRoot, filePath );
                    if ( fileID.empty() == false )
                        refreshFromDisk( _data, resourceRoot, fileID );
                }
                continue;
            }
            if ( _data._mapEntry.contains( assetID ) || FileUtil::isRegularFile( absPath ) )
            {
                refreshFromDisk( _data, resourceRoot, assetID );
                continue;
            }
            // 모르는 이름이 없다 — 사라진 폴더다. 그 아래 줄을 모두 뺀다(폴더 하나라 전체를 한 번 훑는다).
            const string   folderPrefix = assetID + "/";
            vector<string> listRemoved;
            for ( const auto& [entryID, entry] : _data._mapEntry )
            {
                if ( StringUtil::startsWith( entryID, folderPrefix ) )
                    listRemoved.push_back( entryID );
            }
            for ( const string& removedID : listRemoved )
            {
                removeEntry( _data, removedID );
            }
        }
    }

    void EditorAssetIndex::applyText( string_view assetID, string_view text, const FileStamp& fileStamp )
    {
        const string id = FileUtil::normalizePath( assetID ).str();
        const string copy{ text };
        insertEntry( _data, id, fileStamp, isTextAsset( id ) || EditorAssetIndexInternal::isMetaFile( id ) ? &copy : nullptr );
    }

    const EditorAssetEntry* EditorAssetIndex::find( string_view assetID ) const
    {
        const auto itEntry = _data._mapEntry.find( FileUtil::normalizePath( assetID ).str() );
        return itEntry == _data._mapEntry.end() ? nullptr : &itEntry->second;
    }

    optional<string> EditorAssetIndex::findPath( const AssetGUID& guid ) const
    {
        const auto itPath = _data._mapGUIDToPath.find( guid );
        if ( itPath == _data._mapGUIDToPath.end() )
            return nullopt;
        return itPath->second;
    }

    optional<AssetGUID> EditorAssetIndex::findGUID( string_view assetID ) const
    {
        const auto itGUID = _data._mapPathToGUID.find( FileUtil::normalizePath( assetID ).str() );
        if ( itGUID == _data._mapPathToGUID.end() )
            return nullopt;
        return itGUID->second;
    }

    void EditorAssetIndex::findReferrers( string_view targetPath, vector<EditorAssetReference>& outListReference ) const
    {
        outListReference.clear();
        const string targetID = FileUtil::normalizePath( targetPath ).str();
        if ( _data._mapEntry.contains( targetID ) == false )
            return;
        // 경로로 적은 참조와 GUID 로 적은 참조 — 열쇠 둘을 본다.
        string                    arrKey[2] = { targetID, {} };
        const optional<AssetGUID> guid      = findGUID( targetID );
        if ( guid.has_value() )
            arrKey[1] = AssetRegistry::makeReferenceText( *guid );
        for ( const string& key : arrKey )
        {
            const auto itReferrer = key.empty() ? _data._mapReferrer.end() : _data._mapReferrer.find( key );
            if ( itReferrer == _data._mapReferrer.end() )
                continue;
            for ( const string& referrerID : itReferrer->second )
            {
                const auto itEntry = _data._mapEntry.find( referrerID );
                if ( itEntry == _data._mapEntry.end() )
                    continue;
                for ( const EditorAssetLink& link : itEntry->second._listLink )
                {
                    if ( link._target == key )
                        outListReference.push_back( EditorAssetReference{ referrerID, targetID, link._line, link._bInlined, AssetRegistry::isReferenceText( key ) } );
                }
            }
        }
        VectorUtil::sort( outListReference, &EditorAssetIndexInternal::lessByReferrerLine );
    }

    void EditorAssetIndex::findDependencies( string_view referrerPath, vector<EditorAssetReference>& outListReference ) const
    {
        outListReference.clear();
        const string referrerID = FileUtil::normalizePath( referrerPath ).str();
        const auto   itEntry    = _data._mapEntry.find( referrerID );
        if ( itEntry == _data._mapEntry.end() )
            return;
        for ( const EditorAssetLink& link : itEntry->second._listLink )
        {
            string targetID = link._target;
            if ( AssetRegistry::isReferenceText( targetID ) )
            {
                const optional<AssetGUID> guid = AssetRegistry::parseReferenceText( targetID );
                const optional<string>    path = guid.has_value() ? findPath( *guid ) : nullopt;
                if ( path.has_value() == false )
                    continue; // 이 트리에 없는 정체성 — 대상 없음
                targetID = *path;
            }
            if ( _data._mapEntry.contains( targetID ) )
                outListReference.push_back( EditorAssetReference{ referrerID, targetID, link._line, link._bInlined, AssetRegistry::isReferenceText( link._target ) } );
        }
    }

    uint32 EditorAssetIndex::countReferrerFiles( string_view targetPath ) const
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

    void EditorAssetIndex::collectAssetIDs( vector<string>& outListAssetID ) const
    {
        outListAssetID.clear();
        outListAssetID.reserve( _data._mapEntry.size() );
        for ( const auto& [assetID, entry] : _data._mapEntry )
        {
            outListAssetID.push_back( assetID );
        }
        VectorUtil::sort( outListAssetID );
    }
} // namespace sw::editor

namespace sw::editor
{
    void EditorAssetIndexScanJob::request( string_view resourceRoot, TaskManager* pTaskManager )
    {
        const uint32 generation = beginRequest( string{ resourceRoot } );
        submitJob( "EditorAssetIndexScan", SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorAssetIndexScanJob::runJob ), MakeTaskArgs( _pState, generation, pTaskManager ) );
    }

    void EditorAssetIndexScanJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
        const uint32            generation = args.get<uint32>( 1 );
        TaskManager* const      pTasks     = args.get<TaskManager*>( 2 );

        string resourceRoot;
        if ( readInput( pState, generation, resourceRoot ) == false )
            return;

        EditorAssetIndexData data{};
        const StopToken      stopToken = readStopSignal( pState );
        EditorAssetIndex::scan( resourceRoot, data, pTasks, stopToken );
        if ( stopToken.isStopRequested() )
            return; // 종료가 시작돼 스캔이 중간에 멈췄다 — 불완전한 색인을 싣지 않는다

        publish( pState, generation, std::move( data ) );
    }
} // namespace sw::editor
