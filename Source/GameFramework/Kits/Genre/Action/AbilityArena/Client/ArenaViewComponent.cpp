#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Log/LogOnce.h"
#include "Core/Log/Logger.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Visual/Component/MeshComponent.h"

#include "GameFramework/Client/View/GameModeViewComponent.h"
#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Genre/Action/AbilityArena/ArenaUnitView.h"
#include "GameFramework/Kits/Genre/Action/AbilityArena/Client/ArenaProjectileComponent.h"
#include "GameFramework/Kits/Genre/Action/AbilityArena/Component/ArenaGameModeComponent.h"
#include "GameFramework/Runtime/Character/Combat/Projectile/ProjectileSimulation.h"
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
    class ArenaViewComponent : public GameModeViewComponent
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

    ArenaViewComponent::ArenaViewComponent()
        : GameModeViewComponent( GameModeViewBinding::of<&ArenaViewComponent::updateViews>() )
        , _projectilePrefab{}
        , _projectilePoolSize{ 0 }
        , _playerTint{ 0.55f, 0.75f, 1.0f, 1.0f }
        , _gruntTint{ 1.0f, 0.5f, 0.45f, 1.0f }
        , _casterTint{ 0.8f, 0.55f, 1.0f, 1.0f }
        , _projectileTint{ 1.0f, 0.6f, 0.1f, 1.0f }
        , _tintCache{}
        , _projectilePool{ string{}, 0, SpawnLifetime::Derived }
        , _nextProjectileID{ 0 }
    {
        setCanEverTick( true );
    }

    ArenaViewComponent::~ArenaViewComponent() = default;

    void ArenaViewComponent::onBeginPlay()
    {
        GameModeViewComponent::onBeginPlay();
        _projectilePool = PrefabPool( _projectilePrefab.path(), _projectilePoolSize, SpawnLifetime::Derived );
    }

    void ArenaViewComponent::onEndPlay()
    {
        despawnViews();
        GameModeViewComponent::onEndPlay();
    }

    void ArenaViewComponent::despawnViews()
    {
        GameObject* const        pOwner   = getOwner();
        GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr;
        if ( pManager != nullptr )
            _projectilePool.destroyAll( *pManager );
        _nextProjectileID = 0;
    }

    void ArenaViewComponent::updateViews( GameObjectManager& manager, const ArenaGameModeComponent& director )
    {
        tintUnits( manager, director );
        syncProjectiles( manager, director );
    }

    void ArenaViewComponent::tintUnits( GameObjectManager& manager, const ArenaGameModeComponent& director )
    {
        for ( const ArenaUnit& unit : director.getUnits() )
        {
            GameObject* const    pBody = manager.resolveGameObject( unit._object );
            MeshComponent* const pMesh = pBody != nullptr ? pBody->getComponent<MeshComponent>() : nullptr;
            if ( pMesh != nullptr )
                (void)_tintCache.apply( *pMesh, findTint( unit._kind ) ); // 아직 머티리얼이 없으면 다음 프레임에 다시 건다
        }
    }

    void ArenaViewComponent::syncProjectiles( GameObjectManager& manager, const ArenaGameModeComponent& director )
    {
        // 다 쓴 모습은 틱에서 스스로 숨는다(`ArenaProjectileComponent::isFinished`). 끄기는 구조 변경이라 여기(틱 밖)서 풀에 돌려주고, 새 탄이 다시 쓴다.
        _projectilePool.releaseBusyIf( manager, []( const GameObject& object )
        {
            const ArenaProjectileComponent* pView = object.getComponent<ArenaProjectileComponent>();
            return pView == nullptr || pView->isFinished();
        } );
        uint32 nextProjectileID = _nextProjectileID;
        for ( const ProjectileState& projectile : director.getProjectiles() )
        {
            if ( projectile._id < _nextProjectileID )
                continue;
            nextProjectileID = MathUtil::max( nextProjectileID, projectile._id + 1 );
            showProjectile( manager, director, projectile );
        }
        _nextProjectileID = nextProjectileID;
    }

    void ArenaViewComponent::showProjectile( GameObjectManager& manager, const ArenaGameModeComponent& director, const ProjectileState& projectile )
    {
        GameObject* const               pObject = _projectilePool.acquire( manager, "ArenaProjectile" );
        ArenaProjectileComponent* const pView   = pObject != nullptr ? pObject->getComponent<ArenaProjectileComponent>() : nullptr;
        if ( pView == nullptr )
        {
            if ( pObject != nullptr )
            {
                SW_LOG_ERROR_ID( "ArenaView.ProjectilePrefabNoView", "[Arena] projectile prefab '%#' has no ArenaProjectileComponent - the shot flies unseen", _projectilePrefab.c_str() );
                _projectilePool.destroyAll( manager );
            }
            return;
        }
        MeshComponent* const pMesh = pObject->getComponent<MeshComponent>();
        if ( pMesh != nullptr )
            if ( _tintCache.apply( *pMesh, _projectileTint ) == false )
                SW_LOG_ERROR_ONCE_ID( "ArenaView.ProjectileWithoutMaterial", "[Arena]: prefab '%#' has a mesh without a material - it keeps the prefab colour", _projectilePrefab.c_str() );
        pView->bind( director.getOwner()->getHandle(), projectile._id, projectile._position );
    }

    const float4& ArenaViewComponent::findTint( ArenaUnitKind kind ) const
    {
        switch ( kind )
        {
            case ArenaUnitKind::Player:
                return _playerTint;
            case ArenaUnitKind::Grunt:
                return _gruntTint;
            case ArenaUnitKind::Caster:
                return _casterTint;
        }
        return _playerTint; // 열거자는 셋뿐이다(covered-switch) — 닿지 않는다
    }
} // namespace sw

#include "ArenaViewComponent.cpp.gen.inl"
