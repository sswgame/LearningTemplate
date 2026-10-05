#include "pch.h"

#include "Games/Empty/BenchCombatComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/AnimNotify/AnimNotifyComponent.h"
#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/Character/Socket/SocketBindingComponent.h"
#include "Engine/Character/Socket/SocketSetComponent.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"

namespace sw
{
    SW_LOG_CALLER( "BenchCombat" );

    namespace
    {
        struct BenchCombatComponentInternal
        {
            static constexpr const utf8* kEnemyMesh     = "game/shooter3d/models/kaykit/skeleton_warrior.mesh";
            static constexpr const utf8* kEnemySkeleton = "game/shooter3d/models/kaykit/skeleton_warrior/skeleton_warrior.skeleton.json";
            static constexpr const utf8* kEnemyClips    = "game/shooter3d/models/kaykit/skeleton_warrior/clips";
            static constexpr const utf8* kCharacterData = "game/shooter3d/characters/skeleton_warrior/skeleton_warrior";
            static constexpr const utf8* kSwordMesh     = "game/shooter3d/models/kaykit/skeleton_blade.mesh";
            static constexpr const utf8* kHandSocket    = "handslot.r";
            static constexpr float32     kShooterRange  = 3.0f;

            static string makeDataPath( const utf8* pSuffix ) { return string( kCharacterData ) + pSuffix; }
        };
    } // namespace
} // namespace sw

namespace sw
{
    BenchCombatComponent::BenchCombatComponent()
        : _enemy{}
        , _sword{}
        , _elapsed{ 0.0f }
        , _frame{ 0 }
        , _stage{ 0 }
        , _footstepCount{ 0 }
        , _loggedSecond{ 0 }
    {
        setCanEverTick( true );
    }

    void BenchCombatComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 애니메이션 · 물리 뒤 — 이번 프레임의 손 자리로 칼을 맞추고, 이번 프레임에 울린 알림을 센다.
        setTickGroup( TickGroup::PostUpdate );
    }

    void BenchCombatComponent::spawnScene( Scene& scene, vector<GameObjectHandle>& outListObject )
    {
        using Internal              = BenchCombatComponentInternal;
        GameObjectManager* pObjects = scene.getObjectManager();
        if ( pObjects == nullptr )
            return;

        // 바닥 — 정적 강체(재질 Stone — 발소리의 바닥 종류) + 보이는 평면.
        GameObject*         pFloor     = pObjects->createGameObject( hashed_string( "CombatFloor" ) );
        RigidBodyComponent* pFloorBody = pFloor->addComponent<RigidBodyComponent>();
        MeshComponent*      pFloorMesh = pFloor->addComponent<MeshComponent>();
        PhysicsShapeDesc3D  floorBox;
        floorBox._halfExtents   = float3{ 10.0f, 0.5f, 10.0f };
        floorBox._localPosition = float3{ 0.0f, -0.5f, 0.0f };
        pFloorBody->setShape( floorBox );
        pFloorBody->setBodyType( PhysicsBodyType::Static );
        pFloorBody->setLayer( hashed_string( "Static" ) );
        pFloorBody->setMaterial( hashed_string( "Stone" ) );
        pFloorMesh->setMesh( MeshUtil::createPlane( 16 ) );
        pFloorMesh->setLocalScale( float3{ 12.0f, 1.0f, 12.0f } );
        outListObject.push_back( pFloor->getHandle() );

        // 적 — 유닛 · 애니메이터(걷기, 발소리 알림) · 알림 디스패치 · 소켓 표 · 래그돌(히트 존 · 움찔 · 기상).
        GameObject*                pEnemy    = pObjects->createGameObject( hashed_string( "CombatEnemy" ) );
        SkeletalMeshComponent*     pUnit     = pEnemy->addComponent<SkeletalMeshComponent>();
        SkeletalAnimatorComponent* pAnimator = pEnemy->addComponent<SkeletalAnimatorComponent>();
        AnimNotifyComponent*       pNotify   = pEnemy->addComponent<AnimNotifyComponent>();
        SocketSetComponent*        pSockets  = pEnemy->addComponent<SocketSetComponent>();
        RagdollComponent*          pRagdoll  = pEnemy->addComponent<RagdollComponent>();
        pUnit->setMeshId( Internal::kEnemyMesh );
        pUnit->setSkeletonPath( Internal::kEnemySkeleton );
        pUnit->resolveRenderAssets();
        pUnit->setAnimateWhenOffscreen( true );
        // KayKit 은 +Z 를 본다 — 카메라(-Z 쪽)를 보도록 돌린다.
        pUnit->setLocalRotation( float3{ 0.0f, MathUtil::Pi, 0.0f } );
        pAnimator->setClipFolder( Internal::kEnemyClips );
        pAnimator->setInitialState( "Walking_A" );
        pNotify->setNotifyTablePath( Internal::makeDataPath( ".notifies.xml" ) );
        pSockets->setSocketSetPath( Internal::makeDataPath( ".sockets.xml" ) );
        pRagdoll->setPhysicsAssetPath( Internal::makeDataPath( ".physics.xml" ) );
        pRagdoll->setFlinchClip( hashed_string( "Hit_A" ) );
        pRagdoll->setGetUpClips( hashed_string( "Lie_StandUp" ), hashed_string{}, hashed_string( "Idle" ) );
        outListObject.push_back( pEnemy->getHandle() );

        // 칼 — 강체(손을 따르는 키네마틱 → 떼면 동적) + 메시 + 소켓 부착.
        GameObject*             pSword     = pObjects->createGameObject( hashed_string( "CombatSword" ) );
        RigidBodyComponent*     pSwordBody = pSword->addComponent<RigidBodyComponent>();
        MeshComponent*          pSwordMesh = pSword->addComponent<MeshComponent>();
        SocketBindingComponent* pBinding   = pSword->addComponent<SocketBindingComponent>();
        PhysicsShapeDesc3D      blade;
        blade._halfExtents   = float3{ 0.08f, 0.62f, 0.04f };
        blade._localPosition = float3{ 0.0f, 0.42f, 0.0f };
        pSwordBody->setShape( blade );
        pSwordBody->setBodyType( PhysicsBodyType::Kinematic );
        pSwordBody->setLayer( hashed_string( "Debris" ) );
        pSwordBody->setMaterial( hashed_string( "Metal" ) );
        pSwordMesh->setMeshId( Internal::kSwordMesh );
        (void)pBinding;
        outListObject.push_back( pSword->getHandle() );

        // 연출 — 시간표를 돈다.
        GameObject*           pDirector = pObjects->createGameObject( hashed_string( "CombatDirector" ) );
        BenchCombatComponent* pCombat   = pDirector->addComponent<BenchCombatComponent>();
        pCombat->_enemy                 = pEnemy->getHandle();
        pCombat->_sword                 = pSword->getHandle();
        outListObject.push_back( pDirector->getHandle() );
    }

    void BenchCombatComponent::followHandSocket( GameObjectManager& manager, GameObject& enemy, GameObject& sword ) const
    {
        (void)manager;
        SocketBindingComponent* pBinding = sword.getComponent<SocketBindingComponent>();
        SkeletalMeshComponent*  pUnit    = enemy.getComponent<SkeletalMeshComponent>();
        float4x4                hand;
        if ( pBinding == nullptr || pUnit == nullptr || pUnit->findBoneModelTransform( hashed_string( BenchCombatComponentInternal::kHandSocket ), hand ) == false )
            return;
        // 유닛은 적의 루트다 — 모델 공간이 곧 루트 기준이다. 붙기 전이면 붙이고, 붙어 있으면 바뀐 손 자리만 넘긴다.
        if ( pBinding->getState() == SocketBindingState::Bound && pBinding->getHolder() == enemy.getHandle() )
            pBinding->updateSocketTransform( hand );
        else if ( _stage < 3 )
            (void)pBinding->bindToSocket( &enemy, hashed_string( BenchCombatComponentInternal::kHandSocket ), hand );
    }

    void BenchCombatComponent::shootAt( GameObjectManager& manager, GameObject& enemy, const utf8* pBone, bool bFatal )
    {
        float3 target{};
        (void)SocketLookupUtil::findSocketWorldPosition( enemy, hashed_string( pBone ), target );
        // 머리 · 가슴 뼈는 그 바디의 아래 끝이다 — 바디 가운데 높이로 조금 올려 쏜다.
        target._y += 0.15f;
        const float3 muzzle = target + float3{ 0.0f, 0.0f, -BenchCombatComponentInternal::kShooterRange };
        HitInfo      hit;
        if ( bFatal == false )
        {
            if ( CharacterHitUtil::traceWeaponHit( manager, muzzle, float3{ 0.0f, 0.0f, 1.0f }, 10.0f, 0xFFFFFFFFu, getOwner(), 20.0f, 40.0f, false, hit ) )
                SW_LOG_INFO( "[BenchCombat] t=%# hit '%#' zone '%#' x%# (body %#)", _elapsed, pBone, hit._zone.c_str(), hit._damageMultiplier, hit._bodyIndex );
            return;
        }
        CharacterRayHit rayHit;
        if ( CharacterHitUtil::raycast3D( manager, muzzle, float3{ 0.0f, 0.0f, 1.0f }, 10.0f, 0xFFFFFFFFu, 0, rayHit ) == false || rayHit._pObject == nullptr )
            return;
        hit._pInstigator = getOwner();
        hit._body        = rayHit._body;
        hit._point       = rayHit._point;
        hit._normal      = rayHit._normal;
        hit._direction   = float3{ 0.0f, 0.0f, 1.0f };
        hit._damage      = 100.0f;
        hit._impulse     = 180.0f;
        hit._bFatal      = true;
        CharacterHitUtil::resolveHitZone( *rayHit._pObject, rayHit._body, hit );
        CharacterHitUtil::deliverHit( *rayHit._pObject, hit );
        SW_LOG_INFO( "[BenchCombat] t=%# fatal hit '%#' zone '%#' x%# -> ragdoll", _elapsed, pBone, hit._zone.c_str(), hit._damageMultiplier );
    }

    void BenchCombatComponent::onTick( float32 deltaTime )
    {
        Component::onTick( deltaTime );
        GameObject*        pOwner   = getOwner();
        GameObjectManager* pManager = pOwner != nullptr ? pOwner->getManager() : nullptr;
        GameObject*        pEnemy   = pManager != nullptr ? pManager->resolveGameObject( _enemy ) : nullptr;
        GameObject*        pSword   = pManager != nullptr ? pManager->resolveGameObject( _sword ) : nullptr;
        if ( pEnemy == nullptr || pSword == nullptr )
            return;
        _elapsed += deltaTime;
        ++_frame;

        const AnimNotifyComponent* pNotify = pEnemy->getComponent<AnimNotifyComponent>();
        static const hashed_string s_footstep( "Footstep" );
        for ( size_t actionIndex = 0; pNotify != nullptr && actionIndex < pNotify->getActions().size(); ++actionIndex )
        {
            const AnimNotifyAction& action = pNotify->getActions()[actionIndex];
            if ( action._handler == s_footstep )
            {
                ++_footstepCount;
                SW_LOG_INFO( "[BenchCombat] t=%# footstep '%#' surface '%#'", _elapsed, action._notify.c_str(), action._detail.c_str() );
            }
        }

        // 물리 · 질의 · 스폰은 게임 스레드에서 — 틱 뒤로 미룬다(이 틱은 워커일 수 있다).
        const ComponentHandle self = getHandle();
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            BenchCombatComponent* pCombat      = castTo<BenchCombatComponent>( pManager->resolveComponent( self ) );
            GameObject*           pEnemyObject = pCombat != nullptr ? pManager->resolveGameObject( pCombat->_enemy ) : nullptr;
            GameObject*           pSwordObject = pCombat != nullptr ? pManager->resolveGameObject( pCombat->_sword ) : nullptr;
            if ( pEnemyObject == nullptr || pSwordObject == nullptr )
                return;
            pCombat->followHandSocket( *pManager, *pEnemyObject, *pSwordObject );
            if ( pCombat->_stage == 0 && pCombat->_frame >= 60 )
            {
                pCombat->shootAt( *pManager, *pEnemyObject, "head", false );
                pCombat->_stage = 1;
            }
            else if ( pCombat->_stage == 1 && pCombat->_frame >= 120 )
            {
                pCombat->shootAt( *pManager, *pEnemyObject, "chest", false );
                pCombat->_stage = 2;
            }
            else if ( pCombat->_stage == 2 && pCombat->_frame >= 180 )
            {
                pCombat->shootAt( *pManager, *pEnemyObject, "chest", true );
                SocketBindingComponent* pBinding = pSwordObject->getComponent<SocketBindingComponent>();
                if ( pBinding != nullptr && pBinding->release( SocketReleaseMode::Physics, float3{ 1.2f, 2.0f, -1.0f } ) )
                    SW_LOG_INFO( "[BenchCombat] t=%# sword released from '%#' to physics", pCombat->_elapsed, BenchCombatComponentInternal::kHandSocket );
                pCombat->_stage = 3;
            }
            else if ( pCombat->_stage == 3 && pCombat->_frame >= 900 )
            {
                RagdollComponent* pRagdoll = pEnemyObject->getComponent<RagdollComponent>();
                if ( pRagdoll != nullptr && pRagdoll->isSettled() && pRagdoll->getUp() )
                    SW_LOG_INFO( "[BenchCombat] t=%# ragdoll settled -> get up", pCombat->_elapsed );
                pCombat->_stage = 4;
            }
        } );

        const uint32 second = static_cast<uint32>( _elapsed );
        if ( second != _loggedSecond )
        {
            _loggedSecond = second;
            // `[[maybe_unused]]` — 아래 SW_LOG_INFO 에만 쓰이고 그 매크로는 Shipping 에서 사라진다.
            [[maybe_unused]] const RagdollComponent* pRagdoll = pEnemy->getComponent<RagdollComponent>();
            [[maybe_unused]] const float3            sword    = pSword->getPrimarySceneComponent() != nullptr ? pSword->getPrimarySceneComponent()->getWorldPosition() : float3{};
            SW_LOG_INFO( "[BenchCombat] t=%# footsteps %# ragdoll state %# sword (%#, %#, %#)", second, _footstepCount,
                         pRagdoll != nullptr ? static_cast<uint32>( pRagdoll->getState() ) : 0u, sword._x, sword._y, sword._z );
        }
    }
} // namespace sw
