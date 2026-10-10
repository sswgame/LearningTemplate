#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/Physics/RigidBodyComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

#include "Games/Empty/BenchScene.h"

namespace sw
{
    namespace
    {
        struct BenchSceneRigInternal
        {
            static constexpr const utf8* kKnightMesh     = "game/shooter3d/models/kaykit/knight.mesh";
            static constexpr const utf8* kKnightSkeleton = "game/shooter3d/models/kaykit/knight/knight.skeleton.json";
            static constexpr const utf8* kKnightClips    = "game/shooter3d/models/kaykit/knight/clips";
            static constexpr const utf8* kKnightState    = "2H_Ranged_Aiming";
            static constexpr const utf8* kWeaponMesh     = "game/shooter3d/models/kaykit/rogue/parts/2h_crossbow.mesh";
            static constexpr const utf8* kCapeMesh       = "game/shooter3d/models/kaykit/knight/parts/knight_cape.mesh";
            static constexpr const utf8* kKnightRig      = "game/shooter3d/rigs/knight.rig.json";
            static constexpr const utf8* kWeaponRig      = "game/shooter3d/rigs/knight_weapon.rig.json";
            static constexpr const utf8* kCapeRig        = "game/shooter3d/rigs/knight_cape.rig.json";
            static constexpr const utf8* kCapeSkeleton   = "game/shooter3d/rigs/knight_cape.skeleton.json";
            static constexpr const utf8* kWeaponSockets  = "game/shooter3d/rigs/crossbow_2h.sockets.xml";
            /** @brief 기울기(Z 축 둘레, 라디안) — +X 쪽이 높다. 두 발이 다른 높이에 서게 한다. */
            static constexpr float32 kSlopeAngle = 0.3f;
            static constexpr float32 kRigSpacing = 1.4f;

            /**
             * @brief 스킨 없는 메시를 본 하나(0)에 가중치 1 로 묶은 스킨드 사본을 만듭니다 — 암묵 스켈레톤 유닛(무기)이 뿌리 본 포즈로 움직인다.
             */
            static shared_ptr<Mesh> createRigidSkinnedCopy( const Mesh& source )
            {
                shared_ptr<Mesh> copy = Mesh::create();
                copy->setVertices( source.getVertices() );
                copy->setSkin( vector<MeshSkinVertex>( source.getVertices().size(), MeshSkinVertex{} ), 1 );
                return copy;
            }

            /**
             * @brief 망토 메시를 망토 스켈레톤(`knight_cape.skeleton.json` — 뿌리 + 아래로 사슬)에 높이로 나눠 묶은 스킨드 사본을 만듭니다.
             * @details 원본 망토(KayKit)는 스킨이 없는 단단한 부품이다 — 스프링 본을 보이려고 데모가 뼈 데이터를 두고 가중치를 높이로 나눈다
             *          (실제 게임은 DCC 에서 스킨한 망토). 정점 높이가 사슬 본 두 개 사이면 둘에 선형으로 나눈다.
             */
            static shared_ptr<Mesh> createCapeSkinnedCopy( const Mesh& source, const Skeleton& skeleton )
            {
                Pose             reference;
                vector<float4x4> listModel;
                reference.setToReference( skeleton );
                reference.computeModelSpace( skeleton.getParentIndices(), listModel );
                const uint32             boneCount  = skeleton.getBoneCount();
                const vector<RHIVertex>& listVertex = source.getVertices();
                vector<MeshSkinVertex>   listSkin( listVertex.size() );
                for ( size_t index = 0; index < listVertex.size() && boneCount >= 2; ++index )
                {
                    const float32 y     = listVertex[index]._arrPosition[1];
                    uint32        lower = 0;
                    while ( lower + 2 < boneCount && y < listModel[lower + 1].getTranslation()._y )
                    {
                        ++lower;
                    }
                    const float32   top      = listModel[lower].getTranslation()._y;
                    const float32   bottom   = listModel[lower + 1].getTranslation()._y;
                    const float32   fraction = MathUtil::saturate( ( top - y ) / MathUtil::max( top - bottom, 1e-4f ) );
                    MeshSkinVertex& skin     = listSkin[index];
                    skin._arrJoint[0]        = static_cast<uint16>( lower );
                    skin._arrJoint[1]        = static_cast<uint16>( lower + 1 );
                    skin._arrWeight[0]       = 1.0f - fraction;
                    skin._arrWeight[1]       = fraction;
                }
                shared_ptr<Mesh> copy = Mesh::create();
                copy->setVertices( listVertex );
                copy->setSkin( std::move( listSkin ), boneCount );
                return copy;
            }

            /** @brief 오브젝트에 유닛(스켈레탈 메시)을 붙이고 메시 id 를 풉니다. */
            static SkeletalMeshComponent* addUnit( GameObject& object, const utf8* pMeshID )
            {
                SkeletalMeshComponent* pUnit = object.addComponent<SkeletalMeshComponent>();
                if ( pUnit == nullptr )
                    return nullptr;
                pUnit->setMeshID( pMeshID );
                pUnit->resolveRenderAssets();
                return pUnit;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    /**
     * @brief `-gv_benchRig=N` — 후처리 리그를 건 기사 N 명(1 이면 데모 장면, 여럿이면 리그 비용 측정).
     * @details 기사마다 무기(쇠뇌) · 망토 유닛이 자식으로 붙는다. 비용을 재려면 `-gv_benchRigEnabled=0` 과 짝으로 돌려
     *          `GT.Animation.evaluate` 차이를 기사 수로 나눈다.
     */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_benchRig, 0, "후처리 리그 데모 기사 수 (0=사용 안 함, 발 디딤 · 시선 · 왼손 IK · 망토 스프링)" );

    /** @brief `-gv_benchRigEnabled=0` — 리그 컴포넌트를 끈 채 세웁니다(리그 비용 대조군). */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_benchRigEnabled, 1, "리그 데모의 PoseModifierComponent 를 켤지 (0=대조군)" );

    /** @brief `-gv_benchRigView=N` — 리그 데모 카메라(기사 한 명일 때): 0 왼쪽 앞(시선 · 왼손 · 발) · 1 정면 · 2 오른쪽 옆(쇠뇌 · 망토) · 3 오른쪽 뒤(망토 · 발). */
    SW_TEST_GLOBAL_VARIABLE_SHIPPED( int32, gv_benchRigView, 0, "리그 데모 카메라 (0 왼쪽 앞 · 1 정면 · 2 오른쪽 옆 · 3 오른쪽 뒤)" );

    /** @brief 벤치 공통 스위치 — 0 이면 시간 구동 변화를 멈춘다(BenchScene.cpp 가 정의). */
    SW_EXTERN_GLOBAL_VARIABLE( int32, gv_benchAnimate );

    uint32 BenchScene::getRigCharacterCount()
    {
        return static_cast<uint32>( MathUtil::max( gv_benchRig, 0 ) );
    }

    void BenchScene::spawnRigCharacters( uint32 characterCount )
    {
        SceneManager* pSceneManager = game::getService<SceneManager>();
        Scene*        pScene        = ( pSceneManager != nullptr ) ? pSceneManager->getActiveScene() : nullptr;
        if ( pScene == nullptr && pSceneManager != nullptr )
            pScene = pSceneManager->createEmptyActiveScene( "BenchScene" );
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        pScene->ensureDefaultCameras();
        GameObjectManager& objects = *pScene->getObjectManager();
        const bool         bRig    = gv_benchRigEnabled != 0;

        // 기울기 — 물리(정적 상자, 발 디딤의 광선이 맞는다)와 그림(같은 자리 · 크기의 큐브).
        const float32 halfWidth = MathUtil::max( 3.0f, 0.5f * static_cast<float32>( characterCount ) * BenchSceneRigInternal::kRigSpacing + 1.0f );
        const float3  slopeRotation{ 0.0f, 0.0f, BenchSceneRigInternal::kSlopeAngle };
        const float3  slopeCenter{ 0.0f, -0.25f, 0.0f };
        if ( GameObject* pSlope = objects.createGameObject( "RigSlope" ) )
        {
            RigidBodyComponent* pBody = pSlope->addComponent<RigidBodyComponent>();
            if ( pBody != nullptr )
            {
                PhysicsShapeDesc3D box{};
                box._halfExtents = float3{ halfWidth, 0.25f, 3.0f };
                pBody->setShape( box );
                pBody->setBodyType( PhysicsBodyType::Static );
                pBody->setLocalPosition( slopeCenter );
                pBody->setLocalRotation( slopeRotation );
                _listBenchExtra.push_back( pBody->getHandle() );
            }
            MeshComponent* pVisual = pSlope->addComponent<MeshComponent>();
            if ( pVisual != nullptr )
            {
                pVisual->setMesh( MeshUtil::createPrimitive( "Cube" ) );
                pVisual->setMaterial( pScene->getMaterial() );
                pVisual->setLocalPosition( slopeCenter );
                pVisual->setLocalRotation( slopeRotation );
                pVisual->setLocalScale( float3{ halfWidth * 2.0f, 0.5f, 6.0f } );
                pVisual->setVisible( true );
            }
        }

        // 시선 목표 — 작은 구. update 가 기사 앞에서 원을 그린다.
        if ( GameObject* pLook = objects.createGameObject( "RigLookTarget" ) )
        {
            MeshComponent* pBall = pLook->addComponent<MeshComponent>();
            if ( pBall != nullptr )
            {
                pBall->setMesh( MeshUtil::createPrimitive( "Sphere" ) );
                pBall->setMaterial( pScene->getMaterial() );
                pBall->setLocalScale( float3{ 0.12f } );
                pBall->setLocalPosition( float3{ 0.6f, 1.5f, -1.2f } );
                pBall->setVisible( true );
                _rigLookTarget = pBall->getHandle();
                _listBenchExtra.push_back( pBall->getHandle() );
            }
        }

        SocketKindTable       kinds;
        shared_ptr<SocketSet> weaponSockets = make_shared<SocketSet>();
        if ( kinds.loadFromResource( SocketKindTable::kDefaultPath ) == false ||
             weaponSockets->loadFromResource( BenchSceneRigInternal::kWeaponSockets, kinds ) == false )
            SW_LOG_ERROR( "[Bench] 쇠뇌 소켓 '%#' 을 읽지 못했습니다.", BenchSceneRigInternal::kWeaponSockets );

        const float32 origin = -0.5f * static_cast<float32>( characterCount - 1 ) * BenchSceneRigInternal::kRigSpacing;
        for ( uint32 index = 0; index < characterCount; ++index )
        {
            StringBuilder<constant::kMaxBuffer64> name;
            name.append( "RigKnight_" ).append( index );
            GameObject* pKnight = objects.createGameObject( hashed_string( name.c_str(), name.size() ) );
            if ( pKnight == nullptr )
                continue;
            SkeletalMeshComponent*     pBody     = BenchSceneRigInternal::addUnit( *pKnight, BenchSceneRigInternal::kKnightMesh );
            SkeletalAnimatorComponent* pAnimator = pKnight->addComponent<SkeletalAnimatorComponent>();
            if ( pBody == nullptr || pAnimator == nullptr )
                continue;
            pBody->setSkeletonPath( BenchSceneRigInternal::kKnightSkeleton );
            // 기사는 +Z 를 본다 — 카메라(-Z 쪽)를 보게 돌린다. 줄은 X 로 — 기울기라 사람마다 땅 높이가 다르다.
            const float32 x = origin + static_cast<float32>( index ) * BenchSceneRigInternal::kRigSpacing;
            pBody->setLocalPosition( float3{ x, MathUtil::tan( BenchSceneRigInternal::kSlopeAngle ) * x, 0.0f } );
            pBody->setLocalRotation( float3{ 0.0f, MathUtil::kPi, 0.0f } );
            pAnimator->setClipFolder( BenchSceneRigInternal::kKnightClips );
            pAnimator->setInitialState( BenchSceneRigInternal::kKnightState );
            _listRigBody.push_back( pBody->getHandle() );
            _listBenchExtra.push_back( pBody->getHandle() );

            // 무기 유닛 — 오른손 소켓 자리를 리그(CopyTransform)가 따른다. 스킨 없는 쇠뇌를 뿌리 본에 묶은 사본으로 그린다.
            GameObject*            pWeapon     = objects.createGameObject( "RigWeapon" );
            SkeletalMeshComponent* pWeaponUnit = ( pWeapon != nullptr ) ? BenchSceneRigInternal::addUnit( *pWeapon, BenchSceneRigInternal::kWeaponMesh ) : nullptr;
            if ( pWeaponUnit != nullptr && pWeaponUnit->getMesh() != nullptr )
            {
                (void)pWeapon->attachToParent( pKnight ); // 방금 만든 오브젝트끼리라 순환이 없어 실패하지 않는다
                pWeaponUnit->setMesh( BenchSceneRigInternal::createRigidSkinnedCopy( *pWeaponUnit->getMesh() ) );
                pWeaponUnit->resolveRenderAssets();
                PoseModifierComponent* pWeaponRig = pWeapon->addComponent<PoseModifierComponent>();
                if ( pWeaponRig != nullptr )
                {
                    pWeaponRig->setRigPath( BenchSceneRigInternal::kWeaponRig );
                    pWeaponRig->bindUnit( "Body", pBody );
                    pWeaponRig->setRigEnabled( bRig );
                }
                _listBenchExtra.push_back( pWeaponUnit->getHandle() );
            }

            // 망토 유닛 — 데모 데이터의 사슬 뼈 넷. 뿌리는 가슴 자리를 따르고(CopyTransform) 나머지는 스프링이다.
            GameObject*            pCape     = objects.createGameObject( "RigCape" );
            SkeletalMeshComponent* pCapeUnit = ( pCape != nullptr ) ? BenchSceneRigInternal::addUnit( *pCape, BenchSceneRigInternal::kCapeMesh ) : nullptr;
            if ( pCapeUnit != nullptr && pCapeUnit->getMesh() != nullptr )
            {
                (void)pCape->attachToParent( pKnight ); // 방금 만든 오브젝트끼리라 순환이 없어 실패하지 않는다
                pCapeUnit->setSkeletonPath( BenchSceneRigInternal::kCapeSkeleton );
                pCapeUnit->setMesh( BenchSceneRigInternal::createCapeSkinnedCopy( *pCapeUnit->getMesh(), pCapeUnit->getSkeleton() ) );
                pCapeUnit->resolveRenderAssets();
                PoseModifierComponent* pCapeRig = pCape->addComponent<PoseModifierComponent>();
                if ( pCapeRig != nullptr )
                {
                    pCapeRig->setRigPath( BenchSceneRigInternal::kCapeRig );
                    pCapeRig->bindUnit( "Body", pBody );
                    pCapeRig->setRigEnabled( bRig );
                }
                _listBenchExtra.push_back( pCapeUnit->getHandle() );
            }

            PoseModifierComponent* pBodyRig = pKnight->addComponent<PoseModifierComponent>();
            if ( pBodyRig != nullptr )
            {
                pBodyRig->setRigPath( BenchSceneRigInternal::kKnightRig );
                pBodyRig->bindUnit( "Weapon", pWeaponUnit, weaponSockets );
                pBodyRig->setRigEnabled( bRig );
            }
        }

        const float32 halfExtent = MathUtil::max( 2.0f, halfWidth );
        spawnLight( pScene, halfExtent );
        frameRigCameras( pScene );
        SW_LOG_INFO( "[Bench] 리그 기사 %#명을 기울기 위에 세웠습니다(리그 %#).", characterCount, bRig ? "켬" : "끔" );
    }

    void BenchScene::frameRigCameras( Scene* pScene )
    {
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        const uint32  count    = static_cast<uint32>( _listRigBody.size() );
        const float32 halfSpan = 0.5f * static_cast<float32>( count ) * BenchSceneRigInternal::kRigSpacing;
        for ( CameraComponent* pCamera : pScene->getObjectManager()->getCameraRegistry().getAll() )
        {
            if ( count <= 1 )
            {
                // 한 명 — 상반신 · 다리가 함께 들어오게 비스듬히 가까이(손잡이 · 시선 · 망토 · 발이 한 화면).
                const float3 arrEye[] = {
                    float3{-2.3f, 1.5f, -2.9f},
                    float3{ 0.0f, 1.2f, -3.4f},
                    float3{ 3.4f, 1.2f,  0.0f},
                    float3{ 2.9f, 1.7f,  3.3f}
                };
                pCamera->setLocalPosition( arrEye[MathUtil::clamp( gv_benchRigView, 0, 3 )] );
                pCamera->lookAt( float3{ 0.0f, 0.8f, 0.0f } );
                continue;
            }
            const float32 tanHalf  = MathUtil::tan( pCamera->getFieldOfViewY() * 0.5f );
            const float32 distance = ( tanHalf > MathUtil::kEpsilon ) ? ( halfSpan * 1.2f / tanHalf ) : ( halfSpan * 3.0f );
            pCamera->setLocalPosition( float3{ 0.0f, 1.4f, -distance } );
            pCamera->lookAt( float3{ 0.0f, 0.9f, 0.0f } );
        }
    }

    void BenchScene::updateRigCharacters( float32 deltaTime )
    {
        (void)deltaTime;
        if ( _listRigBody.empty() )
            return;
        SceneManager*      pSceneManager = game::getService<SceneManager>();
        Scene*             pScene        = ( pSceneManager != nullptr ) ? pSceneManager->getActiveScene() : nullptr;
        GameObjectManager* pObjects      = ( pScene != nullptr ) ? pScene->getObjectManager() : nullptr;
        if ( pObjects == nullptr )
            return;

        // 거리 LOD 기준점 = 첫 카메라(스프링 본의 lod_distance).
        const vector<CameraComponent*>& listCamera = pObjects->getCameraRegistry().getAll();
        if ( listCamera.empty() == false )
            pObjects->getAnimationSystem().setLodViewPosition( listCamera.front()->getCameraPosition() );

        // `-gv_benchAnimate=0` 이면 멈춘 그림(스크린샷 비교) — 시선 목표 · 몸 틀기를 움직이지 않는다.
        const float32 time = ( gv_benchAnimate != 0 ) ? _benchElapsed : 1.0f;
        if ( MeshComponent* pBall = castTo<MeshComponent>( pObjects->resolveComponent( _rigLookTarget ) ) )
            pBall->setLocalPosition( float3{ 0.9f * MathUtil::sin( time * 0.9f ), 1.35f + 0.35f * MathUtil::sin( time * 1.7f ), -1.1f } );
        // 몸을 좌우로 틀어 망토가 관성으로 흔들리게 한다(스프링 본은 월드 공간 입자라 몸이 돌면 뒤처진다).
        const float32 yaw = MathUtil::kPi + ( ( gv_benchAnimate != 0 ) ? 0.45f * MathUtil::sin( time * 2.2f ) : 0.0f );
        for ( const ComponentHandle handle : _listRigBody )
        {
            if ( SkeletalMeshComponent* pBody = castTo<SkeletalMeshComponent>( pObjects->resolveComponent( handle ) ) )
                pBody->setLocalRotation( float3{ 0.0f, yaw, 0.0f } );
        }
    }
} // namespace sw
