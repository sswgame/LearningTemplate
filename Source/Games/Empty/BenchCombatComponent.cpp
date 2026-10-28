#include "pch.h"

#include "Games/Empty/BenchCombatComponent.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Character/AnimationNotify/AnimationNotifyComponent.h"
#include "Engine/Character/Hit/CharacterHit.h"
#include "Engine/Character/Hit/RagdollComponent.h"
#include "Engine/Character/Socket/SocketBindingComponent.h"
#include "Engine/Character/Socket/SocketSetComponent.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
#include "Engine/Scene/Scene.h"

#include "Games/Empty/BenchSceneSettings.h"

namespace sw
{
    SW_LOG_CALLER( "BenchCombat" );

    namespace
    {
        struct BenchCombatComponentInternal
        {
            static constexpr const utf8* kHandSocket = "handslot.r";
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
        , _shooterRange{ 0.0f }
    {
        setCanEverTick( true );
    }

    void BenchCombatComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        // 애니메이션과 물리가 끝난 뒤에 실행합니다. 이번 프레임의 손 위치로 칼을 맞추고, 이번 프레임에 울린 알림을 셉니다.
        setTickGroup( TickGroup::PostUpdate );
    }

    GameObject* BenchCombatComponent::spawnEnemy( GameObjectManager& objects, const BenchSceneSettings& settings, const hashed_string& name,
                                                  const float3& position )
    {
        // 적에게 필요한 컴포넌트를 붙입니다. 유닛, 애니메이터(걷기와 발소리 알림), 알림 디스패치, 소켓 테이블, 래그돌(히트 존, 움찔, 기상)입니다.
        GameObject* pEnemy = objects.createGameObject( name );
        if ( pEnemy == nullptr )
            return nullptr;
        SkeletalMeshComponent*     pUnit     = pEnemy->addComponent<SkeletalMeshComponent>();
        SkeletalAnimatorComponent* pAnimator = pEnemy->addComponent<SkeletalAnimatorComponent>();
        AnimationNotifyComponent*  pNotify   = pEnemy->addComponent<AnimationNotifyComponent>();
        SocketSetComponent*        pSockets  = pEnemy->addComponent<SocketSetComponent>();
        RagdollComponent*          pRagdoll  = pEnemy->addComponent<RagdollComponent>();
        pUnit->setMeshID( settings._combatEnemyMesh );
        pUnit->setMaterialPath( settings._combatEnemyMaterial );
        pUnit->setSkeletonPath( settings._combatEnemySkeleton );
        pUnit->resolveRenderAssets();
        pUnit->setAnimateWhenOffscreen( true );
        pUnit->setLocalPosition( position );
        // KayKit 모델은 +Z 방향을 봅니다. 카메라(-Z 쪽)를 보도록 돌립니다.
        pUnit->setLocalRotation( float3{ 0.0f, MathUtil::kPi, 0.0f } );
        pAnimator->setClipFolder( settings._combatEnemyClips );
        pAnimator->setInitialState( "Walking_A" );
        pNotify->setNotifyTablePath( settings._combatEnemyData + ".notifies.xml" );
        pSockets->setSocketSetPath( settings._combatEnemyData + ".sockets.xml" );
        pRagdoll->setPhysicsAssetPath( settings._combatEnemyData + ".physics.xml" );
        pRagdoll->setFlinchClip( hashed_string( "Hit_A" ) );
        pRagdoll->setGetUpClips( hashed_string( "Lie_StandUp" ), hashed_string{}, hashed_string( "Idle" ) );
        return pEnemy;
    }

    void BenchCombatComponent::spawnScene( Scene& scene, const BenchSceneSettings& settings, vector<GameObjectHandle>& outListObject )
    {
        GameObjectManager* pObjects = scene.getObjectManager();
        if ( pObjects == nullptr )
            return;

        // 바닥은 정적 강체(재질 Stone, 발소리가 고르는 바닥 종류)와 화면에 보이는 평면으로 구성합니다.
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

        GameObject* pEnemy = spawnEnemy( *pObjects, settings, hashed_string( "CombatEnemy" ), float3{} );
        outListObject.push_back( pEnemy->getHandle() );

        // 칼은 강체(손을 따라갈 때는 키네마틱, 손에서 떨어지면 동적)와 메시, 소켓 부착으로 구성합니다.
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
        pSwordMesh->setMeshID( settings._combatSwordMesh );
        pSwordMesh->setMaterialPath( settings._combatSwordMaterial );
        (void)pBinding;
        outListObject.push_back( pSword->getHandle() );

        // 연출 오브젝트가 프레임 순서대로 전투 연출을 진행합니다.
        GameObject*           pDirector = pObjects->createGameObject( hashed_string( "CombatDirector" ) );
        BenchCombatComponent* pCombat   = pDirector->addComponent<BenchCombatComponent>();
        pCombat->_enemy                 = pEnemy->getHandle();
        pCombat->_sword                 = pSword->getHandle();
        pCombat->_shooterRange          = settings._combatShooterRange;
        outListObject.push_back( pDirector->getHandle() );
    }

    void BenchCombatComponent::followHandSocket( GameObjectManager& manager, GameObject& enemy, GameObject& sword ) const
    {
        (void)manager;
        SocketBindingComponent* pBinding = sword.getComponent<SocketBindingComponent>();
        SkeletalMeshComponent*  pUnit    = enemy.getComponent<SkeletalMeshComponent>();
        float4x4                hand;
        const bool              bHandUnavailable = pBinding == nullptr || pUnit == nullptr || pUnit->findBoneModelTransform( hashed_string( BenchCombatComponentInternal::kHandSocket ), hand ) == false;
        if ( bHandUnavailable )
            return;
        // 유닛이 적의 루트이므로 모델 공간이 곧 루트 기준입니다. 아직 붙지 않았다면 붙이고, 이미 붙어 있다면 바뀐 손 위치만 넘깁니다.
        if ( pBinding->getState() == SocketBindingState::Bound && pBinding->getHolder() == enemy.getHandle() )
            pBinding->updateSocketTransform( hand );
        else if ( _stage < 3 )
            (void)pBinding->bindToSocket( &enemy, hashed_string( BenchCombatComponentInternal::kHandSocket ), hand ); // 못 붙이면 바인딩이 그대로이고 다음 갱신이 다시 붙인다
    }

    void BenchCombatComponent::shootAt( GameObjectManager& manager, GameObject& enemy, const utf8* pBone, bool bFatal )
    {
        float3 target{};
        (void)SocketLookupUtil::findSocketWorldPosition( enemy, hashed_string( pBone ), target ); // 벤치 — 없으면 원점을 쏠 뿐이다
        // 머리와 가슴 뼈는 해당 바디의 아래쪽 끝에 있습니다. 바디 가운데 높이에 맞도록 조금 올려서 쏩니다.
        target._y += 0.15f;
        const float3 muzzle = target + float3{ 0.0f, 0.0f, -_shooterRange };
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

        const AnimationNotifyComponent* pNotify = pEnemy->getComponent<AnimationNotifyComponent>();
        static const hashed_string      s_footstep( "Footstep" );
        for ( size_t actionIndex = 0; pNotify != nullptr && actionIndex < pNotify->getActions().size(); ++actionIndex )
        {
            const AnimationNotifyAction& action = pNotify->getActions()[actionIndex];
            if ( action._handler == s_footstep )
            {
                ++_footstepCount;
                SW_LOG_INFO( "[BenchCombat] t=%# footstep '%#' surface '%#'", _elapsed, action._notify.c_str(), action._detail.c_str() );
            }
        }

        // 물리, 질의, 스폰은 게임 스레드에서 해야 하므로 틱이 끝난 뒤로 미룹니다. 이 틱은 워커 스레드에서 실행될 수 있습니다.
        const TComponentHandle<BenchCombatComponent> self{ this };
        pManager->executeOrDeferPostTick( [pManager, self]()
        {
            BenchCombatComponent* pCombat      = pManager->resolveComponent( self );
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
                RagdollComponent* pRagdoll        = pEnemyObject->getComponent<RagdollComponent>();
                const bool        bRagdollSettled = pRagdoll != nullptr && pRagdoll->isSettled() && pRagdoll->getUp();
                if ( bRagdollSettled )
                    SW_LOG_INFO( "[BenchCombat] t=%# ragdoll settled -> get up", pCombat->_elapsed );
                pCombat->_stage = 4;
            }
        } );

        const uint32 second = static_cast<uint32>( _elapsed );
        if ( second != _loggedSecond )
        {
            _loggedSecond = second;
            // `[[maybe_unused]]` 를 붙인 이유: 아래 SW_LOG_INFO 에서만 쓰이는데, 그 매크로는 Shipping 빌드에서 사라집니다.
            [[maybe_unused]] const RagdollComponent* pRagdoll = pEnemy->getComponent<RagdollComponent>();
            [[maybe_unused]] const float3            sword    = pSword->getPrimarySceneComponent() != nullptr ? pSword->getPrimarySceneComponent()->getWorldPosition() : float3{};
            SW_LOG_INFO( "[BenchCombat] t=%# footsteps %# ragdoll state %# sword (%#, %#, %#)", second, _footstepCount,
                         pRagdoll != nullptr ? static_cast<uint32>( pRagdoll->getState() ) : 0u, sword._x, sword._y, sword._z );
        }
    }
} // namespace sw
