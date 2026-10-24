/**
 * @file AnimationCrowdTestUtil.h
 * @brief 군중 포즈 공유 테스트가 함께 쓰는 사슬 스켈레톤 · 스킨드 큐브 · 반복 클립 · 군중 캐릭터 도우미입니다(`AnimationPoseAccessTest`).
 * @details `TestAnimationCrowd.cpp` 의 `TestAnimationCrowdInternal` 과 같은 모양이다. 그 파일을 쓰는 다른 작업과 겹치지 않게 따로 두었고, 합칠 때는 이 헤더 쪽으로 모은다.
 */
#pragma once
#include "Core/File/FileUtil.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Codec/Raw/RawAnimationCodec.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "EngineTest/AnimationTestUtil.h"
#include "EngineTest/AssetArrivalTestUtil.h"

#include "TestFramework/TestFramework.h"

namespace test
{
    /** @brief 사슬 2 본(bone0 → bone1) 스켈레톤입니다. */
    inline sw::shared_ptr<sw::Skeleton> makeCrowdSkeleton()
    {
        return sw::make_shared<sw::Skeleton>( makeChainSkeleton( 2 ) );
    }

    /** @brief 단위 큐브의 위쪽 정점(y > 0)을 bone1, 아래쪽을 bone0 에 묶은 스킨드 메시입니다. */
    inline sw::shared_ptr<sw::Mesh> makeCrowdSkinnedCube()
    {
        sw::shared_ptr<sw::Mesh>       cube = sw::MeshUtil::createUnitCube();
        sw::vector<sw::MeshSkinVertex> listSkin;
        for ( const sw::RHIVertex& vertex : cube->getVertices() )
        {
            sw::MeshSkinVertex skin{};
            skin._arrJoint[0] = vertex._arrPosition[1] > 0.0f ? 1u : 0u;
            listSkin.push_back( skin );
        }
        sw::shared_ptr<sw::Mesh> mesh = sw::Mesh::create();
        mesh->setVertices( cube->getVertices() );
        mesh->setSkin( listSkin, 2 );
        return mesh;
    }

    /** @brief 반복 클립 `Wave` 를 임시 폴더에 쓰고 그 폴더를 돌려줍니다(실패하면 빈 문자열). */
    inline sw::string writeCrowdWaveClip( const sw::Skeleton& skeleton )
    {
        sw::string                 folder = makeTempPath( "poseaccessclips" );
        const sw::AnimationRawClip wave   = makeChainRawClip( skeleton, 31, 30.0f, 0.6f );
        sw::AnimationClip          clip;
        clip.setName( sw::hashed_string( "Wave" ) );
        clip.setLooping( true );
        const bool bSaved = clip.compressFrom( wave, sw::RawAnimationCodec::getInstance(), sw::AnimationCodecSettings{}, nullptr ) &&
                            clip.saveToFile( sw::FileUtil::joinPath( folder, "wave.animclip" ) );
        if ( bSaved == false )
            folder.clear();
        return folder;
    }

    /** @brief 군중 공유를 켠(@p bShare) 캐릭터 하나(유닛 + 애니메이터, 상태 `Wave`)를 만들고 시작합니다. */
    inline sw::SkeletalMeshComponent* createCrowdCharacter( sw::GameObjectManager& manager, const utf8* pName, const sw::shared_ptr<sw::Skeleton>& skeleton,
                                                            const sw::shared_ptr<sw::Mesh>& mesh, const sw::string& folder, bool bShare = true )
    {
        sw::GameObject* pObject = manager.createGameObject( sw::hashed_string( pName ) );
        if ( pObject == nullptr )
            return nullptr;
        sw::SkeletalMeshComponent*     pUnit     = pObject->addComponent<sw::SkeletalMeshComponent>();
        sw::SkeletalAnimatorComponent* pAnimator = pObject->addComponent<sw::SkeletalAnimatorComponent>();
        if ( pUnit == nullptr || pAnimator == nullptr )
            return nullptr;
        pUnit->setShareCrowdPose( bShare );
        pUnit->setSkeleton( skeleton );
        pUnit->setMesh( mesh );
        pUnit->resolveRenderAssets();
        pAnimator->setClipFolder( makeTempResourceID( folder ) );
        pAnimator->setInitialState( "Wave" );
        pAnimator->setExtractRootMotion( false ); // 클립의 루트 이동이 테스트가 정한 위치를 움직이지 않게 한다
        pAnimator->dispatchBeginPlay();
        if ( finishAssetArrivals( manager ) == false ) // 클립은 워커가 읽는다
            return nullptr;
        return pUnit;
    }
} // namespace test
