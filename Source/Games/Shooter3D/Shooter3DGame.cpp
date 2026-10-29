/**
 * @file Shooter3DGame.cpp
 * @brief 기반의 무기 규칙(GameFramework/Base/Character/Combat)을 실제로 쓰는 테스트 게임 — 상자가 놓인 아레나에서 웨이브로 몰려오는 스켈레톤을 히트스캔 무기로 막는 슈터입니다.
 *
 * @details 실행: `App.exe -game=Shooter3D`(`Ninja-Debug` 빌드). 조작은 `Source/Games/Shooter3D/README.md`. `-gv_shooterAutoPlay=1` 이면 조준 · 사격도 AI 가 한다.
 *          아레나는 씬(`game/shooter3d/maps/arena.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `ShooterDirectorComponent` ·
 *          `ShooterPlayerComponent` 가 섭니다. 이 클래스는 무기 카탈로그 · 장비 아이템 · 외형 데이터(`AppearanceDatabase`)를 게임 서비스로 걸고, 첫 씬을 열고, 상태 저장 전에 판의 진행(웨이브 · 처치 수)을 싣고 디렉터가 세운 것을 걷으며, 복원 뒤 진행을 돌려줍니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Base/Character/Combat/Weapon/Weapon.h"
#include "GameFramework/Base/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Base/Foundation/Framework/GameService.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceDataWatcher.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"

#include "Games/Shooter3D/ShooterDirectorComponent.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 무기 카탈로그 · 아이템 · 외형 데이터를 들고 게임 서비스로 겁니다 — 모듈이 다시 올라오면 새 인스턴스가 다시 건다. */
    class Shooter3DGame : public GameInstance
    {
    public:
        Shooter3DGame();
        ~Shooter3DGame() override;

    protected:
        bool onInitialize() override;
        void onUpdate( float32 deltaTime ) override;

    private:
        WeaponCatalog         _weaponCatalog;
        ItemCatalog           _itemCatalog;
        AppearanceDatabase    _appearanceDatabase;
        AppearanceDataWatcher _appearanceWatcher; ///< 외형 데이터 핫 리로드(데이터를 기억하지 않는다 — 부를 때마다 넘긴다)
    };
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "Shooter3DGame" );

    Shooter3DGame::Shooter3DGame()
        : _weaponCatalog{}
        , _itemCatalog{}
        , _appearanceDatabase{}
        , _appearanceWatcher{}
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
        vector<WeaponItemReference> listUnknownItem;
        if ( _weaponCatalog.findUnknownItems( _itemCatalog, listUnknownItem ) == false )
        {
            // 무기 표의 item · ammo 가 없는 아이템을 가리키면 이유를 남기고 게임을 띄우지 않는다
            for ( const WeaponItemReference& reference : listUnknownItem )
            {
                SW_LOG_ERROR( "[Shooter] weapon '%#' %# names '%#', which is not in the item catalog", reference._weaponID.c_str(), reference._pField, reference._itemID.c_str() );
            }
            return false;
        }
        const bool bAppearanceLoaded = loadRequiredData( AppearanceDatabase::kDataKind,
                                                         [this]( const string& folder )
        {
            if ( _appearanceDatabase.loadFromFolder( folder, &_itemCatalog ) )
            {
                // 고친 외형 파일을 실행 중에 다시 읽는다. 팩 데이터면 디스크 폴더가 없어 감시하지 않는다(그 까닭은 감시자가 알린다).
                (void)_appearanceWatcher.initialize( folder );
                return true;
            }
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

    void Shooter3DGame::onUpdate( float32 deltaTime )
    {
        GameInstance::onUpdate( deltaTime );
        _appearanceWatcher.update( _appearanceDatabase, &_itemCatalog );
    }
} // namespace sw

SW_IMPLEMENT_GAME_MODULE( sw::Shooter3DGame );
