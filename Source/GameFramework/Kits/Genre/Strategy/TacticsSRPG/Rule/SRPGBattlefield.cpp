#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGBattlefield.h"

#include "Core/Foundation/Constants.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Runtime/Character/Navigation/GridReachability.h"
#include "GameFramework/Runtime/Foundation/GameData/DepletingValue.h"

namespace sw
{
    namespace
    {
        struct SRPGBattlefieldInternal
        {
            /** @brief 동쪽(+x)을 보는 패턴 셀을 @p direction 쪽으로 돌립니다. */
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

            /** @brief 쏘는 셀에서 겨눈 셀로 가는 주된 방향(네 방향 중 하나)을 반환합니다. 같은 셀이면 (0, 0) 입니다. */
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
        , _activeUnit{ constant::invalid_index::kInt32 }
        , _phaseTeam{ SRPGTeam::Player }
    {
    }

    void SRPGBattlefield::initialize( const SRPGCatalog* pCatalog, int32 width, int32 height, const hashed_string& defaultTerrain, const SRPGSettings& settings,
                                      uint32 seed )
    {
        _pCatalog = pCatalog;
        _topology = GridTopology{ width, height }; // 음수는 0 셀로 처리한다
        _settings = settings;
        _random.setSeed( seed );
        const SRPGTerrainDef* pTerrain = pCatalog != nullptr ? pCatalog->findTerrain( defaultTerrain ) : nullptr;
        if ( pTerrain == nullptr )
            SW_LOG_WARNING_ID( "SRPGBattlefield.UnknownDefaultTerrainCells", "unknown default terrain '%#' - cells are impassable until painted", defaultTerrain.c_str() );
        _listTerrain.assign( static_cast<size_t>( _topology.getCellCount() ), pTerrain );
        _listUnit.clear();
        _eventBuffer.clear();
        _turn       = 0;
        _activeUnit = constant::invalid_index::kInt32;
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
            return constant::invalid_index::kInt32;
        SRPGUnit unit;
        unit._pDef   = _pCatalog->findUnit( unitID );
        unit._pPilot = _pCatalog->findPilot( pilotID );
        if ( unit._pDef == nullptr || unit._pPilot == nullptr )
        {
            SW_LOG_WARNING_ID( "SRPGBattlefield.CannotPlaceUnitPilot", "cannot place unit '%#' with pilot '%#' - unknown id", unitID.c_str(), pilotID.c_str() );
            return constant::invalid_index::kInt32;
        }
        const bool bCannotPlaceUnit = isInside( cell ) == false || findUnitAt( cell ).has_value() || computeTerrainCost( unit, cell ) < 0;
        if ( bCannotPlaceUnit )
            return constant::invalid_index::kInt32;
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
        _activeUnit = constant::invalid_index::kInt32;
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
        pushEvent( SRPGEvent{ constant::invalid_index::kInt32, constant::invalid_index::kInt32, _turn, SRPGEvent::Kind::TurnStarted, SRPGTeam::Player } );
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
        const SRPGUnit* pUnit      = findUnit( unitIndex );
        const bool      bCannotAct = pUnit == nullptr || _turn <= 0 || pUnit->_bAlive == SW_FALSE || pUnit->_bActed == SW_TRUE;
        if ( bCannotAct )
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
        // 반격에 격파된 유닛도 차례는 넘긴다. 죽은 유닛은 canAct 가 false 라 위에서 걸러진다.
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
        pushEvent( SRPGEvent{ constant::invalid_index::kInt32, constant::invalid_index::kInt32, _turn, SRPGEvent::Kind::PhaseStarted, team } );
    }

    void SRPGBattlefield::advancePhase()
    {
        // 승부가 났으면 넘기지 않는다 — 빈 팀 페이즈를 건너뛰다 한 바퀴 돌아 턴이 오른 뒤에 승리를 판정했다(kitdemo_tactics).
        if ( isBattleDecided() )
            return;
        int32 team = static_cast<int32>( _phaseTeam );
        for ( int32 step = 0; step < kSRPGTeamCount; ++step )
        {
            team = ( team + 1 ) % kSRPGTeamCount;
            if ( team == 0 )
            {
                ++_turn;
                pushEvent( SRPGEvent{ constant::invalid_index::kInt32, constant::invalid_index::kInt32, _turn, SRPGEvent::Kind::TurnStarted, SRPGTeam::Player } );
            }
            if ( countAlive( static_cast<SRPGTeam>( team ) ) > 0 )
            {
                startPhase( static_cast<SRPGTeam>( team ) );
                return;
            }
        }
    }

    bool SRPGBattlefield::isBattleDecided() const
    {
        int32 aliveTeamCount = 0;
        for ( int32 teamIndex = 0; teamIndex < kSRPGTeamCount; ++teamIndex )
        {
            if ( countAlive( static_cast<SRPGTeam>( teamIndex ) ) > 0 )
                ++aliveTeamCount;
        }
        return aliveTeamCount <= 1;
    }

    void SRPGBattlefield::activateNextUnit()
    {
        if ( isBattleDecided() )
            return; // 페이즈 진행과 같은 규칙 — 승부가 난 뒤로는 차례 · 턴이 넘어가지 않는다
        _activeUnit = constant::invalid_index::kInt32;
        while ( _turnOrder.getActorCount() > 0 )
        {
            const int32 unitIndex = _turnOrder.next();
            SRPGUnit*   pUnit     = findUnit( unitIndex );
            if ( _turnOrder.getRound() > _turn )
            {
                _turn = _turnOrder.getRound();
                pushEvent( SRPGEvent{ constant::invalid_index::kInt32, constant::invalid_index::kInt32, _turn, SRPGEvent::Kind::TurnStarted, SRPGTeam::Player } );
            }
            if ( pUnit == nullptr || pUnit->_bAlive == SW_FALSE )
            {
                _turnOrder.removeActor( unitIndex );
                continue;
            }
            resetUnitTurn( *pUnit );
            _activeUnit = unitIndex;
            _phaseTeam  = pUnit->_team;
            pushEvent( SRPGEvent{ unitIndex, constant::invalid_index::kInt32, _turn, SRPGEvent::Kind::UnitTurnStarted, pUnit->_team } );
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
            const bool bUnitStillPending = unit._team == _phaseTeam && unit._bAlive == SW_TRUE && unit._bActed == SW_FALSE;
            if ( bUnitStillPending )
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
            const bool bHostileAdjacent = other._bAlive == SW_TRUE && isHostile( other._team, unit._team ) && computeDistance( other._cell, cell ) == 1;
            if ( bHostileAdjacent )
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
            const optional<int32> occupant = findUnitAt( toCell );
            if ( occupant.has_value() && isHostile( _listUnit[static_cast<size_t>( *occupant )]._team, unit._team ) )
                return -1; // 적이 선 셀은 막는다
            const bool bZocStop = bZoc && fromCell != start && isInEnemyZone( unit, fromCell );
            if ( bZocStop )
                return -1; // 적과 이웃한 셀에 들어오면 거기서 멈춘다
            return computeTerrainCost( unit, toCell );
        },
            [&]( const int2& cell )
        {
            const optional<int32> occupant = findUnitAt( cell );
            return occupant.has_value() == false || occupant == unitIndex; // 아군이 선 셀은 지나가기만 한다
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
        pushEvent( SRPGEvent{ unitIndex, constant::invalid_index::kInt32, steps, SRPGEvent::Kind::Moved, unit._team } );
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
        // 한 셀을 한 번만 처리한다. 표시는 재사용 스크래치에 두어 호출마다 W x H 를 할당하지 않는다.
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
                    if ( _cellMarks.visit( _topology.toIndex( cell ), constant::invalid_index::kInt32 ) )
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
        if ( DepletingValue::take( pUnit->_hp, amount )._bDepleted == SW_FALSE )
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

    void SRPGBattlefield::grantXP( int32 unitIndex, int64 amount )
    {
        SRPGUnit*  pUnit          = findUnit( unitIndex );
        const bool bCannotGrantXP = pUnit == nullptr || _pCatalog == nullptr || amount <= 0;
        if ( bCannotGrantXP )
            return;
        if ( pUnit->_pilotLevel.addXP( _pCatalog->getPilotCurve(), amount ) > 0 )
            pushEvent( SRPGEvent{ unitIndex, constant::invalid_index::kInt32, pUnit->_pilotLevel.getLevel(), SRPGEvent::Kind::PilotLevelUp, pUnit->_team } );
        if ( pUnit->_unitLevel.addXP( _pCatalog->getUnitCurve(), amount ) > 0 )
            pushEvent( SRPGEvent{ unitIndex, constant::invalid_index::kInt32, pUnit->_unitLevel.getLevel(), SRPGEvent::Kind::UnitLevelUp, pUnit->_team } );
    }

    void SRPGBattlefield::pollEvents( const ConsumerLease& lease, vector<SRPGEvent>& outListEvent )
    {
        _eventBuffer.moveAllTo( lease, outListEvent );
    }

    void SRPGBattlefield::serializeState( StateIO& io )
    {
        io.expect( _topology._width );
        io.expect( _topology._height );
        if ( io.check( _pCatalog != nullptr ) == false )
            return;
        for ( const SRPGTerrainDef*& pTerrain : _listTerrain )
        {
            io.reference( pTerrain, [this]( const hashed_string& terrainID )
            { return _pCatalog->findTerrain( terrainID ); } );
        }
        // 유닛마다 이름 둘(8) + 탄 수(4) + 레벨 둘(40) + 셀(8) + HP, EN, 기력, 회피, 명단(20) + 팀, 비트 여섯(7)
        io.list( _listUnit, 87, [&]( SRPGUnit& unit )
        {
            hashed_string unitID    = unit._pDef != nullptr ? unit._pDef->_id : hashed_string{};
            hashed_string pilotID   = unit._pPilot != nullptr ? unit._pPilot->_id : hashed_string{};
            uint32        ammoCount = static_cast<uint32>( unit._listAmmo.size() );
            io.value( unitID );
            io.value( pilotID );
            io.count( 4, ammoCount );
            if ( io.isReading() )
            {
                unit._pDef   = _pCatalog->findUnit( unitID );
                unit._pPilot = _pCatalog->findPilot( pilotID );
                if ( io.check( unit._pDef != nullptr && unit._pPilot != nullptr ) == false )
                    return;
                // 무기는 기체 정의의 순서로 다시 만든다. 탄 수가 그 무기 수와 같아야 한다.
                refillUnit( unit );
                if ( io.check( ammoCount == unit._listAmmo.size() ) == false )
                    return;
            }
            for ( int32& ammo : unit._listAmmo )
            {
                io.value( ammo );
            }
            io.value( unit._pilotLevel );
            io.value( unit._unitLevel );
            io.value( unit._cell );
            io.value( unit._hp );
            io.value( unit._en );
            io.value( unit._morale );
            io.value( unit._dodge );
            io.value( unit._rosterIndex );
            io.valueAs<uint8>( unit._team );
            io.flag( unit._bAlive );
            io.flag( unit._bMoved );
            io.flag( unit._bAttacked );
            io.flag( unit._bActed );
            io.flag( unit._bCommander );
            io.flag( unit._bSupportUsed );
            io.check( 0 <= unit._hp && 0 <= unit._en && 0 <= unit._morale && 0 <= unit._dodge && -1 <= unit._rosterIndex && isInside( unit._cell ) && static_cast<uint8>( unit._team ) < static_cast<uint8>( kSRPGTeamCount ) );
        } );
        io.value( _turnOrder );
        io.value( _random );
        io.value( _turn );
        io.value( _activeUnit );
        io.valueAs<uint8>( _phaseTeam );
        io.check( 0 <= _turn && -1 <= _activeUnit && _activeUnit < static_cast<int32>( _listUnit.size() ) && static_cast<uint8>( _phaseTeam ) < static_cast<uint8>( kSRPGTeamCount ) );
        if ( io.isReading() )
        {
            _eventBuffer.clear();
        }
    }

    void SRPGBattlefield::collectDevelopOptions( int32 unitIndex, vector<hashed_string>& outListUnitID ) const
    {
        outListUnitID.clear();
        const SRPGUnit* pUnit = findUnit( unitIndex );
        // 격파된 유닛을 개발하면 HP 가 가득 찬 채 죽어 있는 유닛이 된다.
        if ( pUnit == nullptr || _pCatalog == nullptr || pUnit->_bAlive == SW_FALSE )
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
        pushEvent( SRPGEvent{ unitIndex, constant::invalid_index::kInt32, 0, SRPGEvent::Kind::Developed, unit._team } );
        return true;
    }

    int32 SRPGBattlefield::computeDistance( const int2& lhs, const int2& rhs )
    {
        return MathUtil::abs( lhs._x - rhs._x ) + MathUtil::abs( lhs._y - rhs._y );
    }

    const SRPGTerrainDef* SRPGBattlefield::findTerrainAt( const int2& cell ) const
    {
        return isInside( cell ) ? _listTerrain[static_cast<size_t>( _topology.toIndex( cell ) )] : nullptr;
    }

    optional<int32> SRPGBattlefield::findUnitAt( const int2& cell ) const
    {
        for ( size_t index = 0; index < _listUnit.size(); ++index )
        {
            if ( _listUnit[index]._bAlive == SW_TRUE && _listUnit[index]._cell == cell )
                return static_cast<int32>( index );
        }
        return nullopt;
    }

    int32 SRPGBattlefield::computeAptitude( const SRPGUnit& unit, const int2& cell ) const
    {
        const SRPGTerrainDef* pTerrain = findTerrainAt( cell );
        if ( pTerrain == nullptr || unit._pDef == nullptr )
            return 0;
        SRPGMoveType domain       = pTerrain->_domain;
        const bool   bFlyingAbove = unit._pDef->_moveType == SRPGMoveType::Air && ( domain == SRPGMoveType::Ground || domain == SRPGMoveType::Water );
        if ( bFlyingAbove )
            domain = SRPGMoveType::Air; // 땅이나 물 위를 나는 유닛은 공중 적성으로 싸운다
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

    int32 SRPGBattlefield::computeNearestHostileDistance( const SRPGUnit& unit, const int2& cell ) const
    {
        int32 nearest = constant::invalid_index::kInt32;
        for ( const SRPGUnit& other : getUnits() )
        {
            if ( other._bAlive == SW_FALSE || isHostile( unit._team, other._team ) == false )
                continue;
            const int32 distance = computeDistance( other._cell, cell );
            if ( nearest < 0 || distance < nearest )
                nearest = distance;
        }
        return nearest;
    }
} // namespace sw
