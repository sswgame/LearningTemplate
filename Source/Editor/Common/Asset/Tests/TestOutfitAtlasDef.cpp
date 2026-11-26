// 옷차림 아틀라스 정의 읽기 — XML 본문에서 부품 · 텍스처 종류 · 수치를 읽고, 부품에 없는 이름을 가리킨 정의는 거절하고 비운다(nogpu, 굽기 · 파일 없음).
#include "pch.h"

#include "Core/TestFramework/TestFramework.h"

#include "Editor/Common/Asset/OutfitAtlasDef.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct OutfitAtlasDefTestInternal
    {
        static constexpr const utf8* kValidText = R"(<OutfitAtlas group="Outfit" outputMesh="game/x/atlas/knight_outfit.mesh" size="512" padding="2">
  <Part name="Torso" mesh="game/x/models/torso.mesh"/>
  <Part name="Legs" mesh="game/x/models/legs.mesh"/>
  <Map name="BaseColor" output="game/x/textures_raw/knight_outfit_basecolor.png">
    <Source part="Torso" texture="game/x/textures_raw/torso_basecolor.png"/>
    <Source part="Legs" texture="game/x/textures_raw/legs_basecolor.png"/>
  </Map>
</OutfitAtlas>)";

        static constexpr const utf8* kUnknownPartText = R"(<OutfitAtlas group="Outfit" outputMesh="game/x/atlas/knight_outfit.mesh">
  <Part name="Torso" mesh="game/x/models/torso.mesh"/>
  <Map name="BaseColor" output="game/x/textures_raw/knight_outfit_basecolor.png">
    <Source part="Arms" texture="game/x/textures_raw/arms_basecolor.png"/>
  </Map>
</OutfitAtlas>)";
    };
} // namespace

/**
 * @brief [OutfitAtlasDefTest] 정의 본문의 수치 · 부품 · 텍스처 종류를 순서대로 읽는다
 */
SW_TEST_CASE( OutfitAtlasDefTest, ParsesPartsMapsAndNumbers )
{
    OutfitAtlasDef def;
    SW_REQUIRE_TRUE( OutfitAtlasDef::loadFromXMLText( OutfitAtlasDefTestInternal::kValidText, "knight.outfitatlas.xml", def ) );
    SW_EXPECT_TRUE( def._group == hashed_string( "Outfit" ) );
    SW_EXPECT_EQUAL( string( "game/x/atlas/knight_outfit.mesh" ), def._outputMesh );
    SW_EXPECT_EQUAL( 512, def._size );
    SW_EXPECT_EQUAL( 2, def._padding );
    SW_REQUIRE_EQUAL( size_t{ 2 }, def._listPart.size() );
    SW_EXPECT_TRUE( def._listPart[1]._name == hashed_string( "Legs" ) );
    SW_REQUIRE_EQUAL( size_t{ 1 }, def._listMap.size() );
    SW_EXPECT_EQUAL( string( "game/x/textures_raw/knight_outfit_basecolor.png" ), def._listMap[0]._output );
    SW_REQUIRE_EQUAL( size_t{ 2 }, def._listMap[0]._listSource.size() );
    SW_EXPECT_TRUE( def._listMap[0]._listSource[0]._part == hashed_string( "Torso" ) );
}

/**
 * @brief [OutfitAtlasDefTest] 부품에 없는 이름을 가리킨 텍스처는 오류이고, 실패하면 반쯤 읽은 정의를 남기지 않는다
 */
SW_TEST_CASE( OutfitAtlasDefTest, UnknownPartIsRefusedAndClearsTheDef )
{
    SW_EXPECT_LOG( "Outfit.MapPartUnknown" );
    OutfitAtlasDef def;
    SW_EXPECT_FALSE( OutfitAtlasDef::loadFromXMLText( OutfitAtlasDefTestInternal::kUnknownPartText, "broken.outfitatlas.xml", def ) );
    SW_EXPECT_TRUE( def._listPart.empty() );
    SW_EXPECT_TRUE( def._listMap.empty() );
}
