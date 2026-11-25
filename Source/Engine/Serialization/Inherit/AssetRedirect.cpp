#include "pch.h"

#include "Engine/Serialization/Inherit/AssetRedirect.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/Subsystem.h"
#include "Core/Subsystem/SubsystemSlot.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct AssetRedirectInternal
        {
            static constexpr const utf8* kRedirectElement = "Redirect";
            /** @brief 폴더 접두 원소입니다 — 팩 · 폴더를 통째로 옮긴 한 줄(언리얼 `[CoreRedirects]` PackageRedirects 의 접두 꼴). */
            static constexpr const utf8* kPrefixRedirectElement = "PrefixRedirect";
            /** @brief 정체성 대체 원소입니다(GUID → GUID). */
            static constexpr const utf8* kIdentityRedirectElement = "IdentityRedirect";

            /** @brief 폴더 접두 줄 하나입니다 — `_from` 폴더 아래 id 를 `_to` 폴더 아래로. */
            struct PrefixRow
            {
                string _from;
                string _to;
            };

            /** @brief 프로세스의 옛 경로 표입니다(`AssetRedirectTable` 이 든다). 사슬은 끝으로 접었다. */
            struct State
            {
                mutex                         _mutex{};
                unordered_map<string, string> _mapOldToNew{};
                vector<PrefixRow>             _listPrefix{}; ///< 정확한 줄에 없을 때만 본다
            };

            /** @brief id 글이 맞춤 꼴(`AssetPaths::normalizeAssetID` 그대로)인지 봅니다. 아니면 오류를 남기고 false 입니다 — 표기가 갈리면 같은 에셋이 두 이름이 된다. */
            static bool isCanonical( string_view id, string_view sourceName, const utf8* pField )
            {
                if ( id.empty() == false && AssetPaths::normalizeAssetID( id ) == id )
                    return true;
                SW_LOG_ERROR_ID( "AssetRedirect.NotCanonicalId", "Asset redirect table '%#': %# '%#' is not a canonical resource id (lowercase, '/', no '..', not a disk path)", sourceName, pField,
                                 id );
                return false;
            }

            /** @brief 표 글 하나 이상을 읽은 것입니다. */
            struct Rows
            {
                unordered_map<string, string> _mapOldToNew; ///< 옛 경로 → 경로(정확한 줄)
                vector<PrefixRow>             _listPrefix;
                vector<AssetIdentityRedirect> _listIdentity;

                /** @brief 정체성 대체 줄 하나를 더합니다. 틀리면 오류를 남기고 false 입니다. */
                [[nodiscard]] bool parseIdentityRow( const XMLNode& node, string_view sourceName )
                {
                    const optional<AssetGUID> from = AssetGUID::tryParse( node.getAttributeText( "from" ) );
                    const optional<AssetGUID> to   = AssetGUID::tryParse( node.getAttributeText( "to" ) );
                    const bool                bBad = from.has_value() == false || to.has_value() == false || *from == *to;
                    const bool                bTwice =
                        bBad == false && VectorUtil::containsIf( _listIdentity, [&from]( const AssetIdentityRedirect& row )
                    { return row._from == *from; } );
                    if ( bBad || bTwice )
                    {
                        SW_LOG_ERROR_ID( "AssetRedirect.IdentityRowInvalid", "Asset redirect table '%#': <%# from='%#' to='%#'> needs two different GUIDs and one row per 'from'", sourceName, kIdentityRedirectElement,
                                         node.getAttributeText( "from" ), node.getAttributeText( "to" ) );
                        return false;
                    }
                    _listIdentity.push_back( AssetIdentityRedirect{ *from, *to } );
                    return true;
                }

                /** @brief 표 글 하나를 더합니다. 틀리면 오류를 남기고 false 입니다. */
                [[nodiscard]] bool parse( string_view text, string_view sourceName )
                {
                    XMLDocument doc;
                    if ( doc.parse( text, sourceName ) == false )
                    {
                        SW_LOG_ERROR_ID( "AssetRedirect.NotXML", "Asset redirect table '%#' could not be parsed: %#", sourceName, doc.getLastError() );
                        return false;
                    }
                    const XMLNode root = doc.getRoot( AssetRedirect::kRootElement );
                    if ( root.isValid() == false )
                    {
                        SW_LOG_ERROR_ID( "AssetRedirect.WrongRoot", "Asset redirect table '%#' has no <%#> root", sourceName, AssetRedirect::kRootElement );
                        return false;
                    }
                    for ( XMLNode node = root.findChild(); node.isValid(); node = node.findNextSibling() )
                    {
                        if ( StringUtil::isNullOrEmpty( node.getName() ) )
                            continue;
                        const string_view name = node.getName();
                        if ( name == kIdentityRedirectElement )
                        {
                            if ( parseIdentityRow( node, sourceName ) == false )
                                return false;
                            continue;
                        }
                        const bool bPrefix = name == kPrefixRedirectElement;
                        if ( bPrefix == false && name != kRedirectElement )
                        {
                            SW_LOG_ERROR_ID( "AssetRedirect.UnknownElement", "Asset redirect table '%#': <%#> is not <%#>, <%#> or <%#>", sourceName, node.getName(), kRedirectElement,
                                             kPrefixRedirectElement, kIdentityRedirectElement );
                            return false;
                        }
                        const string_view from = node.getAttributeText( "from" );
                        const string_view to   = node.getAttributeText( "to" );
                        if ( isCanonical( from, sourceName, "from" ) == false || isCanonical( to, sourceName, "to" ) == false )
                            return false;
                        if ( from == to )
                        {
                            SW_LOG_ERROR_ID( "AssetRedirect.SelfRedirect", "Asset redirect table '%#': '%#' is redirected to itself", sourceName, from );
                            return false;
                        }
                        const bool bTwice = bPrefix ? VectorUtil::containsIf( _listPrefix, [from]( const PrefixRow& row )
                        { return row._from == from; } )
                                                    : _mapOldToNew.find( from ) != _mapOldToNew.end();
                        if ( bTwice )
                        {
                            SW_LOG_ERROR_ID( "AssetRedirect.DuplicateFrom", "Asset redirect table '%#': '%#' is redirected twice", sourceName, from );
                            return false;
                        }
                        if ( bPrefix )
                            _listPrefix.push_back( PrefixRow{ string{ from }, string{ to } } );
                        else
                            _mapOldToNew.emplace( string{ from }, string{ to } );
                    }
                    return true;
                }
            };

            /** @brief 사슬(A→B, B→C)을 끝(A→C)으로 접습니다. 순환이면 오류를 남기고 false 입니다. */
            static bool collapseChains( unordered_map<string, string>& inoutMap )
            {
                unordered_map<string, string> mapFinal;
                for ( const auto& [from, firstTo] : inoutMap )
                {
                    string target = firstTo;
                    size_t steps  = 0;
                    for ( auto it = inoutMap.find( target ); it != inoutMap.end(); it = inoutMap.find( target ) )
                    {
                        target = it->second;
                        if ( ++steps > inoutMap.size() )
                        {
                            SW_LOG_ERROR_ID( "AssetRedirect.Cycle", "Asset redirects loop through '%#' - a redirect must end at an asset", from );
                            return false;
                        }
                    }
                    mapFinal.emplace( from, std::move( target ) );
                }
                inoutMap = std::move( mapFinal );
                return true;
            }
        };
    } // namespace

    /**
     * @class AssetRedirectTable
     * @brief 지금 읽은 옛 경로 표입니다(엔진 서비스 서브시스템 — `ResourceRemapTable` 과 같은 자리). `AssetRedirect` 의 정적 창구가 이것을 쓴다.
     */
    class AssetRedirectTable final : public Subsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "AssetRedirectTable";

        AssetRedirectTable()
            : _state{}
        {
        }

        SubsystemInitResult initializeSubsystem() override { return SubsystemInitResult::Succeeded; }
        void                shutdownSubsystem() override {}
        const utf8*         getSubsystemName() const override { return kSubsystemName; }

        /** @brief 든 표입니다. */
        AssetRedirectInternal::State& getState() { return _state; }

    private:
        AssetRedirectInternal::State _state;

        SW_SUBSYSTEM_SLOT( AssetRedirectTable, Required );
    };
} // namespace sw

namespace sw
{
    expected<vector<AssetIdentityRedirect>> AssetRedirect::parseIdentityRows( string_view text, string_view sourceName )
    {
        AssetRedirectInternal::Rows rows;
        if ( rows.parse( text, sourceName ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the asset redirect table is malformed", sourceName } );
        return std::move( rows._listIdentity );
    }

    bool AssetRedirect::loadFromXMLTexts( const vector<string>& listText, const vector<string>& listSourceName )
    {
        SW_ASSERT( listText.size() == listSourceName.size() );
        AssetRedirectInternal::Rows rows;
        for ( size_t index = 0; index < listText.size(); ++index )
        {
            if ( rows.parse( listText[index], listSourceName[index] ) == false )
                return false;
        }
        if ( AssetRedirectInternal::collapseChains( rows._mapOldToNew ) == false )
            return false;
        AssetRedirectInternal::State& state = engine::getSubsystem<AssetRedirectTable>().getState();
        ScopedLock<mutex>             lock{ state._mutex };
        state._mapOldToNew = std::move( rows._mapOldToNew );
        state._listPrefix  = std::move( rows._listPrefix );
        return true;
    }

    string AssetRedirect::makeTableID( string_view assetID )
    {
        const size_t firstSlash = assetID.find( '/' );
        string_view  domain     = assetID.substr( 0, firstSlash );
        if ( firstSlash != string_view::npos && ( domain == "game" || domain == "dlc" ) )
            domain = assetID.substr( 0, assetID.find( '/', firstSlash + 1 ) ); // 팩 도메인은 두 마디다(`game/<팩>`) — 검색 순서의 "game" 이 활성 팩 뿌리다
        return FileUtil::joinPath( domain, kFileName );
    }

    bool AssetRedirect::isTable( string_view assetID )
    {
        return FileUtil::getFileNamePart( assetID ) == kFileName;
    }

    bool AssetRedirect::loadFromSearchDomains()
    {
        // 검색 순서의 도메인마다 뿌리의 표 하나 — "game" 은 활성 팩 도메인이다(다른 팩의 표는 그 팩이 활성일 때만 뜻이 있다).
        vector<string>    listText;
        vector<string>    listSourceName;
        const GameConfig* pGameConfig = engine::findSubsystem<GameConfig>();
        for ( const string& token : engine::getSubsystem<AssetPaths>().getSearchPriority() )
        {
            const string domain = token == "game" ? ( pGameConfig != nullptr && pGameConfig->_packRoot.isSet() ? string{ pGameConfig->_packRoot.get() } : string{} ) : token;
            if ( domain.empty() )
                continue;
            const AssetID tableID{ FileUtil::joinPath( domain, kFileName ) };
            if ( AssetIO::hasAsset( tableID ) == false )
                continue; // 그 도메인에는 줄이 없다
            string text;
            if ( AssetIO::readTextAsset( tableID, text ) == false )
            {
                SW_LOG_ERROR_ID( "AssetRedirect.NotRead", "Asset redirect table '%#' exists but could not be read", tableID.view() );
                return false;
            }
            listText.push_back( std::move( text ) );
            listSourceName.push_back( tableID.str() );
        }
        return loadFromXMLTexts( listText, listSourceName );
    }

    void AssetRedirect::clear()
    {
        AssetRedirectInternal::State& state = engine::getSubsystem<AssetRedirectTable>().getState();
        ScopedLock<mutex>             lock{ state._mutex };
        state._mapOldToNew.clear();
        state._listPrefix.clear();
    }

    expected<AssetReferenceTarget> AssetRedirect::readSavedReference( string_view text )
    {
        AssetRedirectTable* const pTable = engine::findSubsystem<AssetRedirectTable>();
        if ( pTable == nullptr || AssetRegistry::isReferenceText( text ) )
            return AssetRegistry::readReferenceText( text );
        string current{ text };
        {
            AssetRedirectInternal::State& state = pTable->getState();
            ScopedLock<mutex>             lock{ state._mutex };
            // 정확한 줄이 없으면 폴더 접두 줄 — 바뀐 경로가 정확한 줄의 옛 경로면 한 번 더 푼다(팩 안에서 옮긴 뒤 팩을 옮긴 사슬).
            if ( state._mapOldToNew.find( current ) == state._mapOldToNew.end() )
            {
                for ( const AssetRedirectInternal::PrefixRow& row : state._listPrefix )
                {
                    if ( text.size() <= row._from.size() || text[row._from.size()] != '/' || text.substr( 0, row._from.size() ) != string_view{ row._from } )
                        continue;
                    current = row._to;
                    current.append( text.substr( row._from.size() ) );
                    break;
                }
            }
            const auto it = state._mapOldToNew.find( current );
            if ( it != state._mapOldToNew.end() )
                current = it->second;
        }
        return AssetRegistry::readReferenceText( current );
    }
} // namespace sw

namespace sw
{
    // 옛 경로 표 — 팩 설정(`PackEngineSettingsSubsystem`)이 채우고 저장된 기록을 읽는 쪽(`readSavedReference`)이 묻는다.
    SW_ENGINE_SUBSYSTEM_WITH_SLOT( AssetRedirectTable, HostOnly, Process, Shared, (), SW_ENGINE_DEFAULT_FACTORY( AssetRedirectTable ) );
} // namespace sw
