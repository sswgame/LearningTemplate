#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Character/Fit/BodyShape.h"

// BodyShapeChannelsTest — 체형 축을 "채널 → 본 · 모프" 매핑으로 쓴다: 회전 보정(하이힐 발목), 응답 곡선, 받지 않는 채널 선언, 발 들림 축(nogpu).

namespace
{
    constexpr const char* kShapeText = R"(<BodyShapeSet footLiftAxis="Heel.Height">
      <Axis name="Heel.Height" min="0" max="0.1">
        <Positive><Bone name="foot_l" rotation="0 -0.349066 0"/><Bone name="pelvis" offset="0 0.1 0"/></Positive>
      </Axis>
      <Axis name="Bust.Compression" min="0" max="1" response="2">
        <Positive morph="BustFlat"><Bone name="breast_l" scale="1 0.8 1"/></Positive>
      </Axis>
      <Unsupported name="Tail.Length"/>
    </BodyShapeSet>)";
} // namespace

/**
 * @brief [BodyShapeChannelsTest] 굽 높이 채널의 반이 발목을 반만 굽히고 골반을 반만 올린다(선형), 발 들림은 채널 값 그대로다
 */
SW_TEST_CASE( BodyShapeChannelsTest, HeelChannelRotatesAndLifts )
{
    sw::BodyShapeSet shapes;
    SW_REQUIRE_TRUE( shapes.loadFromXMLText( kShapeText, "test" ) );
    const sw::BodyShapeValue arrValue[] = {
        { sw::hashed_string( "Heel.Height" ), 0.05f }
    };
    sw::vector<sw::BodyMorphWeight> listMorph;
    sw::BoneProportion              proportion;
    sw::CharacterIssueList          issues;
    SW_REQUIRE_TRUE( shapes.evaluate( sw::vector_reference<const sw::BodyShapeValue>( arrValue, 1 ), listMorph, proportion, issues ) );
    const sw::BoneProportionEntry* pFoot   = proportion.findBone( sw::hashed_string( "foot_l" ) );
    const sw::BoneProportionEntry* pPelvis = proportion.findBone( sw::hashed_string( "pelvis" ) );
    SW_REQUIRE_TRUE( pFoot != nullptr && pPelvis != nullptr );
    SW_EXPECT_NEAR( -0.174533f, pFoot->_rotation._y, 1e-4f ); // 반 무게 — -10 도(라디안으로 쥔다)
    SW_EXPECT_NEAR( 0.05f, pPelvis->_offset._y, 1e-5f );
    SW_EXPECT_NEAR( 0.05f, shapes.computeFootLift( sw::vector_reference<const sw::BodyShapeValue>( arrValue, 1 ) ), 1e-6f );
}

/**
 * @brief [BodyShapeChannelsTest] 응답 지수 2 인 채널은 반 값에서 가중치 0.25 다
 */
SW_TEST_CASE( BodyShapeChannelsTest, ResponseCurveShapesWeight )
{
    sw::BodyShapeSet shapes;
    SW_REQUIRE_TRUE( shapes.loadFromXMLText( kShapeText, "test" ) );
    const sw::BodyShapeValue arrValue[] = {
        { sw::hashed_string( "Bust.Compression" ), 0.5f }
    };
    sw::vector<sw::BodyMorphWeight> listMorph;
    sw::BoneProportion              proportion;
    sw::CharacterIssueList          issues;
    SW_REQUIRE_TRUE( shapes.evaluate( sw::vector_reference<const sw::BodyShapeValue>( arrValue, 1 ), listMorph, proportion, issues ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, listMorph.size() );
    SW_EXPECT_NEAR( 0.25f, listMorph[0]._weight, 1e-5f );
    const sw::BoneProportionEntry* pBreast = proportion.findBone( sw::hashed_string( "breast_l" ) );
    SW_REQUIRE_TRUE( pBreast != nullptr );
    SW_EXPECT_NEAR( 0.95f, pBreast->_scale._y, 1e-5f ); // lerp(1, 0.8, 0.25)
}

/**
 * @brief [BodyShapeChannelsTest] 받지 않는다고 선언한 채널은 오류 없이 버리고, 매핑도 선언도 없는 채널은 오류다
 */
SW_TEST_CASE( BodyShapeChannelsTest, UnsupportedChannelIsSilentUnknownIsError )
{
    sw::BodyShapeSet shapes;
    SW_REQUIRE_TRUE( shapes.loadFromXMLText( kShapeText, "test" ) );
    sw::vector<sw::BodyMorphWeight> listMorph;
    sw::BoneProportion              proportion;
    const sw::BodyShapeValue        arrDeclared[] = {
        { sw::hashed_string( "Tail.Length" ), 1.0f }
    };
    sw::CharacterIssueList declaredIssues;
    SW_EXPECT_TRUE( shapes.evaluate( sw::vector_reference<const sw::BodyShapeValue>( arrDeclared, 1 ), listMorph, proportion, declaredIssues ) );
    const sw::BodyShapeValue arrUnknown[] = {
        { sw::hashed_string( "Wing.Span" ), 1.0f }
    };
    sw::CharacterIssueList unknownIssues;
    SW_EXPECT_FALSE( shapes.evaluate( sw::vector_reference<const sw::BodyShapeValue>( arrUnknown, 1 ), listMorph, proportion, unknownIssues ) );
    SW_EXPECT_FALSE( unknownIssues.empty() );
}

/**
 * @brief [BodyShapeChannelsTest] 발 들림 축이 없는 축을 가리키면 로드 오류다
 */
SW_TEST_CASE( BodyShapeChannelsTest, FootLiftAxisMustExist )
{
    sw::BodyShapeSet shapes;
    SW_EXPECT_LOG( "Character.BodyFootLiftAxisUnknown" );
    SW_EXPECT_FALSE( shapes.loadFromXMLText( R"(<BodyShapeSet footLiftAxis="Heel.Height"><Axis name="Height"/></BodyShapeSet>)", "test" ) );
}
