// 임포트 결과 공유 — 합치기 판단(내용 + 설정), 값 차이만 적기, 슬롯 왕복, 안 쓰는 파일 지우기, 결과 · 내용 주소 원본의 정체성.
#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Editor/Common/Asset/ImportedAssetStore.h"
#include "Editor/Common/Asset/ModelImport/ModelImportConfig.h"
#include "Editor/Common/Asset/ModelImport/ModelImportMaterial.h"
#include "Editor/Common/Asset/Tests/EditorScratchDomain.h"
#include "Editor/Common/Asset/Texture/TextureImportConfig.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Content/AssetGUID.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Material/MaterialTypes.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/XML/XMLDocument.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    struct ImportedAssetStoreTestUtil
    {
        static constexpr const utf8* kTemplate = R"(<?xml version="1.0" encoding="utf-8"?>
<MaterialDef formatVersion="0" name="Template" shaderPath="engine/shaders/pbr_lit.hlsl" surfaceModel="Lit" alphaMode="Opaque">
	<_properties>
		<item name="_color" type="Color" defaultValue="1 1 1 1" displayName="Base Color" group="Surface" bHdr="0" bSrgb="1"/>
		<item name="_roughness" type="Range" defaultValue="1" min="0.0" max="1.0" group="Surface"/>
		<item name="_albedoMap" type="Texture2D" displayName="Albedo" group="Surface" bSrgb="1"/>
	</_properties>
	<_permutations quality="High" shaderLOD="300" usage="StaticMesh">
		<_alwaysDefines>
			<item>MATERIAL_DOMAIN_SURFACE</item>
		</_alwaysDefines>
	</_permutations>
</MaterialDef>
)";

        /** @brief 임포터처럼 견본을 읽어 이름 · 거칠기 · 기본색 텍스처를 채운 글입니다. */
        static string makeFull( const utf8* pName, const utf8* pRoughness, const utf8* pAlbedo )
        {
            XMLDocument document;
            SW_EXPECT_TRUE( document.parse( kTemplate ) );
            const XMLNode root = document.getRoot( "MaterialDef" );
            root.setAttribute( "name", pName );
            for ( XMLNode item = root.findChild( "_properties" ).findChild( "item" ); item; item = item.findNextSibling( "item" ) )
            {
                if ( item.getAttributeText( "name" ) == "_roughness" )
                    item.setAttribute( "defaultValue", pRoughness );
                if ( item.getAttributeText( "name" ) == "_albedoMap" )
                    item.setAttribute( "assetPath", pAlbedo );
            }
            return document.saveToString();
        }
    };
} // namespace

/**
 * @brief [ImportedAssetStoreTest] 같은 바이트 + 같은 설정은 같은 키, 바이트나 설정이 다르면 다른 키다 — 설정이 다르면 지금 바이트가 같아도 다른 결과다
 */
SW_TEST_CASE( ImportedAssetStoreTest, KeyMixesBytesAndSettings )
{
    const string bytes = "clip bytes";
    const string same  = ImportedAssetStore::makeKey( bytes.data(), bytes.size(), "codec=acl" );
    SW_EXPECT_EQUAL( same, ImportedAssetStore::makeKey( bytes.data(), bytes.size(), "codec=acl" ) );
    SW_EXPECT_EQUAL( size_t{ 16 }, same.size() );
    SW_EXPECT_TRUE( same != ImportedAssetStore::makeKey( bytes.data(), bytes.size(), "codec=raw" ) );
    const string other = "clip bytez";
    SW_EXPECT_TRUE( same != ImportedAssetStore::makeKey( other.data(), other.size(), "codec=acl" ) );
}

/**
 * @brief [ImportedAssetStoreTest] 클립 설정 글은 클립 바이트를 정하는 칸만 본다 — LOD · 재질 칸이 달라도 같고, 정밀도가 다르면 다르다(KayKit 몸 규칙 둘이 클립을 나눠 쓴다)
 */
SW_TEST_CASE( ImportedAssetStoreTest, ClipSettingsTextIgnoresOtherRuleProperties )
{
    ModelImportRule first;
    ModelImportRule second;
    second._bImportMaterials = SW_FALSE;
    second._listClipName.push_back( "idle" );
    SW_EXPECT_EQUAL( first.makeClipSettingsText(), second.makeClipSettingsText() );
    second._animationPrecision = first._animationPrecision * 2.0f + 1.0f;
    SW_EXPECT_TRUE( first.makeClipSettingsText() != second.makeClipSettingsText() );
}

/**
 * @brief [ImportedAssetStoreTest] 같은 키를 두 번 써도 한 벌이고, 같은 이름에 다른 바이트면 덮어쓰지 않고 실패한다(다른 모델이 쓰는 파일이다)
 */
SW_TEST_CASE( ImportedAssetStoreTest, WriteIfAbsentSharesAndRefusesOtherBytes )
{
    const AbsolutePath root = test::makeScratchPath( "ImportedAssetStore" );
    const AbsolutePath path = ImportedAssetStore::makeStorePath( root, "clips", "aaaa", ".animclip" );
    SW_EXPECT_TRUE( StringUtil::endsWith( path.str(), "/models/_shared/clips/aaaa.animclip" ) );
    const string bytes = "first";
    SW_REQUIRE_TRUE( ImportedAssetStore::writeIfAbsent( path, bytes.data(), bytes.size() ) );
    SW_EXPECT_TRUE( ImportedAssetStore::writeIfAbsent( path, bytes.data(), bytes.size() ) );
    const string other = "other";
    SW_EXPECT_LOG( "ImportedAssetStore.ContentMismatch" );
    SW_EXPECT_FALSE( ImportedAssetStore::writeIfAbsent( path, other.data(), other.size() ) );
    string text;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( path, text ) );
    SW_EXPECT_EQUAL( bytes, text );
    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}

/**
 * @brief [ImportedAssetStoreTest] 도메인은 `models_raw/` 앞이다 — 임포트는 자기 도메인(팩)의 저장소에만 쓴다
 */
SW_TEST_CASE( ImportedAssetStoreTest, DomainFolderIsBeforeRawFolder )
{
    const AbsolutePath domain = test::makeScratchPath( "Content" ).join( "game/playground" );
    SW_EXPECT_TRUE( ImportedAssetStore::findDomainFolder( domain.join( "models_raw/a/b.glb" ) ) == domain );
    SW_EXPECT_TRUE( ImportedAssetStore::findDomainFolder( domain.join( "other/b.glb" ) ).empty() );
}

/**
 * @brief [ImportedAssetStoreTest] 값이 같으면 이름이 달라도 한 키, 값이 다르면 다른 키다. 적는 것은 견본과 다른 칸뿐이다(형식 · 표시 이름 · 셰이더는 부모 것)
 */
SW_TEST_CASE( ImportedAssetStoreTest, SharedMaterialKeepsOnlyDifferencesAndIgnoresName )
{
    using Util = ImportedAssetStoreTestUtil;
    ModelImportMaterial::SharedMaterialText first;
    ModelImportMaterial::SharedMaterialText renamed;
    ModelImportMaterial::SharedMaterialText otherValue;
    ModelImportMaterial::SharedMaterialText otherTexture;
    const string                            parent = "engine/materials/pbrmaterial.material";
    SW_REQUIRE_TRUE( ModelImportMaterial::makeSharedMaterialText( Util::makeFull( "mi_trim", "0.5", "common/textures/a.dds" ), Util::kTemplate, parent, first ) );
    SW_REQUIRE_TRUE( ModelImportMaterial::makeSharedMaterialText( Util::makeFull( "mi_other", "0.5", "common/textures/a.dds" ), Util::kTemplate, parent, renamed ) );
    SW_REQUIRE_TRUE( ModelImportMaterial::makeSharedMaterialText( Util::makeFull( "mi_trim", "0.25", "common/textures/a.dds" ), Util::kTemplate, parent, otherValue ) );
    SW_REQUIRE_TRUE( ModelImportMaterial::makeSharedMaterialText( Util::makeFull( "mi_trim", "0.5", "common/textures/b.dds" ), Util::kTemplate, parent, otherTexture ) );
    SW_EXPECT_EQUAL( first._key, renamed._key );
    SW_EXPECT_EQUAL( first._text, renamed._text );
    SW_EXPECT_TRUE( first._key != otherValue._key );
    SW_EXPECT_TRUE( first._key != otherTexture._key );

    SW_EXPECT_TRUE( StringUtil::contains( first._text, "defaultValue=\"0.5\"" ) );
    SW_EXPECT_TRUE( StringUtil::contains( first._text, "assetPath=\"common/textures/a.dds\"" ) );
    SW_EXPECT_FALSE( StringUtil::contains( first._text, "displayName" ) );
    SW_EXPECT_FALSE( StringUtil::contains( first._text, "shaderPath" ) );
    SW_EXPECT_FALSE( StringUtil::contains( first._text, "_color" ) );
    SW_EXPECT_FALSE( StringUtil::contains( first._text, "_permutations" ) );
    SW_EXPECT_EQUAL( parent, DataInheritance::findBasePath( first._text ) );
}

/**
 * @brief [ImportedAssetStoreTest] 슬롯 → 공유 → 견본을 펼치면 채운 값 그대로다. 두 슬롯이 같은 공유 파일을 가리켜도 인스턴스 값은 인스턴스 것이라 섞이지 않는다
 */
SW_TEST_CASE( ImportedAssetStoreTest, SlotRoundTripsAndInstanceValuesDoNotMix )
{
    using Util = ImportedAssetStoreTestUtil;
    const test::EditorScratchDomain domain;
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "template.material" ), Util::kTemplate ) );
    const string                            templateID = test::EditorScratchDomain::makeID( "template.material" );
    ModelImportMaterial::SharedMaterialText shared;
    SW_REQUIRE_TRUE( ModelImportMaterial::makeSharedMaterialText( Util::makeFull( "slot_a", "0.5", "common/textures/a.dds" ), Util::kTemplate, templateID, shared ) );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "shared.material" ), shared._text ) );
    const string sharedID = test::EditorScratchDomain::makeID( "shared.material" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( domain.makeDiskPath( "slot_a.material" ), ModelImportMaterial::makeSlotMaterialText( "slot_a", sharedID ) ) );

    shared_ptr<Material> material = Material::create();
    SW_REQUIRE_TRUE( material->loadFromAsset( AssetID{ test::EditorScratchDomain::makeID( "slot_a.material" ) } ) );
    SW_EXPECT_EQUAL( string( "slot_a" ), material->getName() );
    SW_EXPECT_EQUAL( string( "engine/shaders/pbr_lit.hlsl" ), material->getShaderPath() );
    const MaterialProperty* pRoughness = material->findProperty( hashed_string( "_roughness" ) );
    const MaterialProperty* pAlbedo    = material->findProperty( hashed_string( "_albedoMap" ) );
    const MaterialProperty* pColor     = material->findProperty( hashed_string( "_color" ) );
    SW_REQUIRE_TRUE( pRoughness != nullptr && pAlbedo != nullptr && pColor != nullptr );
    SW_EXPECT_EQUAL( string( "0.5" ), pRoughness->_defaultValue );
    SW_EXPECT_EQUAL( string( "common/textures/a.dds" ), pAlbedo->_assetPath );
    SW_EXPECT_EQUAL( string( "Base Color" ), pColor->_displayName );
    SW_EXPECT_TRUE( pColor->_shaderType == MaterialPropertyKind::Float4 ); // 파일에 적지 않은 칸 형식

    // 인스턴스 값은 부모(공유 머티리얼)를 고치지 않는다.
    shared_ptr<MaterialInstance> first  = MaterialInstance::create( material.get() );
    shared_ptr<MaterialInstance> second = MaterialInstance::create( material.get() );
    SW_REQUIRE_TRUE( first->setParameter( hashed_string( "_roughness" ), "0.125" ) );
    SW_EXPECT_EQUAL( string( "0.5" ), material->findProperty( hashed_string( "_roughness" ) )->_defaultValue );
    string firstValue;
    string secondValue;
    SW_REQUIRE_TRUE( first->getParameter( hashed_string( "_roughness" ), firstValue ) );
    SW_REQUIRE_TRUE( second->getParameter( hashed_string( "_roughness" ), secondValue ) );
    SW_EXPECT_EQUAL( string( "0.125" ), firstValue );
    SW_EXPECT_EQUAL( string( "0.5" ), secondValue );
}

/**
 * @brief [ImportedAssetStoreTest] 글에서 저장소 경로를 찾고, 가리키는 파일이 사라지거나 바뀌면 결과 해시가 달라진다(스탬프가 잡는다)
 */
SW_TEST_CASE( ImportedAssetStoreTest, ReferencesAreFoundAndMixedIntoTheHash )
{
    const AbsolutePath root = test::makeScratchPath( "ImportedAssetStoreRefs" );
    vector<string>     listReference;
    const string       text = R"(<clip name="a" path="common/models/_shared/clips/aaaa.animclip"/><clip name="b" path="common/models/_shared/clips/bbbb.animclip"/><x y="common/models/other.mesh"/>)";
    ImportedAssetStore::collectReferences( text, listReference );
    SW_REQUIRE_EQUAL( size_t{ 2 }, listReference.size() );
    SW_EXPECT_EQUAL( string( "common/models/_shared/clips/bbbb.animclip" ), listReference[1] );

    const AbsolutePath one     = root.join( "common/models/_shared/clips/aaaa.animclip" );
    const AbsolutePath two     = root.join( "common/models/_shared/clips/bbbb.animclip" );
    const AbsolutePath mesh    = root.join( "common/models/knight.mesh" );
    const string       content = "bytes";
    SW_REQUIRE_TRUE( ImportedAssetStore::writeIfAbsent( one, content.data(), content.size() ) );
    SW_REQUIRE_TRUE( ImportedAssetStore::writeIfAbsent( two, content.data(), content.size() ) );
    const uint64 before = ImportedAssetStore::mixReferencedFiles( mesh, text, 1 );
    SW_EXPECT_EQUAL( before, ImportedAssetStore::mixReferencedFiles( mesh, text, 1 ) );
    SW_REQUIRE_TRUE( FileUtil::removeFile( two ) );
    SW_EXPECT_TRUE( before != ImportedAssetStore::mixReferencedFiles( mesh, text, 1 ) );
    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}

/**
 * @brief [ImportedAssetStoreTest] 전체 임포트 뒤 가리키는 결과가 없는 공유 파일만 지운다
 */
SW_TEST_CASE( ImportedAssetStoreTest, RemovesOnlyUnreferencedFiles )
{
    const AbsolutePath root    = test::makeScratchPath( "ImportedAssetStoreClean" );
    const AbsolutePath kept    = root.join( "common/models/_shared/clips/aaaa.animclip" );
    const AbsolutePath stale   = root.join( "common/models/_shared/clips/bbbb.animclip" );
    const string       content = "bytes";
    SW_REQUIRE_TRUE( ImportedAssetStore::writeIfAbsent( kept, content.data(), content.size() ) );
    SW_REQUIRE_TRUE( ImportedAssetStore::writeIfAbsent( stale, content.data(), content.size() ) );
    const AbsolutePath setPath = root.join( "common/models/knight/knight.animset.xml" );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( setPath, R"(<AnimationSet formatVersion="0"><clip name="a" path="common/models/_shared/clips/aaaa.animclip"/></AnimationSet>)" ) );

    vector<string> listProblem;
    SW_EXPECT_EQUAL( uint32{ 1 }, ImportedAssetStore::removeUnreferenced( vector<AbsolutePath>{ root.join( "common" ) }, listProblem ) );
    SW_EXPECT_TRUE( listProblem.empty() );
    SW_EXPECT_TRUE( FileUtil::exists( kept ) );
    SW_EXPECT_FALSE( FileUtil::exists( stale ) );
    SW_EXPECT_TRUE( FileUtil::removeDirectory( root ) );
}

/**
 * @brief [ImportedAssetStoreTest] 공유 텍스처 원본은 폴더에 적힌 규칙으로 임포트한다 — 설정이 다르면 다른 폴더(다른 원본)이고, 패턴은 보지 않으며, 설정에서 사라진 규칙은 오류다
 */
SW_TEST_CASE( ImportedAssetStoreTest, SharedTextureSourceUsesRuleInItsFolder )
{
    TextureImportConfig config;
    SW_REQUIRE_TRUE( config.loadFromJSONString( R"({
        "rules": [ { "name": "Normal_Maps", "format": "BC5", "sRGB": false, "filter": { "includePatterns": ["*_normal.*"] } },
                   { "name": "Vrm_Character", "format": "BC3", "filter": { "includePatterns": ["**/textures_raw/avatar/**"] } } ]
    })" ) );
    const string normalPath = TextureImportConfig::makeSharedSourcePath( test::makeScratchPath( "game/x" ), "Normal_Maps", "aaaa", ".png" ).str();
    const string colorPath  = TextureImportConfig::makeSharedSourcePath( test::makeScratchPath( "game/x" ), "Vrm_Character", "aaaa", ".png" ).str();
    SW_EXPECT_TRUE( StringUtil::endsWith( normalPath, "game/x/textures_raw/_shared/normal_maps/aaaa.png" ) );
    SW_EXPECT_TRUE( normalPath != colorPath );
    SW_EXPECT_EQUAL( string( "normal_maps" ), TextureImportConfig::findSharedRuleName( normalPath ) );
    SW_EXPECT_TRUE( TextureImportConfig::findSharedRuleName( "game/x/textures_raw/avatar/a.png" ).empty() );

    const TextureImportRuleMatch normal = config.findMatchingRule( normalPath );
    SW_EXPECT_TRUE( normal._bMatched );
    SW_EXPECT_TRUE( normal._rule._format == TextureImportFormat::BC5 );
    const TextureImportRuleMatch color = config.findMatchingRule( colorPath );
    SW_EXPECT_TRUE( color._rule._format == TextureImportFormat::BC3 );
    const TextureImportRuleMatch none = config.findMatchingRule( TextureImportConfig::makeSharedSourcePath( test::makeScratchPath( "game/x" ), {}, "aaaa", ".png" ).str() );
    SW_EXPECT_FALSE( none._bMatched ); // 설정 파일이 없을 때 고른 기본 규칙
    SW_EXPECT_FALSE( none.hasError() );
    SW_EXPECT_TRUE( config.findMatchingRule( TextureImportConfig::makeSharedSourcePath( test::makeScratchPath( "game/x" ), "gone", "aaaa", ".png" ).str() ).hasError() );
}

/**
 * @brief [ImportedAssetStoreTest] 임포트 결과 글은 정체성 머리를 들지 않고(GUID 는 원본에서 나온다), 원본 폴더의 내용 주소 파일은 경로에서 나온 `.meta` 를 함께 쓴다
 */
SW_TEST_CASE( ImportedAssetStoreTest, ResultsCarryNoIdentityAndContentAddressedSourcesGetOne )
{
    const test::EditorScratchDomain domain;
    const AssetPaths&               assetPaths   = editor::getSubsystem<AssetPaths>();
    const AbsolutePath              skeletonPath = domain.makeDiskPath( "models/knight/knight.skeleton.xml" );
    SW_REQUIRE_TRUE( ImportedAssetStore::writeResultText( skeletonPath, R"(<Skeleton formatVersion="0"/>)" ) );
    string text;
    SW_REQUIRE_TRUE( FileUtil::readTextFile( skeletonPath, text ) );
    SW_EXPECT_TRUE( AssetGUIDHeader::read( text )._state == AssetGUIDHeaderState::Missing );

    const AbsolutePath  imagePath = domain.makeDiskPath( "textures_raw/_shared/albedo/00ff00ff00ff00ff.png" );
    const vector<uint8> bytes{ 1, 2, 3 };
    SW_REQUIRE_TRUE( ImportedAssetStore::writeSourceIfAbsent( assetPaths, imagePath, bytes.data(), bytes.size() ) );
    SW_REQUIRE_TRUE( FileUtil::readTextFile( AssetMeta::makePath( imagePath ), text ) );
    AssetMeta                 meta;
    const optional<AssetGUID> guid = meta.parseXML( text, "00ff00ff00ff00ff.png.meta" );
    SW_REQUIRE_TRUE( guid.has_value() );
    SW_EXPECT_TRUE( *guid == AssetGUID::fromName( assetPaths.toAssetID( imagePath ) ) );

    // 이미 있는 서술은 그대로다 — 다시 임포트해도 정체성이 바뀌지 않는다.
    const string kept = AssetMeta::makeText( "00ff00ff00ff00ff.png", AssetGUID::create() );
    SW_REQUIRE_TRUE( FileUtil::writeTextFile( AssetMeta::makePath( imagePath ), kept ) );
    SW_REQUIRE_TRUE( ImportedAssetStore::writeSourceIfAbsent( assetPaths, imagePath, bytes.data(), bytes.size() ) );
    SW_REQUIRE_TRUE( FileUtil::readTextFile( AssetMeta::makePath( imagePath ), text ) );
    SW_EXPECT_EQUAL( kept, text );
}
