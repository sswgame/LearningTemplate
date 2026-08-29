#include "pch.h"

#include "Editor/Common/Workspace/EditorAssetType.h"

#include "TestFramework/TestFramework.h"

SW_TEST_CASE( EditorAssetTypeTest, MatchesKnownSuffixes )
{
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Scene, "maps/town.scene.xml" ) );
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Prefab, "prefabs/hero.prefab.xml" ) );
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::AnimationGraph, "anim/idle.anim.json" ) );
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::TileMap, "maps/overworld.tilemap.xml" ) );
	SW_EXPECT_FALSE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::TileMap, "maps/town.scene.xml" ) );
	SW_EXPECT_FALSE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Prefab, "maps/town.scene.xml" ) );
}

SW_TEST_CASE( EditorAssetTypeTest, PanelTitlesAndToolKinds )
{
	SW_EXPECT_STREQ( "Prefab Editor", sw::editor::EditorAssetTypeRegistry::getPanelTitle( sw::editor::EditorAssetKind::Prefab ) );
	SW_EXPECT_STREQ( "Animation Graph",
					 sw::editor::EditorAssetTypeRegistry::getPanelTitle( sw::editor::EditorAssetKind::AnimationGraph ) );

	uint32							   kindCount{ 0 };
	const sw::editor::EditorAssetKind* pKind = sw::editor::EditorAssetTypeRegistry::getToolPanelKinds( kindCount );
	SW_ASSERT_NOT_NULL( pKind );
	SW_EXPECT_TRUE( kindCount >= 6 );

	bool bHasAnim{ false };
	for ( uint32 index = 0; index < kindCount; ++index )
	{
		if ( pKind[index] == sw::editor::EditorAssetKind::AnimationGraph )
			bHasAnim = true;
	}
	SW_EXPECT_TRUE( bHasAnim );
}

SW_TEST_CASE( EditorAssetTypeTest, FindPanelTitleLongestSuffix )
{
	const auto animTitle = sw::editor::EditorAssetTypeRegistry::findPanelTitleForPath( "content/hero.anim.json" );
	SW_EXPECT_STREQ( "Animation Graph", sw::string{ animTitle }.c_str() );

	const auto unknownTitle = sw::editor::EditorAssetTypeRegistry::findPanelTitleForPath( "readme.md" );
	SW_EXPECT_TRUE( unknownTitle.empty() );
}

SW_TEST_CASE( EditorAssetTypeTest, DataDoesNotStealAnimJson )
{
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::AnimationGraph, "a.anim.json" ) );
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Data, "a.anim.json" ) );
}
