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
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Material, "mats/hero.mat" ) );
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Material, "mats/hero.material" ) );
}

SW_TEST_CASE( EditorAssetTypeTest, PanelTitlesAndToolKinds )
{
	SW_EXPECT_STREQ( "Prefab Editor", sw::editor::EditorAssetTypeRegistry::getPanelTitle( sw::editor::EditorAssetKind::Prefab ) );
	SW_EXPECT_STREQ( "Animation Graph",
					 sw::editor::EditorAssetTypeRegistry::getPanelTitle( sw::editor::EditorAssetKind::AnimationGraph ) );

	SW_EXPECT_STREQ( "Sequencer", sw::editor::EditorAssetTypeRegistry::getPanelTitle( sw::editor::EditorAssetKind::Sequence ) );
	SW_EXPECT_STREQ( "Material", sw::editor::EditorAssetTypeRegistry::getPanelTitle( sw::editor::EditorAssetKind::Material ) );

	uint32							   kindCount{ 0 };
	const sw::editor::EditorAssetKind* pKind = sw::editor::EditorAssetTypeRegistry::getToolPanelKinds( kindCount );
	SW_ASSERT_NOT_NULL( pKind );
	SW_EXPECT_TRUE( kindCount >= 7 );

	bool bHasAnim{ false };
	bool bHasMaterial{ false };
	for ( uint32 index = 0; index < kindCount; ++index )
	{
		if ( pKind[index] == sw::editor::EditorAssetKind::AnimationGraph )
			bHasAnim = true;
		if ( pKind[index] == sw::editor::EditorAssetKind::Material )
			bHasMaterial = true;
	}
	SW_EXPECT_TRUE( bHasAnim );
	SW_EXPECT_TRUE( bHasMaterial );
}

SW_TEST_CASE( EditorAssetTypeTest, FindPanelTitleLongestSuffix )
{
	const auto animTitle = sw::editor::EditorAssetTypeRegistry::findPanelTitleForPath( "content/hero.anim.json" );
	SW_EXPECT_STREQ( "Animation Graph", sw::string{ animTitle }.c_str() );

	const auto matTitle = sw::editor::EditorAssetTypeRegistry::findPanelTitleForPath( "content/hero.mat" );
	SW_EXPECT_STREQ( "Material", sw::string{ matTitle }.c_str() );

	const auto unknownTitle = sw::editor::EditorAssetTypeRegistry::findPanelTitleForPath( "readme.md" );
	SW_EXPECT_TRUE( unknownTitle.empty() );
}

SW_TEST_CASE( EditorAssetTypeTest, DataDoesNotStealAnimJson )
{
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::AnimationGraph, "a.anim.json" ) );
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Data, "a.anim.json" ) );
}

SW_TEST_CASE( EditorAssetTypeTest, AllAssetKindsAndMatchesAny )
{
	// 1) Texture, Shader, Audio, DialogueGraph, SpriteClip 매칭 검증
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Texture, "textures/albedo.png" ) );
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Texture, "textures/normal.dds" ) );
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Shader, "shaders/pbr.hlsl" ) );
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Audio, "audio/bgm.wav" ) );
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::Audio, "audio/sfx.ogg" ) );
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::DialogueGraph, "dialogue/intro.dialogue.json" ) );
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matches( sw::editor::EditorAssetKind::SpriteClip, "sprites/run.sprite.json" ) );

	// 2) matchesAny 및 matchesOther 검증
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matchesAny( "maps/town.scene.xml" ) );
	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matchesAny( "textures/hero.png" ) );
	SW_EXPECT_FALSE( sw::editor::EditorAssetTypeRegistry::matchesAny( "notes/todo.txt" ) );

	SW_EXPECT_TRUE( sw::editor::EditorAssetTypeRegistry::matchesOther( "notes/todo.txt" ) );
	SW_EXPECT_FALSE( sw::editor::EditorAssetTypeRegistry::matchesOther( "maps/town.scene.xml" ) );

	// 3) appendImportExtensions 검증
	sw::vector<sw::string> listImportExts;
	sw::editor::EditorAssetTypeRegistry::appendImportExtensions( listImportExts );
	SW_EXPECT_FALSE( listImportExts.empty() );

	// 4) getBrowserFilters 검증
	uint32										filterCount = 0;
	const sw::editor::EditorAssetBrowserFilter* pFilters	= sw::editor::EditorAssetTypeRegistry::getBrowserFilters( filterCount );
	SW_ASSERT_NOT_NULL( pFilters );
	SW_EXPECT_TRUE( filterCount > 0 );
}
