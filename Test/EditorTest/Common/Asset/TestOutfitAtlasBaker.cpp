#include "pch.h"

#include "Editor/Common/Asset/OutfitAtlasBaker.h"

#include "TestFramework/TestFramework.h"

// OutfitAtlasBakerTest — 아틀라스 굽기 명세 읽기(파일 읽기 · 쓰기와 합성은 엔진의 OutfitAtlasTest 와 명령으로 본다).

namespace
{
    constexpr const utf8* kSpecXML = R"(<OutfitAtlas group="Outfit" outputMesh="game/x/atlas/knight.mesh" size="512" padding="2">
  <Part name="Torso" mesh="game/x/models/torso.mesh"/>
  <Part name="Legs" mesh="game/x/models/legs.mesh"/>
  <Map name="BaseColor" output="game/x/textures_raw/knight_basecolor.png">
    <Source part="Torso" texture="game/x/textures_raw/torso.png"/>
    <Source part="Legs" texture="game/x/textures_raw/legs.png"/>
  </Map>
  <Map name="Normal" output="game/x/textures_raw/knight_normal.png">
    <Source part="Legs" texture="game/x/textures_raw/legs_n.png"/>
    <Source part="Torso" texture="game/x/textures_raw/torso_n.png"/>
  </Map>
</OutfitAtlas>)";
} // namespace

/**
 * @brief [OutfitAtlasBakerTest] 명세를 읽으면 부품 · 텍스처 종류 · 크기가 그대로 들어가고, 안 적은 수치는 정의의 초기값이다
 */
SW_TEST_CASE( OutfitAtlasBakerTest, ParsesPartsMapsAndNumbers )
{
    sw::editor::OutfitAtlasSpec spec;
    SW_ASSERT_TRUE( sw::editor::OutfitAtlasBaker::parseSpec( kSpecXML, "spec.outfitatlas.xml", spec ) );
    SW_EXPECT_TRUE( spec._group == sw::hashed_string( "Outfit" ) );
    SW_EXPECT_EQUAL( "game/x/atlas/knight.mesh", spec._outputMesh );
    SW_EXPECT_EQUAL( 512, spec._size );
    SW_EXPECT_EQUAL( 2, spec._padding );
    SW_ASSERT_EQUAL( size_t( 2 ), spec._listPart.size() );
    SW_EXPECT_EQUAL( "game/x/models/legs.mesh", spec._listPart[1]._mesh );
    SW_ASSERT_EQUAL( size_t( 2 ), spec._listMap.size() );
    SW_EXPECT_EQUAL( "game/x/textures_raw/knight_normal.png", spec._listMap[1]._output );
    SW_ASSERT_EQUAL( size_t( 2 ), spec._listMap[1]._listSource.size() );

    const sw::editor::OutfitAtlasSpec defaults;
    sw::editor::OutfitAtlasSpec       minimal;
    SW_ASSERT_TRUE( sw::editor::OutfitAtlasBaker::parseSpec(
        R"(<OutfitAtlas group="G" outputMesh="a.mesh"><Part name="P" mesh="p.mesh"/><Map name="M" output="m.png"><Source part="P" texture="p.png"/></Map></OutfitAtlas>)", "minimal", minimal ) );
    SW_EXPECT_EQUAL( defaults._size, minimal._size );
    SW_EXPECT_EQUAL( defaults._padding, minimal._padding );
}

/**
 * @brief [OutfitAtlasBakerTest] 모르는 속성 · 원소, 빠진 필수 값, 부품마다 텍스처가 하나씩이 아닌 종류, 모르는 부품 이름은 읽기 오류다
 */
SW_TEST_CASE( OutfitAtlasBakerTest, RejectsMalformedSpecs )
{
    SW_TEST_DEFENSIVE_SCOPE( "malformed outfit atlas specs are load errors" );
    sw::editor::OutfitAtlasSpec spec;
    // 모르는 속성
    SW_EXPECT_FALSE( sw::editor::OutfitAtlasBaker::parseSpec(
        R"(<OutfitAtlas group="G" outputMesh="a.mesh" sizee="9"><Part name="P" mesh="p.mesh"/><Map name="M" output="m.png"><Source part="P" texture="p.png"/></Map></OutfitAtlas>)", "unknown attribute",
        spec ) );
    // 필수 값(group)이 빠졌다
    SW_EXPECT_FALSE( sw::editor::OutfitAtlasBaker::parseSpec(
        R"(<OutfitAtlas outputMesh="a.mesh"><Part name="P" mesh="p.mesh"/><Map name="M" output="m.png"><Source part="P" texture="p.png"/></Map></OutfitAtlas>)", "no group", spec ) );
    // 부품 둘인데 텍스처가 하나뿐인 종류
    SW_EXPECT_FALSE( sw::editor::OutfitAtlasBaker::parseSpec(
        R"(<OutfitAtlas group="G" outputMesh="a.mesh"><Part name="P" mesh="p.mesh"/><Part name="Q" mesh="q.mesh"/><Map name="M" output="m.png"><Source part="P" texture="p.png"/></Map></OutfitAtlas>)",
        "missing source", spec ) );
    // 목록에 없는 부품의 텍스처
    SW_EXPECT_FALSE( sw::editor::OutfitAtlasBaker::parseSpec(
        R"(<OutfitAtlas group="G" outputMesh="a.mesh"><Part name="P" mesh="p.mesh"/><Map name="M" output="m.png"><Source part="Ghost" texture="p.png"/></Map></OutfitAtlas>)", "unknown part", spec ) );
    // 부품이나 종류가 없다
    SW_EXPECT_FALSE( sw::editor::OutfitAtlasBaker::parseSpec( R"(<OutfitAtlas group="G" outputMesh="a.mesh"/>)", "empty", spec ) );
    SW_EXPECT_TRUE( spec._listPart.empty() ); // 실패하면 비어 있다
}
