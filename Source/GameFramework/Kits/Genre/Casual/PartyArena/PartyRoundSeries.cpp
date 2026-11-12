#include "pch.h"

#include "GameFramework/Kits/Genre/Casual/PartyArena/PartyRoundSeries.h"

#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringUtil.h"

#include "Engine/Module/ModuleSharedAssetCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

#include "GameFramework/Runtime/Foundation/GameData/GameDataXML.h"
#include "GameFramework/Runtime/Foundation/GameData/XMLCatalogTable.h"

namespace sw
{
    namespace
    {
        /** @brief 파티 기본 설정입니다. 순위 점수는 3, 2, 1, 0 이고, 먼저 5점을 얻으면 우승합니다. 함께 닿아 총점까지 같으면 서든 데스이며, 라운드 시간과 대기는 시리즈가 세지 않습니다. */
        RoundSeriesSettings makeDefaultSettings()
        {
            RoundSeriesSettings settings;
            settings._listPlacementPoint = { 3, 2, 1, 0 };
            settings._winScore           = 5;
            settings._tieRule            = RoundSeriesTieRule::SuddenDeath;
            return settings;
        }
    } // namespace
} // namespace sw

namespace sw
{
    PartyRoundSeries::PartyRoundSeries()
        : _listRound{}
        , _seriesSettings{ makeDefaultSettings() }
        , _series{}
        , _eventBuffer{}
    {
    }

    bool PartyRoundSeries::start( int32 playerCount )
    {
        if ( playerCount < 2 || _listRound.empty() )
            return false;
        if ( _series.initialize( _seriesSettings ) == false || _series.start( playerCount ) == false )
            return false;
        _eventBuffer.clear();
        pushEvent( PartySeriesEvent::Kind::RoundStarted, nullopt, 0, 0 );
        return true;
    }

    bool PartyRoundSeries::reportRound( const vector<int32>& listRoundScore )
    {
        const int32 playerCount = getPlayerCount();
        if ( _series.getPhase() != RoundSeriesPhase::RoundActive || listRoundScore.size() != static_cast<size_t>( playerCount ) )
            return false;

        // 순위 이벤트는 라운드를 넘기기 전에 보낸다. 끝난 라운드의 id 를 실어야 하기 때문이다.
        for ( int32 player = 0; player < playerCount; ++player )
        {
            const int32 rank = RoundSeries::computeRank( listRoundScore, player );
            pushEvent( PartySeriesEvent::Kind::RoundRanked, player, rank, _series.getPlacementPoint( rank ) );
        }
        // 위에서 같은 조건을 확인했으므로 거절되지 않는다. 대기가 0 이라 끝나지 않으면 바로 다음 라운드이다.
        const RoundSeriesResult result = _series.reportRound( listRoundScore );
        if ( result == RoundSeriesResult::Finished )
        {
            pushEvent( PartySeriesEvent::Kind::SeriesWon, getWinner(), getTotal( getWinner() ), 0 );
            return true;
        }
        pushEvent( PartySeriesEvent::Kind::RoundStarted, nullopt, getRoundNumber(), 0 );
        return true;
    }

    const PartyRoundDef* PartyRoundSeries::getCurrentRound() const
    {
        if ( _listRound.empty() )
            return nullptr;
        return &_listRound[static_cast<size_t>( getRoundNumber() ) % _listRound.size()];
    }

    void PartyRoundSeries::pollEvents( vector<PartySeriesEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( outListEvent );
    }

    uint32 PartyRoundSeries::loadRoot( const XMLCatalogRoot& root, string_view sourceName )
    {
        // 승리 점수와 순위 점수는 적지 않으면 지금 설정 그대로다. 적었으면 모두 읽혀야 한다 — 깨진 값은 오류를 남기고 이 파일을 읽지 않는다(깨진 값을 건너뛰어 순위가 밀리지 않게).
        // 끝까지 읽은 뒤 한 번에 옮긴다. 앞 칸만 바뀐 채 실패하거나, 다시 읽을 때 라운드가 뒤에 덧붙지 않게 한다.
        optional<int32> winScore;
        if ( root.readOptionalAttribute( "winScore", winScore ) == false )
        {
            SW_LOG_ERROR_ID( "PartyRoundSeries.WinScoreNotNumber", "%#: <%# winScore> is not a number", sourceName, root.getName() );
            return 0;
        }
        if ( winScore.has_value() && *winScore < 1 )
        {
            SW_LOG_ERROR_ID( "PartyRoundSeries.WinScoreNeedsMore", "%#: <%# winScore=\"%#\"> needs 1 or more", sourceName, root.getName(), *winScore );
            return 0;
        }
        const string_view points = root.getAttributeText( "placementPoints" );
        vector<int32>     listPlacementPoint;
        if ( points.empty() == false )
        {
            bool bPoints = true;
            StringUtil::forEachToken( points, ",; ", [&]( string_view token )
            {
                int32      value  = 0;
                const bool bValue = StringUtil::parseInt( token, value ) && value >= 0;
                if ( bValue )
                    listPlacementPoint.push_back( value );
                else
                    bPoints = false;
            } );
            if ( bPoints == false )
            {
                SW_LOG_ERROR_ID( "PartyRoundSeries.PlacementPointsNeedsIntegersMore", "%#: <%# placementPoints=\"%#\"> needs integers of 0 or more", sourceName, root.getName(), points );
                return 0;
            }
        }
        // 앞 판은 읽기가 끝나야 버린다. 틀린 판은 readProperties 가 알리고 그 판만 빠진다.
        vector<PartyRoundDef> listPreviousRound;
        listPreviousRound.swap( _listRound );
        using Element                          = XMLCatalogElement<PartyRoundSeries>;
        static constexpr Element kArrElement[] = {
            Element::of<&PartyRoundSeries::_listRound>( "Round" ),
        };
        const uint32 loadedCount = XMLCatalogReadResult::readInDocumentOrder( *this, root, sourceName, kArrElement )._loadedCount;
        if ( loadedCount == 0 )
        {
            _listRound.swap( listPreviousRound );
            return 0;
        }
        if ( winScore.has_value() )
            _seriesSettings._winScore = *winScore;
        if ( points.empty() == false )
            _seriesSettings._listPlacementPoint = listPlacementPoint;
        return loadedCount;
    }

    void PartyRoundSeries::pushEvent( PartySeriesEvent::Kind kind, optional<int32> player, int32 value, int32 points )
    {
        PartySeriesEvent event;
        event._kind                 = kind;
        event._player               = player;
        event._value                = value;
        event._points               = points;
        const PartyRoundDef* pRound = getCurrentRound();
        event._roundID              = pRound != nullptr ? pRound->_id : hashed_string{};
        _eventBuffer.push( event );
    }
} // namespace sw

namespace sw
{
    /** 쓰는 쪽은 공유 캐시의 한 벌을 받는다(`SharedAssetCache<PartyRoundSeries>::acquire`). 경로 규칙 없이 접미사로 가르는 종류라 카탈로그 편집기에는 나오지 않는다. */
    SW_MODULE_SHARED_ASSET_CACHE( PartyRoundSeries, PartyRoundSeries, SW_KIT_API, PartyRoundSeries::kAssetType.withLoader( AssetTypeLoader::of<PartyRoundSeries>( MemoryTag::Game ) ) );
} // namespace sw
