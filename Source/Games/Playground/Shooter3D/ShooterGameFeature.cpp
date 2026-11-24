/**
 * @file ShooterGameFeature.cpp
 * @brief 게임 기능 `Shooter3D` 입니다 — gamesettings `gameFeatures` 가 게임 범위에 켜, 사격장 미니게임(`minigames/shooter3d`)의 무기 표(`data/weapons.xml`)와 자동 플레이 값을 겁니다.
 * @details 미니게임 범위가 아니라 게임 범위인 까닭: 한 프로세스 재생(에디터의 넷 시뮬 — `ShooterNetSimGame`)의 월드는 미니게임에 들어가지 않고 아레나 씬을 풀어 무기를 찾는다.
 *          탄 표(`data/projectiles.xml`)는 여러 미니게임이 나눠 쓰므로 `PlaygroundAbilities` 가 먼저 건다(gamesettings 의 순서).
 *          무기가 가리키는 탄 · 아이템과 탄마다의 탄착은 여기서 본다 — 틀리면 이유를 남기고 게임을 띄우지 않는다(쏘아도 말없이 탄이 없는 무기, 말없이 사라지는 탄착을 만들지 않는다).
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Name/hashed_string.h"

#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileDef.h"
#include "GameFramework/Runtime/Character/Combat/Weapon/Weapon.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureRegistry.h"
#include "GameFramework/Runtime/Foundation/GameFeature/GameFeatureScope.h"
#include "GameFramework/Runtime/Foundation/Service/GameService.h"
#include "GameFramework/Runtime/Gameplay/Inventory/ItemCatalog.h"

#include "Games/Playground/Shooter3D/ShooterAutoAimControllerComponent.h"

namespace sw
{
    namespace
    {
        struct ShooterGameFeatureInternal
        {
            /** @brief 무기가 받는 탄마다 사격장의 두 표면(적의 몸 `Body`, 상자 · 바닥 `Cover`)의 맞음 표 줄이 있는지 봅니다. 없으면 탄마다 오류를 남기고 false 입니다. */
            [[nodiscard]] static bool validateImpacts( const WeaponCatalog& weapons, const ProjectileCatalog& projectiles )
            {
                const hashed_string arrSurface[] = { hashed_string( "Body" ), hashed_string( "Cover" ) };
                bool                bComplete    = true;
                for ( const ProjectileDef& projectile : projectiles.getAll() )
                {
                    const bool bLoaded = VectorUtil::containsIf( weapons.getAll(), [&projectile]( const WeaponDef& weapon )
                    { return weapon.canAccept( projectile ); } ); // 무기가 받지 않는 탄(다른 미니게임의 것)은 이 표면을 맞지 않는다
                    for ( const hashed_string& surface : arrSurface )
                    {
                        if ( bLoaded == false || projectiles.findImpact( surface, projectile._tags ) != nullptr )
                            continue;
                        SW_LOG_ERROR_ID( "Shooter.ProjectileNoImpact", "[Shooter] projectile '%#' has no <Impact surface=\"%#\"> row in projectiles.xml", projectile._id.c_str(), surface.c_str() );
                        bComplete = false;
                    }
                }
                return bComplete;
            }

            /** @brief 무기 표의 item · ammo 가 아이템 카탈로그(gamesettings `<RequiredData kind="items">`)에 있는지 봅니다. */
            [[nodiscard]] static bool validateItems( const WeaponCatalog& weapons )
            {
                const ItemCatalog* const pItemCatalog = game::findSubsystem<const ItemCatalog>();
                if ( pItemCatalog == nullptr )
                {
                    SW_LOG_ERROR_ID( "Shooter.ItemCatalogMissing", "[Shooter] no item catalog - gamesettings must declare <RequiredData kind=\"items\">" );
                    return false;
                }
                vector<WeaponItemReference> listUnknownItem;
                if ( weapons.findUnknownItems( *pItemCatalog, listUnknownItem ) )
                    return true;
                for ( const WeaponItemReference& reference : listUnknownItem )
                {
                    SW_LOG_ERROR_ID( "Shooter.WeaponNamesUnknownItem", "[Shooter] weapon '%#' %# names '%#', which is not in the item catalog", reference._weaponID.c_str(), reference._pField,
                                     reference._itemID.c_str() );
                }
                return false;
            }

            [[nodiscard]] static bool activate( GameFeatureScope& scope )
            {
                if ( scope.loadCatalog<WeaponCatalog>() == false || scope.emplaceService<ShooterAutoPlayOptions>() == false )
                    return false;
                const WeaponCatalog* const     pWeapons     = game::findSubsystem<const WeaponCatalog>();
                const ProjectileCatalog* const pProjectiles = game::findSubsystem<const ProjectileCatalog>();
                if ( pWeapons == nullptr || pProjectiles == nullptr )
                {
                    SW_LOG_ERROR_ID( "Shooter.ProjectileCatalogMissing", "[Shooter] no projectile catalog in the game scope - gamesettings gameFeatures must list PlaygroundAbilities before Shooter3D" );
                    return false;
                }
                return pWeapons->validateProjectiles( *pProjectiles ) && validateImpacts( *pWeapons, *pProjectiles ) && validateItems( *pWeapons );
            }
        };
    } // namespace

    SW_GAME_FEATURE( Shooter3D, "Shooter3D", &ShooterGameFeatureInternal::activate );
} // namespace sw
