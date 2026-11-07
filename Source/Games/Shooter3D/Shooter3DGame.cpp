/**
 * @file Shooter3DGame.cpp
 * @brief 기반의 무기 규칙(GameFramework/Runtime/Character/Combat)을 실제로 쓰는 테스트 게임 — 상자가 놓인 아레나에서 웨이브로 몰려오는 스켈레톤을 히트스캔 무기로 막는 슈터입니다.
 *
 * @details 실행: `App.exe -game=Shooter3D`(`Ninja-Debug` 빌드). 조작은 `Source/Games/Shooter3D/README.md`. `-gv_shooterAutoPlay=1` 이면 조준 · 사격도 AI 가 한다.
 *          아레나는 씬(`game/shooter3d/maps/arena.scene.xml` — 팩의 `data/gamesettings.xml` 시작 맵)과 그 안의 `ShooterGameModeComponent` ·
 *          `ShooterPlayerComponent` 가 섭니다. 이 클래스는 무기 카탈로그를 게임 서비스로 걸고(장비 아이템 · 외형 데이터는 바탕 `GameInstance` 가 gamesettings 로 건다), 첫 씬을 열고, 상태 저장 전에 판의 진행(웨이브 · 처치 수)을 싣고 디렉터가 세운 것을 걷으며, 복원 뒤 진행을 돌려줍니다.
 */
#include "pch.h"

#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"
#include "GameFramework/Runtime/Character/Combat/Weapon/Weapon.h"
#include "GameFramework/Runtime/Foundation/Framework/Flow/GameInstance.h"
#include "GameFramework/Runtime/Foundation/Framework/GameService.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"
#include "GameFramework/Runtime/Online/Service/OnlineSession.h"

#include "Games/Shooter3D/ShooterGameModeComponent.h"
#include "Games/Shooter3D/ShooterOnlineFlow.h"
#include "Games/Shooter3D/ShooterPlayerComponent.h"

#include "ModuleAPI/Export/GameModuleExports.h"

namespace sw
{
    /** @brief 무기 · 탄 카탈로그를 들고 게임 서비스로 겁니다(아이템 · 외형은 바탕이 gamesettings 로 건다) — 모듈이 다시 올라오면 새 인스턴스가 다시 건다. */
    class Shooter3DGame : public GameInstance
    {
    public:
        Shooter3DGame();
        ~Shooter3DGame() override;

    protected:
        bool onInitialize() override;
        void onUpdate( float32 deltaTime ) override;

    private:
        /** @brief 탄마다 이 게임의 두 표면(적의 몸 `Body`, 상자 · 바닥 `Cover`)에 맞음 표 줄이 있는지 봅니다. 없으면 탄마다 오류를 남기고 false 입니다(탄착이 말없이 사라지지 않게). */
        [[nodiscard]] bool checkImpacts() const;

    private:
        shared_ptr<const WeaponCatalog>     _pWeaponCatalog;     ///< 공유 캐시의 한 벌(게임 범위 서비스)
        shared_ptr<const ProjectileCatalog> _pProjectileCatalog; ///< 공유 캐시의 한 벌(게임 범위 서비스)
        ShooterOnlineFlow                   _onlineFlow;         ///< 온라인 키트를 쓰는 흐름(게임 범위 서비스). 연결은 세션(`OnlineSession`)이 맡는다
    };
} // namespace sw

namespace sw
{
    Shooter3DGame::Shooter3DGame()
        : _pWeaponCatalog{}
        , _pProjectileCatalog{}
        , _onlineFlow{}
    {
        // 상태 스냅샷에 오르는 컴포넌트 — 저장 전에 상태를 싣고 세운 것을 걷으며, 복원 뒤 돌려준다.
        getStateSnapshot().registerGameMode<ShooterGameModeComponent>();
        getStateSnapshot().registerViewOwner<ShooterPlayerComponent>();
    }

    Shooter3DGame::~Shooter3DGame() = default;

    bool Shooter3DGame::onInitialize()
    {
        // 무기는 필수다. 아이템 · 외형은 gamesettings 의 <RequiredData> 로 바탕(GameInstance)이 이미 읽어 게임 범위에 걸었다 — 무기 표가 가리키는 아이템을 그 카탈로그로 본다.
        if ( loadRequiredCatalog( _pWeaponCatalog ) == false || loadRequiredCatalog( _pProjectileCatalog ) == false )
            return false;
        // 무기는 기본 탄을 받아야 하고(태그 조건), 탄마다 두 표면의 탄착이 있어야 한다 — 틀리면 이유를 남기고 게임을 띄우지 않는다.
        if ( _pWeaponCatalog->checkProjectiles( *_pProjectileCatalog ) == false || checkImpacts() == false )
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
        bindScopedService( &_onlineFlow );
        // 아레나 씬(시작 맵)을 연다. 에디터가 자기 시작 씬을 열면 그 요청이 뒤에 와서 이긴다.
        (void)getSubsystem<GameSceneFlow>().requestFirstScene();
        return true;
    }

    void Shooter3DGame::onUpdate( float32 deltaTime )
    {
        (void)deltaTime;
        _onlineFlow.update( getSubsystem<OnlineSession>() );
    }

    bool Shooter3DGame::checkImpacts() const
    {
        const hashed_string arrSurface[] = { hashed_string( "Body" ), hashed_string( "Cover" ) };
        bool                bComplete    = true;
        for ( const ProjectileDef& projectile : _pProjectileCatalog->getAll() )
        {
            for ( const hashed_string& surface : arrSurface )
            {
                if ( _pProjectileCatalog->findImpact( surface, projectile._tags ) != nullptr )
                    continue;
                SW_LOG_ERROR_ID( "Shooter3DGame.ProjectileNoImpact", "[Shooter] projectile '%#' has no <Impact surface=\"%#\"> row in projectiles.xml", projectile._id.c_str(), surface.c_str() );
                bComplete = false;
            }
        }
        return bComplete;
    }
} // namespace sw
SW_IMPLEMENT_GAME_MODULE( sw::Shooter3DGame );
