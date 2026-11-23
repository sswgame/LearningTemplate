// AnimationCrowdVertexAnimationTest — 군중 VAT 는 게임 스레드를 멈추지 않는다: 처음 청한 프레임에는 없고(유닛은 묶음으로 그린다) 워커가 굽고, 결정적 실행은 그 자리에서 끝낸다. nogpu.
#include "pch.h"

#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/Codec/AnimationCodec.h"
#include "Engine/Animation/Scene/AnimationCrowd.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Animation/Tests/AnimationTestUtil.h"
#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Mesh/Mesh.h"

using namespace sw;

namespace
{
    /** @brief 범위 동안 "에셋 일을 프레임마다 끝낸다" 를 @p bFinish 로 둡니다. */
    class ScopedFinishAssetJobs
    {
    public:
        explicit ScopedFinishAssetJobs( bool bFinish )
            : _previous{ engine::getSubsystem<DeterministicRun>().getPolicy() }
        {
            DeterministicRunPolicy policy     = _previous;
            policy._bFinishAssetJobsEachFrame = bFinish ? SW_TRUE : SW_FALSE;
            engine::getSubsystem<DeterministicRun>().setPolicy( policy );
        }
        ~ScopedFinishAssetJobs() { engine::getSubsystem<DeterministicRun>().setPolicy( _previous ); }
        ScopedFinishAssetJobs( const ScopedFinishAssetJobs& )            = delete;
        ScopedFinishAssetJobs& operator=( const ScopedFinishAssetJobs& ) = delete;

    private:
        DeterministicRunPolicy _previous;
    };

    /** @brief 사슬 스켈레톤(본 둘)에 묶인 삼각형 하나와 그 클립입니다. */
    struct AnimationCrowdVertexAnimationRig
    {
        shared_ptr<const Skeleton>      _skeleton;
        shared_ptr<const AnimationClip> _clip;
        shared_ptr<Mesh>                _mesh;

        AnimationCrowdVertexAnimationRig()
            : _skeleton{ make_shared<Skeleton>( test::makeChainSkeleton( 2u ) ) }
            , _clip{}
            , _mesh{ Mesh::create( MeshCPUAccess::Keep ) }
        {
            const IAnimationCodec* pCodec = AnimationCodecRegistry::findCodecByName( "raw" );
            SW_REQUIRE_TRUE( pCodec != nullptr );
            shared_ptr<AnimationClip> clip = make_shared<AnimationClip>();
            SW_EXPECT_TRUE( clip->compressFrom( test::makeChainRawClip( *_skeleton, 31u, 30.0f, 0.5f ), *pCodec, AnimationCodecSettings{}, nullptr ) );
            _clip = std::move( clip );

            vector<RHIVertex> listVertex( 3u );
            for ( size_t index = 0; index < listVertex.size(); ++index )
            {
                listVertex[index]._arrPosition[0] = static_cast<float32>( index % 2u );
                listVertex[index]._arrPosition[1] = static_cast<float32>( index );
                listVertex[index]._arrNormal[2]   = 1.0f;
            }
            SW_EXPECT_TRUE( _mesh->setGeometryWithTangents( std::move( listVertex ), vector<uint32>{ 0, 1, 2 } ) );
            vector<MeshSkinVertex> listSkin( 3u );
            for ( MeshSkinVertex& skin : listSkin )
            {
                skin._arrJoint[0]  = 1;
                skin._arrWeight[0] = 1.0f;
            }
            _mesh->setSkin( std::move( listSkin ), MeshSkinBinding::makeIdentity( 2u ) );
        }

        shared_ptr<Mesh> find( AnimationCrowd& crowd ) const { return crowd.findVertexAnimationMesh( _mesh, _skeleton, _clip, false, string_view{} ); }
    };
} // namespace

/** @brief [AnimationCrowdVertexAnimationTest] 결정적 실행은 청한 자리에서 굽고, 같은 조합은 메시 하나를 나누며, 굽지 못한 조합(스킨 없는 메시)은 비어 있다 */
SW_TEST_CASE( AnimationCrowdVertexAnimationTest, DeterministicRunBakesInPlaceAndShares )
{
    const ScopedFinishAssetJobs            finishNow{ true };
    const AnimationCrowdVertexAnimationRig rig;
    AnimationCrowd                         crowd;
    const shared_ptr<Mesh>                 first = rig.find( crowd );
    SW_REQUIRE_TRUE( first != nullptr );
    SW_EXPECT_TRUE( first->findVertexAnimation() != nullptr );
    SW_EXPECT_FALSE( first->hasSkin() );
    SW_EXPECT_EQUAL( rig._mesh->getVertexCount(), first->getVertexCount() );
    SW_EXPECT_TRUE( rig.find( crowd ) == first );

    AnimationCrowdVertexAnimationRig staticRig;
    staticRig._mesh->setSkin( {}, MeshSkinBinding{} );
    SW_EXPECT_LOG( "MeshVertexAnimationBaker.VertexAnimationBakeSkipped" );
    SW_EXPECT_TRUE( staticRig.find( crowd ) == nullptr );
    SW_EXPECT_TRUE( staticRig.find( crowd ) == nullptr ); // 다시 굽지 않는다(경고가 한 번이다)
}

/** @brief [AnimationCrowdVertexAnimationTest] 평소 실행은 처음 청할 때 기다리지 않는다 — 굽는 중에 원본이 사라지거나 군중을 비워도 안전하다 */
SW_TEST_CASE( AnimationCrowdVertexAnimationTest, FreeRunNeverBakesOnTheCaller )
{
    const ScopedFinishAssetJobs freeRun{ false };
    AnimationCrowd              crowd;
    {
        const AnimationCrowdVertexAnimationRig rig;
        SW_EXPECT_TRUE( rig.find( crowd ) == nullptr );
    }
    crowd.clear(); // 굽는 중인 일을 기다린 뒤 비운다
    {
        const AnimationCrowdVertexAnimationRig rig;
        SW_EXPECT_TRUE( rig.find( crowd ) == nullptr );
    }
    // 소멸자도 기다린다
}
