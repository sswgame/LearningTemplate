#include "pch.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Component/SRPGGameModeComponent.h"

#include "Core/Encoding/FourCCUtil.h"
#include "Core/String/fixed_string.h"
#include "Core/String/formatString.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/SRPGCombat.h"

namespace sw
{
    namespace
    {
        struct SRPGGameModeComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDTS" );
            static constexpr uint32 kStateVersion = 3; // 3: 커서가 클라이언트 모듈(`SRPGCursorComponent`)로 가 디렉터 상태 칸에서 빠졌다
            static constexpr int32  kNoSelection  = -1;
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( srpgPhaseTeam, "SRPG.PhaseTeam", "Team whose phase it is (0 player, 1 enemy, 2 third)", SRPGGameModeComponent, &SRPGGameModeComponent::getPhaseTeam );
    SW_AUTOMATION_GETTER_WATCH( srpgTurn, "SRPG.Turn", "Battle turn (1 = first)", SRPGGameModeComponent, &SRPGGameModeComponent::getTurn );
    SW_AUTOMATION_GETTER_WATCH( srpgResult, "SRPG.Result", "Battle result (0 ongoing, 1 victory, 2 defeat)", SRPGGameModeComponent, &SRPGGameModeComponent::getResult );
    SW_AUTOMATION_GETTER_WATCH( srpgSelected, "SRPG.Selected", "Selected unit slot (-1 = none)", SRPGGameModeComponent, &SRPGGameModeComponent::getSelectedUnit );
    SW_AUTOMATION_GETTER_WATCH( srpgEnemies, "SRPG.EnemiesAlive", "Living enemy units", SRPGGameModeComponent, &SRPGGameModeComponent::getEnemiesAlive );
    SW_AUTOMATION_GETTER_WATCH( srpgPlayers, "SRPG.PlayersAlive", "Living player units", SRPGGameModeComponent, &SRPGGameModeComponent::getPlayersAlive );
    SW_AUTOMATION_GETTER_WATCH( srpgLevelUps, "SRPG.LevelUps", "Level-ups left in the battle growth log (pilot and unit)", SRPGGameModeComponent, &SRPGGameModeComponent::getLevelUps );

    SRPGGameModeComponent::SRPGGameModeComponent()
        : _catalogPath{}
        , _width{ 6 }
        , _height{ 3 }
        , _defaultTerrain{}
        , _seed{ 7 }
        , _listPlayerUnit{}
        , _listPlayerCell{}
        , _listEnemyUnit{}
        , _listEnemyCell{}
        , _pilot{}
        , _pCatalog{}
        , _field{}
        , _ai{}
        , _eventLease{}
        , _listEvent{}
        , _selectedUnit{ SRPGGameModeComponentInternal::kNoSelection }
        , _result{ SRPGBattleResult::Ongoing }
        , _growthLog{}
    {
    }

    SRPGGameModeComponent::~SRPGGameModeComponent() = default;

    bool SRPGGameModeComponent::openField( SRPGBattlefield& outField ) const
    {
        const bool bPlayersMatch = _listPlayerUnit.size() == _listPlayerCell.size();
        const bool bEnemiesMatch = _listEnemyUnit.size() == _listEnemyCell.size();
        if ( bPlayersMatch == false || bEnemiesMatch == false || _listPlayerUnit.empty() || _listEnemyUnit.empty() )
        {
            SW_LOG_ERROR_ID( "SRPG.LayoutMismatch", "SRPG game mode: unit and cell lists must be non-empty and of equal length (player %# / %#, enemy %# / %#) - the battle does not open",
                             _listPlayerUnit.size(), _listPlayerCell.size(), _listEnemyUnit.size(), _listEnemyCell.size() );
            return false;
        }
        outField.initialize( *_pCatalog, _width, _height, _defaultTerrain.key(), _seed ); // 규칙은 카탈로그의 `<Rules>` 다
        for ( size_t index = 0; index < _listPlayerUnit.size() + _listEnemyUnit.size(); ++index )
        {
            const bool            bPlayer   = index < _listPlayerUnit.size();
            const size_t          slot      = bPlayer ? index : index - _listPlayerUnit.size();
            const hashed_string&  unitID    = bPlayer ? _listPlayerUnit[slot].key() : _listEnemyUnit[slot].key();
            const int2            cell      = bPlayer ? _listPlayerCell[slot] : _listEnemyCell[slot];
            const expected<int32> unitIndex = outField.addUnit( unitID, _pilot.key(), bPlayer ? SRPGTeam::Player : SRPGTeam::Enemy, cell );
            if ( unitIndex.has_value() == false )
            {
                SW_LOG_ERROR_ID( "SRPG.UnitNotPlaced", "SRPG game mode: unit '%#' with pilot '%#' cannot stand at (%#, %#) - unknown id, outside the field, taken or impassable; the battle does not open",
                                 unitID.c_str(), _pilot.c_str(), cell._x, cell._y );
                return false;
            }
        }
        return true;
    }

    bool SRPGGameModeComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        if ( openField( _field ) == false )
            return false;
        _field.beginBattle();
        if ( _eventLease.isHeld() == false && _field.claimEvents( _eventLease ) == false )
            return false; // 이 디렉터의 표뿐이라 일어날 수 없다 — 둘째 소비자가 생기면 게이트가 어서션을 남긴다
        _selectedUnit = SRPGGameModeComponentInternal::kNoSelection;
        _result       = SRPGBattleResult::Ongoing;
        _listEvent.clear();
        _growthLog.clear();
        return true;
    }

    void SRPGGameModeComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "SRPG.Started", "[SRPG] battle: %# against %# - the player phase waits for commands (confirm cell, end phase)", getPlayersAlive(), getEnemiesAlive() );
    }

    bool SRPGGameModeComponent::isAwaitingCommand() const
    {
        return isStarted() && _result == SRPGBattleResult::Ongoing && _field.getPhaseTeam() == SRPGTeam::Player;
    }

    bool SRPGGameModeComponent::attackCell( int2 cell, int32 targetIndex )
    {
        const SRPGUnit* pUnit = _field.findUnit( _selectedUnit );
        for ( int32 weaponIndex = 0; pUnit != nullptr && weaponIndex < static_cast<int32>( pUnit->_listWeapon.size() ); ++weaponIndex )
        {
            const SRPGWeaponDef* pWeapon = _field.findWeapon( *pUnit, weaponIndex );
            const bool           bUsable = pWeapon != nullptr && _field.computeWeaponStatus( *pUnit, weaponIndex, pUnit->_bMoved == SW_TRUE ) == SRPGWeaponStatus::Ok &&
                                 SRPGBattlefield::isInWeaponRange( *pWeapon, pUnit->_cell, cell );
            if ( bUsable == false )
                continue;
            SRPGCombatResult result;
            if ( SRPGCombat::executeAttack( _field, _selectedUnit, weaponIndex, targetIndex, result ) != SRPGWeaponStatus::Ok )
                return false;
            _field.endUnitAction( _selectedUnit );
            _selectedUnit = SRPGGameModeComponentInternal::kNoSelection;
            return true;
        }
        return false;
    }

    bool SRPGGameModeComponent::confirmCell( int2 cell )
    {
        if ( isAwaitingCommand() == false || _field.isInside( cell ) == false )
            return false;
        const optional<int32> cellUnit = _field.findUnitAt( cell );
        if ( _selectedUnit < 0 )
        {
            const SRPGUnit* pUnit       = cellUnit.has_value() ? _field.findUnit( *cellUnit ) : nullptr;
            const bool      bSelectable = pUnit != nullptr && pUnit->_team == SRPGTeam::Player && _field.canAct( *cellUnit );
            if ( bSelectable )
                _selectedUnit = *cellUnit;
            return bSelectable;
        }
        const SRPGUnit* pSelected = _field.findUnit( _selectedUnit );
        const SRPGUnit* pTarget   = cellUnit.has_value() ? _field.findUnit( *cellUnit ) : nullptr;
        bool            bChanged  = false;
        if ( cellUnit == _selectedUnit )
        {
            // 제자리에서 다시 고르면 대기 — 행동을 끝낸다.
            _field.endUnitAction( _selectedUnit );
            _selectedUnit = SRPGGameModeComponentInternal::kNoSelection;
            bChanged      = true;
        }
        else if ( pTarget != nullptr && pSelected != nullptr && SRPGBattlefield::isHostile( pSelected->_team, pTarget->_team ) )
        {
            bChanged = attackCell( cell, *cellUnit );
            if ( bChanged == false )
                SW_LOG_INFO_ID( "SRPG.CannotAttack", "[SRPG] no weapon reaches (%#, %#) from (%#, %#)", cell._x, cell._y, pSelected->_cell._x, pSelected->_cell._y );
        }
        else if ( cellUnit.has_value() == false )
        {
            bChanged = _field.moveUnit( _selectedUnit, cell );
            if ( bChanged == false )
                SW_LOG_INFO_ID( "SRPG.CannotMove", "[SRPG] the unit cannot move to (%#, %#)", cell._x, cell._y );
        }
        settle();
        return bChanged;
    }

    bool SRPGGameModeComponent::endPlayerPhase()
    {
        if ( isAwaitingCommand() == false )
            return false;
        _selectedUnit = SRPGGameModeComponentInternal::kNoSelection;
        _field.endPhase();
        settle();
        return true;
    }

    void SRPGGameModeComponent::runAIPhase()
    {
        const SRPGTeam team = _field.getPhaseTeam();
        (void)_ai.runPhase( _field, team );
        // 움직일 수 없는 유닛만 남았으면(이동 0 · 칠 적 없음) AI 가 멈춘다 — 페이즈를 넘긴다.
        if ( _field.getPhaseTeam() == team )
            _field.endPhase();
    }

    void SRPGGameModeComponent::settle()
    {
        _listEvent.clear();
        _field.pollEvents( _eventLease, _listEvent );
        fixed_string<constant::buffer::kMax64> cause; // 레벨 오름의 출처 — 같은 이벤트 묶음에서 그 앞의 명중 · 격파다(전장은 경험치를 그 자리에서 준다)
        for ( const SRPGEvent& event : _listEvent )
        {
            if ( event._kind == SRPGEvent::Kind::Destroyed )
                SW_LOG_INFO_ID( "SRPG.Destroyed", "[SRPG] unit %# destroyed by %#", event._unit.value(), event._other.has_value() ? to_string( *event._other ).c_str() : "nothing" );
            else if ( event._kind == SRPGEvent::Kind::PhaseStarted )
                SW_LOG_INFO_ID( "SRPG.Phase", "[SRPG] turn %# - %# phase", _field.getTurn(), static_cast<int32>( event._team ) );
            if ( event._kind == SRPGEvent::Kind::Hit )
                formatstring( cause.data(), cause.capacity(), "hit unit %#", event._other.value() );
            else if ( event._kind == SRPGEvent::Kind::Destroyed )
                formatstring( cause.data(), cause.capacity(), "destroyed unit %#", event._unit.value() );
            const bool bLevelUp = event._kind == SRPGEvent::Kind::PilotLevelUp || event._kind == SRPGEvent::Kind::UnitLevelUp;
            if ( bLevelUp )
            {
                fixed_string<constant::buffer::kMax32> subject;
                formatstring( subject.data(), subject.capacity(), "unit %#", event._unit.value() );
                _growthLog.record( GrowthEventKind::LevelUp, subject.view(), event._kind == SRPGEvent::Kind::PilotLevelUp ? "pilot" : "unit", event._value, cause.view() );
            }
        }
        if ( _result != SRPGBattleResult::Ongoing )
            return;
        if ( _field.countAlive( SRPGTeam::Enemy ) == 0 )
        {
            _result = SRPGBattleResult::Victory;
            SW_LOG_INFO_ID( "SRPG.Victory", "[SRPG] victory on turn %#", _field.getTurn() );
            return;
        }
        if ( _field.countAlive( SRPGTeam::Player ) == 0 )
        {
            _result = SRPGBattleResult::Defeat;
            SW_LOG_INFO_ID( "SRPG.Defeat", "[SRPG] defeat on turn %#", _field.getTurn() );
        }
    }

    void SRPGGameModeComponent::updateGame( float32 deltaTime )
    {
        // 아군 페이즈는 명령(`confirmCell` · `endPlayerPhase`)이 진행한다 — 디렉터는 적 · 제3세력 페이즈만 스스로 돈다.
        if ( deltaTime <= 0.0f || _result != SRPGBattleResult::Ongoing || _field.getPhaseTeam() == SRPGTeam::Player )
            return;
        runAIPhase();
        settle();
    }

    void SRPGGameModeComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 디렉터는 세울 오브젝트가 없다 — 모습은 클라이언트 모듈(`SRPGCursorComponent`)이 읽어 그린다.
        (void)manager;
        (void)bRespawnViews;
    }

    void SRPGGameModeComponent::serializeGameState( StateIO& io )
    {
        io.header( SRPGGameModeComponentInternal::kStateTag, SRPGGameModeComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        // 전장은 같은 카탈로그 · 같은 크기로 `initialize` 한 뒤에 읽는다(키트 계약). 꺼내기 표는 대입이 옮기지 않아 이 디렉터의 표가 그대로다.
        SRPGBattlefield restoredField;
        if ( io.isReading() )
        {
            if ( _pCatalog == nullptr )
            {
                SW_LOG_ERROR_ID( "SRPG.StateNeedsCatalog", "SRPG game mode: state read before the SRPG catalog is loaded - refused" );
                io.fail();
                return;
            }
            restoredField.initialize( *_pCatalog, _width, _height, _defaultTerrain.key(), _seed );
        }
        io.value( io.isWriting() ? _field : restoredField ); // 고른 유닛 · 결과는 상태 칸이다(`GameModeComponent::serializeState`)
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _field = std::move( restoredField );
    }
} // namespace sw
