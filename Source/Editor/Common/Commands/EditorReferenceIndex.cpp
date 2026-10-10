#include "pch.h"

#include "Editor/Common/Commands/EditorReferenceIndex.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Time/MonotonicClock.h"

namespace sw::editor
{
    namespace
    {
        struct EditorReferenceIndexInternal
        {
            /** @brief 참조가 시작할 수 있는 리소스 도메인입니다(리소스 id 의 첫 조각). */
            static constexpr const utf8* kArrDomainPrefix[] = { "engine/", "common/", "game/", "editor/" };
            /** @brief 참조를 훑는 텍스트 에셋의 확장자입니다. 바이너리(dds, mesh, 셰이더 바이너리)는 글이 아니다. */
            static constexpr const utf8* kArrTextExtension[] = { ".xml", ".json", ".material", ".hlsl", ".hlsli" };
            /**
             * @brief 텍스트지만 에셋이 아닌 것입니다 — 자동화 시나리오는 시험 입력이라 참조자로 세지 않는다.
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
            static bool scanFile( string_view resourceRoot, string_view resourceID, const vector<string>& listSortedKnownID,
                                  vector<EditorAssetReference>& outListReference )
            {
                // 목록을 모은 뒤 지워진 파일(에디터가 옮기는 중)은 읽지 않는다 — readTextFile 은 없는 파일을 오류로 남긴다.
                const string filePath = FileUtil::joinPath( resourceRoot, resourceID );
                string       text;
                if ( FileUtil::exists( filePath ) == false || FileUtil::readTextFile( filePath, text ) == false )
                    return false;
                EditorReferenceIndex::extractReferences( resourceID, text, listSortedKnownID, outListReference );
                return true;
            }

            static string makeResourceID( string_view resourceRoot, string_view filePath )
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

    void EditorReferenceIndex::scan( string_view resourceRoot, EditorReferenceIndexData& outData )
    {
        const Stopwatch stopwatch;
        outData = EditorReferenceIndexData{};
        vector<string> listFile;
        if ( resourceRoot.empty() || FileUtil::collectFiles( resourceRoot, "", listFile, true ) == false )
            return;

        outData._listKnownID.reserve( listFile.size() );
        for ( const string& filePath : listFile )
        {
            string resourceID = EditorReferenceIndexInternal::makeResourceID( resourceRoot, filePath );
            if ( resourceID.empty() == false )
                outData._listKnownID.push_back( std::move( resourceID ) );
        }
        std::sort( outData._listKnownID.begin(), outData._listKnownID.end() );

        for ( const string& resourceID : outData._listKnownID )
        {
            if ( isTextAsset( resourceID ) == false )
                continue;
            if ( EditorReferenceIndexInternal::scanFile( resourceRoot, resourceID, outData._listKnownID, outData._listReference ) )
                ++outData._scannedFileCount;
        }
        sortByTarget( outData._listReference );
        outData._scanMilliseconds = stopwatch.getElapsedMilliseconds();
    }

    void EditorReferenceIndex::assign( EditorReferenceIndexData&& data )
    {
        _data   = std::move( data );
        _bReady = SW_TRUE;
    }

    void EditorReferenceIndex::refreshFile( string_view resourceRoot, string_view resourcePath )
    {
        const string resourceID = FileUtil::normalizePath( resourcePath );
        _data._listReference.erase( std::remove_if( _data._listReference.begin(), _data._listReference.end(),
                                                    [&resourceID]( const EditorAssetReference& reference )
        { return reference._referrerPath == resourceID; } ),
                                    _data._listReference.end() );

        const bool bExists = FileUtil::exists( FileUtil::joinPath( resourceRoot, resourceID ) );
        const auto itKnown = std::lower_bound( _data._listKnownID.begin(), _data._listKnownID.end(), resourceID );
        const bool bKnown  = itKnown != _data._listKnownID.end() && *itKnown == resourceID;
        if ( bExists && bKnown == false )
            _data._listKnownID.insert( itKnown, resourceID );
        else if ( bExists == false && bKnown )
            _data._listKnownID.erase( itKnown );

        if ( bExists && isTextAsset( resourceID ) )
            (void)EditorReferenceIndexInternal::scanFile( resourceRoot, resourceID, _data._listKnownID, _data._listReference ); // 읽지 못한 파일은 참조가 없는 것과 같다
        sortByTarget( _data._listReference );
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
