/**
 * @file ArenaViewComponent.h
 * @brief 아레나 키트의 클라이언트 쪽 — 디렉터(`ArenaGameModeComponent`)의 탄을 투사체 프리팹으로 세우고 유닛에 종류별 색을 입힙니다.
 * @details 키트 세 갈래의 클라이언트 몫이다(`GF_Client_AbilityArena`, 전용 서버는 짓지 않는다). 디렉터의 읽기 API(`getUnits` · `getProjectiles`)만 쓰고 규칙을 고치지 않는다.
 *          디렉터는 같은 오브젝트에 먼저 붙인다. 등록(`onRegister`)에서 찾아 핸들로 든다. `PostUpdate` 라 디렉터 걸음 · 발사 뒤의 틱 뒤 작업(게임 스레드)에서 이번 프레임의 판을 본다 —
 *          이번 프레임에 스폰한 유닛과 쏜 탄도 그 프레임에 색 · 모습을 받는다. 무엇이 새 것인지는 늘기만 하는 유닛 번호 · 탄 id 로 안다.
 *          쓰러짐 연출은 유닛 프리팹의 `ArenaDeathSquashComponent` 가 한다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/AbilityArena/ArenaUnitView.h"
#include "GameFramework/Runtime/Foundation/GameMode/PrefabPool.h"
#include "GameFramework/Runtime/Gameplay/Appearance/MaterialTintCache.h"

namespace sw
{
    class ArenaGameModeComponent;
    class GameObjectManager;
    struct ProjectileState;

    /**
     * @class ArenaViewComponent
     * @brief 디렉터와 같은 오브젝트에 붙이는 아레나 뷰입니다. 끝난 투사체 모습은 지우지 않고 꺼 두었다가 다시 씁니다(`PrefabPool`).
     * @details 색은 메시의 머티리얼(씬이 늘 보관하는 팔레트와 기본 머티리얼)에서 만든 인스턴스를 같은 색끼리 나눠 쓴다(`MaterialTintCache`) — 색마다 머티리얼 에셋을 두면 그 에셋이
     *          처음 스폰할 때 올라오고 마지막 것이 사라질 때 내려가며 렌더 스레드의 병렬 기록과 겹친다.
     *          세운 모습은 파생이라 상태에 들지 않는다(`SpawnLifetime::Derived`).
     */
    REFLECT( Category = "AbilityArena", DisplayName = "Arena View", Tooltip = "Client side of an arena: shows the projectiles and tints the units of the Arena Game Mode on the same object" )
    class SW_KIT_CLIENT_API ArenaViewComponent : public GameModeViewComponent
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate;

        ArenaViewComponent();
        virtual ~ArenaViewComponent() override;

        /** @brief 투사체 풀을 정하고 틱 그룹을 뷰의 `PostUpdate` 로 둡니다. */
        void onBeginPlay() override;
        /** @brief 세운 모습을 걷습니다. */
        void onEndPlay() override;

    private:
        /** @brief 세운 모습을 모두 지웁니다(플레이 끝 · 다시 세울 때). 모습은 파생이라 상태를 실을 때 걷지 않는다(`SpawnLifetime::Derived`). */
        void despawnViews();
        /** @brief 틱 뒤 게임 스레드: 유닛에 색을 입히고 투사체 모습을 맞춥니다. */
        void updateViews( GameObjectManager& manager, const ArenaGameModeComponent& director );
        /** @brief 유닛마다 종류의 색을 입힙니다(입은 색은 메시가 거른다). */
        void tintUnits( GameObjectManager& manager, const ArenaGameModeComponent& director );
        /** @brief 탄이 끝나 스스로 숨은 모습을 풀에 돌려주고, 처음 보는 탄에 모습을 세웁니다. */
        void syncProjectiles( GameObjectManager& manager, const ArenaGameModeComponent& director );
        /** @brief 탄 하나의 모습을 풀에서 꺼냅니다. 모습이 없어도 탄은 난다. */
        void showProjectile( GameObjectManager& manager, const ArenaGameModeComponent& director, const ProjectileState& projectile );
        /** @brief 유닛 종류의 색(PROPERTY)입니다. */
        const float4& findTint( ArenaUnitKind kind ) const;

    private:
        PROPERTY( Required, Category = "Prefabs", DisplayName = "Projectile Prefab", Tooltip = "View of one projectile (a mesh and an ArenaProjectileComponent)" )
        AssetRef<PrefabAsset> _projectilePrefab;
        PROPERTY( Required, Category = "Prefabs", DisplayName = "Projectile Pool Size", Tooltip = "Finished projectile views kept switched off for reuse instead of being destroyed", Min = 1 )
        uint32 _projectilePoolSize;
        PROPERTY( Category = "Look", DisplayName = "Player Tint", Meta = "Color", Tooltip = "Multiplies the palette texture of the player model" )
        float4 _playerTint;
        PROPERTY( Category = "Look", DisplayName = "Grunt Tint", Meta = "Color", Tooltip = "Multiplies the palette texture of the grunt model" )
        float4 _gruntTint;
        PROPERTY( Category = "Look", DisplayName = "Caster Tint", Meta = "Color", Tooltip = "Multiplies the palette texture of the caster model" )
        float4 _casterTint;
        PROPERTY( Category = "Look", DisplayName = "Projectile Color", Meta = "Color", Tooltip = "Colour of the projectile sphere" )
        float4 _projectileTint;

        SW_TRANSIENT MaterialTintCache _tintCache;
        SW_TRANSIENT PrefabPool        _projectilePool;   ///< 투사체의 모습. 탄 자체는 디렉터의 시뮬레이션이다
        SW_TRANSIENT uint32            _nextProjectileID; ///< 이 id 부터는 처음 보는 탄이다
    };
} // namespace sw
