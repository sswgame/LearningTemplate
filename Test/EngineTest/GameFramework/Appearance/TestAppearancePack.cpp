#include "pch.h"

#include "Core/File/FileUtil.h"

#include "Engine/Resource/Pack/ResourcePakManager.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Resource/ResourcePaths.h"

#include "EngineTest/ResourcePakTestUtil.h"

#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/Base/Gameplay/Inventory/ItemCatalog.h"

#include "TestFramework/TestFramework.h"

// 외형 데이터가 팩에서 읽히는지 — 팩으로 쿠킹한 빌드에는 리소스 폴더가 디스크에 없다.

/**
 * @brief [AppearancePackTest] 프리셋 에셋 폴더(`presets/`)가 팩에만 있어도 읽힌다
 * @details 디스크를 훑던 나열은 여기서 프리셋을 0개로 읽고도 오류를 내지 않았다(폴더가 없으면 선택이라 조용히 넘어갔다).
 */
SW_TEST_CASE( AppearancePackTest, PresetFolderInPackIsRead )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::ItemCatalog items;
    SW_ASSERT_TRUE( items.loadFromResource( "game/shooter3d/data/items.xml" ) );

    const sw::string presetPath = sw::string( "game/shooter3d/data/appearance/" ) + sw::AppearanceDatabase::kPresetFolder + "/packonlyprobe" +
                                  sw::AppearanceDatabase::kPresetExtension;
    const sw::string packPath = sw::FileUtil::joinPath( test::makeTempDirectory( "packs" ), "appearance_preset_probe.pack" );
    SW_ASSERT_TRUE( sw::test::ResourcePakTestUtil::createPackFile( packPath, 0, sw::PackCompressionType::None,
                                                                   {
                                                                       { presetPath, R"(<CharacterAppearance id="PackOnlyProbe" parent="ShooterPlayer"/>)" }
    } ) );
    sw::ResourcePakManager& packManager = sw::ResourceIO::getPackManager();
    SW_ASSERT_TRUE( packManager.mountPack( packPath, 1000 ) );

    sw::AppearanceDatabase database;
    const bool             bLoaded = database.loadFromFolder( "game/shooter3d/data/appearance", &items );
    (void)packManager.unmountPack( packPath ); // 결과는 아래가 본다 — 판을 먼저 되돌려 뒤 테스트에 남기지 않는다
    SW_ASSERT_TRUE_MSG( bLoaded, database.getReport().joined().c_str() );
    SW_EXPECT_TRUE( database.getPresets().findPreset( sw::hashed_string( "PackOnlyProbe" ) ) != nullptr );
}
