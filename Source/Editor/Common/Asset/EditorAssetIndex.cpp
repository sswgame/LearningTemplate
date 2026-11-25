#include "pch.h"

#include "Editor/Common/Asset/EditorAssetIndex.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Serialization/ByteStream.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Task/TaskManager.h"
#include "Core/Time/MonotonicClock.h"

#include "Editor/Common/Asset/EditorContentScan.h"
#include "Editor/Common/Workspace/EditorService.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Content/AssetScanCache.h"
#include "Engine/Content/ContentHash.h"
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
             * @brief @p offset 에서 시작하는 경로 글자가 붙인 디스크 도메인(@p listMountedDomain — `AssetPaths::mountDiskDomain`) 아래면 true 입니다.
             * @details 붙인 도메인(테스트 픽스처 · 도구 작업의 디스크 폴더)은 고정 표에 없다 — 빠지면 그 안의 참조(`_base`)를 세지 못해 다시 읽기 연쇄가 빈다
             *          (AssetReloadCascadeTest). 목록은 에디터 스레드가 담아 넘긴다(작업 스레드는 에디터 서비스를 찾지 않는다).
             */
            static bool startsWithMountedDomain( string_view text, size_t offset, const vector<string>& listMountedDomain )
            {
                return std::any_of( listMountedDomain.begin(), listMountedDomain.end(), [&]( const string& domain )
                {
                    const size_t slashAt = offset + domain.size();
                    return slashAt < text.size() && text[slashAt] == '/' && startsWithLower( text, offset, domain.c_str() );
                } );
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

            /**
             * @brief 디스크 경로 @p filePath 의 리소스 id 를 디스크 표기(대소문자 그대로, `/` 구분)로 돌려줍니다. 루트 밖이면 빈 글입니다.
             * @details 작업 도메인(`scratch/`)은 리소스 루트 밖, 실행 뿌리 아래다(`AssetPaths::getScratchFolderPath` — 프로세스 상수라 워커에서도 읽는다).
             */
            static string makeDisplayPath( string_view resourceRoot, string_view filePath )
            {
                string underScratch;
                if ( FileUtil::tryMakePathUnderRoot( AssetPaths::getScratchFolderPath().str(), filePath, underScratch ) )
                {
                    StringBuilder<constant::buffer::kMax256> idBuilder;
                    idBuilder.appendFormat( "%#/%#", AssetPaths::kScratchDomain, underScratch );
                    return string( idBuilder.view() );
                }
                string       relativePath;
                const string rootPath = FileUtil::trimTrailingSlashes( FileUtil::normalizeSeparators( resourceRoot ) );
                if ( FileUtil::makeRelativePath( rootPath, FileUtil::normalizeSeparators( filePath ), relativePath ) == false )
                    return {};
                return FileUtil::normalizeSeparators( relativePath );
            }

            /** @brief 표시 경로 @p displayPath 의 디스크 자리입니다(`makeDisplayPath` 의 짝 — 작업 도메인은 실행 뿌리 아래, 나머지는 @p resourceRoot 아래). */
            static AbsolutePath makeDiskPath( const AbsolutePath& resourceRoot, string_view displayPath )
            {
                // 작업 도메인 아래 부분은 원래 표기 그대로 잇는다(접은 키로 자르면 리눅스에서 다른 파일이 된다).
                string rest;
                if ( displayPath == AssetPaths::kScratchDomain || FileUtil::tryMakePathUnderRoot( AssetPaths::kScratchDomain, displayPath, rest ) )
                    return AssetPaths::getScratchFolderPath().join( rest );
                return resourceRoot.join( displayPath );
            }

            /** @brief 색인의 열쇠(소문자 리소스 id)입니다. */
            static string makeKey( string_view displayPath ) { return FileUtil::normalizePath( displayPath ).str(); }

            /** @brief 원본 @p absPath 곁의 `.meta` 를 읽습니다. 없거나 읽지 못하면 false 입니다(정체성 없음 — 결함 알림은 `asset-ids` · `AssetDatabase` 가 한다). */
            static bool readMetaText( const AbsolutePath& absPath, string& outText ) { return FileUtil::readOptionalTextFile( AssetMeta::makePath( absPath ), outText ) == OptionalFileRead::Read; }

            static bool lessByReferrerLine( const EditorAssetReference& left, const EditorAssetReference& right )
            {
                if ( left._referrerPath != right._referrerPath )
                    return left._referrerPath < right._referrerPath;
                return left._line < right._line;
            }

            /** @brief 전체 스캔의 파일 하나입니다(목록 순서 — 표시 경로 순). */
            struct ScanFile
            {
                string    _displayPath;
                FileStamp _fileStamp{};
                FileStamp _sidecarStamp{};
            };

            /** @brief 파일 하나의 읽기 결과입니다 — 워커가 자기 칸에 쓰고 호출 스레드가 차례로 합친다. */
            struct FileRead
            {
                EditorAssetContent     _content;
                uint8                  _bTextAsset : 1; ///< 텍스트 에셋이다(나머지는 원본 — 곁 `.meta` 를 읽는다)
                uint8                  _bRead      : 1; ///< 내용을 얻었다(디스크 · 캐시). 목록 뒤에 사라진 파일은 아니다
                uint8                  _bCached    : 1; ///< 캐시에서 얻었다
                [[maybe_unused]] uint8 _reserved   : 5;

                FileRead()
                    : _bTextAsset{ SW_FALSE }
                    , _bRead{ SW_FALSE }
                    , _bCached{ SW_FALSE }
                    , _reserved{ 0 }
                {
                }
            };

            /** @brief 전체 스캔의 병렬 본문입니다. 파일마다 자기 칸에만 쓰므로 잠금이 없다. */
            struct ScanJob
            {
                AbsolutePath    _resourceRoot;
                const ScanFile* _pFile{ nullptr };
                StopToken       _stopToken{};

                /** @brief 파일 @p index 의 글을 읽어 제 칸 @p outRead 에 씁니다(`runParallelInto` 본문). 캐시가 채운 칸은 그대로 둡니다. */
                void operator()( uint32 index, FileRead& outRead ) const
                {
                    if ( outRead._bCached == SW_TRUE || _stopToken.isStopRequested() )
                        return; // 멈추면 결과는 불완전하니 부른 쪽이 신호를 다시 읽고 버린다
                    const AbsolutePath absPath = makeDiskPath( _resourceRoot, _pFile[index]._displayPath );
                    string             text;
                    // 목록을 모은 뒤 지워진 파일(에디터가 옮기는 중)은 읽지 않는다 — readTextFile 은 없는 파일을 오류로 남긴다.
                    const bool bText = outRead._bTextAsset == SW_TRUE;
                    const bool bRead = bText ? FileUtil::readOptionalTextFile( absPath, text ) == OptionalFileRead::Read : readMetaText( absPath, text );
                    if ( bRead )
                        outRead._content = EditorAssetIndex::makeContent( bText ? &text : nullptr, bText ? nullptr : &text );
                    outRead._bRead = bRead || bText == false ? SW_TRUE : SW_FALSE; // 원본은 `.meta` 가 없어도 정체성이 없는 파일로 안다
                }
            };

            /** @brief 훑기 캐시에 둘 바이트입니다 — 곁 서술의 도장(원본은 서술이 바뀌면 다시 읽는다) · 정체성 · 참조 줄. */
            [[nodiscard]] static vector<uint8> encodeContent( const FileStamp& sidecarStamp, const EditorAssetContent& content )
            {
                vector<uint8> bytes;
                ByteWriter    writer{ bytes };
                writer.writeUint64( sidecarStamp._sizeBytes );
                writer.writeUint64( sidecarStamp._writeTime );
                writer.writeString( content._guid.has_value() ? content._guid->toString() : string{} );
                writer.writeUint32( static_cast<uint32>( content._listLink.size() ) );
                for ( const EditorAssetLink& link : content._listLink )
                {
                    writer.writeString( link._target );
                    writer.writeUint32( link._line );
                    writer.writeUint8( link._bInlined ? uint8{ 1 } : uint8{ 0 } );
                }
                return bytes;
            }

            /** @brief `encodeContent` 의 짝입니다. 곁 서술의 도장이 다르거나 바이트가 그 꼴이 아니면 없음입니다. */
            [[nodiscard]] static optional<EditorAssetContent> decodeContent( const vector<uint8>& bytes, const FileStamp& sidecarStamp )
            {
                /** @brief 참조 줄 하나의 최소 바이트입니다(빈 글 길이 4 + 줄 4 + 깃발 1). */
                constexpr size_t   kMinLinkBytes = 9;
                ByteReader         reader{ bytes.data(), bytes.size() };
                const uint64       sidecarSize = reader.readUint64();
                const uint64       sidecarTime = reader.readUint64();
                const string       guidText    = reader.readString();
                EditorAssetContent content;
                const bool         bLinks = reader.readCountedList( content._listLink, kMinLinkBytes, [&reader]( EditorAssetLink& outLink )
                        {
                    outLink._target   = reader.readString();
                    outLink._line     = reader.readUint32();
                    outLink._bInlined = reader.readUint8() != 0;
                } );
                if ( bLinks == false || reader.isFullyRead() == false || FileStamp{ sidecarSize, sidecarTime } != sidecarStamp )
                    return nullopt;
                if ( guidText.empty() == false )
                {
                    content._guid = AssetGUID::tryParse( guidText );
                    if ( content._guid.has_value() == false )
                        return nullopt;
                }
                return content;
            }
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

    void EditorAssetIndex::extractLinks( string_view text, vector<EditorAssetLink>& outListLink, const vector<string>& listMountedDomain )
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
            if ( bAtBoundary == false || ( Internal::startsWithDomain( text, offset ) == false && Internal::startsWithMountedDomain( text, offset, listMountedDomain ) == false ) )
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
        // 정체성 줄 — GUID 쪽은 이 경로만 뺀다(옮긴 새 자리 · 같은 GUID 를 든 복사본은 남는다).
        const auto itGUID = inoutData._mapPathToGUID.find( assetID );
        if ( itGUID == inoutData._mapPathToGUID.end() )
            return;
        const auto itPath = inoutData._mapGUIDToPath.find( itGUID->second );
        if ( itPath != inoutData._mapGUIDToPath.end() )
        {
            VectorUtil::removeValue( itPath->second, assetID );
            if ( itPath->second.empty() )
                inoutData._mapGUIDToPath.erase( itPath );
        }
        inoutData._mapPathToGUID.erase( itGUID );
    }

    EditorAssetContent EditorAssetIndex::makeContent( const string* pText, const string* pMetaText, const vector<string>& listMountedDomain )
    {
        EditorAssetContent content;
        if ( pText != nullptr )
        {
            content._guid = AssetGUIDHeader::read( *pText )._guid;
            extractLinks( *pText, content._listLink, listMountedDomain );
        }
        else if ( pMetaText != nullptr )
        {
            content._guid = EditorAssetIndexInternal::readMetaGUID( *pMetaText );
        }
        return content;
    }

    void EditorAssetIndex::insertEntry( EditorAssetIndexData& inoutData, string_view displayPath, const FileStamp& fileStamp, EditorAssetContent&& content )
    {
        const string assetID = EditorAssetIndexInternal::makeKey( displayPath );
        removeEntry( inoutData, assetID );
        EditorAssetEntry& entry = inoutData._mapEntry[assetID];
        entry._displayPath      = string( displayPath );
        entry._fileStamp        = fileStamp;
        entry._listLink         = std::move( content._listLink );
        for ( const EditorAssetLink& link : entry._listLink )
        {
            vector<string>& listReferrer = inoutData._mapReferrer[link._target];
            if ( listReferrer.empty() || listReferrer.back() != assetID ) // 이 파일의 줄은 방금 넣은 것이 맨 뒤다 — 같은 대상을 여러 번 적어도 한 번만
                listReferrer.push_back( assetID );
        }
        if ( content._guid.has_value() )
        {
            inoutData._mapPathToGUID.insert_or_assign( assetID, *content._guid ); // AssetGUID 는 기본 생성이 없다(무효 GUID 는 optional 로만) — operator[] 대신
            inoutData._mapGUIDToPath[*content._guid].push_back( assetID );        // 이 경로의 옛 줄은 위 removeEntry 가 뺐다
        }
    }

    void EditorAssetIndex::refreshFromDisk( EditorAssetIndexData& inoutData, const AbsolutePath& resourceRoot, string_view displayPath, bool bForce )
    {
        const string       assetID = EditorAssetIndexInternal::makeKey( displayPath );
        const AbsolutePath absPath = EditorAssetIndexInternal::makeDiskPath( resourceRoot, displayPath );
        FileStamp          stamp{};
        if ( FileUtil::getFileStamp( absPath, stamp ) == false )
        {
            removeEntry( inoutData, assetID );
            return;
        }
        const auto itEntry = inoutData._mapEntry.find( assetID );
        if ( bForce == false && itEntry != inoutData._mapEntry.end() && itEntry->second._fileStamp == stamp && itEntry->second._displayPath == displayPath )
            return; // 크기 · 시각 · 표기가 같다 — 열지 않는다
        string text;
        if ( isTextAsset( assetID ) )
        {
            const bool bRead = FileUtil::readOptionalTextFile( absPath, text ) == OptionalFileRead::Read;
            insertEntry( inoutData, displayPath, stamp, makeContent( bRead ? &text : nullptr, nullptr ) ); // 읽지 못한 글은 참조가 없는 것과 같다
            return;
        }
        const bool bMeta = EditorAssetIndexInternal::readMetaText( absPath, text );
        insertEntry( inoutData, displayPath, stamp, makeContent( nullptr, bMeta ? &text : nullptr ) );
    }

    void EditorAssetIndex::scan( const AbsolutePath& resourceRoot, const AbsolutePath& scratchFolder, EditorAssetIndexData& outData, TaskManager* pTaskManager, const StopToken& stopToken, AssetScanCache* pCache )
    {
        using Internal = EditorAssetIndexInternal;
        const Stopwatch stopwatch;
        outData = EditorAssetIndexData{};
        vector<EditorContentFile> listFile;
        // `.meta` 는 목록의 파일이 아니다(EditorContentScan::isListedFile — 한 파일 갱신과 같은 규칙). 작업 도메인(시나리오 사본)은 실행 뿌리 아래라 따로 훑는다.
        if ( resourceRoot.empty() || EditorContentScan::collectTreeFiles( resourceRoot, scratchFolder, listFile, stopToken ) == false )
            return;

        vector<Internal::ScanFile> listScan;
        listScan.reserve( listFile.size() );
        for ( const EditorContentFile& file : listFile )
        {
            string displayPath = Internal::makeDisplayPath( resourceRoot.str(), file._path );
            if ( displayPath.empty() == false )
                listScan.push_back( Internal::ScanFile{ std::move( displayPath ), file._fileStamp, file._sidecarStamp } );
        }
        // 같은 GUID 를 든 두 파일(복사본)의 승자가 실행마다 같게 — 파일 순서로 합친다
        VectorUtil::sort( listScan, []( const Internal::ScanFile& left, const Internal::ScanFile& right )
        { return left._displayPath < right._displayPath; } );

        const uint32               fileCount = static_cast<uint32>( listScan.size() );
        vector<Internal::FileRead> listRead( fileCount );
        for ( uint32 fileIndex = 0; fileIndex < fileCount; ++fileIndex )
        {
            const Internal::ScanFile& file = listScan[fileIndex];
            Internal::FileRead&       read = listRead[fileIndex];
            read._bTextAsset               = isTextAsset( file._displayPath ) ? SW_TRUE : SW_FALSE;
            const vector<uint8>* pCached   = pCache != nullptr ? pCache->find( file._displayPath, file._fileStamp ) : nullptr;
            if ( pCached == nullptr )
                continue;
            optional<EditorAssetContent> content = Internal::decodeContent( *pCached, file._sidecarStamp );
            if ( content.has_value() == false )
                continue; // 곁 서술이 바뀌었다 — 다시 읽어 줄을 새로 적는다
            read._content = std::move( *content );
            read._bRead   = SW_TRUE;
            read._bCached = SW_TRUE;
        }
        Internal::ScanJob job{};
        job._resourceRoot = resourceRoot;
        job._pFile        = listScan.data();
        job._stopToken    = stopToken;
        if ( pTaskManager != nullptr )
        {
            // 호출 스레드가 워커여도 된다: `runParallel` 은 호출 스레드가 첫 청크부터 직접 돌고, 합류를 기다리는 동안 다른 일을 돕는다.
            pTaskManager->runParallelInto<Internal::FileRead>( listRead, Internal::kSerialFileThreshold, job );
        }
        else
        {
            for ( uint32 fileIndex = 0; fileIndex < fileCount; ++fileIndex )
                job( fileIndex, listRead[fileIndex] );
        }

        outData._mapEntry.reserve( fileCount );
        for ( uint32 fileIndex = 0; fileIndex < fileCount; ++fileIndex )
        {
            const Internal::ScanFile& file = listScan[fileIndex];
            Internal::FileRead&       read = listRead[fileIndex];
            if ( read._bTextAsset == SW_TRUE && read._bRead == SW_TRUE )
                ++outData._scannedFileCount;
            // 읽는 동안 멈춘 스캔의 칸은 비어 있다 — 캐시에 빈 줄을 적지 않는다. 작업 도메인은 실행마다 새 사본이고, 막 쓴 서술은 도장으로 가릴 수 없다(파일 자신의 도장은 `record` 가 본다).
            const bool bNewRow = pCache != nullptr && read._bRead == SW_TRUE && read._bCached == SW_FALSE && ContentHash::isStampStable( file._sidecarStamp );
            if ( bNewRow && FileUtil::startsWithPathComponent( FileUtil::normalizePath( file._displayPath ), AssetPaths::kScratchDomain ) == false )
                pCache->record( file._displayPath, file._fileStamp, Internal::encodeContent( file._sidecarStamp, read._content ) );
            insertEntry( outData, file._displayPath, file._fileStamp, std::move( read._content ) );
        }
        outData._scanMilliseconds = stopwatch.getElapsedMilliseconds();
    }

    void EditorAssetIndex::assign( EditorAssetIndexData&& data )
    {
        _data   = std::move( data );
        _bReady = SW_TRUE;
    }

    void EditorAssetIndex::applyChanges( const AbsolutePath& resourceRoot, const vector<string>& listChangedID )
    {
        for ( const string& changed : listChangedID )
        {
            const string       displayPath = FileUtil::normalizeSeparators( changed );
            const string       assetID     = EditorAssetIndexInternal::makeKey( displayPath );
            const AbsolutePath absPath     = EditorAssetIndexInternal::makeDiskPath( resourceRoot, displayPath );
            if ( FileUtil::isDirectory( absPath ) )
            {
                // 폴더가 생겼다(옮겨 왔다) — 그 아래 파일을 하나씩 넣는다. `.meta` 는 전체 훑기와 같은 규칙으로 빠진다(EditorContentScan).
                vector<AbsolutePath> listFile;
                (void)EditorContentScan::collectFiles( absPath, "", listFile, true );
                for ( const AbsolutePath& filePath : listFile )
                {
                    const string fileDisplayPath = EditorAssetIndexInternal::makeDisplayPath( resourceRoot.str(), filePath.str() );
                    if ( fileDisplayPath.empty() == false )
                        refreshFromDisk( _data, resourceRoot, fileDisplayPath, false );
                }
                continue;
            }
            if ( EditorContentScan::isListedFile( displayPath ) == false )
            {
                // `.meta` 가 바뀌었으면 그 원본의 정체성만 다시 읽는다(원본의 크기 · 시각은 그대로라 억지로).
                const bool   bMeta             = StringUtil::endsWith( assetID, AssetMeta::kExtension, true );
                const string sourceDisplayPath = bMeta ? displayPath.substr( 0, displayPath.size() - AssetMeta::kExtension.size() ) : string{};
                if ( bMeta && EditorContentScan::isListedFile( sourceDisplayPath ) && _data._mapEntry.contains( EditorAssetIndexInternal::makeKey( sourceDisplayPath ) ) )
                    refreshFromDisk( _data, resourceRoot, sourceDisplayPath, true );
                continue;
            }
            if ( _data._mapEntry.contains( assetID ) || FileUtil::isRegularFile( absPath ) )
            {
                refreshFromDisk( _data, resourceRoot, displayPath, false );
                continue;
            }
            // 모르는 이름이 없다 — 사라진 폴더다. 그 아래 줄을 모두 뺀다(폴더 하나라 전체를 한 번 훑는다).
            StringBuilder<constant::buffer::kMax256> folderPrefix;
            folderPrefix.appendFormat( "%#/", assetID );
            vector<string> listRemoved;
            for ( const auto& [entryID, entry] : _data._mapEntry )
            {
                if ( StringUtil::startsWith( entryID, folderPrefix.view() ) )
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
        const string displayPath = FileUtil::normalizeSeparators( assetID );
        const string copy{ text };
        // 붙인 디스크 도메인(테스트 · 도구)의 글도 넣으므로 그 도메인 이름을 참조 앞머리로 함께 본다 — 이 함수는 에디터 스레드에서만 부른다.
        vector<string> listMountedDomain;
        editor::getSubsystem<AssetPaths>().collectMountedDiskDomains( listMountedDomain );
        insertEntry( _data, displayPath, fileStamp, makeContent( isTextAsset( displayPath ) ? &copy : nullptr, nullptr, listMountedDomain ) );
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
            return AssetRegistry::resolve( guid ); // 임포트 산출물은 파일에 정체성이 없다 — 줄은 런타임 색인이 원본에서 만든다
        return itPath->second.front();
    }

    optional<AssetGUID> EditorAssetIndex::findGUID( string_view assetID ) const
    {
        const auto itGUID = _data._mapPathToGUID.find( FileUtil::normalizePath( assetID ).str() );
        if ( itGUID == _data._mapPathToGUID.end() )
            return AssetRegistry::resolveGUID( assetID ); // 임포트 산출물(위와 같다)
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

    void EditorAssetIndex::collectDisplayPaths( vector<string>& outListPath ) const
    {
        outListPath.clear();
        outListPath.reserve( _data._mapEntry.size() );
        for ( const auto& [assetID, entry] : _data._mapEntry )
        {
            outListPath.push_back( entry._displayPath );
        }
        VectorUtil::sort( outListPath );
    }
} // namespace sw::editor

namespace sw::editor
{
    void EditorAssetIndexScanJob::request( const AbsolutePath& resourceRoot, TaskManager* pTaskManager )
    {
        const uint32 generation = beginRequest( resourceRoot );
        submitJob( "EditorAssetIndexScan", SW_DELEGATE_FUNCTION( TaskArgsDelegate, EditorAssetIndexScanJob::runJob ), MakeTaskArgs( _pState, generation, pTaskManager ) );
    }

    void EditorAssetIndexScanJob::runJob( const TaskArgs& args )
    {
        const shared_ptr<State> pState     = args.get<shared_ptr<State>>( 0 );
        const uint32            generation = args.get<uint32>( 1 );
        TaskManager* const      pTasks     = args.get<TaskManager*>( 2 );

        AbsolutePath resourceRoot;
        if ( readInput( pState, generation, resourceRoot ) == false )
            return;

        EditorAssetIndexData data{};
        const StopToken      stopToken = readStopSignal( pState );
        // 에디터를 열 때마다 모든 글 에셋 · `.meta` 를 다시 읽지 않는다(언리얼 AssetRegistry 캐시 · Godot `filesystem_cache`).
        AssetScanCache       cache{ EditorAssetIndex::kScanPayloadVersion };
        const AbsolutePath   cachePath = AssetScanCache::makeLocalPath( "EditorAssetIndex.bin" );
        const expected<void> loaded    = cache.load( cachePath );
        if ( loaded.has_value() == false )
            SW_LOG_INFO_ID( "EditorAssetIndex.ScanCacheDropped", "%# '%#' - reading every asset", loaded.error().message(), loaded.error().detail() );
        EditorAssetIndex::scan( resourceRoot, AssetPaths::getScratchFolderPath(), data, pTasks, stopToken, &cache );
        if ( stopToken.isStopRequested() )
            return; // 종료가 시작돼 스캔이 중간에 멈췄다 — 불완전한 색인 · 캐시를 싣지 않는다
        const expected<void> saved = cache.save( cachePath );
        if ( saved.has_value() == false )
            SW_LOG_INFO_ID( "EditorAssetIndex.ScanCacheNotSaved", "%# '%#' - the next start reads again", saved.error().message(), saved.error().detail() );

        publish( pState, generation, std::move( data ) );
    }
} // namespace sw::editor
