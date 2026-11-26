#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Content/AssetGUID.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetRegistry.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
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
    const string detailGUID = AssetRegistry::makeReferenceText( AssetGUID::fromName( "test/detail.material" ) );
    const string plainGUID  = AssetRegistry::makeReferenceText( AssetGUID::fromName( "test/plain.material" ) );

    MaterialTagIndex index;
    index._listEntry.push_back( MaterialTagIndex::makeEntry( detailGUID, *detail ) );
    index._listEntry.push_back( MaterialTagIndex::makeEntry( plainGUID, *plain ) );

    const vector<string> listDetail = index.findByKeyword( "MATERIAL_DETAIL" );
    SW_REQUIRE_EQUAL( size_t( 1 ), listDetail.size() );
    SW_EXPECT_TRUE( listDetail[0] == detailGUID );
    SW_EXPECT_EQUAL( detail->isKeywordEnabled( "MATERIAL_DETAIL" ), true );
    SW_EXPECT_EQUAL( size_t( 2 ), index.findByShaderPath( "engine/shaders/pbr_lit.hlsl" ).size() );
    SW_EXPECT_TRUE( index.findByShaderPath( "engine/shaders/unlit.hlsl" ).empty() );

    MaterialPassFilter excludeDetail;
    excludeDetail._excludedDefine  = "MATERIAL_DETAIL";
    const vector<string> listDrawn = index.findDrawnByPass( excludeDetail );
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

/**
 * @brief [MaterialTagIndexTest] 정체성(GUID)이 없는 임포트 산출물 머티리얼(`models/_shared/materials/*.material`)은 팩과 같은 키인 경로 글로 색인에 들고, 산출물이 아닌데 정체성이 없으면 오류다
 * @details 팩 색인(`AssetPackIndex`)은 원본 없는 산출물에 GUID 줄을 두지 않고 경로로 싣는다 — 색인이 GUID 만 받으면 쿠킹이 진다(PackCookMaterialIndexInvalid).
 *          음성 대조: `makeIdentityReference` 가 산출물 폴더를 보지 않으면 첫 단언이, 정체성 없는 손 글도 경로로 받으면 마지막 단언이 진다. 글 읽기가 경로 글을 거절하면 다시 읽은 색인의 단언이 진다.
 */
SW_TEST_CASE( MaterialTagIndexTest, ImportOutputWithoutGUIDIsKeyedByPath )
{
    AssetIdentityRules rules;
    rules._listImportedFolder.push_back( AssetImportedFolder{ "models", "models_raw", { ".glb" }, { ".material" }, true } );
    const string sharedID = "common/models/_shared/materials/1764b6b037118896.material";

    const expected<string> sharedReference = rules.makeIdentityReference( sharedID, nullopt );
    SW_REQUIRE_TRUE( sharedReference.has_value() );
    SW_EXPECT_TRUE( *sharedReference == sharedID );
    const expected<string> guidReference = rules.makeIdentityReference( sharedID, AssetGUID::fromName( sharedID ) );
    SW_REQUIRE_TRUE( guidReference.has_value() );
    SW_EXPECT_TRUE( AssetRegistry::parseReferenceText( *guidReference ).has_value() );

    shared_ptr<Material> shared = Material::create();
    SW_REQUIRE_TRUE( shared->loadFromXML( MaterialTagIndexTestUtil::makeMaterialXML( true ) ) );
    MaterialTagIndex index;
    index._listEntry.push_back( MaterialTagIndex::makeEntry( *sharedReference, *shared ) );
    const expected<string> text = index.makeText();
    SW_REQUIRE_TRUE( text.has_value() );
    const expected<MaterialTagIndex> read = MaterialTagIndex::parseText( *text, "test" );
    SW_REQUIRE_TRUE( read.has_value() );
    const vector<string> listFound = read->findByKeyword( "MATERIAL_DETAIL" );
    SW_REQUIRE_EQUAL( size_t( 1 ), listFound.size() );
    SW_EXPECT_TRUE( listFound[0] == sharedID );

    SW_EXPECT_FALSE( rules.makeIdentityReference( "common/materials/handwritten.material", nullopt ).has_value() );
}

/**
 * @brief [MaterialTagIndexTest] 소스 실행의 색인은 도메인 뿌리 밑 하위 폴더의 머티리얼까지 든다 — 쿠킹 색인(팩에 담은 머티리얼마다 한 줄)과 같은 줄이다
 * @details 음성 대조: 도메인을 바로 아래만 나열하면(`AssetIO::collectAssets` — 예전 길) 줄이 하나도 없어 개수 단언이 진다(`game.playground.materialswitch` 의 `matfind` 가 0 개로 졌다).
 *          정체성을 런타임 색인에서 찾으면 이 케이스 도메인의 머리 GUID 가 색인에 없어 오류가 난다 — 정체성은 파일이 든 머리다.
 */
SW_TEST_CASE( MaterialTagIndexTest, SourceIndexWalksTheWholeDomain )
{
    SW_REQUIRE_TRUE( AssetIO::getContentSource() == ContentSource::Source );
    test::ScopedScratchDomain domain;
    const AssetGUID           nested = AssetGUID::create();
    const AssetGUID           plain  = AssetGUID::create();
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "materials/sub/nested.material" ), AssetGUIDHeader::write( MaterialTagIndexTestUtil::makeMaterialXML( true ), nested ) ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "materials/plain.material" ), AssetGUIDHeader::write( MaterialTagIndexTestUtil::makeMaterialXML( false ), plain ) ) );

    const expected<MaterialTagIndex> index = MaterialTagIndex::loadMounted( { test::ScopedScratchDomain::kDomain } );
    SW_REQUIRE_TRUE( index.has_value() );
    SW_REQUIRE_EQUAL( size_t( 2 ), index->_listEntry.size() );
    const vector<string> listDetail = index->findByKeyword( "MATERIAL_DETAIL" );
    SW_REQUIRE_EQUAL( size_t( 1 ), listDetail.size() );
    SW_EXPECT_TRUE( listDetail[0] == AssetRegistry::makeReferenceText( nested ) );
    SW_EXPECT_EQUAL( size_t( 1 ), index->findByKeyword( "MATERIAL_PLAIN" ).size() );
}
