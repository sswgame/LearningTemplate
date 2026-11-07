#include "pch.h"

#include "Engine/Asset/Cook/PackReachability.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Log/Logger.h"

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

            /** @brief 한 번의 훑기 상태입니다. 담은 집합과 읽을 차례(글 에셋)를 든다. */
            struct Walker
            {
                const AssetReferenceFields& _fields;
                const PackCookRules&        _rules;
                const PackReachFiles&       _files;
                const IPackReachSource&     _source;
                unordered_set<string>       _setUnfollowedField;
                unordered_set<string>&      _setIncluded;
                vector<string>              _listPending; ///< 담았고 아직 훑지 않은 파일
                uint8                       _bFailed : 1;

                Walker( const AssetReferenceFields& fields, const PackCookRules& rules, const PackReachFiles& files, const IPackReachSource& source, unordered_set<string>& setIncluded )
                    : _fields{ fields }
                    , _rules{ rules }
                    , _files{ files }
                    , _source{ source }
                    , _setUnfollowedField{}
                    , _setIncluded{ setIncluded }
                    , _listPending{}
                    , _bFailed{ false }
                {
                    for ( const string& field : rules._listUnfollowedField )
                    {
                        _setUnfollowedField.insert( field );
                    }
                }

                /** @brief 담을 수 있는 파일 하나를 담습니다. 처음 담으면 훑을 차례에 넣는다. */
                void include( const string& assetID )
                {
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
                 */
                void resolve( const string& path, string_view referrer, string_view field )
                {
                    if ( _files._setPackable.contains( path ) )
                    {
                        include( path );
                        return;
                    }
                    // 제외 규칙이 뺀 원본(셰이더 원본 — 백엔드 쿠킹본이 스테이징에 있다, 머티리얼 그래프 원본 — 생성 머티리얼이 담긴다)은 빠진 것이 아니다.
                    if ( _files._setExisting.contains( path ) )
                        return;
                    vector<string> listUnder;
                    bool           bPackableUnder = false;
                    collectUnderFolder( _files._setPackable, path, &listUnder, bPackableUnder );
                    if ( bPackableUnder )
                    {
                        // 폴더를 가리키는 참조는 런타임이 그 폴더를 훑는다는 뜻이다(외형 데이터 폴더 · 시나리오 사본 원본) — 그 아래 전부가 쓰인다.
                        for ( const string& assetID : listUnder )
                        {
                            include( assetID );
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

                /** @brief 항상 담기 묶음에 맞는 파일을 뿌리로 담습니다. 뿌리 수를 돌려준다. */
                uint32 addAlwaysCookRoots()
                {
                    uint32 rootCount = 0;
                    for ( const string& assetID : _files._setPackable )
                    {
                        for ( const PackAlwaysCookRule& rule : _rules._listAlwaysCook )
                        {
                            const bool bMatches = std::any_of( rule._listPattern.begin(), rule._listPattern.end(), [&assetID]( const string& pattern )
                            { return StringUtil::matchesPathPattern( pattern, assetID ); } );
                            if ( bMatches == false )
                                continue;
                            include( assetID );
                            ++rootCount;
                            break;
                        }
                    }
                    return rootCount;
                }

                /** @brief 굽는 게임마다 게임 설정의 `<RequiredData kind pack>` 이 가리키는 자리를 뿌리로 담습니다. 뿌리 수를 돌려준다. */
                uint32 addRequiredDataRoots()
                {
                    uint32 rootCount = 0;
                    for ( const string& gameDomain : _files._listGameDomain )
                    {
                        const string settingsID = FileUtil::normalizePath( gameDomain + "/" + _rules._gameSettingsPath );
                        if ( _files._setPackable.contains( settingsID ) == false )
                        {
                            SW_LOG_ERROR_ID( "PackReachability.GameSettingsMissing", "Pack cook: game pack %# has no game settings '%#' - its start map and required data cannot be found", gameDomain, settingsID );
                            _bFailed = true;
                            continue;
                        }
                        include( settingsID );
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
                        const string path = FileUtil::normalizePath( FileUtil::trimTrailingSlashes( FileUtil::joinPath( pack, entry._path ) ) );
                        resolve( path, settingsID, kRequiredDataElement );
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
                        const string normalized = FileUtil::normalizePath( companion );
                        if ( _files._setPackable.contains( normalized ) )
                            include( normalized ); // 곁 파일은 선택 파일이다 — 없으면 넘어간다
                    }
                    if ( _fields.isTextAsset( assetID ) == false )
                        return;
                    string text;
                    if ( _source.readText( assetID, text ) == false )
                    {
                        SW_LOG_ERROR_ID( "PackReachability.TextUnreadable", "Pack cook: '%#' could not be read to follow its references", assetID );
                        _bFailed = true;
                        return;
                    }
                    vector<AssetReference> listReference;
                    _fields.extract( text, AssetReferenceFields::syntaxOf( assetID ), listReference );
                    for ( const AssetReference& reference : listReference )
                    {
                        if ( _setUnfollowedField.contains( reference._field ) || _fields.isChecked( reference._path ) == false )
                            continue;
                        resolve( reference._path, assetID, reference._field );
                    }
                }

                void run()
                {
                    while ( _listPending.empty() == false )
                    {
                        const string assetID = std::move( _listPending.back() );
                        _listPending.pop_back();
                        visit( assetID );
                    }
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    bool PackReachability::collect( const AssetReferenceFields& fields, const PackCookRules& rules, const PackReachFiles& files, const IPackReachSource& source, PackReachResult& outResult )
    {
        outResult._setIncluded.clear();
        outResult._rootCount = 0;
        PackReachabilityInternal::Walker walker( fields, rules, files, source, outResult._setIncluded );
        outResult._rootCount += walker.addAlwaysCookRoots();
        outResult._rootCount += walker.addRequiredDataRoots();
        walker.run();
        return walker._bFailed == false;
    }
} // namespace sw
