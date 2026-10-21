#include "pch.h"

#include "GameFramework/Base/Actor/Combat/Weapon/Weapon.h"
#include "GameFramework/Base/Foundation/Data/GamePackDataUtil.h"
#include "GameFramework/Base/Gameplay/Appearance/AppearanceDatabase.h"
#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/CharacterEditorPackData.h"

#include "TestFramework/TestFramework.h"

// GamePackDataTest — 팩 안 데이터 경로는 데이터 종류 등록 줄 하나가 정본이다(게임 코드 · 에디터 확장은 경로를 적지 않는다). 디바이스 없음(nogpu).

/**
 * @brief [GamePackDataTest] 파일 종류는 팩 루트 + 등록 줄의 접미, 폴더 종류는 팩 루트 + 포함 규칙이다
 * @details 변이: 접미를 붙이지 않거나 폴더 종류의 끝 `/` 를 남기면 진다.
 */
SW_TEST_CASE( GamePackDataTest, KindPathComesFromTheRegistration )
{
    sw::string path;
    sw::string error;
    SW_ASSERT_TRUE( sw::GamePackDataUtil::tryMakeKindPath( "game/shooter3d", sw::WeaponCatalog::kDataKind, path, error ) );
    SW_EXPECT_STREQ( "game/shooter3d/data/weapons.xml", path.c_str() );
    SW_ASSERT_TRUE( sw::GamePackDataUtil::tryMakeKindPath( "game/shooter3d/", sw::AppearanceDatabase::kDataKind, path, error ) );
    SW_EXPECT_STREQ( "game/shooter3d/data/appearance", path.c_str() );
}

/**
 * @brief [GamePackDataTest] 등록되지 않은 종류 · 빈 팩 · 접두 밖 팩은 경로 대신 이유를 돌려준다(다른 경로로 대체하지 않는다)
 */
SW_TEST_CASE( GamePackDataTest, MissingRuleIsAnError )
{
    sw::string path;
    sw::string error;
    SW_EXPECT_FALSE( sw::GamePackDataUtil::tryMakeKindPath( "game/shooter3d", "no_such_kind", path, error ) );
    SW_EXPECT_TRUE( path.empty() && error.empty() == false );
    SW_EXPECT_FALSE( sw::GamePackDataUtil::tryMakeKindPath( "", sw::WeaponCatalog::kDataKind, path, error ) );
    SW_EXPECT_FALSE( sw::GamePackDataUtil::tryMakeKindPath( "engine/settings", sw::WeaponCatalog::kDataKind, path, error ) );
}

/**
 * @brief [GamePackDataTest] 캐릭터 편집 데이터는 다섯 칸이 모두 필수다 — 하나가 빠지거나 모르는 속성이면 읽기가 실패하고 표가 빈다. 레퍼런스 포즈는 스켈레톤 이름에서 만든다
 * @details 미리보기 몸 프리셋(`previewPreset`)이 빠지면 창의 미리보기 씬에 세울 몸이 없다 — 다른 칸과 같이 읽기 실패다(몸 없는 창으로 열지 않는다).
 */
SW_TEST_CASE( GamePackDataTest, CharacterEditorDataNeedsEveryField )
{
    sw::editor::CharacterEditorPackData data;
    SW_ASSERT_TRUE( data.loadFromXMLText(
        R"(<CharacterEditor sockets="a.sockets.xml" skeleton="a.skeleton.json" items="items.xml" appearance="appearance" previewPreset="Adventurer"/>)", "full" ) );
    SW_EXPECT_STREQ( "a.skeleton.json", data._skeletonPath.c_str() );
    SW_EXPECT_STREQ( "a.refpose.xml", data._refPosePath.c_str() );
    SW_EXPECT_STREQ( "Adventurer", data._previewPresetID.c_str() );
    SW_EXPECT_FALSE(
        data.loadFromXMLText( R"(<CharacterEditor sockets="a.sockets.xml" skeleton="a.skeleton.json" appearance="appearance" previewPreset="Adventurer"/>)", "noItems" ) );
    SW_EXPECT_TRUE( data._socketPath.empty() );
    SW_EXPECT_FALSE( data.loadFromXMLText( R"(<CharacterEditor sockets="a.sockets.xml" skeleton="a.skeleton.json" items="items.xml" appearance="appearance"/>)", "noPreviewPreset" ) );
    SW_EXPECT_FALSE( data.loadFromXMLText(
        R"(<CharacterEditor sockets="a.sockets.xml" skeleton="a.json" items="items.xml" appearance="appearance" previewPreset="Adventurer"/>)", "skeletonNotNamed" ) );
    SW_EXPECT_FALSE( data.loadFromXMLText( R"(<CharacterEditor sockets="a" skeleton="b" items="d" appearance="e" previewPreset="p" extra="f"/>)", "unknownAttribute" ) );
}
