#include "pch.h"

#include "Games/Shooter3D/Shooter3DGame.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

#include "Games/Shooter3D/ShooterDirectorComponent.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

#include "RuntimeAPI/Export/GameModuleExports.h"

namespace sw
{
    SW_LOG_CALLER( "Shooter3DGame" );

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
        // 무기 · 장비 아이템 · 외형은 필수다 — 하나라도 못 읽으면 게임을 띄우지 않는다.
        // 무기는 플레이어 컴포넌트가, 외형은 플레이어 몸 · 스켈레톤의 외형 컴포넌트가 게임 범위에서 찾는다. 외형은 아이템을 참조하므로 아이템 뒤에 읽는다.
        if ( loadRequiredCatalog( _weaponCatalog ) == false || loadRequiredCatalog( _itemCatalog ) == false )
            return false;
        const bool bAppearanceLoaded = loadRequiredData( AppearanceDatabase::kDataKind,
                                                         [this]( const string& folder )
        {
            if ( _appearanceDatabase.loadFromFolder( folder, &_itemCatalog ) )
                return true;
            SW_LOG_ERROR( "[Shooter] appearance data report - %#", _appearanceDatabase.getReport().joined().c_str() );
            return false;
        } );
        if ( bAppearanceLoaded == false )
            return false;
        bindScopedService( &_appearanceDatabase );
        // 아레나 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)requestFirstScene();
        return true;
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::Shooter3DGame );
