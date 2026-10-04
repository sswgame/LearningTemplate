#include "pch.h"

#include "Games/NileCity/NileCityGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Framework/GameService.h"

#include "Games/NileCity/NileDirectorComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    NileCityGame::NileCityGame() = default;

    NileCityGame::~NileCityGame() = default;

    bool NileCityGame::onInitialize()
    {
        // 도시 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }

    void NileCityGame::onBeforeStateSerialize()
    {
        // 디렉터가 세운 땅 · 도로 · 건물 · 일꾼은 도시 상태의 모습일 뿐이다 — 스냅샷에 실으면 복원된 것이 다시 세운 것과 겹친다.
        // 걷어 두면(삭제 대기는 스냅샷이 건너뛴다) 다시 만든 디렉터가 시작하며 세우고, 남은 디렉터는 다음 틱에 다시 세운다.
        SceneManager*      pSceneManager = game::getService<SceneManager>();
        Scene*             pScene        = pSceneManager != nullptr ? pSceneManager->getActiveScene() : nullptr;
        GameObjectManager* pManager      = pScene != nullptr ? pScene->getObjectManager() : nullptr;
        if ( pManager == nullptr )
            return;
        // 순회 콜백 안에서는 오브젝트를 지울 수 없다(매니저 잠금 안) — 디렉터를 모은 뒤 걷는다.
        vector<ComponentHandle> listDirector;
        pManager->forEachComponentOfType<NileDirectorComponent>( [&listDirector]( NileDirectorComponent* pDirector )
        { listDirector.push_back( pDirector->getHandle() ); } );
        for ( const ComponentHandle& handle : listDirector )
        {
            NileDirectorComponent* pDirector = static_cast<NileDirectorComponent*>( pManager->resolveComponent( handle ) );
            if ( pDirector != nullptr )
                pDirector->despawnViews();
        }
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::NileCityGame );
