#include "pch.h"

#include "Games/MeadowVillage/MeadowVillageGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/SessionState/SessionStateComponent.h"

#include "Games/MeadowVillage/MeadowFarmDirectorComponent.h"
#include "Games/MeadowVillage/MeadowTownDirectorComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
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
        // 마을 데이터 넷은 필수다 — 하나라도 못 읽으면 게임을 띄우지 않는다. 읽은 것은 디렉터가 찾는 게임 범위에 걸린다.
        const bool bLoaded = loadRequiredCatalog( _cropCatalog ) && loadRequiredCatalog( _creatureCatalog ) && loadRequiredCatalog( _friendshipCatalog ) &&
                             loadRequiredCatalog( _questCatalog );
        if ( bLoaded == false )
            return false;
        (void)requestFirstScene();
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::MeadowVillageGame );
