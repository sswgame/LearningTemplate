#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Animation/AnimClip.h"
#include "Engine/Animation/Codec/Raw/RawAnimCodec.h"
#include "Engine/Animation/Pose.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Object/Animation/AnimationRewind.h"
#include "Engine/Object/Animation/AnimationSystem.h"
#include "Engine/Object/Component/3D/SkeletalAnimatorComponent.h"
#include "Engine/Object/Component/3D/SkeletalMeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Resource/ResourceUtil.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// AnimationRewindTest — 되감기 기록기: 포즈 압축 · 창 · 상태(그래프 · 알림 · 커브) · 되감기 동안 평가 멈춤 · 사라진 유닛의 기록.

namespace
{
    struct TestAnimationRewindInternal
    {
        /** @brief 기록 요청(프로세스 전역)을 켜고, 끝날 때 원래대로 돌립니다. */
        struct ScopedRecording
        {
            explicit ScopedRecording( float32 windowSeconds )
                : _bWasOn{ AnimationRewindRecorder::isRecordingRequested() }
                , _previousSeconds{ AnimationRewindRecorder::getRequestedWindowSeconds() }
            {
                AnimationRewindRecorder::setRecordingRequested( true );
                AnimationRewindRecorder::setRequestedWindowSeconds( windowSeconds );
            }
            ~ScopedRecording()
            {
                AnimationRewindRecorder::setRecordingRequested( _bWasOn );
                AnimationRewindRecorder::setRequestedWindowSeconds( _previousSeconds );
            }
            ScopedRecording( const ScopedRecording& )            = delete;
            ScopedRecording& operator=( const ScopedRecording& ) = delete;

            bool    _bWasOn;
            float32 _previousSeconds;
        };

        /** @brief 반복 클립 `Wave`(알림 `Step` 0.5 초 · 커브 `Speed` 0 → 1)를 임시 폴더에 쓰고 폴더를 돌려줍니다. */
        static string writeWaveClip( const Skeleton& skeleton )
        {
            const string      folder = test::makeTempPath( "rewindclips" );
            const AnimRawClip raw    = test::makeChainRawClip( skeleton, 31, 30.0f, 0.8f );
            AnimClip          clip;
            clip.setName( hashed_string( "Wave" ) );
            clip.setLooping( true );
            if ( clip.compressFrom( raw, RawAnimCodec::getInstance(), AnimCodecSettings{}, nullptr ) == false )
                return string{};
            clip.addNotify( AnimNotifyEvent{ hashed_string( "Step" ), 0.5f, 0.0f } );
            AnimCurve speed{};
            speed._name = hashed_string( "Speed" );
            speed._listKey.push_back( AnimCurveKey{ 0.0f, 0.0f } );
            speed._listKey.push_back( AnimCurveKey{ clip.getDuration(), 1.0f } );
            clip.addCurve( speed );
            if ( clip.saveToFile( FileUtil::joinPath( folder, "wave.animclip" ) ) == false )
                return string{};
            return folder;
        }

        /** @brief 사슬 3 본 캐릭터(유닛 + 애니메이터, Wave 재생)를 만듭니다. */
        static SkeletalMeshComponent* createCharacter( GameObjectManager& manager, const shared_ptr<Skeleton>& skeleton, const string& folder )
        {
            GameObject* pObject = manager.createGameObject( hashed_string( "Waver" ) );
            if ( pObject == nullptr )
                return nullptr;
            SkeletalMeshComponent*     pUnit     = pObject->addComponent<SkeletalMeshComponent>();
            SkeletalAnimatorComponent* pAnimator = pObject->addComponent<SkeletalAnimatorComponent>();
            if ( pUnit == nullptr || pAnimator == nullptr )
                return nullptr;
            pUnit->setSkeleton( skeleton );
            pAnimator->setClipFolder( folder );
            pAnimator->setInitialState( "Wave" );
            pAnimator->dispatchBeginPlay();
            return pUnit;
        }

        static bool isSameRotation( const quaternion& expected, const quaternion& actual, float32 tolerance )
        {
            return MathUtil::abs( MathUtil::abs( expected.dot( actual ) ) - 1.0f ) <= tolerance;
        }
    };
} // namespace

/**
 * @brief [AnimationRewindTest] 포즈 압축은 본마다 14 바이트(스케일이 있으면 20)이고, 회전 · 이동 · 스케일이 양자화 오차 안에서 돌아온다
 */
SW_TEST_CASE( AnimationRewindTest, PoseEncodingRoundTripsWithinQuantization )
{
    Pose pose;
    pose.resize( 4 );
    for ( uint32 boneIndex = 0; boneIndex < 4; ++boneIndex )
    {
        BoneTransform transform{};
        transform._translation = float3{ 0.3f * static_cast<float32>( boneIndex ), -1.7f + static_cast<float32>( boneIndex ), 0.05f };
        transform._rotation    = quaternion::createFromYawPitchRoll( 0.4f * static_cast<float32>( boneIndex ), -0.9f, 2.5f );
        pose.setBoneTransform( boneIndex, transform );
    }
    AnimationRewindFrame frame{};
    AnimationRewindRecorder::encodePose( pose, frame );
    SW_EXPECT_EQUAL( size_t( 4 * 14 ), frame._poseByte.size() );
    SW_EXPECT_NEAR_EQUAL( 0.0f, frame._scaleRange, 0.0f );
    Pose decoded;
    AnimationRewindRecorder::decodePose( frame, decoded );
    SW_ASSERT_EQUAL( 4u, decoded.getBoneCount() );
    for ( uint32 boneIndex = 0; boneIndex < 4; ++boneIndex )
    {
        const BoneTransform expected = pose.getBoneTransform( boneIndex );
        const BoneTransform actual   = decoded.getBoneTransform( boneIndex );
        SW_EXPECT_TRUE( TestAnimationRewindInternal::isSameRotation( expected._rotation, actual._rotation, 1.0e-6f ) );
        SW_EXPECT_TRUE( ( expected._translation - actual._translation ).getLength() < 2.0e-4f );
        SW_EXPECT_NEAR_EQUAL( 1.0f, actual._scale._y, 0.0f );
    }

    BoneTransform scaled = pose.getBoneTransform( 2 );
    scaled._scale        = float3{ 1.5f, 0.5f, 2.0f };
    pose.setBoneTransform( 2, scaled );
    AnimationRewindRecorder::encodePose( pose, frame );
    SW_EXPECT_EQUAL( size_t( 4 * 20 ), frame._poseByte.size() );
    AnimationRewindRecorder::decodePose( frame, decoded );
    SW_EXPECT_NEAR_EQUAL( 0.5f, decoded.getBoneTransform( 2 )._scale._y, 1.0e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, decoded.getBoneTransform( 1 )._scale._x, 1.0e-4f );
}

/**
 * @brief [AnimationRewindTest] 기록은 기본 꺼짐이고, 켜면 일한 유닛마다 창(1 초) 만큼 프레임 · 그래프 상태 · 커브 · 알림을 남긴다
 */
SW_TEST_CASE( AnimationRewindTest, RecordsStateWithinTheWindow )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const shared_ptr<Skeleton> skeleton = make_shared<Skeleton>( test::makeChainSkeleton( 3 ) );
    const string               folder   = TestAnimationRewindInternal::writeWaveClip( *skeleton );
    SW_ASSERT_FALSE( folder.empty() );
    GameObjectManager      manager;
    SkeletalMeshComponent* pUnit = TestAnimationRewindInternal::createCharacter( manager, skeleton, folder );
    SW_ASSERT_NOT_NULL( pUnit );
    AnimationSystem& system = manager.getAnimationSystem();

    // 꺼져 있으면 아무것도 남기지 않는다(요청이 프로세스 전역이라 시험이 직접 확인한다).
    SW_ASSERT_FALSE( AnimationRewindRecorder::isRecordingRequested() );
    system.evaluate( 1.0f / 30.0f );
    SW_EXPECT_TRUE( system.getRewind().getTracks().empty() );

    TestAnimationRewindInternal::ScopedRecording recording( 1.0f );
    bool                                         bNotifySeen = false;
    for ( uint32 frame = 0; frame < 90; ++frame )
    {
        system.evaluate( 1.0f / 30.0f );
        const AnimationRewindTrack* pTrack = system.getRewind().findTrack( pUnit->getHandle() );
        SW_ASSERT_NOT_NULL( pTrack );
        const AnimationRewindFrame& latest = pTrack->getFrame( pTrack->_count - 1 );
        for ( const hashed_string& notify : latest._state._listNotify )
            bNotifySeen = bNotifySeen || notify == hashed_string( "Step" );
    }
    const AnimationRewindRecorder& rewind = system.getRewind();
    const AnimationRewindTrack*    pTrack = rewind.findTrack( pUnit->getHandle() );
    SW_ASSERT_NOT_NULL( pTrack );
    SW_EXPECT_TRUE( pTrack->_kind == AnimationRewindKind::Skeletal );
    SW_EXPECT_TRUE( pTrack->_count >= 30 && pTrack->_count <= 32 );
    SW_EXPECT_TRUE( rewind.getLatestTime() - rewind.getEarliestTime() <= 1.0 + 1.0e-6 );
    SW_EXPECT_TRUE( bNotifySeen );

    const AnimationRewindFrame& latest = pTrack->getFrame( pTrack->_count - 1 );
    SW_EXPECT_TRUE( latest._state._stateName == hashed_string( "Wave" ) );
    SW_EXPECT_EQUAL( 3u, latest._boneCount );
    SW_ASSERT_EQUAL( size_t( 1 ), latest._state._listCurveName.size() );
    SW_EXPECT_TRUE( latest._state._listCurveName[0] == hashed_string( "Speed" ) );
    // 기록된 포즈는 그 프레임의 유닛 포즈다.
    Pose decoded;
    AnimationRewindRecorder::decodePose( latest, decoded );
    SW_EXPECT_TRUE( TestAnimationRewindInternal::isSameRotation( pUnit->getLocalPose().getBoneTransform( 1 )._rotation, decoded.getBoneTransform( 1 )._rotation, 1.0e-6f ) );
    // 창을 줄이면 오래된 것부터 버린다.
    AnimationRewindRecorder::setRequestedWindowSeconds( 0.5f );
    system.evaluate( 1.0f / 30.0f );
    SW_EXPECT_TRUE( system.getRewind().findTrack( pUnit->getHandle() )->_count <= 17 );
    // 요청을 끄면 비운다.
    AnimationRewindRecorder::setRecordingRequested( false );
    system.evaluate( 1.0f / 30.0f );
    SW_EXPECT_TRUE( system.getRewind().getTracks().empty() );
}

/**
 * @brief [AnimationRewindTest] 되감는 동안 평가가 멈추고 기록된 시각의 포즈가 유닛에 걸린다(모델 공간까지) — 풀면 지금 포즈로 다시 만든다
 */
SW_TEST_CASE( AnimationRewindTest, ScrubFreezesEvaluationAndAppliesRecordedPose )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const shared_ptr<Skeleton> skeleton = make_shared<Skeleton>( test::makeChainSkeleton( 3 ) );
    const string               folder   = TestAnimationRewindInternal::writeWaveClip( *skeleton );
    SW_ASSERT_FALSE( folder.empty() );
    GameObjectManager      manager;
    SkeletalMeshComponent* pUnit = TestAnimationRewindInternal::createCharacter( manager, skeleton, folder );
    SW_ASSERT_NOT_NULL( pUnit );
    AnimationSystem&                             system = manager.getAnimationSystem();
    TestAnimationRewindInternal::ScopedRecording recording( 5.0f );

    quaternion recordedRotation{};
    float64    recordedTime = 0.0;
    for ( uint32 frame = 0; frame < 40; ++frame )
    {
        system.evaluate( 1.0f / 30.0f );
        if ( frame == 10 )
        {
            recordedRotation                   = pUnit->getLocalPose().getBoneTransform( 1 )._rotation;
            const AnimationRewindTrack* pTrack = system.getRewind().findTrack( pUnit->getHandle() );
            SW_ASSERT_NOT_NULL( pTrack );
            recordedTime = pTrack->getFrame( pTrack->_count - 1 )._time;
        }
    }
    const quaternion liveRotation = pUnit->getLocalPose().getBoneTransform( 1 )._rotation;
    SW_ASSERT_FALSE( TestAnimationRewindInternal::isSameRotation( recordedRotation, liveRotation, 1.0e-4f ) );
    const uint32 trackFrames = system.getRewind().findTrack( pUnit->getHandle() )->_count;

    system.getRewind().setScrubTime( recordedTime );
    system.evaluate( 1.0f / 30.0f );
    SW_EXPECT_TRUE( TestAnimationRewindInternal::isSameRotation( recordedRotation, pUnit->getLocalPose().getBoneTransform( 1 )._rotation, 1.0e-6f ) );
    // 모델 공간도 그 포즈로 다시 구했다.
    vector<float4x4> listModel;
    pUnit->getLocalPose().computeModelSpace( skeleton->getParentIndices(), listModel );
    SW_EXPECT_TRUE( ( listModel[2].getTranslation() - pUnit->getModelSpaceTransforms()[2].getTranslation() ).getLength() < 1.0e-5f );
    // 되감는 동안에는 기록이 쌓이지 않는다.
    system.evaluate( 1.0f / 30.0f );
    SW_EXPECT_EQUAL( trackFrames, system.getRewind().findTrack( pUnit->getHandle() )->_count );

    // 풀면 지금 시각에서 다시 평가한다(한 바퀴 1 초 클립이라 위상이 겹치지 않게 0.2 초를 더 흘린다).
    system.getRewind().clearScrub();
    system.evaluate( 0.2f );
    SW_EXPECT_FALSE( TestAnimationRewindInternal::isSameRotation( recordedRotation, pUnit->getLocalPose().getBoneTransform( 1 )._rotation, 1.0e-4f ) );
}

/**
 * @brief [AnimationRewindTest] 유닛이 사라져도 기록은 창을 벗어날 때까지 남는다(죽기 직전을 되감아 본다)
 */
SW_TEST_CASE( AnimationRewindTest, TrackOutlivesItsUnitUntilTheWindowPasses )
{
    SW_ASSERT_TRUE( ResourceUtil::initialize() );
    const shared_ptr<Skeleton> skeleton = make_shared<Skeleton>( test::makeChainSkeleton( 3 ) );
    const string               folder   = TestAnimationRewindInternal::writeWaveClip( *skeleton );
    SW_ASSERT_FALSE( folder.empty() );
    GameObjectManager      manager;
    SkeletalMeshComponent* pUnit = TestAnimationRewindInternal::createCharacter( manager, skeleton, folder );
    SW_ASSERT_NOT_NULL( pUnit );
    AnimationSystem&                             system = manager.getAnimationSystem();
    TestAnimationRewindInternal::ScopedRecording recording( 1.0f );
    for ( uint32 frame = 0; frame < 10; ++frame )
        system.evaluate( 1.0f / 30.0f );
    const ComponentHandle handle = pUnit->getHandle();
    manager.destroyObject( pUnit->getOwner() );
    manager.processDeferredDestruction();
    system.evaluate( 0.5f );
    SW_EXPECT_NOT_NULL( system.getRewind().findTrack( handle ) );
    SW_EXPECT_TRUE( system.getRewind().findTrack( handle )->_label == "Waver" );
    system.evaluate( 0.6f );
    system.evaluate( 0.1f );
    SW_EXPECT_NULL( system.getRewind().findTrack( handle ) );
}
