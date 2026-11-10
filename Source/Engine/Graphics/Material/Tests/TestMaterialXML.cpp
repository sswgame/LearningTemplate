#include "pch.h"

#include "Core/File/FileUtil.h"
#include "Core/File/ProcessRoots.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/Tests/ScopedScratchDomain.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialTypes.h"
#include "Engine/Graphics/Material/MaterialXML.h"
#include "Engine/Serialization/XML/XMLDocument.h"

// MaterialXMLTest — 저장소의 실제 .material 을 읽어 프로퍼티 깃발(bSrgb · bHdr)이 데이터대로인지(nogpu).
// 약어 일괄 변경(1dafbae0c9)이 읽기 철자를 bSRGB · bHDR 로 바꿔 데이터의 bSrgb="0" 이 조용히 기본값(true)이 되었다 — 스플랫 · 노멀 맵이 sRGB 로 읽혔다.

namespace
{
    bool findProperty( const sw::string& relativePath, const utf8* pName, sw::MaterialProperty& outProp )
    {
        sw::string text;
        if ( sw::FileUtil::readTextFile( sw::ProcessRoots::get()._project.str() + "/" + relativePath, text ) == false )
            return false;
        sw::XMLDocument doc;
        if ( doc.parse( text ) == false )
            return false;
        for ( sw::XMLNode item = doc.getRoot( "MaterialDef" ).findChild( "_properties" ).findChild( "item" ); item; item = item.findNextSibling( "item" ) )
        {
            sw::MaterialProperty prop{};
            if ( sw::MaterialXML::parsePropertyNode( item, prop ) == false )
                return false;
            if ( prop._name == pName )
            {
                outProp = prop;
                return true;
            }
        }
        return false;
    }
} // namespace

/**
 * @brief [MaterialXMLTest] 실제 머티리얼의 bSrgb="0" · bHdr="1" 이 그대로 읽힌다
 */
SW_TEST_CASE( MaterialXMLTest, ReadsSrgbAndHDRFlagsOfRealMaterials )
{
    sw::MaterialProperty splat{};
    SW_REQUIRE_TRUE( findProperty( "Content/engine/materials/terrain.material", "_splatMap", splat ) );
    SW_EXPECT_TRUE( splat._bSRGB == 0 );

    sw::MaterialProperty emissive{};
    SW_REQUIRE_TRUE( findProperty( "Content/engine/materials/toon.material", "_emissiveColor", emissive ) );
    SW_EXPECT_TRUE( emissive._bHDR == 1 );
    SW_EXPECT_TRUE( emissive._bSRGB == 1 );
}

/**
 * @brief [MaterialXMLTest] 모르는 속성(철자가 다른 bSRGB)은 기본값이 되지 않고 읽기 실패다
 */
SW_TEST_CASE( MaterialXMLTest, RejectsMisspelledPropertyAttribute )
{
    sw::XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( "<item name=\"_albedoMap\" type=\"Texture2D\" bSRGB=\"0\"/>" ) );
    sw::MaterialProperty prop{};
    SW_EXPECT_LOG( "XMLFields.UnknownField" );
    SW_EXPECT_FALSE( sw::MaterialXML::parsePropertyNode( doc.getRoot( "item" ), prop ) );
}

/**
 * @brief [MaterialXMLTest] 칸 형식(`shaderType`)은 파일에 적지 않는다 — 셰이더 리플렉션이 정하므로 적으면 읽기 실패이고, 읽기 전에는 CPU 형식의 기본 형식이다
 */
SW_TEST_CASE( MaterialXMLTest, ShaderTypeIsNotAFileField )
{
    sw::XMLDocument written;
    SW_REQUIRE_TRUE( written.parse( "<item name=\"_color\" type=\"Color\" shaderType=\"Float4\" defaultValue=\"1 1 1 1\"/>" ) );
    sw::MaterialProperty prop{};
    SW_EXPECT_LOG( "XMLFields.UnknownField" );
    SW_EXPECT_FALSE( sw::MaterialXML::parsePropertyNode( written.getRoot( "item" ), prop ) );

    sw::XMLDocument plain;
    SW_REQUIRE_TRUE( plain.parse( "<item name=\"_color\" type=\"Color\" defaultValue=\"1 1 1 1\"/>" ) );
    SW_REQUIRE_TRUE( sw::MaterialXML::parsePropertyNode( plain.getRoot( "item" ), prop ) );
    SW_EXPECT_TRUE( prop._shaderType == sw::MaterialPropertyType::Unknown ); // 패킹 · 리플렉션이 채운다(`defaultShaderTypeFor` · `matchPropertiesToSchema`)
}

/**
 * @brief [MaterialXMLTest] 현재 판(formatVersion=0)의 XML 을 리소스 id 로 읽는다 — 이름 · 셰이더 경로가 그대로다
 */
SW_TEST_CASE( MaterialXMLTest, AssetFormatAcceptsCurrentMaterialXML )
{
    const sw::string shaderPath = sw::engine::getSubsystem<sw::EngineDefaultAssets>()._shaderForwardLit.get();
    const sw::string current    = R"(<?xml version="1.0" encoding="utf-8"?>
<MaterialDef formatVersion="0" name="CurrentMat" shaderPath=")" +
                               shaderPath +
                               R"(" alphaMode="Opaque">
	<_properties>
		<item name="_color" type="Color" defaultValue="1 0 0 1"/>
	</_properties>
</MaterialDef>
)";

    sw::XMLDocument doc;
    SW_REQUIRE_TRUE( doc.parse( current ) );
    const sw::XMLNode root = doc.getRoot( "MaterialDef" );
    SW_REQUIRE_TRUE( root.isValid() );
    SW_EXPECT_TRUE( sw::AssetXMLFormat::upgradeToCurrentXMLVersion( sw::Material::kAssetType, root ) );
    SW_EXPECT_EQUAL( sw::Material::kAssetType._formatVersion, sw::AssetXMLFormat::readXMLVersion( root ) );

    const sw::test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( domain.makeDiskPath( "current.material" ), current ) );
    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_EXPECT_TRUE( material->loadFromAsset( sw::test::ScopedScratchDomain::makeID( "current.material" ) ) );
    SW_EXPECT_EQUAL( sw::string( "CurrentMat" ), material->getName() );
    SW_EXPECT_EQUAL( shaderPath, material->getShaderPath() );
}

namespace
{
    // 부모 상속 테스트의 부모 — 프로퍼티 둘(색 · 거칠기)과 퍼뮤테이션 하나.
    constexpr const utf8* kParentMaterial = R"(<?xml version="1.0" encoding="utf-8"?>
<MaterialDef formatVersion="0" name="ParentMat" shaderPath="engine/shaders/forward_lit.hlsl" alphaMode="Opaque">
	<_properties>
		<item name="_color" type="Color" defaultValue="1 1 1 1" displayName="Base Color" group="Surface" bSrgb="1" />
		<item name="_roughness" type="Range" defaultValue="0.5" min="0.0" max="1.0" group="Surface" />
	</_properties>
	<_permutations quality="High" shaderLOD="300" usage="StaticMesh">
		<_alwaysDefines>
			<item>MATERIAL_DOMAIN_SURFACE</item>
		</_alwaysDefines>
	</_permutations>
</MaterialDef>
)";
} // namespace

/**
 * @brief [MaterialXMLTest] 부모를 가리키는 머티리얼은 부모 칸 위에 자식이 적은 값만 덮는다 — 셰이더 · 다른 프로퍼티 · 퍼뮤테이션은 부모 것이다(언리얼 Material Instance)
 */
SW_TEST_CASE( MaterialXMLTest, ChildInheritsParentAndOverridesOnlyWrittenValues )
{
    const sw::test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( domain.makeDiskPath( "parent.material" ), kParentMaterial ) );
    const sw::string child = R"(<?xml version="1.0" encoding="utf-8"?>
<MaterialDef formatVersion="0" name="ChildMat" _base=")" +
                             sw::test::ScopedScratchDomain::makeID( "parent.material" ).str() +
                             R"(">
	<_properties>
		<item name="_color" defaultValue="0.2 0.4 0.6 1" />
	</_properties>
</MaterialDef>
)";
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( domain.makeDiskPath( "child.material" ), child ) );

    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_REQUIRE_TRUE( material->loadFromAsset( sw::test::ScopedScratchDomain::makeID( "child.material" ) ) );
    SW_EXPECT_EQUAL( sw::string( "ChildMat" ), material->getName() );
    SW_EXPECT_EQUAL( sw::string( "engine/shaders/forward_lit.hlsl" ), material->getShaderPath() );
    const sw::MaterialProperty* pColor     = material->findProperty( sw::hashed_string( "_color" ) );
    const sw::MaterialProperty* pRoughness = material->findProperty( sw::hashed_string( "_roughness" ) );
    SW_REQUIRE_TRUE( pColor != nullptr && pRoughness != nullptr );
    SW_EXPECT_EQUAL( sw::string( "0.2 0.4 0.6 1" ), pColor->_defaultValue );
    SW_EXPECT_EQUAL( sw::string( "Base Color" ), pColor->_displayName ); // 자식이 적지 않은 속성은 부모 것
    SW_EXPECT_TRUE( pColor->_bSRGB == 1 );
    SW_EXPECT_EQUAL( sw::string( "0.5" ), pRoughness->_defaultValue );
    SW_REQUIRE_EQUAL( size_t{ 1 }, material->getPermutations()._listAlwaysDefine.size() );
    SW_EXPECT_EQUAL( sw::string( "MATERIAL_DOMAIN_SURFACE" ), material->getPermutations()._listAlwaysDefine[0] );

    // 저장은 부모와 다른 칸만 적는다 — 다시 읽으면 같은 값이다.
    const sw::string saved = material->saveToString();
    SW_EXPECT_TRUE( saved.find( "_base=" ) != sw::string::npos );
    SW_EXPECT_TRUE( saved.find( "_roughness" ) == sw::string::npos );
    SW_EXPECT_TRUE( saved.find( "_permutations" ) == sw::string::npos );
    sw::shared_ptr<sw::Material> reread = sw::Material::create();
    SW_REQUIRE_TRUE( reread->loadFromXML( saved ) );
    const sw::MaterialProperty* pRereadColor = reread->findProperty( sw::hashed_string( "_color" ) );
    SW_REQUIRE_TRUE( pRereadColor != nullptr );
    SW_EXPECT_EQUAL( sw::string( "0.2 0.4 0.6 1" ), pRereadColor->_defaultValue );
}

/**
 * @brief [MaterialXMLTest] 없는 부모 · 돌고 도는 부모 사슬은 자식 칸만으로 읽지 않고 로드 실패다
 */
SW_TEST_CASE( MaterialXMLTest, MissingOrCyclicParentFailsToLoad )
{
    const sw::test::ScopedScratchDomain domain;
    const sw::string                    idA = sw::test::ScopedScratchDomain::makeID( "a.material" ).str();
    const sw::string                    idB = sw::test::ScopedScratchDomain::makeID( "b.material" ).str();
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( domain.makeDiskPath( "a.material" ), "<MaterialDef formatVersion=\"0\" name=\"A\" _base=\"" + idB + "\"/>" ) );
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( domain.makeDiskPath( "b.material" ), "<MaterialDef formatVersion=\"0\" name=\"B\" _base=\"" + idA + "\"/>" ) );
    sw::shared_ptr<sw::Material> cyclic = sw::Material::create();
    SW_EXPECT_LOG( "DataInheritance.Cycle" );
    SW_EXPECT_FALSE( cyclic->loadFromAsset( sw::test::ScopedScratchDomain::makeID( "a.material" ) ) );

    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( domain.makeDiskPath( "orphan.material" ),
                                                  "<MaterialDef formatVersion=\"0\" name=\"Orphan\" _base=\"" + sw::test::ScopedScratchDomain::makeID( "missing.material" ).str() + "\"/>" ) );
    sw::shared_ptr<sw::Material> orphan = sw::Material::create();
    SW_EXPECT_LOG( "DataInheritance.BaseNotFound" );
    SW_EXPECT_FALSE( orphan->loadFromAsset( sw::test::ScopedScratchDomain::makeID( "orphan.material" ) ) );
}

/**
 * @brief [MaterialXMLTest] formatVersion 없는 옛 Material 루트는 자동 변환하지 않고 거부한다
 */
SW_TEST_CASE( MaterialXMLTest, AssetFormatRejectsLegacyMaterialXML )
{
    const sw::test::ScopedScratchDomain domain;
    SW_REQUIRE_TRUE( sw::FileUtil::writeTextFile( domain.makeDiskPath( "legacy.material" ), R"(<?xml version="1.0" encoding="utf-8"?>
<Material>
	<_name>LegacyMat</_name>
	<_shader>engine/shaders/forward_lit.hlsl</_shader>
</Material>
)" ) );
    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_EXPECT_FALSE( material->loadFromAsset( sw::test::ScopedScratchDomain::makeID( "legacy.material" ) ) );
}
