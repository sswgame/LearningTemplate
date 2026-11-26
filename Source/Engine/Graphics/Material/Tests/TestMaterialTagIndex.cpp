#include "pch.h"

#include "Core/String/StringUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialTagIndex.h"

using namespace sw;

namespace
{
    struct MaterialTagIndexTestUtil
    {
        /** @brief 정적 스위치 하나(켬 `MATERIAL_DETAIL`, 끔 `MATERIAL_PLAIN`)만 다른 머티리얼 XML 입니다. */
        static string makeMaterialXML( bool bDetail )
        {
            return StringUtil::replace( R"(<?xml version="1.0" encoding="utf-8"?>
<MaterialDef formatVersion="0" name="Tagged" shaderPath="engine/shaders/pbr_lit.hlsl" surfaceModel="Lit" alphaMode="Opaque">
	<_properties>
		<item name="_color" type="Color" defaultValue="1 1 1 1"/>
	</_properties>
	<_permutations quality="High" shaderLOD="300" usage="StaticMesh">
		<_staticSwitches>
			<item name="Detail" keyword="MATERIAL_DETAIL" keywordOff="MATERIAL_PLAIN" bEnabled="@" bShaderFeature="1"/>
		</_staticSwitches>
	</_permutations>
</MaterialDef>
)",
                                        "@", bDetail ? "1" : "0" );
        }
    };
} // namespace

/**
 * @brief [MaterialTagIndexTest] 색인 줄은 읽은 머티리얼의 define 이고, 키워드 · 셰이더 · 패스 질의가 `Material` 의 질의와 같은 답을 낸다. 글로 적고 다시 읽어도 같다
 * @details 음성 대조: `makeEntry` 가 define 대신 스위치 이름을 적으면 키워드 질의가 비어 첫 단언이 진다. `findDrawnByPass` 가 거르기를 보지 않으면 둘째 묶음의 개수 단언이 진다.
 *          글 읽기가 구분자로 define 을 나누지 않으면 다시 읽은 색인의 키워드 단언이 진다. 판 검사를 지우면 마지막 단언이 진다.
 */
SW_TEST_CASE( MaterialTagIndexTest, QueriesMatchTheMaterialAndSurviveText )
{
    shared_ptr<Material> detail = Material::create();
    shared_ptr<Material> plain  = Material::create();
    SW_REQUIRE_TRUE( detail->loadFromXML( MaterialTagIndexTestUtil::makeMaterialXML( true ) ) );
    SW_REQUIRE_TRUE( plain->loadFromXML( MaterialTagIndexTestUtil::makeMaterialXML( false ) ) );
    const AssetGUID detailGUID = AssetGUID::fromName( "test/detail.material" );
    const AssetGUID plainGUID  = AssetGUID::fromName( "test/plain.material" );

    MaterialTagIndex index;
    index._listEntry.push_back( MaterialTagIndex::makeEntry( detailGUID, *detail ) );
    index._listEntry.push_back( MaterialTagIndex::makeEntry( plainGUID, *plain ) );

    const vector<AssetGUID> listDetail = index.findByKeyword( "MATERIAL_DETAIL" );
    SW_REQUIRE_EQUAL( size_t( 1 ), listDetail.size() );
    SW_EXPECT_TRUE( listDetail[0] == detailGUID );
    SW_EXPECT_EQUAL( detail->isKeywordEnabled( "MATERIAL_DETAIL" ), true );
    SW_EXPECT_EQUAL( size_t( 2 ), index.findByShaderPath( "engine/shaders/pbr_lit.hlsl" ).size() );
    SW_EXPECT_TRUE( index.findByShaderPath( "engine/shaders/unlit.hlsl" ).empty() );

    MaterialPassFilter excludeDetail;
    excludeDetail._excludedDefine     = "MATERIAL_DETAIL";
    const vector<AssetGUID> listDrawn = index.findDrawnByPass( excludeDetail );
    SW_REQUIRE_EQUAL( size_t( 1 ), listDrawn.size() );
    SW_EXPECT_TRUE( listDrawn[0] == plainGUID );
    SW_EXPECT_EQUAL( plain->isDrawnByPass( excludeDetail ), true );

    const expected<string> text = index.makeText();
    SW_REQUIRE_TRUE( text.has_value() );
    const expected<MaterialTagIndex> read = MaterialTagIndex::parseText( *text, "test" );
    SW_REQUIRE_TRUE( read.has_value() );
    SW_REQUIRE_EQUAL( size_t( 2 ), read->_listEntry.size() );
    SW_EXPECT_EQUAL( size_t( 1 ), read->findByKeyword( "MATERIAL_PLAIN" ).size() );
    SW_EXPECT_TRUE( read->findByKeyword( "MATERIAL_PLAIN" )[0] == plainGUID );

    MaterialTagIndex duplicated = index;
    duplicated._listEntry.push_back( MaterialTagIndex::makeEntry( plainGUID, *plain ) );
    SW_EXPECT_FALSE( duplicated.makeText().has_value() );
    SW_EXPECT_FALSE( MaterialTagIndex::parseText( "<MaterialIndex formatVersion=\"1\"/>", "newer" ).has_value() );
}
