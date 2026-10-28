#include "pch.h"

#include "Games/FrontierTown/FrontierTownGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Gameplay/SessionState/SessionStateComponent.h"

#include "Games/FrontierTown/FrontierCityDirectorComponent.h"
#include "Games/FrontierTown/FrontierSkirmishDirectorComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    FrontierTownGame::FrontierTownGame()
        : _cityCatalog{}
        , _unitCatalog{}
    {
        // 공유 상태가 먼저 — 복원할 때 디렉터보다 먼저 바이트를 받아 두고, 첫 디렉터가 판을 열며 적용한다.
        registerStatefulComponent<SessionStateComponent>();
        registerDirector<FrontierCityDirectorComponent>();
        registerDirector<FrontierSkirmishDirectorComponent>();
    }

    FrontierTownGame::~FrontierTownGame() = default;

    bool FrontierTownGame::onInitialize()
    {
        // 데이터 둘은 필수다 — 하나라도 못 읽으면 게임을 띄우지 않는다. 읽은 것은 디렉터가 찾는 게임 범위에 걸린다.
        const bool bLoaded = loadRequiredCatalog( _cityCatalog ) && loadRequiredCatalog( _unitCatalog );
        if ( bLoaded == false )
            return false;
        (void)requestFirstScene(); // 실패는 함수가 알리고, 핫 리로드로 다시 선 인스턴스는 살아 있는 씬을 쓴다
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::FrontierTownGame );
