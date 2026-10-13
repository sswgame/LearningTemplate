#include "pch.h"

#include "Games/MeadowVillage/MeadowVillageGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/SessionState/SessionStateComponent.h"

#include "Games/MeadowVillage/MeadowFarmDirectorComponent.h"
#include "Games/MeadowVillage/MeadowTownDirectorComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "MeadowVillageGame" );

    MeadowVillageGame::MeadowVillageGame()
        : _cropCatalog{}
        , _creatureCatalog{}
        , _friendshipCatalog{}
        , _questCatalog{}
    {
        // 공유 상태가 먼저 — 복원할 때 디렉터보다 먼저 바이트를 받아 두고, 첫 디렉터가 판을 열며 적용한다.
        registerStatefulComponent<SessionStateComponent>();
        registerDirector<MeadowFarmDirectorComponent>();
        registerDirector<MeadowTownDirectorComponent>();
    }

    MeadowVillageGame::~MeadowVillageGame() = default;

    bool MeadowVillageGame::onInitialize()
    {
        _cropCatalog.setKnownSeasons( { "Spring", "Summer", "Fall", "Winter" } );
        const bool bLoaded = _cropCatalog.loadFromResource( "game/meadowvillage/data/crops.xml" ) &&
                             _creatureCatalog.loadFromResource( "game/meadowvillage/data/creatures.xml" ) &&
                             _friendshipCatalog.loadFromResource( "game/meadowvillage/data/friendship.xml" ) &&
                             _questCatalog.loadFromResource( "game/meadowvillage/data/quests.xml" );
        if ( bLoaded == false )
        {
            SW_LOG_WARNING( "[Meadow] the village data could not be loaded - the village cannot start" );
            return true; // 모듈은 뜬다(에디터에서 데이터를 고칠 수 있게) — 디렉터만 서지 않는다
        }
        // 디렉터가 찾는 자리. 게임 범위는 이 인스턴스가 소유하므로 모듈이 다시 올라오면 옛 범위와 함께 풀린다.
        bindScopedService( &_cropCatalog );
        bindScopedService( &_creatureCatalog );
        bindScopedService( &_friendshipCatalog );
        bindScopedService( &_questCatalog );
        (void)requestFirstScene();
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::MeadowVillageGame );
