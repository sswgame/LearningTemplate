#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Character/PairedAnimation/PairedAnimationDef.h"

#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/PairedAnimationEditModel.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

// PairedAnimationEditModelTest — 페어드 애니메이션 편집 모델(에디터 M3): 쓰기 · 다시 읽기 왕복, 값 편집과 되돌리기, 규칙에 어긋난 내용의 저장 거절, 한 시계 미리보기 계산, 오프셋 기즈모의 역변환.

namespace
{
    struct TestPairedAnimationEditModelInternal
    {
        static constexpr const utf8* kXML = R"(<PairedAnimation duration="1.6">
            <Role name="Archer" anchor="true" required="true" clip="Bow_Shoot_Archer" enterBlend="0.15" exitState="Idle" exitBlend="0.2"/>
            <Role name="Victim" required="true" clip="Grab_Victim" align="warp" offset="0 0 1" yaw="180" warpEnd="0.3" maxAlignDistance="1.5" enterBlend="0.1" exitState="Idle" exitBlend="0.2"/>
            <Role name="Arrow" required="false" clip="Arrow_Nock" start="0.2" align="none" enterBlend="0" exitState="Arrow_Hidden" exitBlend="0"/>
            <Notify name="ReleaseArrow" time="0.9" role="Arrow"/>
            <Select role="Victim" minAngle="-45" maxAngle="45" maxDistance="2"/>
        </PairedAnimation>)";
    };
} // namespace

/**
 * @brief [PairedAnimationEditModelTest] 왕복 — 읽은 내용을 `makeXMLText` 로 쓰고 다시 읽으면 같은 내용이고, 막 읽은 모델은 dirty 가 아니다
 */
SW_TEST_CASE( PairedAnimationEditModelTest, WriteAndReadRoundTrips )
{
    PairedAnimationEditModel model;
    SW_ASSERT_TRUE( model.loadFromXMLText( TestPairedAnimationEditModelInternal::kXML, "edit.pairedanim.xml" ) );
    SW_EXPECT_FALSE( model.isDirty() );
    SW_EXPECT_TRUE( model.isValid() );

    PairedAnimationDef reread;
    SW_ASSERT_TRUE( reread.loadFromXMLText( model.saveToXMLText(), "reread.pairedanim.xml" ) );
    SW_ASSERT_EQUAL( static_cast<size_t>( 3 ), reread.getRoles().size() );
    const PairedAnimationRoleDef& victim = reread.getRoles()[1];
    SW_EXPECT_TRUE( victim._align == PairedAnimationAlign::Warp );
    SW_EXPECT_NEAR_EQUAL( 1.0f, victim._offset._z, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( MathUtil::kPi, MathUtil::abs( victim._yawRadians ), 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.3f, victim._warpEndSeconds, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.2f, reread.getRoles()[2]._startSeconds, 1e-5f );
    SW_ASSERT_NOT_NULL( reread.findSelect() );
    SW_EXPECT_NEAR_EQUAL( 45.0f * MathUtil::kDegreeToRadian, reread.findSelect()->_maxBearingRadians, 1e-4f );
}

/**
 * @brief [PairedAnimationEditModelTest] 편집 — 알림 시각 끌기는 되돌리기 한 단계이고, 범위 밖 값 · 정렬하지 않는 역할의 오프셋은 바꾸지 않고 거절하며, 규칙에 어긋난 내용(앵커보다 늦은 시작)은 유효하지 않다
 */
SW_TEST_CASE( PairedAnimationEditModelTest, EditsUndoAndRefuseInvalidValues )
{
    PairedAnimationEditModel model;
    SW_ASSERT_TRUE( model.loadFromXMLText( TestPairedAnimationEditModelInternal::kXML, "edit.pairedanim.xml" ) );
    string error;
    SW_EXPECT_TRUE( model.setNotifyTime( 0, 0.8f, true, error ) );
    SW_EXPECT_TRUE( model.setNotifyTime( 0, 0.7f, true, error ) );
    model.endContinuousEdit();
    SW_EXPECT_TRUE( model.isDirty() );
    SW_EXPECT_NEAR_EQUAL( 0.7f, model.getContent()._listNotify[0]._time, 1e-6f );
    SW_EXPECT_TRUE( model.undo() );
    SW_EXPECT_NEAR_EQUAL( 0.9f, model.getContent()._listNotify[0]._time, 1e-6f );
    SW_EXPECT_FALSE( model.canUndo() );
    SW_EXPECT_FALSE( model.isDirty() );

    SW_EXPECT_FALSE( model.setNotifyTime( 0, 2.0f, false, error ) );           // 타임라인 밖
    SW_EXPECT_FALSE( model.setRoleOffset( 2, float3{}, 0.0f, false, error ) ); // Arrow 는 정렬하지 않는다
    SW_EXPECT_FALSE( model.setRoleStart( 0, 0.1f, false, error ) );            // 앵커
    SW_EXPECT_FALSE( model.addNotify( hashed_string( "Hit" ), hashed_string( "Nobody" ), 0.5f, error ) );
    SW_EXPECT_FALSE( model.canUndo() );

    SW_EXPECT_TRUE( model.addNotify( hashed_string( "Hit" ), hashed_string( "Victim" ), 0.5f, error ) );
    SW_EXPECT_EQUAL( static_cast<size_t>( 2 ), model.getContent()._listNotify.size() );
    SW_EXPECT_TRUE( model.isValid() );
    // 역할 시작이 길이 뒤면 다시 읽기가 거절한다 — 저장하지 않는다.
    SW_EXPECT_TRUE( model.setRoleStart( 2, 1.7f, false, error ) );
    SW_EXPECT_FALSE( model.isValid() );
}

/**
 * @brief [PairedAnimationEditModelTest] 미리보기 — 스크럽 시각에서 역할 단계 · 클립 시각과 지난 알림을 세션 규칙대로 구하고, 오프셋 기즈모의 역변환이 정렬 목표로 되돌아온다
 */
SW_TEST_CASE( PairedAnimationEditModelTest, PreviewFollowsSessionRulesAndGizmoInverts )
{
    PairedAnimationEditModel model;
    SW_ASSERT_TRUE( model.loadFromXMLText( TestPairedAnimationEditModelInternal::kXML, "edit.pairedanim.xml" ) );
    model.setScrubTime( 0.1f );
    SW_EXPECT_TRUE( model.computeRolePreview( 2, 0.5f )._stage == PairedAnimationPreviewStage::Waiting );
    model.setScrubTime( 0.5f );
    const PairedAnimationRolePreview arrow = model.computeRolePreview( 2, 0.5f );
    SW_EXPECT_TRUE( arrow._stage == PairedAnimationPreviewStage::Playing );
    SW_EXPECT_NEAR_EQUAL( 0.3f, arrow._clipSeconds, 1e-5f );
    model.setScrubTime( 0.8f );
    SW_EXPECT_TRUE( model.computeRolePreview( 2, 0.5f )._stage == PairedAnimationPreviewStage::Exited );
    model.setScrubTime( 5.0f );
    SW_EXPECT_NEAR_EQUAL( 1.6f, model.getScrubTime(), 1e-6f ); // 길이로 자른다

    vector<uint32> listFired;
    model.collectNotifiesBetween( 0.5f, 1.0f, listFired );
    SW_ASSERT_EQUAL( static_cast<size_t>( 1 ), listFired.size() );
    SW_EXPECT_EQUAL( 0u, listFired[0] );

    // 앵커가 (1, 0, 2) 에서 요 90° 이면 오프셋 (0, 0, 1) 은 +X 로 돌아 (2, 0, 2).
    float3  target{};
    float32 targetYaw = 0.0f;
    model.computeRoleTarget( 1, float3{ 1.0f, 0.0f, 2.0f }, MathUtil::kHalfPi, target, targetYaw );
    SW_EXPECT_NEAR_EQUAL( 2.0f, target._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 2.0f, target._z, 1e-4f );
    // 기즈모로 옮긴 자리를 넣으면 같은 앵커에서 그 자리가 목표가 된다.
    string error;
    SW_ASSERT_TRUE( model.setRoleOffsetFromWorld( 1, float3{ 1.0f, 0.0f, 2.0f }, MathUtil::kHalfPi, float3{ 1.0f, 0.0f, 4.0f }, 0.25f, false, error ) );
    model.computeRoleTarget( 1, float3{ 1.0f, 0.0f, 2.0f }, MathUtil::kHalfPi, target, targetYaw );
    SW_EXPECT_NEAR_EQUAL( 1.0f, target._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, target._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, targetYaw, 1e-4f );
}
