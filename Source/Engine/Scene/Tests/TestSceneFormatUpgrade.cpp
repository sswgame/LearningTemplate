// SceneFormatUpgradeTest — 씬 · 프리팹 판 올림 단계: 형제 순서 칸 이름(`_siblingIndex` → `_siblingOrder`, 설계 E-6)(nogpu).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"

/**
 * @brief [SceneFormatUpgradeTest] 옛 씬(판 2)의 형제 번호는 같은 값의 정렬 키가 된다 — 중첩 오브젝트 · 프리팹 오버라이드까지, 그래서 옛 씬이 같은 순서로 열린다
 */
SW_TEST_CASE( SceneFormatUpgradeTest, SiblingIndexBecomesSiblingOrder )
{
    sw::XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( "<Scene formatVersion=\"2\" name=\"Old\"><entities>"
                                "<GameObject _name=\"A\" _siblingIndex=\"3\"><GameObject _name=\"Child\" _siblingIndex=\"1\"/></GameObject>"
                                "<GameObject _name=\"B\"/>"
                                "</entities></Scene>" ) );
    const sw::XMLNode root = doc.getRoot( "Scene" );
    SW_REQUIRE_TRUE( sw::AssetXMLFormat::upgradeToCurrentXMLVersion( sw::SceneDocument::kAssetType, root ) );
    const sw::XMLNode objectA = root.findChild( "entities" ).findChild( "GameObject" );
    SW_EXPECT_TRUE( objectA.findAttribute( "_siblingIndex" ) == nullptr );
    SW_EXPECT_EQUAL( sw::string( "3" ), sw::string( objectA.findAttribute( "_siblingOrder" ) ) );
    SW_EXPECT_EQUAL( sw::string( "1" ), sw::string( objectA.findChild( "GameObject" ).findAttribute( "_siblingOrder" ) ) );
    SW_EXPECT_TRUE( objectA.findNextSibling( "GameObject" ).findAttribute( "_siblingOrder" ) == nullptr ); // 적히지 않은 기본값은 그대로 없다
    SW_EXPECT_EQUAL( sw::AssetXMLFormat::readXMLVersion( root ), sw::SceneDocument::kAssetType._formatVersion );

    sw::XMLDocument prefabDoc;
    SW_REQUIRE_TRUE( prefabDoc.parse( "<Prefab formatVersion=\"0\" name=\"Old\"><GameObject _name=\"Root\" _siblingIndex=\"2\"/></Prefab>" ) );
    const sw::XMLNode prefabRoot = prefabDoc.getRoot( "Prefab" );
    SW_REQUIRE_TRUE( sw::AssetXMLFormat::upgradeToCurrentXMLVersion( sw::PrefabAsset::kAssetType, prefabRoot ) );
    SW_EXPECT_EQUAL( sw::string( "2" ), sw::string( prefabRoot.findChild( "GameObject" ).findAttribute( "_siblingOrder" ) ) );
}
