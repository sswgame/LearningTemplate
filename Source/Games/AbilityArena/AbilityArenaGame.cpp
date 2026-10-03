#include "pch.h"

#include "Games/AbilityArena/AbilityArenaGame.h"

#include "GameFramework/Base/GameService.h"

#include "Games/AbilityArena/ArenaAbilities.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "AbilityArenaGame" );

    AbilityArenaGame::AbilityArenaGame()
        : _abilityCatalog{}
        , _arenaWorld{}
    {
    }

    AbilityArenaGame::~AbilityArenaGame() = default;

    void AbilityArenaGame::configureBootstrap( BootstrapConfig& outConfig )
    {
        outConfig._packRoot = "game/abilityarena";
    }

    bool AbilityArenaGame::onInitialize()
    {
        // 클래스를 먼저 등록하고 데이터를 읽는다 — 읽을 때 모르는 클래스 이름은 경고한다.
        ArenaAbilities::registerClasses( _abilityCatalog );
        if ( _abilityCatalog.loadFromResource( "game/abilityarena/data/abilities.xml" ) == false )
        {
            SW_LOG_WARNING( "[Arena] abilities.xml could not be loaded - the arena cannot start" );
            return true; // 모듈은 뜬다(에디터에서 데이터를 고칠 수 있게) — 아레나만 서지 않는다
        }

        // 컴포넌트 · 어빌리티가 찾는 자리. 모듈이 다시 올라오면 새 인스턴스가 다시 건다.
        game::bindLocalService<AbilityCatalog>( &_abilityCatalog );
        game::bindLocalService<ArenaWorld>( &_arenaWorld );
        (void)_arenaWorld.spawn( &_abilityCatalog ); // 씬 서비스가 아직 없으면 첫 update 가 다시 세운다
        return true;
    }

    void AbilityArenaGame::onShutdown()
    {
        _arenaWorld.despawn();
        game::unbindLocalService<ArenaWorld>();
        game::unbindLocalService<AbilityCatalog>();
    }

    void AbilityArenaGame::onUpdate( float32 deltaTime )
    {
        GameInstanceBase::onUpdate( deltaTime );
        if ( _abilityCatalog.getAbilitySetCount() > 0 )
            _arenaWorld.update( deltaTime );
    }

    void AbilityArenaGame::onBeforeStateSerialize()
    {
        _arenaWorld.despawn();
    }

    void AbilityArenaGame::onAfterStateDeserialize()
    {
        // 복원은 씬을 스냅샷대로 다시 만든다 — 아레나는 스냅샷에 없으므로 그 씬에 다시 세운다.
        (void)_arenaWorld.spawn( &_abilityCatalog );
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::AbilityArenaGame );
