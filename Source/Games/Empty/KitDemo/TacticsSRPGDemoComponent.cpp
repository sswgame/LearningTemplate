#include "pch.h"

#include "Games/Empty/KitDemo/TacticsSRPGDemoComponent.h"

#include "Core/Encoding/FourCCUtil.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Console/DebugHUDRegistry.h"
#include "Engine/Graphics/Debug/DebugDrawQueue.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Strategy/TacticsSRPG/Rule/SRPGCombat.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Foundation/Framework/GameplayDebugTarget.h"

namespace sw
{
    namespace
    {
        struct TacticsSRPGDemoComponentInternal
        {
            static constexpr uint32 kStateTag     = FourCCUtil::make( "KDTS" );
            static constexpr uint32 kStateVersion = 1;

            static constexpr const utf8* kConfirmAction = "KitDemo.Confirm";
            static constexpr const utf8* kEndTurnAction = "KitDemo.EndTurn";

            /** @brief 축이 이보다 기울어야 커서를 한 칸 옮긴다(스틱 떨림을 칸 이동으로 읽지 않게). */
            static constexpr float32 kCursorThreshold = 0.5f;

            /** @brief 씬의 칸 자리(정수로 적은 float2)를 칸으로 바꿉니다. */
            static int2 toCell( const float2& value ) { return int2{ static_cast<int32>( value._x ), static_cast<int32>( value._y ) }; }

            /** @brief 축 값의 방향(-1 · 0 · 1)입니다. */
            static int32 computeStep( float32 axis )
            {
                if ( axis > kCursorThreshold )
                    return 1;
                if ( axis < -kCursorThreshold )
                    return -1;
                return 0;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoTacticsPhaseTeam, "KitDemo.Tactics.PhaseTeam", "Team whose phase it is (0 player, 1 enemy, 2 third)", TacticsSRPGDemoComponent,
                                &TacticsSRPGDemoComponent::getPhaseTeam );
    SW_AUTOMATION_GETTER_WATCH( kitDemoTacticsTurn, "KitDemo.Tactics.Turn", "Battle turn (1 = first)", TacticsSRPGDemoComponent, &TacticsSRPGDemoComponent::getTurn );
    SW_AUTOMATION_GETTER_WATCH( kitDemoTacticsResult, "KitDemo.Tactics.Result", "Battle result (0 ongoing, 1 victory, 2 defeat)", TacticsSRPGDemoComponent, &TacticsSRPGDemoComponent::getResult );
    SW_AUTOMATION_GETTER_WATCH( kitDemoTacticsCursorX, "KitDemo.Tactics.CursorX", "Cursor cell X", TacticsSRPGDemoComponent, &TacticsSRPGDemoComponent::getCursorX );
    SW_AUTOMATION_GETTER_WATCH( kitDemoTacticsCursorY, "KitDemo.Tactics.CursorY", "Cursor cell Y", TacticsSRPGDemoComponent, &TacticsSRPGDemoComponent::getCursorY );
    SW_AUTOMATION_GETTER_WATCH( kitDemoTacticsSelected, "KitDemo.Tactics.Selected", "Selected unit slot (-1 = none)", TacticsSRPGDemoComponent, &TacticsSRPGDemoComponent::getSelectedUnit );
    SW_AUTOMATION_GETTER_WATCH( kitDemoTacticsEnemies, "KitDemo.Tactics.EnemiesAlive", "Living enemy units", TacticsSRPGDemoComponent, &TacticsSRPGDemoComponent::getEnemiesAlive );
    SW_AUTOMATION_GETTER_WATCH( kitDemoTacticsPlayers, "KitDemo.Tactics.PlayersAlive", "Living player units", TacticsSRPGDemoComponent, &TacticsSRPGDemoComponent::getPlayersAlive );

    TacticsSRPGDemoComponent::TacticsSRPGDemoComponent()
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
        , _cursor{}
        , _lastStep{}
        , _selectedUnit{ -1 }
        , _result{ TacticsDemoResult::Ongoing }
        , _growthLog{}
    {
    }

    TacticsSRPGDemoComponent::~TacticsSRPGDemoComponent() = default;

    bool TacticsSRPGDemoComponent::openField( SRPGBattlefield& outField ) const
    {
        const bool bPlayersMatch = _listPlayerUnit.size() == _listPlayerCell.size();
        const bool bEnemiesMatch = _listEnemyUnit.size() == _listEnemyCell.size();
        if ( bPlayersMatch == false || bEnemiesMatch == false || _listPlayerUnit.empty() || _listEnemyUnit.empty() )
        {
            SW_LOG_ERROR_ID( "KitDemo.TacticsLayoutMismatch", "Tactics demo: unit and cell lists must be non-empty and of equal length (player %# / %#, enemy %# / %#) - the battle does not open",
                             _listPlayerUnit.size(), _listPlayerCell.size(), _listEnemyUnit.size(), _listEnemyCell.size() );
            return false;
        }
        outField.initialize( _pCatalog.get(), _width, _height, _defaultTerrain, SRPGSettings{}, _seed );
        for ( size_t index = 0; index < _listPlayerUnit.size() + _listEnemyUnit.size(); ++index )
        {
            const bool           bPlayer   = index < _listPlayerUnit.size();
            const size_t         slot      = bPlayer ? index : index - _listPlayerUnit.size();
            const hashed_string& unitID    = bPlayer ? _listPlayerUnit[slot] : _listEnemyUnit[slot];
            const int2           cell      = TacticsSRPGDemoComponentInternal::toCell( bPlayer ? _listPlayerCell[slot] : _listEnemyCell[slot] );
            const int32          unitIndex = outField.addUnit( unitID, _pilot, bPlayer ? SRPGTeam::Player : SRPGTeam::Enemy, cell );
            if ( unitIndex < 0 )
            {
                SW_LOG_ERROR_ID( "KitDemo.TacticsUnitNotPlaced", "Tactics demo: unit '%#' with pilot '%#' cannot stand at (%#, %#) - unknown id, outside the field, taken or impassable; the battle does not open",
                                 unitID.c_str(), _pilot.c_str(), cell._x, cell._y );
                return false;
            }
        }
        return true;
    }

    bool TacticsSRPGDemoComponent::startGame()
    {
        if ( acquireRequired( _catalogPath, _pCatalog ) == false )
            return false;
        if ( openField( _field ) == false )
            return false;
        _field.beginBattle();
        if ( _eventLease.isHeld() == false && _field.claimEvents( _eventLease ) == false )
            return false; // 이 디렉터의 표뿐이라 일어날 수 없다 — 둘째 소비자가 생기면 게이트가 어서션을 남긴다
        _cursor       = TacticsSRPGDemoComponentInternal::toCell( _listPlayerCell.front() );
        _lastStep     = int2{};
        _selectedUnit = -1;
        _result       = TacticsDemoResult::Ongoing;
        _listEvent.clear();
        _growthLog.clear();
        return true;
    }

    void TacticsSRPGDemoComponent::onGameStarted()
    {
        SW_LOG_INFO_ID( "KitDemo.TacticsStarted", "[KitDemo] tactics: %# against %# - Move steps the cursor, KitDemo.Confirm selects / moves / attacks, KitDemo.EndTurn ends the phase",
                        getPlayersAlive(), getEnemiesAlive() );
    }

    void TacticsSRPGDemoComponent::moveCursor()
    {
        static const hashed_string s_moveAction( CommonInputActions::kMove );
        const float2               axis = getActionVector2D( s_moveAction );
        const int2                 step{ TacticsSRPGDemoComponentInternal::computeStep( axis._x ), TacticsSRPGDemoComponentInternal::computeStep( axis._y ) };
        const int2                 fresh{ step._x != _lastStep._x ? step._x : 0, step._y != _lastStep._y ? step._y : 0 };
        _lastStep = step;
        const int2 moved{ _cursor._x + fresh._x, _cursor._y + fresh._y };
        if ( _field.isInside( moved ) )
            _cursor = moved;
    }

    bool TacticsSRPGDemoComponent::attackAtCursor( int32 targetIndex )
    {
        const SRPGUnit* pUnit = _field.findUnit( _selectedUnit );
        for ( int32 weaponIndex = 0; pUnit != nullptr && weaponIndex < static_cast<int32>( pUnit->_listWeapon.size() ); ++weaponIndex )
        {
            const SRPGWeaponDef* pWeapon = _field.findWeapon( *pUnit, weaponIndex );
            const bool           bUsable = pWeapon != nullptr && _field.computeWeaponStatus( *pUnit, weaponIndex, pUnit->_bMoved == SW_TRUE ) == SRPGWeaponStatus::Ok &&
                                 SRPGBattlefield::isInWeaponRange( *pWeapon, pUnit->_cell, _cursor );
            if ( bUsable == false )
                continue;
            SRPGCombatResult result;
            if ( SRPGCombat::executeAttack( _field, _selectedUnit, weaponIndex, targetIndex, result ) != SRPGWeaponStatus::Ok )
                return false;
            _field.endUnitAction( _selectedUnit );
            _selectedUnit = -1;
            return true;
        }
        return false;
    }

    void TacticsSRPGDemoComponent::confirmAtCursor()
    {
        const optional<int32> cursorUnit = _field.findUnitAt( _cursor );
        if ( _selectedUnit < 0 )
        {
            const SRPGUnit* pUnit       = cursorUnit.has_value() ? _field.findUnit( *cursorUnit ) : nullptr;
            const bool      bSelectable = pUnit != nullptr && pUnit->_team == SRPGTeam::Player && _field.canAct( *cursorUnit );
            if ( bSelectable )
                _selectedUnit = *cursorUnit;
            return;
        }
        const SRPGUnit* pSelected = _field.findUnit( _selectedUnit );
        if ( cursorUnit == _selectedUnit )
        {
            // 제자리에서 다시 고르면 대기 — 행동을 끝낸다.
            _field.endUnitAction( _selectedUnit );
            _selectedUnit = -1;
            return;
        }
        const SRPGUnit* pTarget = cursorUnit.has_value() ? _field.findUnit( *cursorUnit ) : nullptr;
        if ( pTarget != nullptr && SRPGBattlefield::isHostile( pSelected->_team, pTarget->_team ) )
        {
            if ( attackAtCursor( *cursorUnit ) == false )
                SW_LOG_INFO_ID( "KitDemo.TacticsCannotAttack", "[KitDemo] tactics: no weapon reaches (%#, %#) from (%#, %#)", _cursor._x, _cursor._y, pSelected->_cell._x, pSelected->_cell._y );
            return;
        }
        if ( cursorUnit.has_value() == false && _field.moveUnit( _selectedUnit, _cursor ) == false )
            SW_LOG_INFO_ID( "KitDemo.TacticsCannotMove", "[KitDemo] tactics: the unit cannot move to (%#, %#)", _cursor._x, _cursor._y );
    }

    void TacticsSRPGDemoComponent::runAIPhase()
    {
        const SRPGTeam team = _field.getPhaseTeam();
        (void)_ai.runPhase( _field, team );
        // 움직일 수 없는 유닛만 남았으면(이동 0 · 칠 적 없음) AI 가 멈춘다 — 페이즈를 넘긴다.
        if ( _field.getPhaseTeam() == team )
            _field.endPhase();
    }

    void TacticsSRPGDemoComponent::settle()
    {
        _listEvent.clear();
        _field.pollEvents( _eventLease, _listEvent );
        string cause; // 레벨 오름의 출처 — 같은 이벤트 묶음에서 그 앞의 명중 · 격파다(전장은 경험치를 그 자리에서 준다)
        for ( const SRPGEvent& event : _listEvent )
        {
            if ( event._kind == SRPGEvent::Kind::Destroyed )
                SW_LOG_INFO_ID( "KitDemo.TacticsDestroyed", "[KitDemo] tactics: unit %# destroyed by %#", event._unit.value(), event._other.has_value() ? to_string( *event._other ).c_str() : "nothing" );
            else if ( event._kind == SRPGEvent::Kind::PhaseStarted )
                SW_LOG_INFO_ID( "KitDemo.TacticsPhase", "[KitDemo] tactics: turn %# - %# phase", _field.getTurn(), static_cast<int32>( event._team ) );
            if ( event._kind == SRPGEvent::Kind::Hit )
                cause = "hit unit " + to_string( event._other.value() );
            else if ( event._kind == SRPGEvent::Kind::Destroyed )
                cause = "destroyed unit " + to_string( event._unit.value() );
            const bool bLevelUp = event._kind == SRPGEvent::Kind::PilotLevelUp || event._kind == SRPGEvent::Kind::UnitLevelUp;
            if ( bLevelUp )
                _growthLog.record( GrowthEventKind::LevelUp, "unit " + to_string( event._unit.value() ), event._kind == SRPGEvent::Kind::PilotLevelUp ? "pilot" : "unit", event._value, cause );
        }
        if ( _field.countAlive( SRPGTeam::Enemy ) == 0 )
        {
            _result = TacticsDemoResult::Victory;
            SW_LOG_INFO_ID( "KitDemo.TacticsVictory", "[KitDemo] tactics: victory on turn %#", _field.getTurn() );
            return;
        }
        if ( _field.countAlive( SRPGTeam::Player ) == 0 )
        {
            _result = TacticsDemoResult::Defeat;
            SW_LOG_INFO_ID( "KitDemo.TacticsDefeat", "[KitDemo] tactics: defeat on turn %#", _field.getTurn() );
        }
    }

    void TacticsSRPGDemoComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f || _result != TacticsDemoResult::Ongoing )
        {
            drawField();
            return;
        }
        if ( _field.getPhaseTeam() != SRPGTeam::Player )
        {
            runAIPhase();
            settle();
            drawField();
            return;
        }
        static const hashed_string s_confirmAction( TacticsSRPGDemoComponentInternal::kConfirmAction );
        static const hashed_string s_endTurnAction( TacticsSRPGDemoComponentInternal::kEndTurnAction );
        moveCursor();
        if ( wasActionTriggered( s_confirmAction ) )
            confirmAtCursor();
        if ( wasActionTriggered( s_endTurnAction ) )
        {
            _selectedUnit = -1;
            _field.endPhase();
        }
        settle();
        drawField();
    }

    void TacticsSRPGDemoComponent::drawField() const
    {
        DebugDrawQueue* pDraw = game::findSubsystem<DebugDrawQueue>();
        if ( pDraw == nullptr )
            return;
        // 칸 (x, y) 를 월드 (x, 0, y) 에 둔다 — 위에서 내려다본다.
        for ( int32 y = 0; y < _field.getHeight(); ++y )
        {
            for ( int32 x = 0; x < _field.getWidth(); ++x )
            {
                pDraw->drawBox( float3( static_cast<float32>( x ), -0.05f, static_cast<float32>( y ) ), float3( 0.48f, 0.05f, 0.48f ), float4( 0.3f, 0.35f, 0.3f, 1.0f ) );
            }
        }
        for ( size_t index = 0; index < _field.getUnits().size(); ++index )
        {
            const SRPGUnit& unit = _field.getUnits()[index];
            if ( unit._bAlive == SW_FALSE )
                continue;
            const bool   bSelected = static_cast<int32>( index ) == _selectedUnit;
            const float4 color     = unit._team == SRPGTeam::Player ? ( bSelected ? float4( 0.6f, 0.9f, 1.0f, 1.0f ) : float4( 0.2f, 0.6f, 1.0f, 1.0f ) ) : float4( 1.0f, 0.3f, 0.2f, 1.0f );
            pDraw->drawSphere( float3( static_cast<float32>( unit._cell._x ), 0.4f, static_cast<float32>( unit._cell._y ) ), 0.35f, color );
        }
        pDraw->drawBox( float3( static_cast<float32>( _cursor._x ), 0.05f, static_cast<float32>( _cursor._y ) ), float3( 0.5f, 0.02f, 0.5f ), float4( 1.0f, 0.95f, 0.3f, 1.0f ) );
    }

    void TacticsSRPGDemoComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        // 모습은 디버그 그리기뿐이라 세울 오브젝트가 없다.
        (void)manager;
        (void)bRespawnViews;
    }

    void TacticsSRPGDemoComponent::serializeState( StateIO& io )
    {
        io.header( TacticsSRPGDemoComponentInternal::kStateTag, TacticsSRPGDemoComponentInternal::kStateVersion );
        if ( io.isOk() == false )
            return;
        // 전장은 같은 카탈로그 · 같은 크기로 `initialize` 한 뒤에 읽는다(키트 계약). 꺼내기 표는 대입이 옮기지 않아 이 디렉터의 표가 그대로다.
        SRPGBattlefield restoredField;
        if ( io.isReading() )
            restoredField.initialize( _pCatalog.get(), _width, _height, _defaultTerrain, SRPGSettings{}, _seed );
        io.value( io.isWriting() ? _field : restoredField );
        int2              cursor       = _cursor;
        int32             selectedUnit = _selectedUnit;
        TacticsDemoResult result       = _result;
        io.value( cursor );
        io.value( selectedUnit );
        io.enumValue( result, TacticsDemoResult::Defeat );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        _field        = std::move( restoredField );
        _cursor       = cursor;
        _lastStep     = int2{};
        _selectedUnit = selectedUnit;
        _result       = result;
    }

    void TacticsSRPGDemoComponent::writeGrowthDebugLines( DebugHUDSectionWriter& writer ) const
    {
        const vector<SRPGUnit>& listUnit = _field.getUnits();
        for ( size_t unitIndex = 0; unitIndex < listUnit.size(); ++unitIndex )
        {
            const SRPGUnit& unit  = listUnit[unitIndex];
            string          value = unit._pDef != nullptr ? string( unit._pDef->_id.c_str() ) : string( "?" );
            value += unit._bAlive == SW_TRUE ? "" : " (down)";
            value += " pilot Lv " + to_string( unit._pilotLevel.getLevel() ) + " xp " + to_string( unit._pilotLevel.getXP() );
            if ( _pCatalog != nullptr )
                value += "/" + to_string( _pCatalog->getPilotCurve().getXPToNext( unit._pilotLevel.getLevel() ) );
            value += ", unit Lv " + to_string( unit._unitLevel.getLevel() ) + " xp " + to_string( unit._unitLevel.getXP() );
            if ( _pCatalog != nullptr )
                value += "/" + to_string( _pCatalog->getUnitCurve().getXPToNext( unit._unitLevel.getLevel() ) );
            writer.addLine( "unit " + to_string( unitIndex ), value );
        }
        const uint32 used = static_cast<uint32>( listUnit.size() );
        _growthLog.writeDebugLines( writer, GameplayDebugTarget::kMaxLines > used ? GameplayDebugTarget::kMaxLines - used : 0u );
    }

    namespace
    {
        /** @brief 성장 디버거(HUD 섹션 `tacticsgrowth`) — 이 판 전장 유닛의 레벨 · 경험치 · 다음까지와 최근 레벨 오름의 출처. 읽기만 한다. */
        struct TacticsGrowthDebugSectionInternal
        {
            static void writeGrowth( DebugHUDSectionWriter& writer )
            {
                const GameObjectManager* const        pManager  = GameplayDebugTarget::findActiveObjectManager();
                const TacticsSRPGDemoComponent* const pDirector = pManager != nullptr ? GameplayDebugTarget::findTargetComponent<TacticsSRPGDemoComponent>( *pManager ) : nullptr;
                if ( pDirector == nullptr )
                {
                    writer.addLine( "target", "none (no tactics demo in the active scene)" );
                    return;
                }
                pDirector->writeGrowthDebugLines( writer );
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( kitDemoTacticsLevelUps, "KitDemo.Tactics.LevelUps", "Level-ups left in the tactics demo growth log (pilot and unit)", TacticsSRPGDemoComponent,
                                &TacticsSRPGDemoComponent::getLevelUps );
    SW_DEBUG_HUD_SECTION( tacticsGrowth, "tacticsgrowth", "Tactics growth (levels, xp, sources)", 290, DebugHUDSectionFlag::kNone, &TacticsGrowthDebugSectionInternal::writeGrowth );
} // namespace sw
