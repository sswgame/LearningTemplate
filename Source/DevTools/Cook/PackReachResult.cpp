#include "pch.h"

#include "DevTools/Cook/PackReachResult.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/Cook/AssetCompanion.h"
#include "Engine/Asset/Reference/AssetReferenceFields.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Content/Pack/PackConfig.h"
#include "Engine/Serialization/Inherit/AssetRedirect.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLReadUtil.h"

namespace sw
{
    namespace
    {
        struct PackReachabilityInternal
        {
            /** @brief 게임 설정의 필수 데이터 요소와 그 속성입니다(`GameSettings::loadFromXMLText` 가 읽는 이름 — Engine 이 GameFramework 를 볼 수 없어 이름을 여기에도 적는다). */
            static constexpr const utf8* kRequiredDataElement = "RequiredData";
            static constexpr const utf8* kRequiredDataKind    = "kind";
            static constexpr const utf8* kRequiredDataPack    = "pack";

            /** @brief 항목 부모 칸입니다(`_base` — 같은 카탈로그의 id, 다른 파일이면 `파일#id` 로 참조 칸이 푼다). */
            static constexpr const utf8* kEntryBaseAttribute = "_base";
            /** @brief 목록 원소 요소의 이름입니다(`<_listX><item>id</item></_listX>`). */
            static constexpr const utf8* kItemElement = "item";
            /** @brief 파일 이름의 줄기(첫 `.` 앞)입니다. */
            static string_view getFirstStem( string_view fileName )
            {
                const size_t dot = fileName.find( '.' );
                return dot == string_view::npos ? fileName : fileName.substr( 0, dot );
            }

            /** @brief 경로의 파일 이름입니다. */
            static string_view getFileName( string_view path )
            {
                const size_t slash = path.rfind( '/' );
                return slash == string_view::npos ? path : path.substr( slash + 1 );
            }

            /** @brief 원본 표의 열쇠(`<폴더 id>/<줄기>`)입니다. */
            static string makeSourceStemKey( string_view assetID )
            {
                const string_view                        fileName = getFileName( assetID );
                StringBuilder<constant::buffer::kMax512> builder;
                builder.appendFormat( "%#%#", assetID.substr( 0, assetID.size() - fileName.size() ), getFirstStem( fileName ) );
                return string( builder.view() );
            }

            /** @brief 두 낱말의 열쇠(`<앞>\t<뒤>` — 셰이더 표의 도메인 · 줄기, 불린 항목의 이름 공간 · id)입니다. */
            static string makeTabKey( string_view domain, string_view stem )
            {
                StringBuilder<constant::buffer::kMax256> builder;
                builder.appendFormat( "%#\t%#", domain, stem );
                return string( builder.view() );
            }

            /** @brief 항목 하나가 불렸다고 적습니다. 경로 · GUID 참조는 참조 칸이 푼다. */
            static void appendNamedKey( string_view kind, string_view id, PackReachText& inoutText )
            {
                if ( id.empty() == false && id.find( '/' ) == string_view::npos && AssetRegistry::isReferenceText( id ) == false )
                    inoutText._listNamedKey.push_back( makeTabKey( kind, id ) );
            }

            /** @brief 값 @p value 를 낱말로 갈라 @p kind 의 항목으로 부릅니다. */
            static void appendNamedTokens( string_view kind, string_view value, PackReachText& inoutText )
            {
                size_t cursor = 0;
                while ( cursor < value.size() )
                {
                    const size_t start = value.find_first_not_of( XMLReadUtil::kListSeparators, cursor );
                    if ( start == string_view::npos )
                        return;
                    const size_t end = MathUtil::min( value.find_first_of( XMLReadUtil::kListSeparators, start ), value.size() );
                    appendNamedKey( kind, value.substr( start, end - start ), inoutText );
                    cursor = end;
                }
            }

            /** @brief 열린 카탈로그의 항목 하나입니다(이번 고르기에서 닿은 카탈로그 — 그래프의 항목을 가리킨다). */
            struct OpenEntry
            {
                const PackReachEntry* _pEntry{ nullptr };
                const PackReachFile*  _pCatalog{ nullptr };
            };

            /** @brief 한 번의 고르기 상태입니다. 담은 집합과 걸을 차례를 든다 — 글을 읽고 푸는 일은 그래프(`PackReachGraph`)가 파일마다 한 번만 한다. */
            struct ReachTraversal
            {
                PackReachGraph&                             _graph;
                const unordered_set<string>&                _setDeferredRoot;
                unordered_set<string>&                      _setIncluded;
                unordered_set<string>&                      _setReferencedExcluded;
                vector<string>                              _listPending;     ///< 담았고 아직 걷지 않은 파일
                unordered_set<string>                       _setWholeCatalog; ///< 모든 항목이 쓰이는 카탈로그 파일
                unordered_set<string>                       _setNamedKey;     ///< 불린 항목(`<이름 공간>\t<id>`)
                vector<string>                              _listPendingKey;  ///< 불렸고 아직 찾아보지 않은 항목
                unordered_set<const PackReachEntry*>        _setVisitedEntry;
                unordered_map<string, vector<OpenEntry>>    _mapOpenEntry;   ///< 항목 열쇠 → 열린 카탈로그의 그 항목들(연 순서)
                unordered_map<string, const PackReachFile*> _mapOpenCatalog; ///< 카탈로그 파일 id → 그 사실(주소는 그래프가 고정한다)
                uint8                                       _bFailed : 1;

                ReachTraversal( PackReachGraph& graph, const unordered_set<string>& setDeferredRoot, unordered_set<string>& setIncluded, unordered_set<string>& setReferencedExcluded )
                    : _graph{ graph }
                    , _setDeferredRoot{ setDeferredRoot }
                    , _setIncluded{ setIncluded }
                    , _setReferencedExcluded{ setReferencedExcluded }
                    , _listPending{}
                    , _setWholeCatalog{}
                    , _setNamedKey{}
                    , _listPendingKey{}
                    , _setVisitedEntry{}
                    , _mapOpenEntry{}
                    , _mapOpenCatalog{}
                    , _bFailed{ SW_FALSE }
                {
                }

                /** @brief 글 하나의 참조와 부르는 항목을 따라갑니다. */
                void follow( const PackReachText& text, const string& referrer )
                {
                    for ( const string& key : text._listNamedKey )
                    {
                        if ( _setNamedKey.insert( key ).second )
                            _listPendingKey.push_back( key );
                    }
                    for ( const PackReachReference& reference : text._listReference )
                    {
                        resolve( reference._path, referrer, reference._field, reference._reach );
                    }
                }

                /** @brief 항목 하나를 따라갑니다(한 번만). */
                void visitEntry( const PackReachEntry& entry, const string& catalogID )
                {
                    if ( _setVisitedEntry.insert( &entry ).second )
                        follow( entry._text, catalogID );
                }

                /** @brief 닿은 카탈로그를 엽니다. 항목 밖의 글은 늘 따라가고, 이미 불린 항목 · 통째로 쓰는 파일의 항목을 따라간다. */
                void openCatalog( const PackReachFile& file )
                {
                    _mapOpenCatalog[file._assetID] = &file;
                    follow( file._text, file._assetID );
                    const bool bWhole = _setWholeCatalog.contains( file._assetID );
                    for ( const PackReachEntry& entry : file._listEntry )
                    {
                        if ( bWhole || _setNamedKey.contains( entry._key ) )
                            visitEntry( entry, file._assetID );
                    }
                    for ( const PackReachEntry& entry : file._listEntry )
                    {
                        _mapOpenEntry[entry._key].push_back( OpenEntry{ &entry, &file } );
                    }
                }

                /** @brief 불린 항목을 열린 카탈로그에서 찾아 따라갑니다. 아직 열지 않은 파일은 열 때 `_setNamedKey` 로 본다. 없는 id 는 담을 것이 없다. */
                void visitNamedKey( const string& key )
                {
                    const auto entryIter = _mapOpenEntry.find( key );
                    if ( entryIter == _mapOpenEntry.end() )
                        return;
                    const vector<OpenEntry> listOpen = entryIter->second; // 따라가는 동안 새 카탈로그가 열려 색인이 자랄 수 있다 — 지금 연 것까지만 본다
                    for ( const OpenEntry& open : listOpen )
                    {
                        visitEntry( *open._pEntry, open._pCatalog->_assetID );
                    }
                }

                /** @brief 통째로 쓰게 된 카탈로그를 표시합니다. 이미 열었으면 모든 항목을 따라간다. */
                void markWholeCatalog( const string& assetID )
                {
                    if ( _setWholeCatalog.insert( assetID ).second == false )
                        return;
                    const auto catalogIter = _mapOpenCatalog.find( assetID );
                    if ( catalogIter == _mapOpenCatalog.end() )
                        return;
                    const PackReachFile& file = *catalogIter->second;
                    for ( const PackReachEntry& entry : file._listEntry )
                    {
                        visitEntry( entry, assetID );
                    }
                }

                /** @brief 담을 수 있는 파일 하나를 담습니다. 처음 담으면 걸을 차례에 넣는다. */
                void include( const string& assetID, PackCatalogReach reach )
                {
                    if ( reach == PackCatalogReach::Whole && _graph.findCatalogRule( assetID ) != nullptr )
                        markWholeCatalog( assetID );
                    if ( _setIncluded.insert( assetID ).second )
                        _listPending.push_back( assetID );
                }

                /**
                 * @brief 참조 하나(@p path — 정규화)를 풉니다. 담을 파일이면 담고, 폴더면 그 아래 전부를 담고, 제외 규칙이 뺀 원본이면 넘어간다. 아무것도 아니면 오류다.
                 * @param referrer 가리킨 쪽(오류 문구용 — 파일 id 와 칸)
                 * @param reach 이 참조가 카탈로그 파일에 닿으면 그 파일을 어떻게 쓰는가
                 */
                void resolve( const string& path, string_view referrer, string_view field, PackCatalogReach reach )
                {
                    if ( _graph.getFiles()._setPackable.contains( path ) )
                    {
                        include( path, reach );
                        return;
                    }
                    // 제외 규칙이 뺀 원본(셰이더 원본 — 백엔드 쿠킹본이 스테이징에 있다, 머티리얼 그래프 원본 — 생성 머티리얼이 담긴다)은 빠진 것이 아니다.
                    if ( _graph.getFiles()._setExisting.contains( path ) )
                    {
                        _setReferencedExcluded.insert( path );
                        return;
                    }
                    if ( const vector<string>* const pListUnder = _graph.findPackableUnder( path ); pListUnder != nullptr )
                    {
                        // 폴더를 가리키는 참조는 런타임이 그 폴더를 훑는다는 뜻이다(외형 데이터 폴더 · 시나리오 사본 원본) — 그 아래 전부가 쓰인다.
                        for ( const string& assetID : *pListUnder )
                        {
                            include( assetID, reach );
                        }
                        return;
                    }
                    if ( _graph.isExistingFolder( path ) )
                        return;
                    SW_LOG_ERROR_ID( "PackReachability.MissingReference", "Pack cook: %# (%#) references '%#', which is not in any cooked domain - fix the reference or add the asset (nothing is dropped silently)",
                                     referrer, field, path );
                    _bFailed = SW_TRUE;
                }

                /** @brief @p assetID 가 이 구성에서 쓰는 항상 담기 묶음 하나에 맞으면 true 입니다. */
                bool matchesAlwaysCook( const string& assetID ) const
                {
                    for ( const PackAlwaysCookRule& rule : _graph.getRules()._listAlwaysCook )
                    {
                        if ( rule._bAutomationOnly && _graph.getFiles()._automation == PackAutomationContent::Excluded )
                            continue; // 자동화 없는 배포 - 시나리오는 읽히지 않는다
                        if ( rule._scope.isApplicableTo( assetID ) == false )
                            continue; // 이 팩에는 닿지 않는 규칙이다(다른 게임의 규칙 · 이 게임이 덮은 엔진 규칙)
                        const auto matches = [&assetID]( const string& pattern )
                        { return StringUtil::matchesPathPattern( pattern, assetID ); };
                        if ( std::any_of( rule._listPattern.begin(), rule._listPattern.end(), matches ) &&
                             std::none_of( rule._listExcludePattern.begin(), rule._listExcludePattern.end(), matches ) )
                            return true;
                    }
                    return false;
                }

                /** @brief 항상 담기 묶음에 맞는 파일을 뿌리로 담습니다(제외 규칙이 뺀 원본이면 그 쿠킹본만 담도록 적는다). 뿌리 수를 돌려준다. */
                uint32 addAlwaysCookRoots()
                {
                    uint32 rootCount = 0;
                    for ( const string& assetID : _graph.getFiles()._setExisting )
                    {
                        if ( matchesAlwaysCook( assetID ) == false || _setDeferredRoot.contains( assetID ) )
                            continue;
                        ++rootCount;
                        if ( _graph.getFiles()._setPackable.contains( assetID ) )
                            include( assetID, PackCatalogReach::Whole );
                        else
                            _setReferencedExcluded.insert( assetID );
                    }
                    return rootCount;
                }

                /** @brief 코드가 상수로 읽는 에셋을 뿌리로 담습니다. 뿌리 수를 돌려준다. */
                uint32 addCodeRoots()
                {
                    for ( const string& root : _graph.getFiles()._listCodeRoot )
                    {
                        resolve( FileUtil::normalizePath( root ).str(), "SW_ASSET_COOK_ROOT", root, PackCatalogReach::Whole );
                    }
                    return static_cast<uint32>( _graph.getFiles()._listCodeRoot.size() );
                }

                /**
                 * @brief 도메인 뿌리의 리다이렉트 표(`AssetRedirect::makeTableID` 자리의 `redirects.xml`)를 담습니다. 뿌리 수를 돌려준다.
                 * @details 런타임이 이름으로 읽어(`AssetRedirect::loadFromSearchDomains`) 참조 칸으로는 닿지 않는다 — 빠지면 배포 팩에서 경로를 적던 때의 세이브가 죽는다.
                 *          훑지 않는다: `from` 은 이제 없는 옛 id 라 따라가면 빠진 참조다(참조 검사도 표를 보지 않는다 — `AssetReferenceFields::isReferrer`).
                 *          언리얼은 리다이렉터가 패키지(`UObjectRedirector`)라 그 경로를 참조하는 쪽이 쿠킹에 끌고 오고, 여기는 도메인마다 표 하나라 뿌리로 둔다.
                 */
                uint32 addRedirectTableRoots()
                {
                    uint32 rootCount = 0;
                    // 디스크에 있는 도메인 표를 본다 — 담을 수 있는 집합만 보면 제외 규칙(`PackConfig.json`)이 뺀 표가 조용히 빠지고, 런타임은 "표가 없는 도메인" 을
                    // 정상으로 봐 옛 세이브의 씬을 찾지 못한다(설계 2026-11-17 X1). 그 표는 쿠킹 오류다.
                    for ( const string& assetID : _graph.getFiles()._setExisting )
                    {
                        if ( AssetRedirect::isTable( assetID ) == false || AssetRedirect::makeTableID( assetID ) != assetID )
                            continue;
                        if ( _graph.getFiles()._setPackable.contains( assetID ) == false )
                        {
                            SW_LOG_ERROR_ID( "PackCooker.RedirectTableExcluded", "Pack cook: the redirect table '%#' is excluded by the pack rules - old paths would not resolve at runtime (keep it in the pack)",
                                             assetID );
                            _bFailed = true;
                            continue;
                        }
                        _setIncluded.insert( assetID );
                        ++rootCount;
                    }
                    return rootCount;
                }

                /** @brief 굽는 게임마다 게임 설정의 `<RequiredData kind pack>` 이 가리키는 자리를 뿌리로 담습니다. 뿌리 수를 돌려준다. */
                uint32 addRequiredDataRoots()
                {
                    uint32 rootCount = 0;
                    for ( const string& gameDomain : _graph.getFiles()._listGameDomain )
                    {
                        StringBuilder<constant::buffer::kMax512> settingsPath;
                        settingsPath.appendFormat( "%#/%#", gameDomain, _graph.getRules()._gameSettingsPath );
                        const string settingsID = FileUtil::normalizePath( settingsPath.view() ).str();
                        if ( _graph.getFiles()._setPackable.contains( settingsID ) == false )
                        {
                            SW_LOG_ERROR_ID( "PackReachability.GameSettingsMissing", "Pack cook: game pack %# has no game settings '%#' - its start map and required data cannot be found", gameDomain, settingsID );
                            _bFailed = true;
                            continue;
                        }
                        include( settingsID, PackCatalogReach::Whole );
                        string text;
                        if ( _graph.getSource().readText( settingsID, text ) == false )
                        {
                            SW_LOG_ERROR_ID( "PackReachability.GameSettingsUnreadable", "Pack cook: game settings '%#' could not be read", settingsID );
                            _bFailed = true;
                            continue;
                        }
                        XMLDocument document;
                        if ( document.parse( text, settingsID ) == false )
                        {
                            SW_LOG_ERROR_ID( "PackReachability.GameSettingsInvalid", "Pack cook: game settings '%#' is not valid XML - %#", settingsID, document.getLastError() );
                            _bFailed = true;
                            continue;
                        }
                        for ( XMLNode line = document.getRoot().findChild( kRequiredDataElement ); line.isValid(); line = line.findNextSibling( kRequiredDataElement ) )
                        {
                            rootCount += addRequiredData( settingsID, line.getAttributeText( kRequiredDataKind ), line.getAttributeText( kRequiredDataPack ) );
                        }
                    }
                    return rootCount;
                }

                /** @brief 필수 데이터 한 줄을 풉니다. 표(`_listRequiredData`)에 없는 종류는 오류다 — 자리를 모른 채 넘어가면 배포본에 그 데이터가 없다. */
                uint32 addRequiredData( const string& settingsID, string_view kind, string_view pack )
                {
                    if ( kind.empty() || pack.empty() )
                    {
                        SW_LOG_ERROR_ID( "PackReachability.RequiredDataNeedsBoth", "Pack cook: %#: <RequiredData> needs both kind and pack (kind='%#' pack='%#')", settingsID, kind, pack );
                        _bFailed = true;
                        return 0;
                    }
                    for ( const PackRequiredDataPath& entry : _graph.getRules()._listRequiredData )
                    {
                        if ( StringUtil::equals( entry._kind, kind ) == false )
                            continue;
                        const string path = FileUtil::normalizePath( FileUtil::trimTrailingSlashes( FileUtil::joinPath( pack, entry._path ) ) ).str();
                        resolve( path, settingsID, kRequiredDataElement, PackCatalogReach::Named ); // 데이터를 읽는다는 뜻일 뿐이다 - 어느 항목을 쓰는지는 id 칸이 말한다
                        return 1;
                    }
                    SW_LOG_ERROR_ID( "PackReachability.RequiredDataKindUnknown",
                                     "Pack cook: %#: <RequiredData kind=\"%#\"> is not in the cook rules' _listRequiredData (Config/Engine/PackConfig.json) - add its in-pack path", settingsID, kind );
                    _bFailed = true;
                    return 0;
                }

                /** @brief 담은 파일 하나의 곁 파일과 참조를 따라갑니다. */
                void visit( const string& assetID )
                {
                    const PackReachFile& file = _graph.getFile( assetID );
                    if ( file._bFailed == SW_TRUE )
                        _bFailed = SW_TRUE;
                    for ( const string& companion : file._listCompanion )
                    {
                        include( companion, PackCatalogReach::Whole );
                    }
                    if ( file._bEntryCatalog == SW_TRUE )
                        openCatalog( file );
                    else
                        follow( file._text, assetID );
                }

                void run()
                {
                    while ( _listPending.empty() == false || _listPendingKey.empty() == false )
                    {
                        if ( _listPending.empty() == false )
                        {
                            const string assetID = std::move( _listPending.back() );
                            _listPending.pop_back();
                            visit( assetID );
                            continue;
                        }
                        const string key = std::move( _listPendingKey.back() );
                        _listPendingKey.pop_back();
                        visitNamedKey( key );
                    }
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    // ------------------------------------------------------------------------------
    // PackReachGraph
    // ------------------------------------------------------------------------------
    PackReachGraph::PackReachGraph( const AssetReferenceFields& params, const PackCookRules& rules, const PackReachFiles& files, const IPackReachSource& source )
        : _params{ params }
        , _rules{ rules }
        , _files{ files }
        , _source{ source }
        , _setUnfollowedField{}
        , _mapIDFieldKind{}
        , _mapPackableUnder{}
        , _setExistingFolder{}
        , _mapFile{}
    {
        // 파일마다 조상 폴더(`a/b/c.x` → `a/b` · `a`)를 한 번 색인한다 — 폴더 참조마다 파일 목록 전체를 훑지 않는다.
        for ( const string& assetID : files._setPackable )
        {
            for ( size_t slash = assetID.rfind( '/' ); slash != string::npos && slash > 0; slash = assetID.rfind( '/', slash - 1 ) )
                _mapPackableUnder[assetID.substr( 0, slash )].push_back( assetID );
        }
        for ( const string& assetID : files._setExisting )
        {
            for ( size_t slash = assetID.rfind( '/' ); slash != string::npos && slash > 0; slash = assetID.rfind( '/', slash - 1 ) )
                _setExistingFolder.insert( assetID.substr( 0, slash ) );
        }
        for ( const string& field : rules._listUnfollowedField )
        {
            _setUnfollowedField.insert( field );
        }
        for ( const PackEntryIDField& idField : rules._listEntryIDField )
        {
            _mapIDFieldKind[idField._field].push_back( &idField );
        }
    }

    PackReachGraph::~PackReachGraph() = default;

    const PackReachFile& PackReachGraph::getFile( const string& assetID )
    {
        const auto [fileIter, bInserted] = _mapFile.try_emplace( assetID, nullptr );
        if ( bInserted )
        {
            fileIter->second           = make_unique<PackReachFile>();
            fileIter->second->_assetID = assetID;
            loadFile( assetID, *fileIter->second );
        }
        return *fileIter->second;
    }

    const vector<string>* PackReachGraph::findPackableUnder( const string& path ) const
    {
        const auto underIter = _mapPackableUnder.find( path );
        return underIter != _mapPackableUnder.end() ? &underIter->second : nullptr;
    }

    const PackEntryCatalog* PackReachGraph::findCatalogRule( string_view assetID ) const
    {
        for ( const PackEntryCatalog& rule : _rules._listEntryCatalog )
        {
            StringBuilder<constant::buffer::kMax256> suffix;
            suffix.appendFormat( "/%#", rule._path );
            if ( StringUtil::endsWith( assetID, suffix.view() ) )
                return &rule;
        }
        return nullptr;
    }

    const string* PackReachGraph::findIDFieldKind( string_view fieldName, string_view assetID ) const
    {
        const auto itField = _mapIDFieldKind.find( fieldName );
        if ( itField == _mapIDFieldKind.end() )
            return nullptr;
        for ( const PackEntryIDField* pField : itField->second )
        {
            if ( pField->_scope.isApplicableTo( assetID ) )
                return &pField->_kind;
        }
        return nullptr;
    }

    void PackReachGraph::collectNamedEntries( XMLNode node, const string& assetID, PackReachText& inoutText ) const
    {
        using Internal = PackReachabilityInternal;
        for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
        {
            if ( const string* pKind = findIDFieldKind( attribute.getName(), assetID ); pKind != nullptr )
                Internal::appendNamedTokens( *pKind, attribute.getValue(), inoutText );
        }
        for ( XMLNode child = node.findChild(); child.isValid(); child = child.findNextSibling() )
        {
            if ( const string* pKind = findIDFieldKind( child.getName(), assetID ); pKind != nullptr )
            {
                Internal::appendNamedTokens( *pKind, child.getText(), inoutText );
                for ( XMLNode item = child.findChild( Internal::kItemElement ); item.isValid(); item = item.findNextSibling( Internal::kItemElement ) )
                {
                    Internal::appendNamedTokens( *pKind, item.getText(), inoutText );
                }
            }
            collectNamedEntries( child, assetID, inoutText );
        }
    }

    void PackReachGraph::collectReferences( string_view text, const string& assetID, PackReachText& inoutText ) const
    {
        vector<AssetReference> listReference;
        _params.extract( text, AssetReferenceFields::getSyntaxOf( assetID ), listReference );
        for ( AssetReference& reference : listReference )
        {
            if ( _setUnfollowedField.contains( reference._field ) || _params.isChecked( reference._path ) == false )
                continue;
            const size_t after  = reference._offset + reference._length;
            const bool   bEntry = after < text.size() && text[after] == '#';
            if ( bEntry )
            {
                const PackEntryCatalog* pRule = findCatalogRule( reference._path );
                if ( pRule != nullptr )
                {
                    const string_view rest = text.substr( after + 1 );
                    PackReachabilityInternal::appendNamedKey( pRule->_kind, rest.substr( 0, MathUtil::min( rest.find_first_of( ",; \t\r\n\"'<" ), rest.size() ) ), inoutText );
                }
            }
            inoutText._listReference.push_back( PackReachReference{ std::move( reference._path ), std::move( reference._field ), bEntry ? PackCatalogReach::Named : PackCatalogReach::Whole } );
        }
    }

    void PackReachGraph::loadFile( const string& assetID, PackReachFile& outFile )
    {
        vector<string> listCompanion;
        AssetCompanionRule::appendCompanionPaths( assetID, listCompanion );
        for ( const string& companion : listCompanion )
        {
            string normalized = FileUtil::normalizePath( companion ).str();
            if ( _files._setPackable.contains( normalized ) )
                outFile._listCompanion.push_back( std::move( normalized ) ); // 곁 파일은 선택 파일이다 — 없으면 넘어간다
        }
        if ( _params.isTextAsset( assetID ) == false )
            return;
        string text;
        if ( _source.readText( assetID, text ) == false )
        {
            SW_LOG_ERROR_ID( "PackReachability.TextUnreadable", "Pack cook: '%#' could not be read to follow its references", assetID );
            outFile._bFailed = SW_TRUE;
            return;
        }
        if ( const PackEntryCatalog* pRule = findCatalogRule( assetID ); pRule != nullptr )
        {
            loadCatalog( *pRule, assetID, text, outFile );
            return;
        }
        collectReferences( text, assetID, outFile._text );
        if ( _mapIDFieldKind.empty() || AssetReferenceFields::getSyntaxOf( assetID ) != AssetReferenceSyntax::XML )
            return;
        XMLDocument document;
        // 깨진 글은 런타임도 읽지 못한다 - 경로 칸은 위에서 이미 모았고, 이 글이 부르는 항목은 없다.
        if ( document.parse( text, assetID ) )
            collectNamedEntries( document.getRoot(), assetID, outFile._text );
    }

    void PackReachGraph::loadCatalog( const PackEntryCatalog& rule, const string& assetID, const string& text, PackReachFile& outFile )
    {
        using Internal         = PackReachabilityInternal;
        outFile._bEntryCatalog = SW_TRUE;
        XMLDocument document;
        if ( document.parse( text, assetID ) == false )
        {
            SW_LOG_ERROR_ID( "PackReachability.CatalogInvalid", "Pack cook: entry catalog '%#' is not valid XML - %#", assetID, document.getLastError() );
            outFile._bFailed = SW_TRUE;
            return;
        }
        struct Span
        {
            XMLNode _node;
            size_t  _start;
        };
        const string_view allText = text;
        vector<Span>      listSpan;
        for ( XMLNode child = document.getRoot().findChild(); child.isValid(); child = child.findNextSibling() )
        {
            const optional<size_t> offset = child.getSourceOffset();
            if ( offset.has_value() == false || string_view( child.getName() ).empty() )
                continue; // 글 노드
            // 위치는 요소 이름을 가리킨다 — 태그의 `<` 부터 잘라야 뽑기가 태그로 읽는다.
            const size_t start = allText.rfind( '<', *offset );
            listSpan.push_back( Span{ child, start == string_view::npos ? 0 : start } );
        }
        // 첫 자식 앞(루트 태그)까지가 항목 밖의 글이다.
        collectReferences( allText.substr( 0, listSpan.empty() ? allText.size() : listSpan.front()._start ), assetID, outFile._text );
        for ( size_t index = 0; index < listSpan.size(); ++index )
        {
            const Span&       span    = listSpan[index];
            const size_t      end     = index + 1 < listSpan.size() ? listSpan[index + 1]._start : allText.size();
            const string_view element = allText.substr( span._start, end - span._start );
            if ( StringUtil::equals( span._node.getName(), string_view( rule._entryElement ) ) == false )
            {
                collectReferences( element, assetID, outFile._text ); // 항목이 아닌 요소는 늘 쓰인다
                collectNamedEntries( span._node, assetID, outFile._text );
                continue;
            }
            PackReachEntry& entry = outFile._listEntry.emplace_back();
            entry._key            = Internal::makeTabKey( rule._kind, span._node.getAttributeText( rule._idAttribute.c_str() ) );
            collectReferences( element, assetID, entry._text );
            collectNamedEntries( span._node, assetID, entry._text );
            Internal::appendNamedKey( rule._kind, span._node.getAttributeText( Internal::kEntryBaseAttribute ), entry._text );
        }
    }

    // ------------------------------------------------------------------------------
    // PackReachResult
    // ------------------------------------------------------------------------------
    bool PackReachResult::collect( PackReachGraph& graph, const unordered_set<string>& setDeferredRoot, PackReachResult& outResult )
    {
        outResult._setIncluded.clear();
        outResult._setReferencedExcluded.clear();
        outResult._rootCount = 0;
        PackReachabilityInternal::ReachTraversal traversal( graph, setDeferredRoot, outResult._setIncluded, outResult._setReferencedExcluded );
        outResult._rootCount += traversal.addCodeRoots();
        outResult._rootCount += traversal.addAlwaysCookRoots();
        outResult._rootCount += traversal.addRequiredDataRoots();
        outResult._rootCount += traversal.addRedirectTableRoots();
        traversal.run();
        return traversal._bFailed == SW_FALSE;
    }

    bool PackReachResult::collectFromRoots( PackReachGraph& graph, const vector<string>& listRoot, PackReachResult& outResult )
    {
        outResult._setIncluded.clear();
        outResult._setReferencedExcluded.clear();
        outResult._rootCount = static_cast<uint32>( listRoot.size() );
        const unordered_set<string>              setNoDeferredRoot{};
        PackReachabilityInternal::ReachTraversal traversal( graph, setNoDeferredRoot, outResult._setIncluded, outResult._setReferencedExcluded );
        for ( const string& root : listRoot )
        {
            traversal.resolve( root, "PackSceneChunk", root, PackCatalogReach::Whole );
        }
        traversal.run();
        return traversal._bFailed == SW_FALSE;
    }

    void PackStagedSelection::build( const vector<PackDomainSources>& listSources, const PackReachResult& reach )
    {
        _mapSourceStem.clear();
        _mapShaderStem.clear();
        for ( const PackDomainSources& sources : listSources )
        {
            const string domain = FileUtil::normalizePath( sources._domain ).str();
            for ( const string& assetID : sources._listExistingID )
            {
                const bool bUsed     = reach._setIncluded.contains( assetID ) || reach._setReferencedExcluded.contains( assetID );
                bool&      bStemUsed = _mapSourceStem.try_emplace( PackReachabilityInternal::makeSourceStemKey( assetID ), false ).first->second;
                bStemUsed            = bStemUsed || bUsed;
                if ( StringUtil::endsWith( assetID, ".hlsl" ) == false )
                    continue; // 바이너리는 `.hlsl` 마다 나온다(`.hlsli` 는 포함 파일이다)
                const string_view fileName    = PackReachabilityInternal::getFileName( assetID );
                bool&             bShaderUsed = _mapShaderStem.try_emplace( PackReachabilityInternal::makeTabKey( domain, PackReachabilityInternal::getFirstStem( fileName ) ), false ).first->second;
                bShaderUsed                   = bShaderUsed || bUsed;
            }
        }
    }

    bool PackStagedSelection::isSelected( string_view domain, string_view relativePath ) const
    {
        const string assetID = FileUtil::normalizePath( FileUtil::joinPath( domain, relativePath ) ).str();
        if ( StringUtil::contains( assetID, "/shaders/bin/" ) )
        {
            const string      normalizedDomain = FileUtil::normalizePath( domain ).str();
            const string_view fileName         = PackReachabilityInternal::getFileName( assetID );
            // 가장 긴 줄기부터 - `post_bloom_ps` 는 `post` 가 아니라 `post_bloom` 의 것이다.
            for ( size_t underscore = fileName.rfind( '_' ); underscore != string_view::npos && underscore > 0; underscore = fileName.rfind( '_', underscore - 1 ) )
            {
                const auto itStem = _mapShaderStem.find( PackReachabilityInternal::makeTabKey( normalizedDomain, fileName.substr( 0, underscore ) ) );
                if ( itStem != _mapShaderStem.end() )
                    return itStem->second;
            }
            return true; // 스탬프 · 반사 목록 같은 폴더 공용 파일
        }
        const auto itSource = _mapSourceStem.find( PackReachabilityInternal::makeSourceStemKey( assetID ) );
        return itSource == _mapSourceStem.end() || itSource->second;
    }
} // namespace sw
