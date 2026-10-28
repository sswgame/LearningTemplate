#include "pch.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Animation/Component/SkeletalAnimatorComponent.h"
#include "Engine/Animation/Component/SkeletalMeshComponent.h"
#include "Engine/Animation/Scene/AnimationSystem.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Physics/Component/RigidBodyComponent.h"
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
            /**
             * @brief 스킨 없는 메시를 본 하나(0)에 가중치 1 로 묶은 스킨드 사본을 만듭니다 — 암묵 스켈레톤 유닛(무기)이 루트 본 포즈로 움직인다.
             */
            static shared_ptr<Mesh> createRigidSkinnedCopy( const Mesh& source )
            {
                shared_ptr<Mesh> copy = Mesh::create();
                copy->setVertices( source.getVertices() );
                copy->setSkin( vector<MeshSkinVertex>( source.getVertices().size(), MeshSkinVertex{} ), 1 );
                return copy;
            }

            /**
             * @brief 망토 메시를 망토 스켈레톤(`knight_cape.skeleton.xml` — 루트 + 아래로 사슬)에 높이로 나눠 묶은 스킨드 사본을 만듭니다.
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

            /** @brief 오브젝트에 유닛(스켈레탈 메시)을 붙이고 메시 id 와 머티리얼(KayKit 아틀라스)을 풉니다. */
            static SkeletalMeshComponent* addUnit( GameObject& object, const string& meshID, const string& materialPath )
            {
                SkeletalMeshComponent* pUnit = object.addComponent<SkeletalMeshComponent>();
                if ( pUnit == nullptr )
                    return nullptr;
                pUnit->setMeshID( meshID.c_str() );
                pUnit->setMaterialPath( materialPath );
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
     * @details 기사마다 무기(쇠뇌) · 망토 유닛이 자식으로 붙는다. 비용을 측정하려면 `-gv_benchRigEnabled=0` 과 짝으로 돌려
     *          `GT.Animation.evaluate` 차이를 기사 수로 나눈다.
     */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchRig, 0, "후처리 리그 데모 기사 수 (0=사용 안 함, 발 디딤 · 시선 · 왼손 IK · 망토 스프링)" );

    /** @brief `-gv_benchRigEnabled=0` — 리그 컴포넌트를 끈 채 세웁니다(리그 비용 대조군). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchRigEnabled, 1, "리그 데모의 PoseModifierComponent 를 켤지 (0=대조군)" );

    /** @brief `-gv_benchRigView=N` — 리그 데모 카메라(기사 한 명일 때): 0 왼쪽 앞(시선 · 왼손 · 발) · 1 정면 · 2 오른쪽 옆(쇠뇌 · 망토) · 3 오른쪽 뒤(망토 · 발). */
    SW_TEST_GLOBAL_VARIABLE( int32, gv_benchRigView, 0, "리그 데모 카메라 (0 왼쪽 앞 · 1 정면 · 2 오른쪽 옆 · 3 오른쪽 뒤)" );

    /** @brief 벤치 공통 스위치 — 0 이면 시간 구동 변화를 멈춘다(BenchScene.cpp 가 정의). */
    SW_EXTERN_GLOBAL_VARIABLE( int32, gv_benchAnimate );

    uint32 BenchScene::getRigCharacterCount()
    {
        return static_cast<uint32>( MathUtil::max( gv_benchRig, 0 ) );
    }

    int32 BenchScene::getRigView()
    {
        return gv_benchRigView;
    }

    void BenchScene::spawnRigSlope( Scene* pScene, GameObjectManager& objects, float32 halfWidth )
    {
        // 기울어진 바닥입니다. 물리용 정적 상자(발 디딤의 광선이 맞는 대상)와, 같은 위치와 크기의 큐브(화면에 보이는 것)로 이루어집니다.
        const float3  slopeRotation{ 0.0f, 0.0f, _settings._rigSlopeAngle };
        const float3& slopeCenter = _settings._rigSlopeCenter;
        GameObject*   pSlope      = objects.createGameObject( "RigSlope" );
        if ( pSlope == nullptr )
            return;
        RigidBodyComponent* pBody = pSlope->addComponent<RigidBodyComponent>();
        if ( pBody != nullptr )
        {
            PhysicsShapeDesc3D box{};
            box._halfExtents = float3{ halfWidth, _settings._rigSlopeHalfThickness, _settings._rigSlopeHalfDepth };
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
            pVisual->setLocalScale( float3{ halfWidth * 2.0f, _settings._rigSlopeHalfThickness * 2.0f, _settings._rigSlopeHalfDepth * 2.0f } );
            pVisual->setVisible( true );
        }
    }

    void BenchScene::spawnRigLookTarget( Scene* pScene, GameObjectManager& objects )
    {
        // 시선 목표인 작은 구입니다. update 가 기사 앞에서 원을 그리며 움직입니다.
        GameObject* pLook = objects.createGameObject( "RigLookTarget" );
        if ( pLook == nullptr )
            return;
        MeshComponent* pBall = pLook->addComponent<MeshComponent>();
        if ( pBall == nullptr )
            return;
        pBall->setMesh( MeshUtil::createPrimitive( "Sphere" ) );
        pBall->setMaterial( pScene->getMaterial() );
        pBall->setLocalScale( float3{ _settings._rigLookTargetScale } );
        pBall->setLocalPosition( _settings._rigLookTargetStart );
        pBall->setVisible( true );
        _rigLookTarget = TComponentHandle<MeshComponent>{ pBall };
        _listBenchExtra.push_back( pBall->getHandle() );
    }

    shared_ptr<const Skeleton> BenchScene::createSingleBoneSkeleton()
    {
        shared_ptr<Skeleton> skeleton = make_shared<Skeleton>();
        (void)skeleton->addBone( hashed_string( "root" ), -1, BoneTransform{}, float4x4::Identity );
        return skeleton;
    }

    SkeletalMeshComponent* BenchScene::spawnRigWeapon( GameObjectManager& objects, GameObject* pKnight, SkeletalMeshComponent* pBody, bool bRig )
    {
        // 무기 유닛입니다. 리그(CopyTransform)가 오른손 소켓 위치를 따라갑니다. 스킨이 없는 쇠뇌를 루트 본에 묶은 사본으로 그립니다.
        GameObject*            pWeapon     = objects.createGameObject( "RigWeapon" );
        SkeletalMeshComponent* pWeaponUnit = ( pWeapon != nullptr ) ? BenchSceneRigInternal::addUnit( *pWeapon, _settings._rigWeaponMesh, _settings._rigWeaponMaterial ) : nullptr;
        // 사본을 만들 원본은 지금 있어야 한다 — 컴포넌트의 비동기 로드를 기다리지 않고 같은 로드를 동기로 받는다(`MeshCache::acquire`).
        const shared_ptr<Mesh> weaponSource = MeshCache::acquire( _settings._rigWeaponMesh );
        if ( pWeaponUnit == nullptr || weaponSource == nullptr )
            return pWeaponUnit;
        (void)pWeapon->attachToParent( pKnight ); // 방금 만든 오브젝트끼리라 순환이 없어 실패하지 않는다
        // 리그는 데이터 스켈레톤을 받은 유닛에만 묶인다(암묵 스켈레톤이면 묶지 않아 쇠뇌가 손을 따르지 못하고 바닥에 남는다) — 루트 본 하나를 데이터로 준다.
        pWeaponUnit->setSkeleton( createSingleBoneSkeleton() );
        pWeaponUnit->setMesh( BenchSceneRigInternal::createRigidSkinnedCopy( *weaponSource ) );
        pWeaponUnit->resolveRenderAssets();
        PoseModifierComponent* pWeaponRig = pWeapon->addComponent<PoseModifierComponent>();
        if ( pWeaponRig != nullptr )
        {
            pWeaponRig->setRigPath( _settings._rigWeaponRig.c_str() );
            pWeaponRig->bindUnit( "Body", pBody );
            pWeaponRig->setRigEnabled( bRig );
        }
        _listBenchExtra.push_back( pWeaponUnit->getHandle() );
        return pWeaponUnit;
    }

    void BenchScene::spawnRigCape( GameObjectManager& objects, GameObject* pKnight, SkeletalMeshComponent* pBody, bool bRig )
    {
        // 망토 유닛입니다. 데모 데이터에는 사슬 뼈가 네 개 있습니다. 루트 뼈는 가슴 위치를 따라가고(CopyTransform) 나머지는 스프링으로 움직입니다.
        GameObject*            pCape      = objects.createGameObject( "RigCape" );
        SkeletalMeshComponent* pCapeUnit  = ( pCape != nullptr ) ? BenchSceneRigInternal::addUnit( *pCape, _settings._rigCapeMesh, _settings._rigCapeMaterial ) : nullptr;
        const shared_ptr<Mesh> capeSource = MeshCache::acquire( _settings._rigCapeMesh ); // 무기와 같다 — 사본의 원본은 동기로 받는다
        if ( pCapeUnit == nullptr || capeSource == nullptr )
            return;
        (void)pCape->attachToParent( pKnight ); // 방금 만든 오브젝트끼리라 순환이 없어 실패하지 않는다
        pCapeUnit->setSkeletonPath( _settings._rigCapeSkeleton.c_str() );
        pCapeUnit->setMesh( BenchSceneRigInternal::createCapeSkinnedCopy( *capeSource, pCapeUnit->getSkeleton() ) );
        pCapeUnit->resolveRenderAssets();
        PoseModifierComponent* pCapeRig = pCape->addComponent<PoseModifierComponent>();
        if ( pCapeRig != nullptr )
        {
            pCapeRig->setRigPath( _settings._rigCapeRig.c_str() );
            pCapeRig->bindUnit( "Body", pBody );
            pCapeRig->setRigEnabled( bRig );
        }
        _listBenchExtra.push_back( pCapeUnit->getHandle() );
    }

    void BenchScene::spawnRigKnight( GameObjectManager& objects, uint32 index, float32 origin, bool bRig, const shared_ptr<SocketSet>& weaponSockets )
    {
        StringBuilder<constant::kMaxBuffer64> name;
        name.append( "RigKnight_" ).append( index );
        GameObject* pKnight = objects.createGameObject( hashed_string( name.c_str(), name.size() ) );
        if ( pKnight == nullptr )
            return;
        SkeletalMeshComponent*     pBody     = BenchSceneRigInternal::addUnit( *pKnight, _settings._knightMesh, _settings._knightMaterial );
        SkeletalAnimatorComponent* pAnimator = pKnight->addComponent<SkeletalAnimatorComponent>();
        if ( pBody == nullptr || pAnimator == nullptr )
            return;
        pBody->setSkeletonPath( _settings._knightSkeleton.c_str() );
        // 기사는 +Z 방향을 봅니다. 카메라(-Z 쪽)를 보도록 돌립니다. 기사는 X 축을 따라 한 줄로 서고, 바닥이 기울어 있어서 사람마다 땅 높이가 다릅니다.
        const float32 x = origin + static_cast<float32>( index ) * _settings._rigSpacing;
        pBody->setLocalPosition( float3{ x, MathUtil::tan( _settings._rigSlopeAngle ) * x, 0.0f } );
        pBody->setLocalRotation( float3{ 0.0f, MathUtil::kPi, 0.0f } );
        pAnimator->setClipFolder( _settings._knightClips.c_str() );
        pAnimator->setInitialState( _settings._rigAimState.c_str() );
        _listRigBody.push_back( TComponentHandle<SkeletalMeshComponent>{ pBody } );
        _listBenchExtra.push_back( pBody->getHandle() );

        SkeletalMeshComponent* pWeaponUnit = spawnRigWeapon( objects, pKnight, pBody, bRig );
        spawnRigCape( objects, pKnight, pBody, bRig );

        PoseModifierComponent* pBodyRig = pKnight->addComponent<PoseModifierComponent>();
        if ( pBodyRig != nullptr )
        {
            pBodyRig->setRigPath( _settings._rigKnightRig.c_str() );
            pBodyRig->bindUnit( "Weapon", pWeaponUnit, weaponSockets );
            pBodyRig->setRigEnabled( bRig );
        }
    }

    void BenchScene::spawnRigCharacters( uint32 characterCount )
    {
        SceneManager* pSceneManager = game::findSubsystem<SceneManager>();
        Scene*        pScene        = ( pSceneManager != nullptr ) ? pSceneManager->getActiveScene() : nullptr;
        if ( pScene == nullptr && pSceneManager != nullptr )
            pScene = pSceneManager->createEmptyActiveScene( "BenchScene" );
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        pScene->pickActiveGameCamera();
        GameObjectManager& objects = *pScene->getObjectManager();
        const bool         bRig    = gv_benchRigEnabled != 0;

        const float32 halfWidth =
            MathUtil::max( _settings._rigSlopeMinHalfWidth, 0.5f * static_cast<float32>( characterCount ) * _settings._rigSpacing + _settings._rigSlopeEndMargin );
        spawnRigSlope( pScene, objects, halfWidth );
        spawnRigLookTarget( pScene, objects );

        SocketKindTable       kinds;
        shared_ptr<SocketSet> weaponSockets = make_shared<SocketSet>();
        if ( kinds.loadDefault() == false )
            SW_LOG_ERROR( "[Bench] Failed to read the default socket kind table the crossbow sockets need." );
        else if ( weaponSockets->loadFromAsset( AssetID{ _settings._rigWeaponSockets.c_str() }, kinds ) == false )
            SW_LOG_ERROR( "[Bench] Failed to read the crossbow sockets '%#'.", _settings._rigWeaponSockets.c_str() );

        const float32 origin = -0.5f * static_cast<float32>( characterCount - 1 ) * _settings._rigSpacing;
        for ( uint32 index = 0; index < characterCount; ++index )
        {
            spawnRigKnight( objects, index, origin, bRig, weaponSockets );
        }

        const float32 halfExtent = MathUtil::max( _settings._rigMinHalfExtent, halfWidth );
        spawnLight( pScene, halfExtent );
        applyFraming( pScene, FramingMode::Rig );
        SW_LOG_INFO( "[Bench] Placed %# rig knights on the slope (rig %#).", characterCount, bRig ? "on" : "off" );
    }

    void BenchScene::frameRigCameras( Scene* pScene )
    {
        if ( pScene == nullptr || pScene->getObjectManager() == nullptr )
            return;
        const uint32  count    = static_cast<uint32>( _listRigBody.size() );
        const float32 halfSpan = 0.5f * static_cast<float32>( count ) * _settings._rigSpacing;
        for ( CameraComponent* pCamera : pScene->getObjectManager()->getCameraRegistry().getAll() )
        {
            if ( isFramedCamera( pCamera ) == false )
                continue;
            if ( count <= 1 )
            {
                // 한 명 — 상반신 · 다리가 함께 들어오게 비스듬히 가까이(손잡이 · 시선 · 망토 · 발이 한 화면).
                const float3 arrEye[] = { _settings._rigViewFrontLeft, _settings._rigViewFront, _settings._rigViewRightSide, _settings._rigViewBackRight };
                pCamera->setLocalPosition( arrEye[MathUtil::clamp( gv_benchRigView, 0, 3 )] );
                pCamera->lookAt( _settings._rigViewFocus );
                continue;
            }
            const float32 tanHalf  = MathUtil::tan( pCamera->getFieldOfViewY() * 0.5f );
            const float32 distance = ( tanHalf > MathUtil::kEpsilon ) ? ( halfSpan * _settings._characterFrameMargin / tanHalf ) : ( halfSpan * 3.0f );
            pCamera->setLocalPosition( float3{ 0.0f, _settings._characterEyeHeight, -distance } );
            pCamera->lookAt( float3{ 0.0f, _settings._characterLookHeight, 0.0f } );
        }
    }

    void BenchScene::updateRigCharacters( float32 deltaTime )
    {
        (void)deltaTime;
        if ( _listRigBody.empty() )
            return;
        SceneManager*      pSceneManager = game::findSubsystem<SceneManager>();
        Scene*             pScene        = ( pSceneManager != nullptr ) ? pSceneManager->getActiveScene() : nullptr;
        GameObjectManager* pObjects      = ( pScene != nullptr ) ? pScene->getObjectManager() : nullptr;
        if ( pObjects == nullptr )
            return;

        // 거리 LOD 기준점 = 첫 카메라(스프링 본의 lod_distance).
        const vector<CameraComponent*>& listCamera = pObjects->getCameraRegistry().getAll();
        if ( listCamera.empty() == false )
            pObjects->getSubsystem<AnimationSystem>().setLODViewPosition( listCamera.front()->getCameraPosition() );

        // `-gv_benchAnimate=0` 이면 화면을 멈춰 둡니다(스크린샷 비교용). 시선 목표와 몸 돌리기를 움직이지 않습니다.
        const float32 time = ( gv_benchAnimate != 0 ) ? _benchElapsed : _settings._rigStillTime;
        if ( MeshComponent* pBall = pObjects->resolveComponent( _rigLookTarget ) )
            pBall->setLocalPosition( float3{ _settings._rigLookSwing * MathUtil::sin( time * _settings._rigLookSwingRate ),
                                             _settings._rigLookHeight + _settings._rigLookBob * MathUtil::sin( time * _settings._rigLookBobRate ), _settings._rigLookDepth } );
        // 몸을 좌우로 틀어 망토가 관성으로 흔들리게 한다(스프링 본은 월드 공간 입자라 몸이 돌면 뒤처진다).
        const float32 yaw = MathUtil::kPi + ( ( gv_benchAnimate != 0 ) ? _settings._rigSwayAngle * MathUtil::sin( time * _settings._rigSwayRate ) : 0.0f );
        for ( const TComponentHandle<SkeletalMeshComponent> handle : _listRigBody )
        {
            if ( SkeletalMeshComponent* pBody = pObjects->resolveComponent( handle ) )
                pBody->setLocalRotation( float3{ 0.0f, yaw, 0.0f } );
        }
    }
} // namespace sw
