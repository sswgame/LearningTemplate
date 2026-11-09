// RigFileRewriteTest — 리그 파일 되쓰기: 변형(`_base`) 파일은 물려받은 노드 · 대상을 이름 열쇠의 덮어쓰기 줄로 더하고 부모를 지킨다(nogpu).
#include "pch.h"

#define SW_TEST_INTERNAL // 임시 리소스 도메인 픽스처가 엔진 서비스(`AssetPaths`)를 직접 쓴다

#include "Core/File/FileUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Commands/Asset/RigSaveParams.h"

#include "Engine/Animation/Rig/RigAsset.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"

using namespace sw;
using namespace sw::editor;

/**
 * @brief [RigFileRewriteTest] 변형 저장 — 물려받은 노드를 고치면 그 이름의 덮어쓰기 줄이 생기고(모든 칸), 물려받은 다른 칸은 적지 않으며 `_base` 가 남는다. 다시 읽으면 고친 값이다
 */
SW_TEST_CASE( RigFileRewriteTest, VariantWritesOverrideRows )
{
    const test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "base.rig.xml" ), R"(<RigAsset><targets><item name="Grip" bone="hand.r" /></targets><nodes>
        <item type="TwoBoneIK" name="Arm" weight="0.5" root="upperarm.l" mid="lowerarm.l" end="hand.l" target="Grip" />
        <item type="Position" name="Follow" bone="weapon" target="Grip" />
    </nodes></RigAsset>)" ) );
    const string baseID    = test::ScopedScratchDomain::makeID( "base.rig.xml" ).str();
    const string childText = "<RigAsset _base=\"" + baseID + "\"><nodes><item name=\"Arm\" weight=\"0.25\" /></nodes></RigAsset>";

    RigSaveParams params;
    params._listNode.push_back( RigNodeSaveParams{ hashed_string( "Follow" ), 0.75f, false } );
    RigTargetSaveParams grip;
    grip._name        = hashed_string( "Grip" );
    grip._translation = float3{ 0.0f, 1.0f, 0.0f };
    grip._rotation    = quaternion::Identity;
    params._listTarget.push_back( grip );
    string written;
    SW_REQUIRE_TRUE( RigSaveParams::rewriteSettings( childText, "child.rig.xml", params, written ) );
    SW_EXPECT_TRUE( written.find( baseID ) != string::npos );
    SW_EXPECT_TRUE( written.find( "Follow" ) != string::npos );
    SW_EXPECT_TRUE( written.find( "weapon" ) == string::npos ); // 물려받은 칸은 적지 않는다
    SW_EXPECT_TRUE( written.find( "hand.r" ) == string::npos );

    RigAsset asset;
    SW_REQUIRE_TRUE( asset.loadFromXMLText( written, "child.rig.xml" ) );
    SW_REQUIRE_EQUAL( 2u, static_cast<uint32>( asset.getNodes().size() ) );
    SW_EXPECT_NEAR( 0.25f, asset.getNodes()[0]->getWeight(), 1e-6f );
    SW_EXPECT_NEAR( 0.75f, asset.getNodes()[1]->getWeight(), 1e-6f );
    SW_EXPECT_TRUE( asset.findTargetIndex( hashed_string( "Grip" ) ) == 0u );
}
