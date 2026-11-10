#include "pch.h"

#include "Engine/Serialization/Inherit/AssetRedirect.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/unordered_map.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringUtil.h"
#include "Core/Subsystem/ISubsystem.h"
#include "Core/Subsystem/ServiceSlot.h"

#include "Engine/Config/GameConfig.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    namespace
    {
        struct AssetRedirectInternal
        {
            static constexpr const utf8* kRedirectElement = "Redirect";

            /** @brief 표의 한 줄입니다(글 순서를 지키려고 맵이 아니라 목록에 둔다). */
            struct Entry
            {
                string _from;
                string _to;
            };

            /** @brief 표 전체입니다(`AssetRedirectTable` 이 든다). 옛 id → 끝 id(사슬을 접은 것). */
            struct State
            {
                mutex                         _mutex{};
                unordered_map<string, string> _mapOldToNew{};
                atomic<uint32>                _entryCount{ 0 }; ///< 표가 비었는지 잠금 없이 묻는다 — 모든 리소스 읽기가 지나는 자리다
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

            /** @brief 표 글 하나를 @p inoutMap 에 더합니다. 틀리면 오류를 남기고 false 입니다. */
            static bool parseInto( string_view text, string_view sourceName, unordered_map<string, string>& inoutMap )
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
                    if ( string_view( node.getName() ) != kRedirectElement )
                    {
                        SW_LOG_ERROR_ID( "AssetRedirect.UnknownElement", "Asset redirect table '%#': <%#> is not <%#>", sourceName, node.getName(), kRedirectElement );
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
                    if ( inoutMap.emplace( string{ from }, string{ to } ).second == false )
                    {
                        SW_LOG_ERROR_ID( "AssetRedirect.DuplicateFrom", "Asset redirect table '%#': '%#' is redirected twice", sourceName, from );
                        return false;
                    }
                }
                return true;
            }

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
     * @brief 지금 읽은 리다이렉트 표입니다(엔진 서비스 서브시스템 — `ResourceRemapTable` 과 같은 자리). `AssetRedirect` 의 정적 창구가 이것을 쓴다.
     */
    class AssetRedirectTable final : public ISubsystem
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
    };

    SW_SERVICE_SLOT( AssetRedirectTable, Required );
} // namespace sw

namespace sw
{
    bool AssetRedirect::loadFromXMLTexts( const vector<string>& listText, const vector<string>& listSourceName )
    {
        SW_ASSERT( listText.size() == listSourceName.size() );
        unordered_map<string, string> mapOldToNew;
        for ( size_t index = 0; index < listText.size(); ++index )
        {
            if ( AssetRedirectInternal::parseInto( listText[index], listSourceName[index], mapOldToNew ) == false )
                return false;
        }
        if ( AssetRedirectInternal::collapseChains( mapOldToNew ) == false )
            return false;
        AssetRedirectInternal::State& state = engine::getSubsystem<AssetRedirectTable>().getState();
        ScopedLock<mutex>             lock{ state._mutex };
        state._mapOldToNew = std::move( mapOldToNew );
        state._entryCount.store( static_cast<uint32>( state._mapOldToNew.size() ) );
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

    expected<string> AssetRedirect::addToXMLText( string_view tableText, string_view sourceName, const vector<string>& listFrom, const vector<string>& listTo )
    {
        using Internal = AssetRedirectInternal;
        SW_ASSERT( listFrom.size() == listTo.size() );
        // 틀린 표에 줄을 더하지 않는다 — 기동이 멈출 표를 키우지 않게. 이유는 parseInto 가 로그에 남겼다.
        unordered_map<string, string> mapCheck;
        if ( Internal::parseInto( tableText, sourceName, mapCheck ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the asset redirect table is malformed", sourceName } );
        XMLDocument document;
        if ( document.parse( tableText, sourceName ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the asset redirect table is malformed", sourceName } );
        vector<Internal::Entry> listEntry;
        for ( XMLNode node = document.getRoot( kRootElement ).findChild(); node.isValid(); node = node.findNextSibling() )
        {
            if ( StringUtil::isNullOrEmpty( node.getName() ) == false )
                listEntry.push_back( Internal::Entry{ string{ node.getAttributeText( "from" ) }, string{ node.getAttributeText( "to" ) } } );
        }
        for ( size_t index = 0; index < listFrom.size(); ++index )
        {
            const string&           from = listFrom[index];
            const string&           to   = listTo[index];
            vector<Internal::Entry> listKept;
            for ( Internal::Entry& entry : listEntry )
            {
                if ( entry._from != from && entry._from != to )
                    listKept.push_back( std::move( entry ) );
            }
            listKept.push_back( Internal::Entry{ from, to } );
            listEntry = std::move( listKept );
        }
        string text = string( "<" ) + kRootElement + ">\n";
        for ( const Internal::Entry& entry : listEntry )
            text += string( "\t<" ) + Internal::kRedirectElement + " from=\"" + entry._from + "\" to=\"" + entry._to + "\" />\n";
        text += string( "</" ) + kRootElement + ">\n";
        // 쓸 글을 런타임과 같은 규칙으로 다시 읽는다 — 맞춤 꼴이 아닌 id · 자기 자신으로 · 순환이면 쓰지 않는다.
        unordered_map<string, string> mapResult;
        if ( Internal::parseInto( text, sourceName, mapResult ) == false || Internal::collapseChains( mapResult ) == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the asset redirect table would not load after the move", sourceName } );
        return text;
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
                continue; // 그 도메인에는 옮긴 에셋이 없다
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
        state._entryCount.store( 0 );
    }

    bool AssetRedirect::resolve( string_view assetID, string& outAssetID )
    {
        // 모든 파일 읽기가 지난다 — 엔진이 서기 전(설정 · 기동 파일)에는 표도 없다.
        AssetRedirectTable* const pTable = engine::findSubsystem<AssetRedirectTable>();
        if ( pTable == nullptr || pTable->getState()._entryCount.load() == 0 )
            return false;
        AssetRedirectInternal::State& state = pTable->getState();
        ScopedLock<mutex>             lock{ state._mutex };
        const auto                    it = state._mapOldToNew.find( string{ assetID } );
        if ( it == state._mapOldToNew.end() )
            return false;
        outAssetID = it->second;
        return true;
    }
} // namespace sw

namespace sw
{
    // 리다이렉트 표 — 팩 설정(`PackEngineSettingsSubsystem`)이 채우고 파일 읽기(`AssetIO`)가 묻는다.
    SW_ENGINE_SERVICE_SUBSYSTEM( AssetRedirectTable, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( AssetRedirectTable ) );
} // namespace sw
