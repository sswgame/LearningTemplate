#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/RealTimeStrategy/SkirmishMatch.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Reflection/TypeFqn.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Runtime/Character/AI/BehaviorTreeAsset.h"

namespace sw
{
    namespace
    {
        struct SkirmishMatchInternal
        {
            /** @brief 성향 @p style(씬 데이터)에 판의 유닛 id 를 채운 AI 설정입니다. */
            static RTSCommanderAISettings makeAISettings( const RTSCommanderAISettings& style, const SkirmishMatchSettings& matchSettings )
            {
                RTSCommanderAISettings settings = style;
                settings._workerID              = matchSettings._workerID;
                settings._depotID               = matchSettings._depotID;
                settings._supplyID              = matchSettings._supplyID;
                settings._productionID          = matchSettings._productionID;
                settings._armyUnitID            = matchSettings._armyUnitID;
                return settings;
            }

            /// @brief 시작할 때 일꾼을 보내는 광물을 시작 지점에서 찾는 반경(m)
            static constexpr float32 kStartMineralRadius = 16.0f;

            static bool isArmy( const RTSUnit& unit ) { return unit.isMobile() && unit._pDef->_bWorker == SW_FALSE && unit._pDef->canAttack(); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SkirmishMatch::SkirmishMatch()
        : _world{}
        , _listPlayer{}
        , _listEvent{}
        , _listFrameEvent{}
        , _settings{}
        , _statusTimer{ 0.0f }
        , _huntTimer{ 0.0f }
        , _humanMask{ 0 }
        , _bReportedOver{ SW_FALSE }
    {
    }

    bool SkirmishMatch::initialize( const RTSCatalog* pCatalog, const SkirmishMap& map, const TileMapXMLData& tileMap, uint8 humanMask, const SkirmishMatchSettings& matchSettings )
    {
        if ( _world.initialize( pCatalog, tileMap._width, tileMap._height ) == false )
            return false;
        if ( _world.claimEvents( _eventLease ) == false )
            return false; // 이 매치의 표뿐이라 일어날 수 없다 — 둘째 소비자가 생기면 게이트가 어서션을 남긴다
        _settings = matchSettings;
        if ( validateMap( *pCatalog, map, tileMap ) == false )
            return false;
        _listEvent.clear();
        _listFrameEvent.clear();
        _statusTimer   = 0.0f;
        _huntTimer     = 0.0f;
        _bReportedOver = SW_FALSE;

        const int32 cliffLayer = static_cast<int32>( *tileMap.findDataLayer( SkirmishMap::kCliffLayer ) ); // 있음은 validateMap 이 봤다
        for ( int32 y = 0; y < tileMap._height; ++y )
        {
            for ( int32 x = 0; x < tileMap._width; ++x )
            {
                int32 cliff = 0;
                if ( tileMap.getCellData( cliffLayer, x, y, cliff ) && cliff != 0 )
                    _world.setTerrainBlocked( x, y, true );
            }
        }

        const RTSSettings& settings = _world.getSettings();
        _listPlayer.clear();
        _listPlayer.resize( map._listPlayerStart.size() ); // 월드가 지갑의 주소를 쥔다 — 아래에서 크기가 바뀌지 않는다
        _humanMask = static_cast<uint8>( humanMask & ( ( 1u << _listPlayer.size() ) - 1u ) );
        for ( size_t index = 0; index < _listPlayer.size(); ++index )
        {
            Player& player = _listPlayer[index];
            player._start  = map._listPlayerStart[index];
            player._wallet.add( settings._mineralCurrency, _settings._startMinerals );
            (void)_world.addPlayer( player._start._team, player._wallet, float3{ player._start._position._x, 0.0f, player._start._position._y } );
        }
        if ( placeUnits( map ) == false )
            return false;

        for ( int32 player = 0; player < getPlayerCount(); ++player )
        {
            sendWorkersToMinerals( player ); // AI 쪽 일꾼은 AI 가 다시 정한다
        }
        for ( int32 player = 0; player < getPlayerCount(); ++player )
        {
            if ( isHumanPlayer( player ) )
                continue;
            Player&                       state = _listPlayer[static_cast<size_t>( player )];
            const RTSCommanderAISettings& style = hasHumanPlayer() ? _settings._opponentAI : state._start._aiRole == SkirmishAIRole::Rush ? _settings._rushAI
                                                                                                                                          : _settings._macroAI;
            SW_ASSERT( _settings._pCommanderTree != nullptr && "the game mode must read the commander behavior tree before starting a match with a computer opponent" );
            state._pAI = make_unique<RTSCommanderAI>();
            state._pAI->initialize( &_world, player, SkirmishMatchInternal::makeAISettings( style, _settings ), _settings._pCommanderTree->getTree() );
        }
        SW_LOG_INFO_ID( "SkirmishMatch.MatchStart", "[Skirmish] match start - %# players, human mask %#", getPlayerCount(), static_cast<uint32>( _humanMask ) );
        return true;
    }

    bool SkirmishMatch::validateMap( const RTSCatalog& catalog, const SkirmishMap& map, const TileMapXMLData& tileMap ) const
    {
        bool bValid = true;
        if ( tileMap.findDataLayer( SkirmishMap::kCliffLayer ).has_value() == false )
        {
            SW_LOG_ERROR_ID( "SkirmishMatch.MapLayerMissing", "[Skirmish] tile map '%#' has no data layer '%#' - the match cannot start", tileMap._name, SkirmishMap::kCliffLayer );
            bValid = false;
        }
        const int32 playerCount = static_cast<int32>( map._listPlayerStart.size() );
        if ( playerCount < kMinPlayerCount || playerCount > kMaxPlayerCount )
        {
            SW_LOG_ERROR_ID( "SkirmishMatch.MapPlayerCount", "[Skirmish] the map has %# player starts (%#..%#) - the match cannot start", playerCount, kMinPlayerCount, kMaxPlayerCount );
            bValid = false;
        }
        for ( const SkirmishPlayerStart& start : map._listPlayerStart )
        {
            const bool bInside = 0.0f <= start._position._x && start._position._x < static_cast<float32>( tileMap._width ) && 0.0f <= start._position._y && start._position._y < static_cast<float32>( tileMap._height );
            if ( bInside )
                continue;
            SW_LOG_ERROR_ID( "SkirmishMatch.MapStartOutside", "[Skirmish] a player start (%#, %#) is outside the %#x%# map - the match cannot start", start._position._x, start._position._y, tileMap._width,
                             tileMap._height );
            bValid = false;
        }
        if ( map._listPlacement.empty() )
        {
            SW_LOG_ERROR_ID( "SkirmishMatch.MapPlacementsMissing", "[Skirmish] the map places nothing - the match cannot start" );
            bValid = false;
        }
        // 놓는 유닛이 카탈로그에 없거나 주인이 없는 플레이어면 조용히 건너뛰지 않고 판을 열지 않는다.
        for ( const SkirmishUnitPlacement& placement : map._listPlacement )
        {
            if ( catalog.find( placement._unitID ) == nullptr )
            {
                SW_LOG_ERROR_ID( "SkirmishMatch.MapLayoutPlacesBut", "[Skirmish] the map places '%#' but the unit catalog has no such unit - the match cannot start", placement._unitID.c_str() );
                bValid = false;
            }
            if ( placement._owner != RTSWorld::kNoOwner && ( placement._owner < 0 || placement._owner >= playerCount ) )
            {
                SW_LOG_ERROR_ID( "SkirmishMatch.MapOwnerUnknown", "[Skirmish] the map places '%#' for player %# but has %# players - the match cannot start", placement._unitID.c_str(), placement._owner, playerCount );
                bValid = false;
            }
        }
        return bValid;
    }

    bool SkirmishMatch::placeUnits( const SkirmishMap& map )
    {
        bool bPlaced = true;
        for ( const SkirmishUnitPlacement& placement : map._listPlacement )
        {
            const float3 position{ static_cast<float32>( placement._cell._x ) + 0.5f, 0.0f, static_cast<float32>( placement._cell._y ) + 0.5f };
            if ( _world.spawnUnit( placement._unitID.key(), placement._owner, position ).isValid() )
                continue;
            SW_LOG_ERROR_ID( "SkirmishMatch.MapPlacementBlocked", "[Skirmish] the map cannot place '%#' at cell (%#, %#) - blocked or outside, the match cannot start", placement._unitID.c_str(),
                             placement._cell._x, placement._cell._y );
            bPlaced = false;
        }
        return bPlaced;
    }

    void SkirmishMatch::sendWorkersToMinerals( int32 player )
    {
        const RTSPlayer* const pPlayer = _world.findPlayer( player );
        vector<RTSUnitID>      listWorker;
        _world.forEachUnit( [&]( const RTSUnit& unit )
        {
            if ( unit._owner == player && unit._pDef->_bWorker != SW_FALSE )
                listWorker.push_back( unit._id );
        } );
        for ( const RTSUnitID workerID : listWorker )
        {
            const RTSUnitID mineralID = _world.findNearestResource( pPlayer->_startPosition, RTSResourceKind::Minerals, SkirmishMatchInternal::kStartMineralRadius );
            if ( mineralID.isValid() )
                (void)_world.issueGather( workerID, mineralID );
        }
    }

    void SkirmishMatch::serializeState( StateIO& io )
    {
        io.expect( _humanMask ); // 설정 값 — 같은 사람 여부로 `initialize` 한 판에만 읽는다(무엇을 바꾸기 전에 본다)
        io.reflected( *this );
        io.value( _world );
        if ( io.isOk() == false )
            return;
        for ( Player& player : _listPlayer )
        {
            io.value( player._wallet );
        }
        for ( Player& player : _listPlayer )
        {
            if ( player._pAI != nullptr )
                io.value( *player._pAI );
        }
        if ( io.isReading() )
        {
            _listEvent.clear();
            _listFrameEvent.clear();
        }
    }

    void SkirmishMatch::update( float32 deltaTime, float32 timeScale )
    {
        // AI 는 월드가 실제로 진행한 시간으로 돈다 — 자르지 않은 델타를 받으면 배속 · 히치에서 AI 시계가 월드보다 앞선다(감사 K-6).
        const float32 worldDelta = _world.update( deltaTime, timeScale );
        for ( Player& player : _listPlayer )
        {
            if ( player._pAI != nullptr )
                player._pAI->update( worldDelta );
        }
        // `RTSWorld::pollEvents` 는 뒤에 붙인다(바꿔 넣지 않는다) — 비우지 않으면 지난 알림을 AI 에 다시 넘긴다.
        _listFrameEvent.clear();
        _world.pollEvents( _eventLease, _listFrameEvent );
        for ( const RTSEvent& event : _listFrameEvent )
        {
            for ( Player& player : _listPlayer )
            {
                if ( player._pAI != nullptr )
                    player._pAI->notify( event );
            }
            if ( event._kind == RTSEvent::Kind::PlayerDefeated )
                SW_LOG_INFO_ID( "SkirmishMatch.TPlayerLostEvery", "[Skirmish] t=%#s player %# lost every building and is defeated", static_cast<int32>( _world.getTime() ), *event._player );
        }
        _listEvent.insert( _listEvent.end(), _listFrameEvent.begin(), _listFrameEvent.end() );

        if ( isOver() )
        {
            if ( _bReportedOver == SW_FALSE )
            {
                _bReportedOver                            = SW_TRUE;
                [[maybe_unused]] const int32 winner       = _world.getWinningTeam();
                [[maybe_unused]] const bool  bHumanResult = _humanMask == kHumanPlayer0; // 혼자 하는 판만 "승리 · 패배" 다(2 인 대전은 두 사람이다)
                logStatus();
                SW_LOG_INFO_ID( "SkirmishMatch.GameOverTeam", "[Skirmish] game over at t=%#s - team %# wins%#", static_cast<int32>( _world.getTime() ), winner,
                                bHumanResult ? ( winner == _listPlayer.front()._start._team ? " (victory!)" : " (defeat)" ) : "" );
            }
            return;
        }
        _huntTimer += deltaTime;
        if ( _huntTimer >= _settings._huntIntervalSeconds )
        {
            _huntTimer = 0.0f;
            for ( int32 player = 0; player < getPlayerCount(); ++player )
            {
                if ( _listPlayer[static_cast<size_t>( player )]._pAI == nullptr )
                    continue;
                for ( int32 enemy = 0; enemy < getPlayerCount(); ++enemy )
                {
                    if ( _world.areEnemies( player, enemy ) )
                        huntRemaining( player, enemy );
                }
            }
        }
        _statusTimer += deltaTime;
        if ( _statusTimer >= _settings._statusIntervalSeconds )
        {
            _statusTimer = 0.0f;
            logStatus();
        }
    }

    void SkirmishMatch::pollEvents( vector<RTSEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    void SkirmishMatch::huntRemaining( int32 player, int32 enemy )
    {
        const RTSPlayer* pEnemy = _world.findPlayer( enemy );
        if ( pEnemy == nullptr || pEnemy->_bDefeated != SW_FALSE )
            return;
        vector<RTSUnitID> listArrived;
        float3            groupPosition{};
        _world.forEachUnit( [&]( const RTSUnit& unit )
        {
            const bool bNotIdleArmyUnit = unit._owner != player || SkirmishMatchInternal::isArmy( unit ) == false || unit.isIdle() == false || unit._attackTarget.isValid();
            if ( bNotIdleArmyUnit )
                return;
            if ( float3::getHorizontalDistance( unit._position, pEnemy->_startPosition ) > _settings._arrivedRadius )
                return;
            listArrived.push_back( unit._id );
            groupPosition = unit._position;
        } );
        if ( listArrived.empty() )
            return;
        float32 bestDistance = MathUtil::kMaxFloat;
        float3  target{};
        _world.forEachUnit( [&]( const RTSUnit& unit )
        {
            if ( unit._owner != enemy || unit.isBuilding() == false )
                return;
            const float32 distance = float3::getHorizontalDistance( unit._position, groupPosition );
            if ( distance < bestDistance )
            {
                bestDistance = distance;
                target       = unit._position;
            }
        } );
        if ( bestDistance < MathUtil::kMaxFloat )
            (void)_world.issueGroupMove( listArrived, target, true );
    }

    uint32 SkirmishMatch::computeChecksum() const
    {
        // 세이브 바이트를 해시한다 - 월드 · 지갑 · AI 진행 · 시계가 모두 들어 어긋난 칸을 빠뜨리지 않는다(칸을 골라 섞으면 새 칸을 잊는다).
        Archive archive;
        writeState( archive );
        return HashUtil::appendBytes32( HashUtil::kFNVOffset32, archive.getData(), static_cast<size_t>( archive.getSize() ) );
    }

    SkirmishPlayerSummary SkirmishMatch::makeSummary( int32 player ) const
    {
        SkirmishPlayerSummary summary;
        _world.forEachUnit( [&]( const RTSUnit& unit )
        {
            if ( unit._owner != player )
                return;
            if ( unit._pDef->_bWorker != SW_FALSE )
                ++summary._workers;
            else if ( SkirmishMatchInternal::isArmy( unit ) )
                ++summary._army;
            else if ( unit.isBuilding() )
                ++summary._buildings;
        } );
        const RTSPlayer* pPlayer = _world.findPlayer( player );
        if ( pPlayer != nullptr )
        {
            summary._minerals   = static_cast<int32>( _world.getMinerals( player ) );
            summary._gas        = static_cast<int32>( _world.getGas( player ) );
            summary._supplyUsed = pPlayer->_supplyUsed;
            summary._supplyCap  = pPlayer->_supplyCap;
        }
        return summary;
    }

    void SkirmishMatch::logStatus() const
    {
        for ( int32 player = 0; player < getPlayerCount(); ++player )
        {
            [[maybe_unused]] const SkirmishPlayerSummary summary = makeSummary( player );
            SW_LOG_INFO_ID( "SkirmishMatch.PlayerStatus", "[Skirmish] t=%#s p%# workers %# army %# buildings %# minerals %# supply %#/%#", static_cast<int32>( _world.getTime() ), player, summary._workers,
                            summary._army, summary._buildings, summary._minerals, summary._supplyUsed, summary._supplyCap );
        }
    }
} // namespace sw
