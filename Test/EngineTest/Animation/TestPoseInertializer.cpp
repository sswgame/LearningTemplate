#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Math/MatrixMath.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Animation/Graph/AnimationGraphPlayer.h"
#include "Engine/Animation/Skeletal/Pose.h"
#include "Engine/Animation/Skeletal/PoseInertializer.h"

#include "EngineTest/AnimationTestUtil.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

// ------------------------------------------------------------------------------
// 1) PoseInertializerTest — 포즈 단위 inertialization(감쇠 수렴 · 속도 연속성 · 본 수 오류)과 그래프 링크의 "blend_mode"
// ------------------------------------------------------------------------------

namespace
{
    /** @brief 본 하나짜리 포즈를 만듭니다(위치 x, Y 축 회전 yaw 라디안). */
    Pose makeSingleBonePose( float32 x, float32 yaw = 0.0f )
    {
        Pose pose;
        pose.resize( 1 );
        BoneTransform transform{};
        transform._translation = float3{ x, 0.0f, 0.0f };
        transform._rotation    = quaternion::makeFromAxisAngle( float3::UnitY, yaw );
        pose.setBoneTransform( 0, transform );
        return pose;
    }
} // namespace

/**
 * @brief [PoseInertializerTest] 시작 직후 apply 는 target 을 source 로 되돌리고, 시간이 흐르면 오프셋이 0 으로 줄어 끝난다
 * @details 시작 속도 차이가 0 이면 임계 감쇠 스프링은 넘치지 않고 단조롭게 줄어든다.
 */
SW_TEST_CASE( PoseInertializerTest, OffsetDecaysToZero )
{
    const Pose       source = makeSingleBonePose( 0.0f, MathUtil::kHalfPi );
    const Pose       target = makeSingleBonePose( 1.0f );
    PoseInertializer inertializer;
    SW_ASSERT_TRUE( inertializer.begin( source, nullptr, target, nullptr, 0.0f, 0.1f ) );

    // 경과 0 — target 위에 더하면 source 와 같다(튀지 않는다).
    Pose blended = target;
    inertializer.apply( blended );
    SW_EXPECT_NEAR_EQUAL( 0.0f, blended.getTranslations()[0]._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, MathUtil::abs( source.getRotations()[0].dot( blended.getRotations()[0] ) ), 1e-4f );

    float32 previousDistance = MathUtil::abs( inertializer.computeTranslationOffset( 0 )._x );
    float32 previousAngle    = inertializer.computeRotationOffset( 0 ).getLength();
    for ( uint32 stepIndex = 0; stepIndex < 30; ++stepIndex )
    {
        inertializer.update( 0.02f );
        const float32 distance = MathUtil::abs( inertializer.computeTranslationOffset( 0 )._x );
        const float32 angle    = inertializer.computeRotationOffset( 0 ).getLength();
        SW_EXPECT_TRUE( distance <= previousDistance );
        SW_EXPECT_TRUE( angle <= previousAngle );
        previousDistance = distance;
        previousAngle    = angle;
    }
    SW_EXPECT_TRUE( previousDistance < 0.01f ); // 반감기 6 배(0.6 초)가 지나면 1 m 가 1 cm 아래다

    // 반감기의 kSettleHalfLifeCount 배가 지나면 끝나고, apply 는 포즈를 건드리지 않는다.
    inertializer.update( 0.1f * PoseInertializer::kSettleHalfLifeCount );
    SW_EXPECT_FALSE( inertializer.isActive() );
    Pose settled = target;
    inertializer.apply( settled );
    SW_EXPECT_NEAR_EQUAL( 1.0f, settled.getTranslations()[0]._x, 1e-6f );
}

/**
 * @brief [PoseInertializerTest] 끊긴 순간의 속도가 이어진다 — 시작 직후 내보낸 포즈의 속도가 source 의 속도와 같다
 * @details source 는 1 m/s 로 +x 로 가던 중이고 target 은 멈춰 있다. 크로스페이드라면 첫 프레임에 속도가 끊긴다.
 */
SW_TEST_CASE( PoseInertializerTest, VelocityIsContinuous )
{
    const float32    deltaSeconds   = 0.01f;
    const Pose       sourcePrevious = makeSingleBonePose( 0.0f );
    const Pose       source         = makeSingleBonePose( 0.01f ); // 0.01 초에 1 cm — 1 m/s
    const Pose       target         = makeSingleBonePose( 1.0f );
    PoseInertializer inertializer;
    SW_ASSERT_TRUE( inertializer.begin( source, &sourcePrevious, target, nullptr, deltaSeconds, 0.2f ) );

    Pose atStart = target;
    inertializer.apply( atStart );
    const float32 smallStep = 1.0e-3f;
    inertializer.update( smallStep );
    Pose afterStep = target;
    inertializer.apply( afterStep );

    const float32 velocity = ( afterStep.getTranslations()[0]._x - atStart.getTranslations()[0]._x ) / smallStep;
    SW_EXPECT_NEAR_EQUAL( 0.01f, atStart.getTranslations()[0]._x, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, velocity, 0.05f );

    // 새 재생의 속도도 주면 그만큼 뺀다 — target 이 같은 1 m/s 로 가면 오프셋의 속도 차이는 0 이다.
    const Pose targetPrevious = makeSingleBonePose( 0.99f );
    SW_ASSERT_TRUE( inertializer.begin( source, &sourcePrevious, target, &targetPrevious, deltaSeconds, 0.2f ) );
    const float3 offsetAtStart = inertializer.computeTranslationOffset( 0 );
    inertializer.update( smallStep );
    const float32 offsetVelocity = ( inertializer.computeTranslationOffset( 0 )._x - offsetAtStart._x ) / smallStep;
    // x'(0) = v0 = 0 이므로 차이의 기울기는 감쇠(2 차 항)만큼만 움직인다.
    SW_EXPECT_NEAR_EQUAL( 0.0f, offsetVelocity, 0.05f );
}

/**
 * @brief [PoseInertializerTest] 본 수가 다른 포즈나 0 이하 반감기는 오류다(조용히 섞지 않는다)
 */
SW_TEST_CASE( PoseInertializerTest, MismatchedBoneCountIsError )
{
    Pose source;
    source.resize( 3 );
    Pose target;
    target.resize( 2 );
    Pose previous;
    previous.resize( 3 );

    test::ScopedDefensiveTestLog expected( "inertialization rejects mismatched poses" );
    PoseInertializer             inertializer;
    SW_EXPECT_FALSE( inertializer.begin( source, nullptr, target, nullptr, 0.0f, 0.1f ) );
    SW_EXPECT_FALSE( inertializer.isActive() );
    SW_EXPECT_FALSE( inertializer.begin( target, &previous, target, nullptr, 0.01f, 0.1f ) );
    SW_EXPECT_FALSE( inertializer.begin( target, nullptr, target, nullptr, 0.0f, 0.0f ) );
    SW_EXPECT_FALSE( inertializer.isActive() );

    // 끝난(시작하지 못한) 상태의 apply 는 포즈를 건드리지 않는다.
    Pose untouched = makeSingleBonePose( 2.0f );
    inertializer.apply( untouched );
    SW_EXPECT_NEAR_EQUAL( 2.0f, untouched.getTranslations()[0]._x, 1e-6f );
}

/**
 * @brief [PoseInertializerTest] 링크의 "blend_mode" 가 XML 왕복에서 남고, 그래프 플레이어는 그 링크에서 섞지 않고 inertialization 을 요청한다
 * @details 모르는 표기와 반감기 없는 inertialization 은 로드 오류다. 키가 없는 링크는 크로스페이드다(데이터 기본값).
 */
SW_TEST_CASE( PoseInertializerTest, GraphLinkSelectsInertialization )
{
    const string_view   json = R"(<AnimationGraphAsset>
	<nodes>
		<node id="1" name="Idle" />
		<node id="2" name="Walk" />
	</nodes>
	<links>
		<link id="1" from="1" to="2" blend_mode="inertialization" half_life="0.08">
			<condition param="Speed" op="&gt;" value="0.5" />
		</link>
		<link id="2" from="2" to="1" blend="0.2">
			<condition param="Speed" op="&lt;" value="0.5" />
		</link>
	</links>
</AnimationGraphAsset>)";
    AnimationGraphAsset graph;
    SW_ASSERT_TRUE( graph.parseXML( json ) );
    AnimationGraphAsset parsed;
    SW_ASSERT_TRUE( parsed.parseXML( graph.toXML() ) );
    SW_ASSERT_EQUAL( 2u, static_cast<uint32>( parsed._listLink.size() ) );
    SW_EXPECT_TRUE( parsed._listLink[0]._blendMode == AnimationBlendMode::Inertialization );
    SW_EXPECT_NEAR_EQUAL( 0.08f, parsed._listLink[0]._halfLifeSeconds, 1e-6f );
    SW_EXPECT_TRUE( parsed._listLink[1]._blendMode == AnimationBlendMode::Crossfade );

    {
        test::ScopedDefensiveTestLog expected( "unknown blend_mode and inertialization without half_life are load errors" );
        AnimationGraphAsset          broken;
        SW_EXPECT_FALSE( broken.parseXML( R"(<AnimationGraphAsset><nodes><node id="1" name="A" /></nodes><links><link id="1" from="1" to="1" blend_mode="snap" /></links></AnimationGraphAsset>)" ) );
        SW_EXPECT_FALSE( broken.parseXML( R"(<AnimationGraphAsset><nodes><node id="1" name="A" /></nodes><links><link id="1" from="1" to="1" blend_mode="inertialization" /></links></AnimationGraphAsset>)" ) );
    }

    test::TestPlayable       idle( 1.0f, true );
    test::TestPlayable       walk( 1.0f, true );
    test::TestPlayableSource source;
    source.add( "Idle", &idle );
    source.add( "Walk", &walk );
    AnimationGraphPlayer player;
    player.setGraph( &parsed );
    player.setPlayableSource( &source );
    AnimationParameterSet parameter;
    SW_ASSERT_TRUE( player.play( hashed_string{}, true, 0.0f ) );

    parameter.setFloat( hashed_string( "Speed" ), 1.0f );
    player.update( 0.1f, &parameter, nullptr );
    SW_EXPECT_EQUAL( 2, player.getCurrentNodeID() );
    SW_EXPECT_FALSE( player.getPlayer().isCrossfading() ); // 섞지 않고 바로 Walk
    SW_EXPECT_TRUE( player.getPlayer().getCurrentPlayable() == &walk );
    SW_EXPECT_EQUAL( 1u, player.getPlayer().getInertializationCount() );
    SW_EXPECT_NEAR_EQUAL( 0.08f, player.getPlayer().getInertializationHalfLife(), 1e-6f );

    // 돌아가는 링크는 크로스페이드다 — inertialization 횟수는 그대로다.
    parameter.setFloat( hashed_string( "Speed" ), 0.0f );
    player.update( 0.1f, &parameter, nullptr );
    SW_EXPECT_EQUAL( 1, player.getCurrentNodeID() );
    SW_EXPECT_TRUE( player.getPlayer().isCrossfading() );
    SW_EXPECT_EQUAL( 1u, player.getPlayer().getInertializationCount() );
}
