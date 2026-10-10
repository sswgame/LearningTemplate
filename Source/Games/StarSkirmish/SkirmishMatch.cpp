#include "pch.h"

#include "Games/StarSkirmish/SkirmishMatch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    SW_LOG_CALLER( "SkirmishMatch" );

    namespace
    {
        struct SkirmishMatchInternal
        {
            static constexpr float32 kStatusInterval = 30.0f;
            static constexpr float32 kHuntInterval   = 4.0f;
            static constexpr float32 kArrivedRadius  = 10.0f; ///< 적 시작 지점에서 이 안이면 "닿았다"
            static constexpr int32   kStartMinerals  = 50;

            /** @brief 성향별 AI 설정입니다 — 0 러시(일찍 작게) · 1 운영(일꾼 · 병영을 늘려 크게) · 2 사람 상대(그 사이). */
            static RTSAiSettings makeAiSettings( int32 style )
            {
                RTSAiSettings settings;
                settings._workerId     = hashed_string( "worker" );
                settings._depotId      = hashed_string( "command_center" );
                settings._supplyId     = hashed_string( "supply_depot" );
                settings._productionId = hashed_string( "barracks" );
                settings._armyUnitId   = hashed_string( "marine" );
                if ( style == 0 )
                {
                    settings._workerTarget     = 12;
                    settings._productionTarget = 2;
                    settings._attackArmySize   = 6;
                }
                else if ( style == 1 )
                {
                    settings._workerTarget     = 18;
                    settings._productionTarget = 3;
                    settings._attackArmySize   = 14;
                    settings._supplyMargin     = 4;
                }
                else
                {
                    settings._workerTarget     = 16;
                    settings._productionTarget = 2;
                    settings._attackArmySize   = 10;
                }
                return settings;
            }

            static float32 computeFlatDistance( const float3& lhs, const float3& rhs )
            {
                const float32 dx = lhs._x - rhs._x;
                const float32 dz = lhs._z - rhs._z;
                return MathUtil::sqrt( dx * dx + dz * dz );
            }

            static bool isArmy( const RTSUnit& unit ) { return unit.isMobile() && unit._pDef->_bWorker == SW_FALSE && unit._pDef->canAttack(); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SkirmishMatch::SkirmishMatch()
        : _world{}
        , _arrAi{}
        , _arrWallet{}
        , _listEvent{}
        , _listFrameEvent{}
        , _listCliff{}
        , _statusTimer{ 0.0f }
        , _huntTimer{ 0.0f }
        , _arrAiActive{ SW_FALSE, SW_FALSE }
        , _bHumanPlayer{ SW_FALSE }
        , _bReportedOver{ SW_FALSE }
    {
    }

    void SkirmishMatch::initialize( const RTSCatalog* pCatalog, bool bHumanPlayer )
    {
        _world.initialize( pCatalog, kMapSize, kMapSize, RTSSettings{} );
        _listEvent.clear();
        _listFrameEvent.clear();
        _statusTimer   = 0.0f;
        _huntTimer     = 0.0f;
        _bHumanPlayer  = bHumanPlayer ? SW_TRUE : SW_FALSE;
        _bReportedOver = SW_FALSE;
        paintMap();

        // 0 번은 남서(본진 가운데 10, 10), 1 번은 점 대칭인 북동. 팀이 달라 서로 적이다.
        const float32      farCenter = static_cast<float32>( kMapSize ) - 10.0f;
        const RTSSettings& settings  = _world.getSettings();
        for ( Wallet& wallet : _arrWallet )
        {
            wallet.clear();
            wallet.add( settings._mineralCurrency, SkirmishMatchInternal::kStartMinerals );
        }
        (void)_world.addPlayer( 0, &_arrWallet[0], float3{ 10.0f, 0.0f, 10.0f } );
        (void)_world.addPlayer( 1, &_arrWallet[1], float3{ farCenter, 0.0f, farCenter } );
        spawnBase( 0, false );
        spawnBase( 1, true );
        // 가운데 길목의 확장 광물(누구 것도 아니다).
        for ( int32 index = 0; index < 5; ++index )
        {
            spawnResource( "rich_minerals", 3, 38 + index, false );
            spawnResource( "rich_minerals", 3, 38 + index, true );
        }
        spawnResource( "geyser", 6, 45, false );
        spawnResource( "geyser", 6, 45, true );

        for ( int32 player = 0; player < kPlayerCount; ++player )
        {
            const bool bAi       = bHumanPlayer == false || player != 0;
            _arrAiActive[player] = bAi ? SW_TRUE : SW_FALSE;
            const int32 style    = bHumanPlayer ? 2 : player;
            if ( bAi )
                _arrAi[player].initialize( &_world, player, SkirmishMatchInternal::makeAiSettings( style ) );
        }
        SW_LOG_INFO( "[Skirmish] match start - %#", bHumanPlayer ? "you (blue, south-west) vs computer (red)" : "computer (rush, blue) vs computer (macro, red)" );
    }

    void SkirmishMatch::writeState( Archive& outArchive ) const
    {
        outArchive << _bHumanPlayer;
        _world.writeState( outArchive );
        for ( const Wallet& wallet : _arrWallet )
        {
            wallet.writeState( outArchive );
        }
        for ( int32 player = 0; player < kPlayerCount; ++player )
        {
            if ( _arrAiActive[player] == SW_TRUE )
                _arrAi[player].writeState( outArchive );
        }
        outArchive << _statusTimer;
        outArchive << _huntTimer;
        outArchive << _bReportedOver;
    }

    bool SkirmishMatch::readState( Archive& archive )
    {
        uint8 bHumanPlayer = SW_FALSE;
        archive >> bHumanPlayer;
        if ( archive.isError() || bHumanPlayer != _bHumanPlayer || _world.readState( archive ) == false )
            return false;
        for ( Wallet& wallet : _arrWallet )
        {
            if ( wallet.readState( archive ) == false )
                return false;
        }
        for ( int32 player = 0; player < kPlayerCount; ++player )
        {
            if ( _arrAiActive[player] == SW_TRUE && _arrAi[player].readState( archive ) == false )
                return false;
        }
        archive >> _statusTimer;
        archive >> _huntTimer;
        archive >> _bReportedOver;
        _listEvent.clear();
        _listFrameEvent.clear();
        return archive.isOk();
    }

    void SkirmishMatch::paintMap()
    {
        _listCliff.assign( static_cast<size_t>( kMapSize * kMapSize ), SW_FALSE );
        for ( int32 y = 0; y < kMapSize; ++y )
        {
            for ( int32 x = 0; x < kMapSize; ++x )
            {
                // 반대각선 능선(두 기지를 가른다) — 가운데와 양쪽에 비탈길.
                const int32 diagonal = x + y - ( kMapSize - 1 );
                const int32 across   = x - y;
                const bool  bRidge   = MathUtil::abs( diagonal ) <= 1;
                const bool  bRamp    = MathUtil::abs( across ) <= 3 || MathUtil::abs( across - 34 ) <= 2 || MathUtil::abs( across + 34 ) <= 2;
                // 고지 둘(점 대칭) — 확장 앞을 가린다.
                const bool bPlateau = ( x >= 20 && x <= 23 && y >= 38 && y <= 41 ) || ( x >= kMapSize - 24 && x <= kMapSize - 21 && y >= kMapSize - 42 && y <= kMapSize - 39 );
                if ( ( bRidge && bRamp == false ) || bPlateau )
                {
                    _listCliff[static_cast<size_t>( y * kMapSize + x )] = SW_TRUE;
                    _world.setTerrainBlocked( x, y, true );
                }
            }
        }
    }

    bool SkirmishMatch::isCliff( int32 x, int32 y ) const
    {
        if ( x < 0 || y < 0 || x >= kMapSize || y >= kMapSize )
            return false;
        return _listCliff[static_cast<size_t>( y * kMapSize + x )] != SW_FALSE;
    }

    RTSUnitId SkirmishMatch::spawnAt( const utf8* pDefId, int32 owner, int32 x, int32 y, bool bMirror )
    {
        const RTSUnitDef* pDef = _world.getCatalog()->findUnit( hashed_string( pDefId ) );
        if ( pDef == nullptr )
        {
            SW_LOG_WARNING( "[Skirmish] units.xml has no '%#' - not placed", pDefId );
            return RTSUnitId{};
        }
        // 점 대칭 — 건물 · 자원은 왼쪽 아래 칸이 기준이라 자리 폭만큼 더 민다.
        const int32 footprint = pDef->isMobile() ? 1 : pDef->_footprint;
        const int32 cellX     = bMirror ? kMapSize - x - footprint : x;
        const int32 cellY     = bMirror ? kMapSize - y - footprint : y;
        return _world.spawnUnit( pDef->_id, owner, float3{ static_cast<float32>( cellX ) + 0.5f, 0.0f, static_cast<float32>( cellY ) + 0.5f } );
    }

    void SkirmishMatch::spawnResource( const utf8* pDefId, int32 x, int32 y, bool bMirror )
    {
        (void)spawnAt( pDefId, RTSWorld::kNoOwner, x, y, bMirror ); // id 는 쓰지 않는다 — 정의가 없으면 spawnAt 이 경고한다
    }

    void SkirmishMatch::spawnBase( int32 player, bool bMirror )
    {
        // 본진(8..11, 8..11) · 뒤쪽 광물 줄(x 3, y 6..13) · 아래 간헐천(8..9, 2..3) · 사이에 일꾼 넷.
        (void)spawnAt( "command_center", player, 8, 8, bMirror );
        for ( int32 y = 6; y <= 13; ++y )
        {
            spawnResource( "minerals", 3, y, bMirror );
        }
        spawnResource( "geyser", 8, 2, bMirror );
        for ( int32 index = 0; index < 4; ++index )
        {
            (void)spawnAt( "worker", player, 6, 8 + index, bMirror ); // id 는 쓰지 않는다 — 정의가 없으면 spawnAt 이 경고한다
        }

        // 사람 쪽 일꾼도 놀지 않게 처음 한 번 캐러 보낸다(AI 쪽은 AI 가 보낸다).
        const RTSPlayer*  pPlayer = _world.findPlayer( player );
        vector<RTSUnitId> listWorker;
        _world.forEachUnit( [&]( const RTSUnit& unit )
        {
            if ( unit._owner == player && unit._pDef->_bWorker != SW_FALSE )
                listWorker.push_back( unit._id );
        } );
        for ( const RTSUnitId workerId : listWorker )
        {
            const RTSUnitId mineralId = _world.findNearestResource( pPlayer->_startPosition, RTSResourceType::Minerals, 16.0f );
            if ( mineralId.isValid() )
                (void)_world.issueGather( workerId, mineralId );
        }
    }

    void SkirmishMatch::update( float32 deltaTime )
    {
        _world.update( deltaTime );
        for ( int32 player = 0; player < kPlayerCount; ++player )
        {
            if ( _arrAiActive[player] != SW_FALSE )
                _arrAi[player].update( deltaTime );
        }
        // `RTSWorld::drainEvents` 는 뒤에 붙인다(바꿔 넣지 않는다) — 비우지 않으면 지난 알림을 AI 에 다시 넘긴다.
        _listFrameEvent.clear();
        _world.drainEvents( _listFrameEvent );
        for ( const RTSEvent& event : _listFrameEvent )
        {
            for ( int32 player = 0; player < kPlayerCount; ++player )
            {
                if ( _arrAiActive[player] != SW_FALSE )
                    _arrAi[player].notify( event );
            }
            if ( event._kind == RTSEvent::Kind::PlayerDefeated )
                SW_LOG_INFO( "[Skirmish] t=%#s player %# lost every building and is defeated", static_cast<int32>( _world.getTime() ), event._player );
        }
        _listEvent.insert( _listEvent.end(), _listFrameEvent.begin(), _listFrameEvent.end() );

        if ( isOver() )
        {
            if ( _bReportedOver == SW_FALSE )
            {
                _bReportedOver                            = SW_TRUE;
                [[maybe_unused]] const int32 winner       = _world.getWinningTeam();
                [[maybe_unused]] const bool  bHumanResult = _bHumanPlayer != SW_FALSE;
                logStatus();
                SW_LOG_INFO( "[Skirmish] game over at t=%#s - player %# wins%#", static_cast<int32>( _world.getTime() ), winner,
                             bHumanResult ? ( winner == 0 ? " (victory!)" : " (defeat)" ) : "" );
            }
            return;
        }
        _huntTimer += deltaTime;
        if ( _huntTimer >= SkirmishMatchInternal::kHuntInterval )
        {
            _huntTimer = 0.0f;
            for ( int32 player = 0; player < kPlayerCount; ++player )
            {
                if ( _arrAiActive[player] != SW_FALSE )
                    huntRemaining( player );
            }
        }
        _statusTimer += deltaTime;
        if ( _statusTimer >= SkirmishMatchInternal::kStatusInterval )
        {
            _statusTimer = 0.0f;
            logStatus();
        }
    }

    void SkirmishMatch::drainEvents( vector<RTSEvent>& outListEvent )
    {
        outListEvent.insert( outListEvent.end(), _listEvent.begin(), _listEvent.end() );
        _listEvent.clear();
    }

    void SkirmishMatch::huntRemaining( int32 player )
    {
        const int32      enemy  = 1 - player;
        const RTSPlayer* pEnemy = _world.findPlayer( enemy );
        if ( pEnemy == nullptr || pEnemy->_bDefeated != SW_FALSE )
            return;
        vector<RTSUnitId> listArrived;
        float3            groupPosition{};
        _world.forEachUnit( [&]( const RTSUnit& unit )
        {
            if ( unit._owner != player || SkirmishMatchInternal::isArmy( unit ) == false || unit.isIdle() == false || unit._attackTarget.isValid() )
                return;
            if ( SkirmishMatchInternal::computeFlatDistance( unit._position, pEnemy->_startPosition ) > SkirmishMatchInternal::kArrivedRadius )
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
            const float32 distance = SkirmishMatchInternal::computeFlatDistance( unit._position, groupPosition );
            if ( distance < bestDistance )
            {
                bestDistance = distance;
                target       = unit._position;
            }
        } );
        if ( bestDistance < MathUtil::kMaxFloat )
            (void)_world.issueGroupMove( listArrived, target, true );
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
        [[maybe_unused]] const SkirmishPlayerSummary first  = makeSummary( 0 );
        [[maybe_unused]] const SkirmishPlayerSummary second = makeSummary( 1 );
        SW_LOG_INFO( "[Skirmish] t=%#s p0 workers %# army %# buildings %# minerals %# supply %#/%# | p1 workers %# army %# buildings %# minerals %# supply %#/%#",
                     static_cast<int32>( _world.getTime() ), first._workers, first._army, first._buildings, first._minerals, first._supplyUsed, first._supplyCap,
                     second._workers, second._army, second._buildings, second._minerals, second._supplyUsed, second._supplyCap );
    }
} // namespace sw
