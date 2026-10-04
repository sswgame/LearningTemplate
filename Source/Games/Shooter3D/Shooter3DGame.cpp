#include "pch.h"

#include "Games/Shooter3D/Shooter3DGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Framework/ComponentStateStore.h"
#include "GameFramework/Framework/GameService.h"

#include "Games/Shooter3D/ShooterDirectorComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "Shooter3DGame" );

    Shooter3DGame::Shooter3DGame()
        : _weaponCatalog{}
    {
    }

    Shooter3DGame::~Shooter3DGame() = default;

    bool Shooter3DGame::onInitialize()
    {
        if ( _weaponCatalog.loadFromResource( "game/shooter3d/data/weapons.xml" ) == false )
        {
            SW_LOG_WARNING( "[Shooter] weapons.xml could not be loaded - the arena cannot start" );
            return true;
        }
        // 플레이어 컴포넌트가 찾는 자리. 모듈이 다시 올라오면 새 인스턴스가 다시 건다.
        game::bindLocalService<WeaponCatalog>( &_weaponCatalog );
        // 아레나 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }

    void Shooter3DGame::onShutdown()
    {
        game::unbindLocalService<WeaponCatalog>();
    }

    void Shooter3DGame::onBeforeStateSerialize()
    {
        GameObjectManager* pManager = findActiveObjectManager();
        if ( pManager == nullptr )
            return;
        // 판의 진행(웨이브 · 처치 수)는 PROPERTY 가 아니다 — 컴포넌트 섹션에 실어 다시 만든 디렉터에 돌려준다.
        getComponentStateStore().capture<ShooterDirectorComponent>( *pManager );
        // 디렉터가 세운 드론 · 효과는 판의 모습일 뿐이다 — 스냅샷에 실으면 복원된 것이 다시 세운 것과 겹친다.
        // 순회 콜백 안에서는 오브젝트를 지울 수 없다(매니저 잠금 안) — 디렉터를 모은 뒤 걷는다.
        vector<ComponentHandle> listDirector;
        pManager->forEachComponentOfType<ShooterDirectorComponent>( [&listDirector]( ShooterDirectorComponent* pDirector )
        { listDirector.push_back( pDirector->getHandle() ); } );
        for ( const ComponentHandle& handle : listDirector )
        {
            ShooterDirectorComponent* pDirector = static_cast<ShooterDirectorComponent*>( pManager->resolveComponent( handle ) );
            if ( pDirector != nullptr )
                pDirector->despawnRuntime();
        }
    }

    void Shooter3DGame::onAfterStateDeserialize()
    {
        GameObjectManager* pManager = findActiveObjectManager();
        if ( pManager != nullptr )
            getComponentStateStore().restore<ShooterDirectorComponent>( *pManager );
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::Shooter3DGame );
