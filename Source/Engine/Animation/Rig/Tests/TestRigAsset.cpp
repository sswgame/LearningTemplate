// RigAssetTest — 리그 파일(`*.rig.xml`) 읽기: 노드 종류마다의 칸(RigNodeFields.h)과 목록 원소, 모르는 키 · 범위 밖 값 · 모드에 없는 키를 막는지.
#include "pch.h"

#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Rig/RigNode.h"

using namespace sw;

/**
 * @brief [RigAssetTest] 노드 종류마다의 칸과 목록 원소를 읽는다 — 공통 키(가중치 · 켜짐), 열거(충돌체 모양 · 관절 제한 종류), 도 단위 회전
 */
SW_TEST_CASE( RigAssetTest, ReadsNodeFieldsAndItems )
{
    RigAsset asset;
    SW_REQUIRE_TRUE( asset.parseXML( R"(<RigAsset planar="true">
        <targets>
            <item name="Grip" socket="Grip" unit="Weapon" space="hand.r" rotation="0,90,0" />
        </targets>
        <nodes>
            <item type="TwoBoneIK" name="Arm" weight="0.5" enabled="false" root="upperarm.l" mid="lowerarm.l" end="hand.l" target="Grip" match_rotation="true" />
            <item type="CCDChain" name="Tail" target="Grip" iterations="4" max_step_degrees="30">
                <bones><item>tail1</item><item>tail2</item></bones>
                <limits><item bone="tail1" type="Hinge" min_degrees="-10" max_degrees="10" /></limits>
            </item>
            <item type="SpringChain" name="Cape" stiffness="0.2" damping="0.3" gravity="0,-5,0">
                <bones><item>cape1</item><item>cape2</item><item>cape3</item></bones>
                <colliders><item bone="spine" shape="Capsule" a="0,0,0" b="0,0.3,0" radius="0.1" /></colliders>
                <joints><item stiffness="0.5" /><item /></joints>
            </item>
            <item type="Position" name="Follow" bone="weapon" target="Grip" maintain_offset="true" />
        </nodes>
    </RigAsset>)",
                                     "good.rig.xml" ) );
    SW_EXPECT_TRUE( asset.isPlanar() );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( asset.getTargets().size() ) );
    SW_EXPECT_TRUE( asset.getTargets()[0]._kind == RigTargetKind::Socket );
    SW_EXPECT_TRUE( asset.getTargets()[0]._space == hashed_string( "hand.r" ) );
    SW_REQUIRE_EQUAL( 4u, static_cast<uint32>( asset.getNodes().size() ) );
    const RigNode& arm = *asset.getNodes()[0];
    SW_EXPECT_NEAR( 0.5f, arm.getWeight(), 1e-6f );
    SW_EXPECT_FALSE( arm.isEnabled() );
    SW_EXPECT_TRUE( asset.getNodes()[3]->isEnabled() ); // 적지 않으면 켜짐
    SW_EXPECT_NEAR( 1.0f, asset.getNodes()[3]->getWeight(), 1e-6f );
}

namespace
{
    /** @brief 노드 한 줄을 리그로 읽습니다(대상 `Grip` 은 늘 있다). */
    bool parseNode( const utf8* pNodeLine )
    {
        RigAsset     asset;
        const string xml = string( R"(<RigAsset><targets><item name="Grip" bone="hand.r" /></targets><nodes>)" ) + pNodeLine + "</nodes></RigAsset>";
        return asset.parseXML( xml, "bad.rig.xml" );
    }
} // namespace

/** @brief [RigAssetTest] 모르는 키는 칸 읽기가 아니라 리더의 `finish` 가 한 번 낸다 */
SW_TEST_CASE( RigAssetTest, RejectsUnknownKey )
{
    SW_EXPECT_LOG( "Rig.Rig" );
    SW_EXPECT_FALSE( parseNode( R"(<item type="TwoBoneIK" name="Arm" root="a" mid="b" end="c" target="Grip" typo="1" />)" ) );
}

/** @brief [RigAssetTest] 공통 키의 범위(가중치 0..1)는 PROPERTY 가 본다 */
SW_TEST_CASE( RigAssetTest, RejectsWeightOutOfRange )
{
    SW_EXPECT_LOG( "Rig.Rig" );
    SW_EXPECT_FALSE( parseNode( R"(<item type="TwoBoneIK" name="Arm" weight="1.5" root="a" mid="b" end="c" target="Grip" />)" ) );
}

/** @brief [RigAssetTest] 필수 칸이 빠지면 실패다 */
SW_TEST_CASE( RigAssetTest, RejectsMissingRequiredField )
{
    SW_EXPECT_LOG( "Rig.Rig" );
    SW_EXPECT_FALSE( parseNode( R"(<item type="TwoBoneIK" name="Arm" root="a" mid="b" target="Grip" />)" ) );
}

/** @brief [RigAssetTest] 위치 제약은 변환 복사의 `position` 키를 받지 않는다(모드에 없는 키) */
SW_TEST_CASE( RigAssetTest, RejectsKeyOutsideMode )
{
    SW_EXPECT_LOG( "Rig.Rig" );
    SW_EXPECT_FALSE( parseNode( R"(<item type="Position" name="Follow" bone="weapon" target="Grip" position="false" />)" ) );
}

/** @brief [RigAssetTest] 열거 칸은 이름으로 읽는다 — 모르는 이름은 실패다 */
SW_TEST_CASE( RigAssetTest, RejectsUnknownEnumName )
{
    SW_EXPECT_LOG( "Rig.Rig" );
    SW_EXPECT_LOG( "Serialization.UnknownEnumerator" );
    SW_EXPECT_FALSE( parseNode( R"(<item type="SpringChain" name="Cape"><bones><item>a</item><item>b</item></bones>
        <colliders><item bone="spine" shape="Box" radius="0.1" /></colliders></item>)" ) );
}

/**
 * @brief [RigAssetTest] 리타깃 프로필 — 쓴 글을 다시 읽으면 같은 값이다(루트 칸 · 짝 · 사슬 · 리타깃 포즈)
 */
SW_TEST_CASE( RigAssetTest, RetargetProfileRoundTrips )
{
    RetargetProfile profile;
    SW_REQUIRE_TRUE( profile.parseXML( R"(<RetargetProfile source_skeleton="a.skeleton.xml" translation="Copy" map_same_names="true" height_ratio="1.25">
        <root source="root" target="Root" />
        <pelvis source="hips" target="Hips" />
        <chains><item name="LeftArm" ik_goal="true"><source><item>upperarm.l</item></source><target><item>UpperArm_L</item></target></item></chains>
        <bone_map><item source="head" target="Head" /></bone_map>
        <target_pose><item bone="UpperArm_L" rotation="0,0,-40" /></target_pose>
    </RetargetProfile>)",
                                       "a.retarget.xml" ) );
    RetargetProfile again;
    SW_REQUIRE_TRUE( again.parseXML( profile.toXML(), "again.retarget.xml" ) );
    SW_EXPECT_TRUE( again.getTranslationMode() == RetargetTranslationMode::Copy );
    SW_EXPECT_TRUE( again.isMappingSameNames() );
    SW_EXPECT_NEAR( 1.25f, again.getHeightRatioOverride(), 1e-6f );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( again.getChains().size() ) );
    SW_EXPECT_TRUE( again.getChains()[0]._bIKGoal == SW_TRUE );
    SW_REQUIRE_EQUAL( 1u, static_cast<uint32>( again.getBonePairs().size() ) );
    SW_EXPECT_TRUE( again.getBonePairs()[0]._target == hashed_string( "Head" ) );
    SW_EXPECT_TRUE( again.toXML() == profile.toXML() );
}

/** @brief [RigAssetTest] 리타깃 높이 비는 적으면 양수여야 한다(적지 않은 0 은 "계산한다" 뜻이라 검사하지 않는다) */
SW_TEST_CASE( RigAssetTest, RetargetRejectsNonPositiveHeightRatio )
{
    SW_EXPECT_LOG( "Rig.Rig" );
    RetargetProfile profile;
    SW_EXPECT_FALSE( profile.parseXML( R"(<RetargetProfile height_ratio="0"><root source="a" target="b" /><pelvis source="c" target="d" /><chains /></RetargetProfile>)", "bad.retarget.xml" ) );
}
