#include "pch.h"

#include "Games/Playground/Velmora/VelmoraEncounterGameModeComponent.h"

#include "Core/Log/Logger.h"

#include "Engine/Automation/Watch/AutomationComponentWatch.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Serialization/Format/StateIO.h"

#include "GameFramework/Kits/Genre/Action/ActionAdventure/AdventureRegionCatalog.h"
#include "GameFramework/Kits/Genre/Action/ActionAdventure/Component/AdventureGameModeComponent.h"
#include "GameFramework/Kits/Genre/Action/ActionCombat/MonsterCatalog.h"
#include "GameFramework/Runtime/Foundation/Service/GameSubsystems.h"
#include "GameFramework/Runtime/Gameplay/GameState/PlayerComponentUtil.h"

namespace sw
{
    namespace
    {
        struct VelmoraEncounterGameModeComponentInternal
        {
            /** @brief 지역 표의 배치 줄을 액션 룸의 배치 줄로 옮깁니다(두 키트는 서로의 형식을 모른다). */
            static vector<ActionRoomSpawnDef> makeRoomSpawns( const vector<AdventureSpawnDef>& listSpawn )
            {
                vector<ActionRoomSpawnDef> listRoomSpawn;
                for ( const AdventureSpawnDef& spawn : listSpawn )
                {
                    listRoomSpawn.push_back( ActionRoomSpawnDef{ spawn._monsterID, spawn._position } );
                }
                return listRoomSpawn;
            }
        };
    } // namespace

    SW_AUTOMATION_GETTER_WATCH( velmoraPhase, "Velmora.Phase", "Velmora region fight phase (0 explore, 1 hall, 2 boss wait, 3 boss)", VelmoraEncounterGameModeComponent,
                                &VelmoraEncounterGameModeComponent::getPhase );
    SW_AUTOMATION_GETTER_WATCH( velmoraAlive, "Velmora.Alive", "Living enemies in the Velmora region fight", VelmoraEncounterGameModeComponent,
                                &VelmoraEncounterGameModeComponent::getAliveEnemyCount );
    SW_AUTOMATION_GETTER_WATCH( velmoraAttackReady, "Velmora.AttackReady", "1 once the Velmora player's attack cooldown has run out (the next attack press lands)",
                                VelmoraEncounterGameModeComponent, &VelmoraEncounterGameModeComponent::isAttackReady );

    VelmoraEncounterGameModeComponent::VelmoraEncounterGameModeComponent()
        : _bossDelaySeconds{ 1.0f }
        , _roomSettings{}
        , _room{}
        , _bossDelay{}
        , _phase{ VelmoraPhase::Explore }
        , _bAttackQueued{ SW_FALSE }
        , _bDashQueued{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    VelmoraEncounterGameModeComponent::~VelmoraEncounterGameModeComponent() = default;

    bool VelmoraEncounterGameModeComponent::validateRegionMonsters( const AdventureRegionCatalog& regions )
    {
        const MonsterCatalog* const pMonsters = game::findSubsystem<const MonsterCatalog>();
        if ( pMonsters == nullptr )
        {
            SW_LOG_ERROR_ID( "Velmora.NoMonsters", "Velmora: the game has no monster catalog service - the region fights do not open" );
            return false;
        }
        bool bAllFound = true;
        for ( const AdventureRegionDef& region : regions.getAll() )
        {
            for ( const vector<AdventureSpawnDef>* pListSpawn : { &region._listHallSpawn, &region._listBossSpawn } )
            {
                for ( const AdventureSpawnDef& spawn : *pListSpawn )
                {
                    if ( pMonsters->findMonster( spawn._monsterID ) != nullptr )
                        continue;
                    SW_LOG_ERROR_ID( "Velmora.UnknownMonster", "Velmora: region '%#' places monster '%#', which the monster catalog does not have", region._id.c_str(), spawn._monsterID.c_str() );
                    bAllFound = false;
                }
            }
        }
        return bAllFound;
    }

    bool VelmoraEncounterGameModeComponent::startGame()
    {
        // 판(지역 · 몸)은 앞에 붙은 디렉터가 연다 — 열리지 않았으면 여기서 대신 열지 않고 멈춘다.
        AdventureGameModeComponent* const pDirector = PlayerComponentUtil::findOnOwner<AdventureGameModeComponent>( *this );
        if ( pDirector == nullptr || pDirector->isStarted() == false )
        {
            SW_LOG_ERROR_ID( "Velmora.DirectorNotOpen", "Velmora: the region fights cannot open - no started Adventure Game Mode in front of them on this object" );
            return false;
        }
        const AdventureRegionCatalog* const pRegions = pDirector->findRegions();
        if ( pRegions == nullptr || validateRegionMonsters( *pRegions ) == false )
            return false; // 지역 표는 디렉터가 열린 것으로 이미 있다, 빠진 종은 위에서 알렸다
        pDirector->cancelEncounter();
        closeEncounter();
        return true;
    }

    bool VelmoraEncounterGameModeComponent::submitCombatInput( bool bAttackPressed, bool bDashPressed )
    {
        if ( isStarted() == false )
            return false;
        if ( bAttackPressed )
            _bAttackQueued = SW_TRUE;
        if ( bDashPressed )
            _bDashQueued = SW_TRUE;
        return true;
    }

    void VelmoraEncounterGameModeComponent::closeEncounter()
    {
        _room.clear();
        _bossDelay.clear();
        _phase         = VelmoraPhase::Explore;
        _bAttackQueued = SW_FALSE;
        _bDashQueued   = SW_FALSE;
    }

    void VelmoraEncounterGameModeComponent::beginBossWait( AdventureGameModeComponent& director )
    {
        _phase = VelmoraPhase::BossWait;
        _bossDelay.start( _bossDelaySeconds );
        director.announceBoss();
    }

    void VelmoraEncounterGameModeComponent::beginEncounter( const AdventureRegionDef& region, AdventureGameModeComponent& director )
    {
        ActionRoomLayout layout;
        layout._listHallSpawn = VelmoraEncounterGameModeComponentInternal::makeRoomSpawns( region._listHallSpawn );
        layout._listBossSpawn = VelmoraEncounterGameModeComponentInternal::makeRoomSpawns( region._listBossSpawn );
        _room.setSettings( _roomSettings ); // 룸 규칙에는 기본값이 없다 — 씬 데이터가 그대로 들어간다(ActionCombatGameModeComponent 와 같다)
        _room.setLayout( layout );
        if ( region._listHallSpawn.empty() )
        {
            beginBossWait( director );
            return;
        }
        _room.beginHall();
        _phase = VelmoraPhase::Hall;
        SW_LOG_INFO_ID( "Velmora.HallStarted", "[Velmora] '%#': a fight with %# enemies", region._name, _room.getAliveEnemyCount() );
    }

    void VelmoraEncounterGameModeComponent::updateGame( float32 deltaTime )
    {
        if ( deltaTime <= 0.0f )
            return;
        ActionRoomFrameInput input;
        input._bAttackPressed = _bAttackQueued;
        input._bDashPressed   = _bDashQueued;
        _bAttackQueued        = SW_FALSE;
        _bDashQueued          = SW_FALSE;

        AdventureGameModeComponent* const   pDirector = PlayerComponentUtil::findOnOwner<AdventureGameModeComponent>( *this );
        const AdventureRegionCatalog* const pRegions  = pDirector != nullptr ? pDirector->findRegions() : nullptr;
        if ( pDirector == nullptr || pRegions == nullptr )
            return;
        if ( _phase == VelmoraPhase::Explore )
        {
            if ( pDirector->getPendingEncounter().empty() )
                return;
            if ( const AdventureRegionDef* const pRegion = pDirector->openEncounter() )
                beginEncounter( *pRegion, *pDirector );
        }
        // 디렉터가 전투를 닫았거나(복원) 다시 읽은 표가 그 지역을 잃었으면 싸움을 닫는다.
        const AdventureRegionDef* const pArena = pRegions->find( pDirector->getEncounterRegion() );
        if ( pArena == nullptr )
        {
            pDirector->cancelEncounter();
            closeEncounter();
            return;
        }
        if ( _phase == VelmoraPhase::BossWait )
        {
            if ( _bossDelay.update( deltaTime ) )
            {
                _room.beginBoss();
                _phase = VelmoraPhase::Boss;
                SW_LOG_INFO_ID( "Velmora.BossStarted", "[Velmora] '%#': the boss fight begins with %# enemies", pArena->_name, _room.getAliveEnemyCount() );
            }
            return;
        }
        input._playerPos                   = pDirector->getPlayerPosition();
        input._facing                      = pDirector->getFacing();
        const ActionRoomFrameResult result = _room.update( deltaTime, input );
        // 적의 닿은 피해(`MonsterStatsDef::_atk`)는 4분의 1 하트 단위다.
        if ( result._damageToPlayer > 0 && pDirector->applyPlayerDamage( result._damageToPlayer ) )
        {
            _room.onPlayerDefeated();
            closeEncounter();
            return;
        }
        if ( result._bClearedThisFrame == SW_FALSE )
            return;
        if ( _phase == VelmoraPhase::Hall && pArena->_listBossSpawn.empty() == false )
        {
            beginBossWait( *pDirector );
            SW_LOG_INFO_ID( "Velmora.HallCleared", "[Velmora] '%#': hall cleared, the boss comes in %# s", pArena->_name, _bossDelaySeconds );
            return;
        }
        pDirector->completeEncounter();
        _phase = VelmoraPhase::Explore;
        SW_LOG_INFO_ID( "Velmora.RegionCleared", "[Velmora] '%#' cleared%#", pArena->_name, result._bBossDefeated == SW_TRUE ? " - the boss fell" : "" );
    }

    void VelmoraEncounterGameModeComponent::serializeGameState( StateIO& io )
    {
        // 싸움은 싣지 않는다(클래스 설명) — 머리만 쓰고, 읽으면 싸움을 닫는다.
        io.header( kStateFormat );
        io.end();
        if ( io.isWriting() || io.isOk() == false )
            return;
        closeEncounter();
    }
} // namespace sw
