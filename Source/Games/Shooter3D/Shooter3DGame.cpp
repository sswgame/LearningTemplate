#include "pch.h"

#include "Games/Shooter3D/Shooter3DGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Foundation/Framework/Presentation/GameSound.h"

#include "Games/Shooter3D/ShooterDirectorComponent.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "Shooter3DGame" );

    namespace
    {
        struct Shooter3DGameInternal
        {
            static constexpr const utf8* kAudioEvents = "game/shooter3d/audio/shooter3d.audioevents.xml";
        };
    } // namespace

    Shooter3DGame::Shooter3DGame()
        : _weaponCatalog{}
        , _itemCatalog{}
        , _appearanceDatabase{}
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        registerDirector<ShooterDirectorComponent>();
        registerViewOwner<ShooterPlayerComponent>();
    }

    Shooter3DGame::~Shooter3DGame() = default;

    bool Shooter3DGame::onInitialize()
    {
        // 사운드 이벤트 — 게임 코드는 이름만 안다(무슨 클립을 어떻게 낼지는 데이터).
        if ( GameSound::loadEvents( Shooter3DGameInternal::kAudioEvents ) == false )
            SW_LOG_WARNING( "[Shooter] %# could not be loaded - sounds stay silent", Shooter3DGameInternal::kAudioEvents );
        if ( _weaponCatalog.loadFromResource( "game/shooter3d/data/weapons.xml" ) == false )
        {
            SW_LOG_WARNING( "[Shooter] weapons.xml could not be loaded - the arena cannot start" );
            return true;
        }
        // 플레이어 컴포넌트가 찾는 자리. 모듈이 다시 올라오면 새 인스턴스가 다시 건다.
        game::bindLocalService<WeaponCatalog>( &_weaponCatalog );
        // 외형 — 플레이어 몸 · 스켈레톤의 외형 컴포넌트가 프리셋을 여기서 푼다.
        if ( _itemCatalog.loadFromResource( "game/shooter3d/data/items.xml" ) == false ||
             _appearanceDatabase.loadFromFolder( "game/shooter3d/data/appearance", &_itemCatalog ) == false )
            SW_LOG_ERROR( "[Shooter] appearance data could not be loaded - %#", _appearanceDatabase.getReport().joined().c_str() );
        else
            game::bindLocalService<AppearanceDatabase>( &_appearanceDatabase );
        // 아레나 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }

    void Shooter3DGame::onShutdown()
    {
        GameSound::unloadEvents( Shooter3DGameInternal::kAudioEvents );
        game::unbindLocalService<WeaponCatalog>();
        if ( game::getService<AppearanceDatabase>() == &_appearanceDatabase )
            game::unbindLocalService<AppearanceDatabase>();
    }

} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::Shooter3DGame );
