/**
 * @file Shooter3DGame.cpp
 * @brief 기반의 무기 규칙(GameFramework/Runtime/Character/Combat)을 실제로 쓰는 테스트 게임 — 상자가 놓인 아레나에서 웨이브로 몰려오는 스켈레톤을 히트스캔 무기로 막는 슈터입니다.
 *
 * @details 실행: `App.exe -game=Shooter3D`(`Ninja-Debug` 빌드). 조작은 `Source/Games/Shooter3D/README.md`. `-gv_shooterAutoPlay=1` 이면 조준 · 사격도 AI 가 한다.
 *          아레나는 씬(`game/shooter3d/maps/arena.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `ShooterDirectorComponent` ·
 *          `ShooterPlayerComponent` 가 섭니다. 이 클래스는 무기 카탈로그를 게임 서비스로 걸고(장비 아이템 · 외형 데이터는 바탕 `GameInstance` 가 gamesettings 로 건다), 첫 씬을 열고, 상태 저장 전에 판의 진행(웨이브 · 처치 수)을 싣고 디렉터가 세운 것을 걷으며, 복원 뒤 진행을 돌려줍니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Combat/Weapon/Weapon.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

#include "Games/Shooter3D/ShooterDirectorComponent.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 무기 카탈로그를 들고 게임 서비스로 겁니다(아이템 · 외형은 바탕이 gamesettings 로 건다) — 모듈이 다시 올라오면 새 인스턴스가 다시 건다. */
    class Shooter3DGame : public GameInstance
    {
    public:
        Shooter3DGame();
        ~Shooter3DGame() override;

    protected:
        bool onInitialize() override;

    private:
        shared_ptr<const WeaponCatalog> _pWeaponCatalog; ///< 공유 캐시의 한 벌(게임 범위 서비스)
    };
} // namespace sw

namespace sw
{
    Shooter3DGame::Shooter3DGame()
        : _pWeaponCatalog{}
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        getStateSnapshot().registerDirector<ShooterDirectorComponent>();
        getStateSnapshot().registerViewOwner<ShooterPlayerComponent>();
    }

    Shooter3DGame::~Shooter3DGame() = default;

    bool Shooter3DGame::onInitialize()
    {
        // 무기는 필수다. 아이템 · 외형은 gamesettings 의 <RequiredData> 로 바탕(GameInstance)이 이미 읽어 게임 범위에 걸었다 — 무기 표가 가리키는 아이템을 그 카탈로그로 본다.
        if ( loadRequiredCatalog( _pWeaponCatalog ) == false )
            return false;
        const ItemCatalog*          pItemCatalog = game::findSubsystem<ItemCatalog>();
        vector<WeaponItemReference> listUnknownItem;
        if ( pItemCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "Shooter3DGame.ItemCatalogMissing", "[Shooter] no item catalog - gamesettings must declare <RequiredData kind=\"items\">" );
            return false;
        }
        if ( _pWeaponCatalog->findUnknownItems( *pItemCatalog, listUnknownItem ) == false )
        {
            // 무기 표의 item · ammo 가 없는 아이템을 가리키면 이유를 남기고 게임을 띄우지 않는다
            for ( const WeaponItemReference& reference : listUnknownItem )
            {
                SW_LOG_ERROR_ID( "Shooter3DGame.WeaponNamesWhichNot", "[Shooter] weapon '%#' %# names '%#', which is not in the item catalog", reference._weaponID.c_str(), reference._pField, reference._itemID.c_str() );
            }
            return false;
        }
        // 아레나 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)getSubsystem<GameSceneFlow>().requestFirstScene();
        return true;
    }
} // namespace sw
SW_IMPLEMENT_GAME_MODULE( sw::Shooter3DGame );
