#include "pch.h"

#include "Engine/Asset/Cook/PackReachResult.h"

#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringUtil.h"

#include "Engine/Asset/Cook/AssetCompanion.h"
#include "Engine/Asset/Reference/AssetReferenceFields.h"
#include "Engine/Content/Pack/PackConfig.h"
#include "Engine/Serialization/XML/XMLDocument.h"

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
            /** @brief id 칸의 값을 가르는 글자입니다. */
            static constexpr string_view kIDSeparators = ",; \t\r\n";

            /** @brief 카탈로그 파일에 닿은 길입니다. */
            enum class CatalogReach : uint8
            {
                Whole, ///< 폴더 · 항상 담기 · `#` 없는 파일 참조 — 모든 항목이 쓰인다
                Named, ///< 필수 데이터 · `파일#id` — 이름이 불린 항목만 쓰인다
            };

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
                const string_view fileName = getFileName( assetID );
                return string( assetID.substr( 0, assetID.size() - fileName.size() ) ) + string( getFirstStem( fileName ) );
            }

            /** @brief 두 낱말의 열쇠(`<앞>\t<뒤>` — 셰이더 표의 도메인 · 줄기, 불린 항목의 이름 공간 · id)입니다. */
            static string makeTabKey( string_view domain, string_view stem ) { return string( domain ) + "\t" + string( stem ); }

            /** @brief 항목 하나입니다 — 글 안 자리 [_start, _end) 와 노드. */
            struct CatalogEntry
            {
                string  _id;
                size_t  _start{ 0 };
                size_t  _end{ 0 };
                XMLNode _node;
                uint8   _bVisited : 1;

                CatalogEntry()
                    : _id{}
                    , _start{ 0 }
                    , _end{ 0 }
                    , _node{}
                    , _bVisited{ SW_FALSE }
                {
                }
            };

            /** @brief 연 카탈로그 파일 하나입니다. 항목의 노드가 문서를 가리키므로 옮기지 않는다(`unique_ptr` 로 든다). */
            struct CatalogFile
            {
                string                  _assetID;
                const PackEntryCatalog* _pRule{ nullptr };
                string                  _text;
                XMLDocument             _document;
                vector<CatalogEntry>    _listEntry;
            };

            /** @brief 불린 항목 id 하나입니다. */
            struct NamedEntry
            {
                string _kind;
                string _id;
            };

            /** @brief 한 번의 훑기 상태입니다. 담은 집합과 읽을 차례(글 에셋)를 든다. */
            struct Walker
            {
                const AssetReferenceFields&     _params;
                const PackCookRules&            _rules;
                const PackReachFiles&           _files;
                const IPackReachSource&         _source;
                unordered_set<string>           _setUnfollowedField;
                unordered_set<string>&          _setIncluded;
                unordered_set<string>&          _setReferencedExcluded;
                vector<string>                  _listPending;      ///< 담았고 아직 훑지 않은 파일
                unordered_map<string, string>   _mapIDFieldKind;   ///< id 칸 이름 → 이름 공간
                unordered_set<string>           _setWholeCatalog;  ///< 모든 항목이 쓰이는 카탈로그 파일
                unordered_set<string>           _setNamedEntry;    ///< 불린 항목(`<이름 공간>\t<id>`)
                vector<NamedEntry>              _listPendingEntry; ///< 불렸고 아직 찾아보지 않은 항목
                vector<unique_ptr<CatalogFile>> _listCatalog;      ///< 연 카탈로그 파일
                uint8                           _bFailed : 1;

                Walker( const AssetReferenceFields& params, const PackCookRules& rules, const PackReachFiles& files, const IPackReachSource& source, unordered_set<string>& setIncluded,
                        unordered_set<string>& setReferencedExcluded )
                    : _params{ params }
                    , _rules{ rules }
                    , _files{ files }
                    , _source{ source }
                    , _setUnfollowedField{}
                    , _setIncluded{ setIncluded }
                    , _setReferencedExcluded{ setReferencedExcluded }
                    , _listPending{}
                    , _mapIDFieldKind{}
                    , _setWholeCatalog{}
                    , _setNamedEntry{}
                    , _listPendingEntry{}
                    , _listCatalog{}
                    , _bFailed{ SW_FALSE }
                {
                    for ( const string& field : rules._listUnfollowedField )
                    {
                        _setUnfollowedField.insert( field );
                    }
                    for ( const PackEntryIDField& idField : rules._listEntryIDField )
                    {
                        _mapIDFieldKind[idField._field] = idField._kind;
                    }
                }

                /** @brief @p assetID 가 항목 단위 카탈로그면 그 규칙, 아니면 nullptr 입니다. */
                const PackEntryCatalog* findCatalogRule( string_view assetID ) const
                {
                    for ( const PackEntryCatalog& rule : _rules._listEntryCatalog )
                    {
                        if ( StringUtil::endsWith( assetID, string( "/" ) + rule._path ) )
                            return &rule;
                    }
                    return nullptr;
                }

                /** @brief 항목 하나가 불렸다고 적습니다. 처음 불리면 찾아볼 차례에 넣는다. */
                void markNamed( string_view kind, string_view id )
                {
                    if ( id.empty() || id.find( '/' ) != string_view::npos )
                        return; // 경로는 참조 칸이 푼다
                    if ( _setNamedEntry.insert( makeTabKey( kind, id ) ).second )
                    {
                        NamedEntry& named = _listPendingEntry.emplace_back();
                        named._kind       = string( kind );
                        named._id         = string( id );
                    }
                }

                /** @brief 값 @p value 를 낱말로 갈라 @p kind 의 항목으로 부릅니다. */
                void markNamedTokens( string_view kind, string_view value )
                {
                    size_t cursor = 0;
                    while ( cursor < value.size() )
                    {
                        const size_t start = value.find_first_not_of( kIDSeparators, cursor );
                        if ( start == string_view::npos )
                            return;
                        const size_t end = MathUtil::min( value.find_first_of( kIDSeparators, start ), value.size() );
                        markNamed( kind, value.substr( start, end - start ) );
                        cursor = end;
                    }
                }

                /** @brief @p node 와 그 아래의 id 칸(속성 · 요소 글 · `<item>` 목록)이 부르는 항목을 적습니다. */
                void collectNamedEntries( XMLNode node )
                {
                    for ( XMLAttribute attribute = node.getFirstAttribute(); attribute.isValid(); attribute = attribute.getNext() )
                    {
                        const auto itKind = _mapIDFieldKind.find( string( attribute.getName() ) );
                        if ( itKind != _mapIDFieldKind.end() )
                            markNamedTokens( itKind->second, attribute.getValue() );
                    }
                    for ( XMLNode child = node.findChild(); child.isValid(); child = child.findNextSibling() )
                    {
                        const auto itKind = _mapIDFieldKind.find( string( child.getName() ) );
                        if ( itKind != _mapIDFieldKind.end() )
                        {
                            markNamedTokens( itKind->second, child.getText() );
                            for ( XMLNode item = child.findChild( kItemElement ); item.isValid(); item = item.findNextSibling( kItemElement ) )
                            {
                                markNamedTokens( itKind->second, item.getText() );
                            }
                        }
                        collectNamedEntries( child );
                    }
                }

                /** @brief 글 @p text 의 참조 칸을 따라갑니다. `파일#id` 는 그 항목만 부르고, `#` 없는 파일 참조는 파일 전부를 쓴다. */
                void followReferences( string_view text, const string& referrer )
                {
                    vector<AssetReference> listReference;
                    _params.extract( text, AssetReferenceFields::getSyntaxOf( referrer ), listReference );
                    for ( const AssetReference& reference : listReference )
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
                                markNamed( pRule->_kind, rest.substr( 0, MathUtil::min( rest.find_first_of( ",; \t\r\n\"'<" ), rest.size() ) ) );
                            }
                        }
                        resolve( reference._path, referrer, reference._field, bEntry ? CatalogReach::Named : CatalogReach::Whole );
                    }
                }

                /** @brief 항목 하나를 따라갑니다(한 번만). */
                void visitEntry( const CatalogFile& catalog, CatalogEntry& ioEntry )
                {
                    if ( ioEntry._bVisited )
                        return;
                    ioEntry._bVisited = SW_TRUE;
                    followReferences( string_view( catalog._text ).substr( ioEntry._start, ioEntry._end - ioEntry._start ), catalog._assetID );
                    collectNamedEntries( ioEntry._node );
                    markNamed( catalog._pRule->_kind, ioEntry._node.getAttributeText( kEntryBaseAttribute ) );
                }

                /** @brief 카탈로그 파일을 열어 항목으로 나눕니다. 항목 밖의 글은 늘 따라가고, 이미 불린 항목 · 통째로 쓰는 파일의 항목을 따라간다. */
                void openCatalog( const PackEntryCatalog& rule, const string& assetID, string text )
                {
                    unique_ptr<CatalogFile> pCatalog = make_unique<CatalogFile>();
                    pCatalog->_assetID               = assetID;
                    pCatalog->_pRule                 = &rule;
                    pCatalog->_text                  = std::move( text );
                    CatalogFile& catalog             = *pCatalog;
                    if ( catalog._document.parse( catalog._text, assetID ) == false )
                    {
                        SW_LOG_ERROR_ID( "PackReachability.CatalogInvalid", "Pack cook: entry catalog '%#' is not valid XML - %#", assetID, catalog._document.getLastError() );
                        _bFailed = SW_TRUE;
                        return;
                    }
                    const string_view allText = catalog._text;
                    const XMLNode     root    = catalog._document.getRoot();
                    size_t            head    = allText.size(); // 첫 자식 앞(루트 태그)까지가 항목 밖의 글이다
                    struct Span
                    {
                        XMLNode _node;
                        size_t  _start;
                    };
                    vector<Span> listSpan;
                    for ( XMLNode child = root.findChild(); child.isValid(); child = child.findNextSibling() )
                    {
                        const optional<size_t> offset = child.getSourceOffset();
                        if ( offset.has_value() == false || string_view( child.getName() ).empty() )
                            continue; // 글 노드
                        // 위치는 요소 이름을 가리킨다 — 태그의 `<` 부터 잘라야 뽑기가 태그로 읽는다.
                        const size_t start = allText.rfind( '<', *offset );
                        listSpan.push_back( Span{ child, start == string_view::npos ? 0 : start } );
                    }
                    if ( listSpan.empty() == false )
                        head = listSpan.front()._start;
                    followReferences( allText.substr( 0, head ), assetID );
                    for ( size_t index = 0; index < listSpan.size(); ++index )
                    {
                        const size_t end = index + 1 < listSpan.size() ? listSpan[index + 1]._start : allText.size();
                        if ( StringUtil::equals( listSpan[index]._node.getName(), string_view( rule._entryElement ) ) == false )
                        {
                            followReferences( allText.substr( listSpan[index]._start, end - listSpan[index]._start ), assetID ); // 항목이 아닌 요소는 늘 쓰인다
                            collectNamedEntries( listSpan[index]._node );
                            continue;
                        }
                        CatalogEntry& entry = catalog._listEntry.emplace_back();
                        entry._id           = string( listSpan[index]._node.getAttributeText( rule._idAttribute.c_str() ) );
                        entry._start        = listSpan[index]._start;
                        entry._end          = end;
                        entry._node         = listSpan[index]._node;
                    }
                    const bool bWhole = _setWholeCatalog.contains( assetID );
                    for ( CatalogEntry& entry : catalog._listEntry )
                    {
                        if ( bWhole || _setNamedEntry.contains( makeTabKey( rule._kind, entry._id ) ) )
                            visitEntry( catalog, entry );
                    }
                    _listCatalog.push_back( std::move( pCatalog ) );
                }

                /** @brief 불린 항목을 연 카탈로그에서 찾아 따라갑니다. 아직 열지 않은 파일은 열 때 `_setNamedEntry` 로 본다. 없는 id 는 담을 것이 없다. */
                void visitNamedEntry( const NamedEntry& named )
                {
                    for ( const unique_ptr<CatalogFile>& pCatalog : _listCatalog )
                    {
                        if ( pCatalog->_pRule->_kind != named._kind )
                            continue;
                        for ( CatalogEntry& entry : pCatalog->_listEntry )
                        {
                            if ( entry._id == named._id )
                                visitEntry( *pCatalog, entry );
                        }
                    }
                }

                /** @brief 통째로 쓰게 된 카탈로그를 표시합니다. 이미 열었으면 모든 항목을 따라간다. */
                void markWholeCatalog( const string& assetID )
                {
                    if ( _setWholeCatalog.insert( assetID ).second == false )
                        return;
                    for ( const unique_ptr<CatalogFile>& pCatalog : _listCatalog )
                    {
                        if ( pCatalog->_assetID != assetID )
                            continue;
                        for ( CatalogEntry& entry : pCatalog->_listEntry )
                        {
                            visitEntry( *pCatalog, entry );
                        }
                    }
                }

                /** @brief 담을 수 있는 파일 하나를 담습니다. 처음 담으면 훑을 차례에 넣는다. */
                void include( const string& assetID, CatalogReach reach )
                {
                    if ( reach == CatalogReach::Whole && findCatalogRule( assetID ) != nullptr )
                        markWholeCatalog( assetID );
                    if ( _setIncluded.insert( assetID ).second )
                        _listPending.push_back( assetID );
                }

                /** @brief @p folder 아래(`folder/…`)의 파일을 @p setFile 에서 찾아 @p outListFile 에 더합니다. */
                static void collectUnderFolder( const unordered_set<string>& setFile, const string& folder, vector<string>* pOutListFile, bool& outbAny )
                {
                    const string prefix = folder + "/";
                    for ( const string& assetID : setFile )
                    {
                        if ( StringUtil::startsWith( assetID, prefix ) == false )
                            continue;
                        outbAny = true;
                        if ( pOutListFile == nullptr )
                            return;
                        pOutListFile->push_back( assetID );
                    }
                }

                /**
                 * @brief 참조 하나(@p path — 정규화)를 풉니다. 담을 파일이면 담고, 폴더면 그 아래 전부를 담고, 제외 규칙이 뺀 원본이면 넘어간다. 아무것도 아니면 오류다.
                 * @param referrer 가리킨 쪽(오류 문구용 — 파일 id 와 칸)
                 * @param reach 이 참조가 카탈로그 파일에 닿으면 그 파일을 어떻게 쓰는가
                 */
                void resolve( const string& path, string_view referrer, string_view field, CatalogReach reach )
                {
                    if ( _files._setPackable.contains( path ) )
                    {
                        include( path, reach );
                        return;
                    }
                    // 제외 규칙이 뺀 원본(셰이더 원본 — 백엔드 쿠킹본이 스테이징에 있다, 머티리얼 그래프 원본 — 생성 머티리얼이 담긴다)은 빠진 것이 아니다.
                    if ( _files._setExisting.contains( path ) )
                    {
                        _setReferencedExcluded.insert( path );
                        return;
                    }
                    vector<string> listUnder;
                    bool           bPackableUnder = false;
                    collectUnderFolder( _files._setPackable, path, &listUnder, bPackableUnder );
                    if ( bPackableUnder )
                    {
                        // 폴더를 가리키는 참조는 런타임이 그 폴더를 훑는다는 뜻이다(외형 데이터 폴더 · 시나리오 사본 원본) — 그 아래 전부가 쓰인다.
                        for ( const string& assetID : listUnder )
                        {
                            include( assetID, reach );
                        }
                        return;
                    }
                    bool bExistingUnder = false;
                    collectUnderFolder( _files._setExisting, path, nullptr, bExistingUnder );
                    if ( bExistingUnder )
                        return;
                    SW_LOG_ERROR_ID( "PackReachability.MissingReference", "Pack cook: %# (%#) references '%#', which is not in any cooked domain - fix the reference or add the asset (nothing is dropped silently)",
                                     referrer, field, path );
                    _bFailed = true;
                }

                /** @brief @p assetID 가 이 구성에서 쓰는 항상 담기 묶음 하나에 맞으면 true 입니다. */
                bool matchesAlwaysCook( const string& assetID ) const
                {
                    for ( const PackAlwaysCookRule& rule : _rules._listAlwaysCook )
                    {
                        if ( rule._bAutomationOnly && _files._automation == PackAutomationContent::Excluded )
                            continue; // 자동화 없는 배포 - 시나리오는 읽히지 않는다
                        const bool bMatches = std::any_of( rule._listPattern.begin(), rule._listPattern.end(), [&assetID]( const string& pattern )
                        { return StringUtil::matchesPathPattern( pattern, assetID ); } );
                        if ( bMatches )
                            return true;
                    }
                    return false;
                }

                /** @brief 항상 담기 묶음에 맞는 파일을 뿌리로 담습니다(제외 규칙이 뺀 원본이면 그 쿠킹본만 담도록 적는다). 뿌리 수를 돌려준다. */
                uint32 addAlwaysCookRoots()
                {
                    uint32 rootCount = 0;
                    for ( const string& assetID : _files._setExisting )
                    {
                        if ( matchesAlwaysCook( assetID ) == false )
                            continue;
                        ++rootCount;
                        if ( _files._setPackable.contains( assetID ) )
                            include( assetID, CatalogReach::Whole );
                        else
                            _setReferencedExcluded.insert( assetID );
                    }
                    return rootCount;
                }

                /** @brief 코드가 상수로 읽는 에셋을 뿌리로 담습니다. 뿌리 수를 돌려준다. */
                uint32 addCodeRoots()
                {
                    for ( const string& root : _files._listCodeRoot )
                    {
                        resolve( FileUtil::normalizePath( root ).str(), "SW_ASSET_COOK_ROOT", root, CatalogReach::Whole );
                    }
                    return static_cast<uint32>( _files._listCodeRoot.size() );
                }

                /** @brief 굽는 게임마다 게임 설정의 `<RequiredData kind pack>` 이 가리키는 자리를 뿌리로 담습니다. 뿌리 수를 돌려준다. */
                uint32 addRequiredDataRoots()
                {
                    uint32 rootCount = 0;
                    for ( const string& gameDomain : _files._listGameDomain )
                    {
                        const string settingsID = FileUtil::normalizePath( gameDomain + "/" + _rules._gameSettingsPath ).str();
                        if ( _files._setPackable.contains( settingsID ) == false )
                        {
                            SW_LOG_ERROR_ID( "PackReachability.GameSettingsMissing", "Pack cook: game pack %# has no game settings '%#' - its start map and required data cannot be found", gameDomain, settingsID );
                            _bFailed = true;
                            continue;
                        }
                        include( settingsID, CatalogReach::Whole );
                        string text;
                        if ( _source.readText( settingsID, text ) == false )
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
                    for ( const PackRequiredDataPath& entry : _rules._listRequiredData )
                    {
                        if ( StringUtil::equals( entry._kind, kind ) == false )
                            continue;
                        const string path = FileUtil::normalizePath( FileUtil::trimTrailingSlashes( FileUtil::joinPath( pack, entry._path ) ) ).str();
                        resolve( path, settingsID, kRequiredDataElement, CatalogReach::Named ); // 데이터를 읽는다는 뜻일 뿐이다 - 어느 항목을 쓰는지는 id 칸이 말한다
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
                    vector<string> listCompanion;
                    AssetCompanionRegistry::appendCompanionPaths( assetID, listCompanion );
                    for ( const string& companion : listCompanion )
                    {
                        const string normalized = FileUtil::normalizePath( companion ).str();
                        if ( _files._setPackable.contains( normalized ) )
                            include( normalized, CatalogReach::Whole ); // 곁 파일은 선택 파일이다 — 없으면 넘어간다
                    }
                    if ( _params.isTextAsset( assetID ) == false )
                        return;
                    string text;
                    if ( _source.readText( assetID, text ) == false )
                    {
                        SW_LOG_ERROR_ID( "PackReachability.TextUnreadable", "Pack cook: '%#' could not be read to follow its references", assetID );
                        _bFailed = true;
                        return;
                    }
                    if ( const PackEntryCatalog* pRule = findCatalogRule( assetID ); pRule != nullptr )
                    {
                        openCatalog( *pRule, assetID, std::move( text ) );
                        return;
                    }
                    followReferences( text, assetID );
                    if ( _mapIDFieldKind.empty() || AssetReferenceFields::getSyntaxOf( assetID ) != AssetReferenceSyntax::XML )
                        return;
                    XMLDocument document;
                    // 깨진 글은 런타임도 읽지 못한다 - 경로 칸은 위에서 이미 따라갔고, 이 글이 부르는 항목은 없다.
                    if ( document.parse( text, assetID ) )
                        collectNamedEntries( document.getRoot() );
                }

                void run()
                {
                    while ( _listPending.empty() == false || _listPendingEntry.empty() == false )
                    {
                        if ( _listPending.empty() == false )
                        {
                            const string assetID = std::move( _listPending.back() );
                            _listPending.pop_back();
                            visit( assetID );
                            continue;
                        }
                        const NamedEntry named = std::move( _listPendingEntry.back() );
                        _listPendingEntry.pop_back();
                        visitNamedEntry( named );
                    }
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool PackReachResult::collect( const AssetReferenceFields& params, const PackCookRules& rules, const PackReachFiles& files, const IPackReachSource& source, PackReachResult& outResult )
    {
        outResult._setIncluded.clear();
        outResult._setReferencedExcluded.clear();
        outResult._rootCount = 0;
        PackReachabilityInternal::Walker walker( params, rules, files, source, outResult._setIncluded, outResult._setReferencedExcluded );
        outResult._rootCount += walker.addCodeRoots();
        outResult._rootCount += walker.addAlwaysCookRoots();
        outResult._rootCount += walker.addRequiredDataRoots();
        walker.run();
        return walker._bFailed == false;
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
