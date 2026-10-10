#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGBattlefield.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Base/Actor/Navigation/GridReachability.h"
#include "GameFramework/Base/Foundation/Utility/StateArchiveUtil.h"

namespace sw
{
    namespace
    {
        struct SRPGBattlefieldInternal
        {
            /** @brief 동쪽(+x)을 보는 무늬 칸을 @p direction 쪽으로 돌립니다. */
            static int2 rotateOffset( const int2& offset, const int2& direction )
            {
                if ( direction._x > 0 )
                    return offset;
                if ( direction._x < 0 )
                    return int2{ -offset._x, -offset._y };
                if ( direction._y > 0 )
                    return int2{ -offset._y, offset._x };
                return int2{ offset._y, -offset._x };
            }

            /** @brief 쏘는 칸 → 겨눈 칸의 주된 방향(네 방향 중 하나)입니다. 같은 칸이면 (0, 0) 입니다. */
            static int2 computeDirection( const int2& fromCell, const int2& aimCell )
            {
                const int32 deltaX = aimCell._x - fromCell._x;
                const int32 deltaY = aimCell._y - fromCell._y;
                if ( deltaX == 0 && deltaY == 0 )
                    return int2{ 0, 0 };
                if ( MathUtil::abs( deltaX ) >= MathUtil::abs( deltaY ) )
                    return int2{ deltaX > 0 ? 1 : -1, 0 };
                return int2{ 0, deltaY > 0 ? 1 : -1 };
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "SRPGBattlefield" );

    SRPGBattlefield::SRPGBattlefield()
        : _listTerrain{}
        , _listUnit{}
        , _eventBuffer{}
        , _cellMarks{}
        , _settings{}
        , _turnOrder{}
        , _random{}
        , _pCatalog{ nullptr }
        , _topology{}
        , _land{}
        , _turn{ 0 }
        , _activeUnit{ -1 }
        , _phaseTeam{ SRPGTeam::Player }
    {
    }

    void SRPGBattlefield::initialize( const SRPGCatalog* pCatalog, int32 width, int32 height, const hashed_string& defaultTerrain, const SRPGSettings& settings,
                                      uint32 seed )
    {
        _pCatalog = pCatalog;
        _topology = GridTopology{ width, height }; // 음수는 0 칸
        _settings = settings;
        _random.setSeed( seed );
        const SRPGTerrainDef* pTerrain = pCatalog != nullptr ? pCatalog->findTerrain( defaultTerrain ) : nullptr;
        if ( pTerrain == nullptr )
            SW_LOG_WARNING( "unknown default terrain '%#' - cells are impassable until painted", defaultTerrain.c_str() );
        _listTerrain.assign( static_cast<size_t>( _topology.getCellCount() ), pTerrain );
        _listUnit.clear();
        _eventBuffer.clear();
        _turn       = 0;
        _activeUnit = -1;
        _phaseTeam  = SRPGTeam::Player;
    }

    bool SRPGBattlefield::setTerrain( const int2& cell, const hashed_string& terrainID )
    {
        const SRPGTerrainDef* pTerrain = _pCatalog != nullptr ? _pCatalog->findTerrain( terrainID ) : nullptr;
        if ( pTerrain == nullptr || isInside( cell ) == false )
            return false;
        _listTerrain[static_cast<size_t>( _topology.toIndex( cell ) )] = pTerrain;
        return true;
    }

    int32 SRPGBattlefield::fillTerrain( const int2& fromCell, const int2& toCell, const hashed_string& terrainID )
    {
        int32 count = 0;
        for ( int32 cellY = MathUtil::min( fromCell._y, toCell._y ); cellY <= MathUtil::max( fromCell._y, toCell._y ); ++cellY )
        {
            for ( int32 cellX = MathUtil::min( fromCell._x, toCell._x ); cellX <= MathUtil::max( fromCell._x, toCell._x ); ++cellX )
            {
                count += setTerrain( int2{ cellX, cellY }, terrainID ) ? 1 : 0;
            }
        }
        return count;
    }

    int32 SRPGBattlefield::addUnit( const hashed_string& unitID, const hashed_string& pilotID, SRPGTeam team, const int2& cell, int32 pilotLevel )
    {
        if ( _pCatalog == nullptr )
            return -1;
        SRPGUnit unit;
        unit._pDef   = _pCatalog->findUnit( unitID );
        unit._pPilot = _pCatalog->findPilot( pilotID );
        if ( unit._pDef == nullptr || unit._pPilot == nullptr )
        {
            SW_LOG_WARNING( "cannot place unit '%#' with pilot '%#' - unknown id", unitID.c_str(), pilotID.c_str() );
            return -1;
        }
        if ( isInside( cell ) == false || findUnitAt( cell ) >= 0 || computeTerrainCost( unit, cell ) < 0 )
            return -1;
        unit._cell = cell;
        unit._team = team;
        unit._pilotLevel.setLevel( _pCatalog->getPilotCurve(), pilotLevel );
        unit._unitLevel.setLevel( _pCatalog->getUnitCurve(), 1 );
        unit._morale = _settings._moraleStart;
        refillUnit( unit );
        _listUnit.push_back( unit );
        return static_cast<int32>( _listUnit.size() ) - 1;
    }

    void SRPGBattlefield::setCommander( int32 unitIndex, bool bCommander )
    {
        SRPGUnit* pUnit = findUnit( unitIndex );
        if ( pUnit != nullptr )
            pUnit->_bCommander = bCommander ? SW_TRUE : SW_FALSE;
    }

    bool SRPGBattlefield::bindLand( LandRegistry* pLand, const int2& origin )
    {
        LandBinding land;
        land.bind( pLand, origin, hashed_string( "TacticsSRPG" ) );
        if ( _topology.getCellCount() > 0 && land.claimRect( 0, 0, _topology._width - 1, _topology._height - 1, false ) == false )
            return false;
        _land = land;
        return true;
    }

    void SRPGBattlefield::releaseLand()
    {
        if ( _topology.getCellCount() > 0 )
            _land.releaseRect( 0, 0, _topology._width - 1, _topology._height - 1 );
        _land = LandBinding{};
    }

    void SRPGBattlefield::beginBattle()
    {
        _turn       = 0;
        _activeUnit = -1;
        if ( _settings._turnMode == SRPGTurnMode::Individual )
        {
            _turnOrder.initialize( TurnOrderMode::Rounds, _random.nextUint() );
            for ( size_t index = 0; index < _listUnit.size(); ++index )
            {
                const SRPGUnit& unit = _listUnit[index];
                if ( unit._bAlive == SW_FALSE )
                    continue;
                const int32 speed = unit.computeStat( SRPGPilotStat::Reaction ) + unit._pDef->_mobility;
                _turnOrder.addActor( static_cast<int32>( index ), static_cast<float32>( MathUtil::max( 1, speed ) ) );
            }
            activateNextUnit();
            return;
        }
        _turn = 1;
        pushEvent( SRPGEvent{ -1, -1, _turn, SRPGEvent::Kind::TurnStarted, SRPGTeam::Player } );
        for ( int32 team = 0; team < kSRPGTeamCount; ++team )
        {
            if ( countAlive( static_cast<SRPGTeam>( team ) ) > 0 )
            {
                startPhase( static_cast<SRPGTeam>( team ) );
                return;
            }
        }
    }

    bool SRPGBattlefield::canAct( int32 unitIndex ) const
    {
        const SRPGUnit* pUnit = findUnit( unitIndex );
        if ( pUnit == nullptr || _turn <= 0 || pUnit->_bAlive == SW_FALSE || pUnit->_bActed == SW_TRUE )
            return false;
        if ( _settings._turnMode == SRPGTurnMode::Individual )
            return unitIndex == _activeUnit;
        return pUnit->_team == _phaseTeam;
    }

    void SRPGBattlefield::endUnitAction( int32 unitIndex )
    {
        const SRPGUnit* pUnit = findUnit( unitIndex );
        if ( pUnit == nullptr || _turn <= 0 )
            return;
        if ( canAct( unitIndex ) )
            _listUnit[static_cast<size_t>( unitIndex )]._bActed = SW_TRUE;
        // 반격에 격파된 유닛도 차례는 넘긴다 — 죽은 유닛은 canAct 가 false 라 위에서 걸러진다
        if ( _settings._turnMode == SRPGTurnMode::Individual )
        {
            if ( unitIndex == _activeUnit )
                activateNextUnit();
        }
        else if ( pUnit->_team == _phaseTeam && isPhaseFinished() )
        {
            advancePhase();
        }
    }

    void SRPGBattlefield::endPhase()
    {
        if ( _settings._turnMode == SRPGTurnMode::Individual )
        {
            endUnitAction( _activeUnit );
            return;
        }
        for ( SRPGUnit& unit : _listUnit )
        {
            if ( unit._team == _phaseTeam )
                unit._bActed = SW_TRUE;
        }
        advancePhase();
    }

    void SRPGBattlefield::startPhase( SRPGTeam team )
    {
        _phaseTeam = team;
        for ( SRPGUnit& unit : _listUnit )
        {
            if ( unit._team == team && unit._bAlive == SW_TRUE )
                resetUnitTurn( unit );
        }
        pushEvent( SRPGEvent{ -1, -1, _turn, SRPGEvent::Kind::PhaseStarted, team } );
    }

    void SRPGBattlefield::advancePhase()
    {
        int32 team = static_cast<int32>( _phaseTeam );
        for ( int32 step = 0; step < kSRPGTeamCount; ++step )
        {
            team = ( team + 1 ) % kSRPGTeamCount;
            if ( team == 0 )
            {
                ++_turn;
                pushEvent( SRPGEvent{ -1, -1, _turn, SRPGEvent::Kind::TurnStarted, SRPGTeam::Player } );
            }
            if ( countAlive( static_cast<SRPGTeam>( team ) ) > 0 )
            {
                startPhase( static_cast<SRPGTeam>( team ) );
                return;
            }
        }
    }

    void SRPGBattlefield::activateNextUnit()
    {
        _activeUnit = -1;
        while ( _turnOrder.getActorCount() > 0 )
        {
            const int32 unitIndex = _turnOrder.next();
            SRPGUnit*   pUnit     = findUnit( unitIndex );
            if ( _turnOrder.getRound() > _turn )
            {
                _turn = _turnOrder.getRound();
                pushEvent( SRPGEvent{ -1, -1, _turn, SRPGEvent::Kind::TurnStarted, SRPGTeam::Player } );
            }
            if ( pUnit == nullptr || pUnit->_bAlive == SW_FALSE )
            {
                _turnOrder.removeActor( unitIndex );
                continue;
            }
            resetUnitTurn( *pUnit );
            _activeUnit = unitIndex;
            _phaseTeam  = pUnit->_team;
            pushEvent( SRPGEvent{ unitIndex, -1, _turn, SRPGEvent::Kind::UnitTurnStarted, pUnit->_team } );
            return;
        }
    }

    void SRPGBattlefield::resetUnitTurn( SRPGUnit& unit )
    {
        unit._bMoved       = SW_FALSE;
        unit._bAttacked    = SW_FALSE;
        unit._bActed       = SW_FALSE;
        unit._bSupportUsed = SW_FALSE;
        unit._dodge        = 0;
    }

    void SRPGBattlefield::refillUnit( SRPGUnit& unit )
    {
        unit._hp = unit._pDef->_hp;
        unit._en = unit._pDef->_en;
        unit._listWeapon.clear();
        unit._listAmmo.clear();
        for ( const hashed_string& weaponID : unit._pDef->_listWeaponID )
        {
            const SRPGWeaponDef* pWeapon = _pCatalog->findWeapon( weaponID );
            if ( pWeapon == nullptr )
                continue;
            unit._listWeapon.push_back( pWeapon );
            unit._listAmmo.push_back( pWeapon->_ammo );
        }
    }

    bool SRPGBattlefield::isPhaseFinished() const
    {
        for ( const SRPGUnit& unit : _listUnit )
        {
            if ( unit._team == _phaseTeam && unit._bAlive == SW_TRUE && unit._bActed == SW_FALSE )
                return false;
        }
        return true;
    }

    int32 SRPGBattlefield::computeTerrainCost( const SRPGUnit& unit, const int2& cell ) const
    {
        const SRPGTerrainDef* pTerrain = findTerrainAt( cell );
        if ( pTerrain == nullptr || unit._pDef == nullptr )
            return -1;
        const int32 cost = pTerrain->_arrMoveCost[static_cast<size_t>( unit._pDef->_moveType )];
        if ( cost < 0 || computeAptitude( unit, cell ) <= 0 )
            return -1;
        return cost;
    }

    bool SRPGBattlefield::isInEnemyZone( const SRPGUnit& unit, const int2& cell ) const
    {
        for ( const SRPGUnit& other : _listUnit )
        {
            if ( other._bAlive == SW_TRUE && isHostile( other._team, unit._team ) && computeDistance( other._cell, cell ) == 1 )
                return true;
        }
        return false;
    }

    void SRPGBattlefield::computeMoveRange( int32 unitIndex, GridReachability& outReach ) const
    {
        const SRPGUnit* pUnit = findUnit( unitIndex );
        if ( pUnit == nullptr || pUnit->_bAlive == SW_FALSE )
        {
            outReach.compute( _topology._width, _topology._height, int2{ -1, -1 }, 0, []( const int2&, const int2& )
            { return -1; }, []( const int2& )
            { return false; } );
            return;
        }
        const SRPGUnit& unit  = *pUnit;
        const int2      start = unit._cell;
        const bool      bZoc  = _settings._bZoneOfControl == SW_TRUE;
        outReach.compute(
            _topology._width, _topology._height, start, unit._pDef->_move,
            [&]( const int2& fromCell, const int2& toCell )
        {
            const int32 occupant = findUnitAt( toCell );
            if ( occupant >= 0 && isHostile( _listUnit[static_cast<size_t>( occupant )]._team, unit._team ) )
                return -1; // 적은 막는다
            if ( bZoc && fromCell != start && isInEnemyZone( unit, fromCell ) )
                return -1; // 적과 이웃한 칸에 들어오면 거기서 멈춘다
            return computeTerrainCost( unit, toCell );
        },
            [&]( const int2& cell )
        {
            const int32 occupant = findUnitAt( cell );
            return occupant < 0 || occupant == unitIndex; // 아군 칸은 지나가기만
        } );
    }

    bool SRPGBattlefield::moveUnit( int32 unitIndex, const int2& cell )
    {
        if ( canAct( unitIndex ) == false )
            return false;
        SRPGUnit& unit = _listUnit[static_cast<size_t>( unitIndex )];
        if ( unit._bMoved == SW_TRUE || unit._bAttacked == SW_TRUE )
            return false;
        if ( cell == unit._cell )
            return true;
        GridReachability reach;
        computeMoveRange( unitIndex, reach );
        vector<int2> listPath;
        if ( reach.isReachable( cell ) == false || reach.makePath( cell, listPath ) == false )
            return false;
        const int32 steps = static_cast<int32>( listPath.size() ) - 1;
        unit._cell        = cell;
        unit._bMoved      = SW_TRUE;
        if ( _settings._bMoveDodge == SW_TRUE )
            unit._dodge = MathUtil::min( _settings._dodgeMax, steps * _settings._dodgePerCell );
        pushEvent( SRPGEvent{ unitIndex, -1, steps, SRPGEvent::Kind::Moved, unit._team } );
        return true;
    }

    void SRPGBattlefield::collectThreatCells( int32 unitIndex, vector<int2>& outListCell ) const
    {
        outListCell.clear();
        const SRPGUnit* pUnit = findUnit( unitIndex );
        if ( pUnit == nullptr || pUnit->_bAlive == SW_FALSE )
            return;
        vector<int2> listStand;
        if ( pUnit->_bMoved == SW_FALSE )
        {
            GridReachability reach;
            computeMoveRange( unitIndex, reach );
            reach.collectReachable( listStand );
        }
        // 한 칸을 한 번만 — 표시는 재사용 스크래치에(호출마다 W × H 를 잡지 않는다).
        _cellMarks.begin( _topology.getCellCount() );
        vector<int2> listRange;
        for ( int32 weaponIndex = 0; weaponIndex < static_cast<int32>( pUnit->_listWeapon.size() ); ++weaponIndex )
        {
            const SRPGWeaponDef& weapon     = *pUnit->_listWeapon[static_cast<size_t>( weaponIndex )];
            const bool           bPostMove  = weapon._bPostMove == SW_TRUE && pUnit->_bMoved == SW_FALSE;
            const bool           bAfterMove = pUnit->_bMoved == SW_TRUE;
            if ( weapon.isMap() || computeWeaponStatus( *pUnit, weaponIndex, bAfterMove ) != SRPGWeaponStatus::Ok )
                continue;
            const size_t standCount = bPostMove ? listStand.size() : 1;
            for ( size_t standIndex = 0; standIndex < standCount; ++standIndex )
            {
                const int2& stand = bPostMove ? listStand[standIndex] : pUnit->_cell;
                GridReachability::collectRangeCells( stand, weapon._minRange, weapon._maxRange, _topology._width, _topology._height, listRange );
                for ( const int2& cell : listRange )
                {
                    if ( _cellMarks.visit( _topology.toIndex( cell ), -1 ) )
                        outListCell.push_back( cell );
                }
            }
        }
    }

    const SRPGWeaponDef* SRPGBattlefield::findWeapon( const SRPGUnit& unit, int32 weaponIndex ) const
    {
        if ( weaponIndex < 0 || weaponIndex >= static_cast<int32>( unit._listWeapon.size() ) )
            return nullptr;
        return unit._listWeapon[static_cast<size_t>( weaponIndex )];
    }

    SRPGWeaponStatus SRPGBattlefield::computeWeaponStatus( const SRPGUnit& unit, int32 weaponIndex, bool bAfterMove ) const
    {
        const SRPGWeaponDef* pWeapon = findWeapon( unit, weaponIndex );
        if ( pWeapon == nullptr )
            return SRPGWeaponStatus::InvalidWeapon;
        if ( unit._en < pWeapon->_enCost )
            return SRPGWeaponStatus::NotEnoughEnergy;
        if ( pWeapon->usesAmmo() && unit._listAmmo[static_cast<size_t>( weaponIndex )] <= 0 )
            return SRPGWeaponStatus::NoAmmo;
        if ( unit._morale < pWeapon->_moraleRequired )
            return SRPGWeaponStatus::LowMorale;
        if ( bAfterMove && pWeapon->_bPostMove == SW_FALSE )
            return SRPGWeaponStatus::NotAfterMove;
        return SRPGWeaponStatus::Ok;
    }

    bool SRPGBattlefield::isInWeaponRange( const SRPGWeaponDef& weapon, const int2& fromCell, const int2& toCell )
    {
        const int32 distance = computeDistance( fromCell, toCell );
        return weapon._minRange <= distance && distance <= weapon._maxRange;
    }

    void SRPGBattlefield::consumeWeapon( int32 unitIndex, int32 weaponIndex )
    {
        SRPGUnit* pUnit = findUnit( unitIndex );
        if ( pUnit == nullptr )
            return;
        const SRPGWeaponDef* pWeapon = findWeapon( *pUnit, weaponIndex );
        if ( pWeapon == nullptr )
            return;
        pUnit->_en = MathUtil::max( 0, pUnit->_en - pWeapon->_enCost );
        if ( pWeapon->usesAmmo() )
        {
            int32& ammo = pUnit->_listAmmo[static_cast<size_t>( weaponIndex )];
            ammo        = MathUtil::max( 0, ammo - 1 );
        }
    }

    bool SRPGBattlefield::collectMapCells( int32 unitIndex, int32 weaponIndex, const int2& aimCell, vector<int2>& outListCell ) const
    {
        outListCell.clear();
        const SRPGUnit* pUnit = findUnit( unitIndex );
        if ( pUnit == nullptr )
            return false;
        const SRPGWeaponDef* pWeapon = findWeapon( *pUnit, weaponIndex );
        if ( pWeapon == nullptr || pWeapon->isMap() == false )
            return false;
        int2 anchor    = aimCell;
        int2 direction = int2{ 1, 0 };
        if ( pWeapon->_mapAnchor == SRPGMapAnchor::Self )
        {
            direction = SRPGBattlefieldInternal::computeDirection( pUnit->_cell, aimCell );
            if ( direction == int2{ 0, 0 } )
                return false;
            anchor = pUnit->_cell;
        }
        else if ( isInWeaponRange( *pWeapon, pUnit->_cell, aimCell ) == false )
        {
            return false;
        }
        for ( const int2& offset : pWeapon->_listMapOffset )
        {
            const int2 cell = anchor + SRPGBattlefieldInternal::rotateOffset( offset, direction );
            if ( isInside( cell ) )
                outListCell.push_back( cell );
        }
        return true;
    }

    bool SRPGBattlefield::applyDamage( int32 unitIndex, int32 amount, int32 attackerIndex )
    {
        SRPGUnit* pUnit = findUnit( unitIndex );
        if ( pUnit == nullptr || pUnit->_bAlive == SW_FALSE )
            return false;
        pUnit->_hp = MathUtil::max( 0, pUnit->_hp - MathUtil::max( 0, amount ) );
        if ( pUnit->_hp > 0 )
            return false;
        pUnit->_bAlive = SW_FALSE;
        if ( _settings._turnMode == SRPGTurnMode::Individual )
            _turnOrder.removeActor( unitIndex );
        pushEvent( SRPGEvent{ unitIndex, attackerIndex, 0, SRPGEvent::Kind::Destroyed, pUnit->_team } );
        return true;
    }

    void SRPGBattlefield::addMorale( int32 unitIndex, int32 delta )
    {
        SRPGUnit* pUnit = findUnit( unitIndex );
        if ( pUnit != nullptr )
            pUnit->_morale = MathUtil::clamp( pUnit->_morale + delta, _settings._moraleMin, _settings._moraleMax );
    }

    void SRPGBattlefield::grantXp( int32 unitIndex, int64 amount )
    {
        SRPGUnit* pUnit = findUnit( unitIndex );
        if ( pUnit == nullptr || _pCatalog == nullptr || amount <= 0 )
            return;
        if ( pUnit->_pilotLevel.addXp( _pCatalog->getPilotCurve(), amount ) > 0 )
            pushEvent( SRPGEvent{ unitIndex, -1, pUnit->_pilotLevel.getLevel(), SRPGEvent::Kind::PilotLevelUp, pUnit->_team } );
        if ( pUnit->_unitLevel.addXp( _pCatalog->getUnitCurve(), amount ) > 0 )
            pushEvent( SRPGEvent{ unitIndex, -1, pUnit->_unitLevel.getLevel(), SRPGEvent::Kind::UnitLevelUp, pUnit->_team } );
    }

    void SRPGBattlefield::drainEvents( vector<SRPGEvent>& outListEvent )
    {
        _eventBuffer.drainTo( outListEvent );
    }

    void SRPGBattlefield::writeState( Archive& outArchive ) const
    {
        outArchive << _topology._width;
        outArchive << _topology._height;
        for ( const SRPGTerrainDef* pTerrain : _listTerrain )
        {
            StateArchiveUtil::writeName( outArchive, pTerrain != nullptr ? pTerrain->_id : hashed_string{} );
        }
        outArchive << static_cast<uint32>( _listUnit.size() );
        for ( const SRPGUnit& unit : _listUnit )
        {
            StateArchiveUtil::writeName( outArchive, unit._pDef->_id );
            StateArchiveUtil::writeName( outArchive, unit._pPilot->_id );
            outArchive << static_cast<uint32>( unit._listAmmo.size() );
            for ( const int32 ammo : unit._listAmmo )
            {
                outArchive << ammo;
            }
            unit._pilotLevel.writeState( outArchive );
            unit._unitLevel.writeState( outArchive );
            StateArchiveUtil::writeInt2( outArchive, unit._cell );
            outArchive << unit._hp;
            outArchive << unit._en;
            outArchive << unit._morale;
            outArchive << unit._dodge;
            outArchive << unit._rosterIndex;
            outArchive << static_cast<uint8>( unit._team );
            outArchive << unit._bAlive;
            outArchive << unit._bMoved;
            outArchive << unit._bAttacked;
            outArchive << unit._bActed;
            outArchive << unit._bCommander;
            outArchive << unit._bSupportUsed;
        }
        _turnOrder.writeState( outArchive );
        StateArchiveUtil::writeRandom( outArchive, _random );
        outArchive << _turn;
        outArchive << _activeUnit;
        outArchive << static_cast<uint8>( _phaseTeam );
    }

    bool SRPGBattlefield::readState( Archive& archive )
    {
        int32 width  = 0;
        int32 height = 0;
        archive >> width;
        archive >> height;
        if ( archive.isError() || _pCatalog == nullptr || width != _topology._width || height != _topology._height )
            return false;
        // 사본에 읽고 끝까지 맞으면 바꾼다 — 카탈로그 · 설정은 사본이 그대로 든다.
        SRPGBattlefield restored = *this;
        for ( const SRPGTerrainDef*& pTerrain : restored._listTerrain )
        {
            hashed_string terrainID;
            if ( StateArchiveUtil::readName( archive, terrainID ) == false )
                return false;
            pTerrain = terrainID.empty() ? nullptr : _pCatalog->findTerrain( terrainID );
            if ( terrainID.empty() == false && pTerrain == nullptr )
                return false;
        }

        uint32 unitCount = 0;
        // 유닛마다 이름 둘(8) + 탄 수(4) + 레벨 둘(40) + 칸(8) + HP · EN · 기력 · 회피 · 명단(20) + 팀 · 비트 여섯(7)
        if ( StateArchiveUtil::readCount( archive, 87, unitCount ) == false )
            return false;
        restored._listUnit.assign( unitCount, SRPGUnit{} );
        for ( SRPGUnit& unit : restored._listUnit )
        {
            hashed_string unitID;
            hashed_string pilotID;
            uint32        ammoCount = 0;
            const bool    bHeadRead =
                StateArchiveUtil::readName( archive, unitID ) && StateArchiveUtil::readName( archive, pilotID ) && StateArchiveUtil::readCount( archive, 4, ammoCount );
            if ( bHeadRead == false )
                return false;
            unit._pDef   = _pCatalog->findUnit( unitID );
            unit._pPilot = _pCatalog->findPilot( pilotID );
            if ( unit._pDef == nullptr || unit._pPilot == nullptr )
                return false;
            // 무기는 기체 정의의 순서로 다시 짓는다 — 탄 수가 그 무기 수와 같아야 한다.
            restored.refillUnit( unit );
            if ( ammoCount != unit._listAmmo.size() )
                return false;
            for ( int32& ammo : unit._listAmmo )
            {
                archive >> ammo;
            }
            uint8      team       = 0;
            const bool bLevelRead = unit._pilotLevel.readState( archive ) && unit._unitLevel.readState( archive );
            StateArchiveUtil::readInt2( archive, unit._cell );
            archive >> unit._hp;
            archive >> unit._en;
            archive >> unit._morale;
            archive >> unit._dodge;
            archive >> unit._rosterIndex;
            archive >> team;
            archive >> unit._bAlive;
            archive >> unit._bMoved;
            archive >> unit._bAttacked;
            archive >> unit._bActed;
            archive >> unit._bCommander;
            archive >> unit._bSupportUsed;
            const bool bFlagValid = unit._bAlive <= SW_TRUE && unit._bMoved <= SW_TRUE && unit._bAttacked <= SW_TRUE && unit._bActed <= SW_TRUE &&
                                    unit._bCommander <= SW_TRUE && unit._bSupportUsed <= SW_TRUE;
            const bool bValid = bLevelRead && archive.isOk() && isInside( unit._cell ) && team < static_cast<uint8>( kSRPGTeamCount ) && bFlagValid;
            if ( bValid == false )
                return false;
            unit._team = static_cast<SRPGTeam>( team );
        }

        uint8      phaseTeam = 0;
        const bool bPartRead = restored._turnOrder.readState( archive ) && StateArchiveUtil::readRandom( archive, restored._random );
        archive >> restored._turn;
        archive >> restored._activeUnit;
        archive >> phaseTeam;
        const bool bValid = bPartRead && archive.isOk() && 0 <= restored._turn && -1 <= restored._activeUnit &&
                            restored._activeUnit < static_cast<int32>( restored._listUnit.size() ) && phaseTeam < static_cast<uint8>( kSRPGTeamCount );
        if ( bValid == false )
            return false;
        restored._phaseTeam = static_cast<SRPGTeam>( phaseTeam );
        restored._eventBuffer.clear();
        *this = std::move( restored );
        return true;
    }

    void SRPGBattlefield::collectDevelopOptions( int32 unitIndex, vector<hashed_string>& outListUnitID ) const
    {
        outListUnitID.clear();
        const SRPGUnit* pUnit = findUnit( unitIndex );
        if ( pUnit == nullptr || _pCatalog == nullptr )
            return;
        for ( const SRPGDevelopTarget& target : pUnit->_pDef->_listDevelop )
        {
            if ( pUnit->_unitLevel.getLevel() >= target._requiredLevel && _pCatalog->findUnit( target._unitID ) != nullptr )
                outListUnitID.push_back( target._unitID );
        }
    }

    bool SRPGBattlefield::developUnit( int32 unitIndex, const hashed_string& targetUnitID )
    {
        vector<hashed_string> listOption;
        collectDevelopOptions( unitIndex, listOption );
        bool bAllowed = false;
        for ( const hashed_string& option : listOption )
        {
            bAllowed = bAllowed || option == targetUnitID;
        }
        if ( bAllowed == false )
            return false;
        SRPGUnit& unit = _listUnit[static_cast<size_t>( unitIndex )];
        unit._pDef     = _pCatalog->findUnit( targetUnitID );
        unit._unitLevel.setLevel( _pCatalog->getUnitCurve(), 1 );
        refillUnit( unit );
        pushEvent( SRPGEvent{ unitIndex, -1, 0, SRPGEvent::Kind::Developed, unit._team } );
        return true;
    }

    int32 SRPGBattlefield::computeDistance( const int2& lhs, const int2& rhs ) { return MathUtil::abs( lhs._x - rhs._x ) + MathUtil::abs( lhs._y - rhs._y ); }

    const SRPGTerrainDef* SRPGBattlefield::findTerrainAt( const int2& cell ) const
    {
        return isInside( cell ) ? _listTerrain[static_cast<size_t>( _topology.toIndex( cell ) )] : nullptr;
    }

    int32 SRPGBattlefield::findUnitAt( const int2& cell ) const
    {
        for ( size_t index = 0; index < _listUnit.size(); ++index )
        {
            if ( _listUnit[index]._bAlive == SW_TRUE && _listUnit[index]._cell == cell )
                return static_cast<int32>( index );
        }
        return -1;
    }

    int32 SRPGBattlefield::computeAptitude( const SRPGUnit& unit, const int2& cell ) const
    {
        const SRPGTerrainDef* pTerrain = findTerrainAt( cell );
        if ( pTerrain == nullptr || unit._pDef == nullptr )
            return 0;
        SRPGMoveType domain       = pTerrain->_domain;
        const bool   bFlyingAbove = unit._pDef->_moveType == SRPGMoveType::Air && ( domain == SRPGMoveType::Ground || domain == SRPGMoveType::Water );
        if ( bFlyingAbove )
            domain = SRPGMoveType::Air; // 땅 · 물 위를 나는 유닛은 공중 적성으로 싸운다
        return unit._pDef->_arrAptitude[static_cast<size_t>( domain )];
    }

    int32 SRPGBattlefield::countAlive( SRPGTeam team ) const
    {
        int32 count = 0;
        for ( const SRPGUnit& unit : _listUnit )
        {
            count += unit._team == team && unit._bAlive == SW_TRUE ? 1 : 0;
        }
        return count;
    }

    SRPGUnit* SRPGBattlefield::findUnit( int32 unitIndex )
    {
        return 0 <= unitIndex && unitIndex < static_cast<int32>( _listUnit.size() ) ? &_listUnit[static_cast<size_t>( unitIndex )] : nullptr;
    }

    const SRPGUnit* SRPGBattlefield::findUnit( int32 unitIndex ) const
    {
        return 0 <= unitIndex && unitIndex < static_cast<int32>( _listUnit.size() ) ? &_listUnit[static_cast<size_t>( unitIndex )] : nullptr;
    }
} // namespace sw
