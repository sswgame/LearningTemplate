// RigAssetTest — 리그 파일(`*.rig.xml`) 읽기: 노드 종류마다의 칸(RigNodeParams.h)과 목록 원소, 모르는 키 · 범위 밖 값 · 모드에 없는 키를 막는지.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/Path/AbsolutePath.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Animation/Retarget/RetargetProfile.h"
#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Animation/Rig/RigNode.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"

using namespace sw;

/**
 * @brief [RigAssetTest] 노드 종류마다의 칸과 목록 원소를 읽는다 — 공통 키(가중치 · 켜짐), 열거(충돌체 모양 · 관절 제한 종류), 도 단위 회전
 */
SW_TEST_CASE( RigAssetTest, ReadsNodeFieldsAndItems )
{
    RigAsset asset;
    SW_REQUIRE_TRUE( asset.loadFromXMLText( R"(<RigAsset planar="true">
        <targets>
            <item name="Grip" socket="Grip" skeletalMesh="Weapon" space="hand.r" rotation="0,90,0" />
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
    bool loadNodeFromXML( const utf8* pNodeLine )
    {
        RigAsset     asset;
        const string xml = string( R"(<RigAsset><targets><item name="Grip" bone="hand.r" /></targets><nodes>)" ) + pNodeLine + "</nodes></RigAsset>";
        return asset.loadFromXMLText( xml, "bad.rig.xml" );
    }
} // namespace

/** @brief [RigAssetTest] 모르는 키는 칸 읽기가 아니라 리더의 `finish` 가 한 번 낸다 */
SW_TEST_CASE( RigAssetTest, RejectsUnknownKey )
{
    SW_EXPECT_LOG( "Rig.Rig" );
    SW_EXPECT_FALSE( loadNodeFromXML( R"(<item type="TwoBoneIK" name="Arm" root="a" mid="b" end="c" target="Grip" typo="1" />)" ) );
}

/** @brief [RigAssetTest] 공통 키의 범위(가중치 0..1)는 PROPERTY 가 본다 */
SW_TEST_CASE( RigAssetTest, RejectsWeightOutOfRange )
{
    SW_EXPECT_LOG( "Rig.Rig" );
    SW_EXPECT_FALSE( loadNodeFromXML( R"(<item type="TwoBoneIK" name="Arm" weight="1.5" root="a" mid="b" end="c" target="Grip" />)" ) );
}

/** @brief [RigAssetTest] 필수 칸이 빠지면 실패다 */
SW_TEST_CASE( RigAssetTest, RejectsMissingRequiredField )
{
    SW_EXPECT_LOG( "Rig.Rig" );
    SW_EXPECT_FALSE( loadNodeFromXML( R"(<item type="TwoBoneIK" name="Arm" root="a" mid="b" target="Grip" />)" ) );
}

/** @brief [RigAssetTest] 위치 제약은 변환 복사의 `position` 키를 받지 않는다(모드에 없는 키) */
SW_TEST_CASE( RigAssetTest, RejectsKeyOutsideMode )
{
    SW_EXPECT_LOG( "Rig.Rig" );
    SW_EXPECT_FALSE( loadNodeFromXML( R"(<item type="Position" name="Follow" bone="weapon" target="Grip" position="false" />)" ) );
}

/** @brief [RigAssetTest] 열거 칸은 이름으로 읽는다 — 모르는 이름은 실패다 */
SW_TEST_CASE( RigAssetTest, RejectsUnknownEnumName )
{
    SW_EXPECT_LOG( "Rig.Rig" );
    SW_EXPECT_LOG( "Serialization.UnknownEnumerator" );
    SW_EXPECT_FALSE( loadNodeFromXML( R"(<item type="SpringChain" name="Cape"><bones><item>a</item><item>b</item></bones>
        <colliders><item bone="spine" shape="Box" radius="0.1" /></colliders></item>)" ) );
}

/**
 * @brief [RigAssetTest] 리타깃 프로필 — 쓴 글을 다시 읽으면 같은 값이다(루트 칸 · 짝 · 사슬 · 리타깃 포즈)
 */
SW_TEST_CASE( RigAssetTest, RetargetProfileRoundTrips )
{
    RetargetProfile profile;
    SW_REQUIRE_TRUE( profile.loadFromXMLText( R"(<RetargetProfile source_skeleton="a.skeleton.xml" translation="Copy" map_same_names="true" height_ratio="1.25">
        <root source="root" target="Root" />
        <pelvis source="hips" target="Hips" />
        <chains><item name="LeftArm" ik_goal="true"><source><item>upperarm.l</item></source><target><item>UpperArm_L</item></target></item></chains>
        <bone_map><item source="head" target="Head" /></bone_map>
        <target_pose><item bone="UpperArm_L" rotation="0,0,-40" /></target_pose>
    </RetargetProfile>)",
                                              "a.retarget.xml" ) );
    RetargetProfile again;
    SW_REQUIRE_TRUE( again.loadFromXMLText( profile.toXML(), "again.retarget.xml" ) );
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
    SW_EXPECT_FALSE( profile.loadFromXMLText( R"(<RetargetProfile height_ratio="0"><root source="a" target="b" /><pelvis source="c" target="d" /><chains /></RetargetProfile>)", "bad.retarget.xml" ) );
}

/**
 * @brief [RigAssetTest] 차이 파일 — 노드는 이름으로 합치고(적지 않은 칸은 물려받음), `_before` 로 끼우고 `_op="Remove"` 로 뺀다. 평가 순서가 그대로 따라온다
 */
SW_TEST_CASE( RigAssetTest, ChildMergesNodesByName )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "base.rig.xml" ), R"(<RigAsset><targets><item name="Grip" bone="hand.r" /></targets><nodes>
        <item type="TwoBoneIK" name="Arm" weight="0.5" root="upperarm.l" mid="lowerarm.l" end="hand.l" target="Grip" />
        <item type="Position" name="Follow" bone="weapon" target="Grip" />
        <item type="Position" name="Extra" bone="extra" target="Grip" />
    </nodes></RigAsset>)" ) );
    const string baseID = test::ScopedScratchDomain::makeID( "base.rig.xml" ).str();
    RigAsset     asset;
    SW_REQUIRE_TRUE( asset.loadFromXMLText( "<RigAsset _base=\"" + baseID + "\"><nodes><item name=\"Arm\" weight=\"0.25\" /><item name=\"Extra\" _op=\"Remove\" />"
                                                                            "<item type=\"Position\" name=\"First\" bone=\"first\" target=\"Grip\" _before=\"Arm\" /></nodes></RigAsset>",
                                            "child.rig.xml" ) );
    SW_REQUIRE_EQUAL( 3u, static_cast<uint32>( asset.getNodes().size() ) );
    SW_EXPECT_TRUE( asset.getNodes()[0]->getName() == hashed_string( "First" ) );
    SW_EXPECT_TRUE( asset.getNodes()[1]->getName() == hashed_string( "Arm" ) );
    SW_EXPECT_NEAR( 0.25f, asset.getNodes()[1]->getWeight(), 1e-6f );
    SW_EXPECT_TRUE( asset.getNodes()[2]->getName() == hashed_string( "Follow" ) );
    SW_EXPECT_TRUE( asset.findTargetIndex( hashed_string( "Grip" ) ) == 0u ); // 물려받음
}

/**
 * @brief [RigAssetTest] 리타겟 차이 파일 — 사슬은 이름, 뼈 짝은 원본 뼈, 자세는 뼈로 합치고 `root` 처럼 하나뿐인 칸은 적은 속성만 덮는다
 */
SW_TEST_CASE( RigAssetTest, RetargetChildMergesByKeys )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "base.retarget.xml" ), R"(<RetargetProfile>
        <root source="root" target="root" /><pelvis source="pelvis" target="hips" />
        <chains>
            <item name="LeftArm"><source><item>a</item></source><target><item>b</item></target></item>
            <item name="RightArm"><source><item>c</item></source><target><item>d</item></target></item>
        </chains>
        <bone_map><item source="head" target="head" /></bone_map>
    </RetargetProfile>)" ) );
    const string    baseID = test::ScopedScratchDomain::makeID( "base.retarget.xml" ).str();
    RetargetProfile profile;
    SW_REQUIRE_TRUE( profile.loadFromXMLText( "<RetargetProfile _base=\"" + baseID + "\"><pelvis target=\"pelvis_b\" /><chains><item name=\"RightArm\"><target><item>e</item></target></item></chains>"
                                                                                     "<bone_map><item source=\"head\" target=\"head_b\" /></bone_map></RetargetProfile>",
                                              "child.retarget.xml" ) );
    const string written = profile.toXML();
    SW_EXPECT_TRUE( written.find( "pelvis_b" ) != string::npos );
    SW_EXPECT_TRUE( written.find( "source=\"pelvis\"" ) != string::npos ); // 물려받음
    SW_EXPECT_TRUE( written.find( "head_b" ) != string::npos );
    SW_EXPECT_TRUE( written.find( "LeftArm" ) != string::npos );
    SW_EXPECT_TRUE( written.find( "<item>e</item>" ) != string::npos );
    SW_EXPECT_TRUE( written.find( "<item>d</item>" ) == string::npos ); // 뼈 목록은 통째로 바뀐다
}

/**
 * @brief [RigAssetTest] 리타겟 변형 저장 — 부모(`_base`)를 가리키던 프로필은 부모와의 차이로 쓰고(물려받은 사슬 · 짝은 적지 않는다), 저장 → 다시 읽기가 같은 프로필이다
 */
SW_TEST_CASE( RigAssetTest, RetargetChildSavesOnlyItsDifference )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "base.retarget.xml" ), R"(<RetargetProfile>
        <root source="root" target="root" /><pelvis source="pelvis" target="hips" />
        <chains>
            <item name="LeftArm"><source><item>a</item></source><target><item>b</item></target></item>
            <item name="RightArm"><source><item>c</item></source><target><item>d</item></target></item>
        </chains>
    </RetargetProfile>)" ) );
    const string    baseID = test::ScopedScratchDomain::makeID( "base.retarget.xml" ).str();
    RetargetProfile profile;
    SW_REQUIRE_TRUE( profile.loadFromXMLText( "<RetargetProfile _base=\"" + baseID + "\"><pelvis target=\"pelvis_b\" /></RetargetProfile>", "child.retarget.xml" ) );
    SW_EXPECT_EQUAL( baseID, profile.getBaseID() );
    profile.addBonePair( RetargetBonePair{ hashed_string( "head" ), hashed_string( "head_b" ) } );

    const optional<AbsolutePath> childPath = AbsolutePath::tryMake( domain.makeDiskPath( "child.retarget.xml" ) );
    SW_REQUIRE_TRUE( childPath.has_value() );
    SW_REQUIRE_TRUE( profile.saveToFile( *childPath ) );
    string saved;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( childPath->str(), saved ) );
    SW_EXPECT_TRUE( saved.find( baseID ) != string::npos );
    SW_EXPECT_TRUE( saved.find( "head_b" ) != string::npos );
    SW_EXPECT_TRUE( saved.find( "LeftArm" ) == string::npos ); // 물려받은 사슬은 적지 않는다

    const shared_ptr<const RetargetProfile> pReloaded = SharedAssetCache<RetargetProfile>::acquire( test::ScopedScratchDomain::makeID( "child.retarget.xml" ).view() );
    SW_REQUIRE_TRUE( pReloaded != nullptr );
    SW_EXPECT_EQUAL( profile.toXML(), pReloaded->toXML() );
    SW_EXPECT_EQUAL( baseID, pReloaded->getBaseID() );
}
