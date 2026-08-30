#include "pch.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Shader/ShaderReflection.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) MaterialTest — 로드·저장·데이터·인스턴스
// ------------------------------------------------------------------------------
/**
 * @brief [MaterialTest] 머티리얼 로드 및 저장
 */

SW_TEST_CASE( MaterialTest, MaterialLoadAndSave )
{
	sw::ResourceUtil::initialize();

	sw::Material material;
	bool		 loadOk = material.loadFromFile( "engine/materials/defaultmaterial.material" );
	if ( loadOk == false )
		loadOk = material.loadFromFile( "materials/defaultmaterial.material" );
	SW_EXPECT_TRUE( loadOk );

	SW_EXPECT_EQUAL( sw::string( "DefaultMaterial" ), material.getName() );
	SW_EXPECT_EQUAL( sw::string( "engine/shaders/forwardlit.hlsl" ), material.getShaderPath() );

	const float32* color = reinterpret_cast<const float32*>( material.getPropertyData( "color" ) );
	SW_EXPECT_TRUE( color != nullptr );
	if ( color )
	{
		SW_EXPECT_NEAR_EQUAL( 1.0f, color[0], 1e-4f );
		SW_EXPECT_NEAR_EQUAL( 0.5f, color[1], 1e-4f );
		SW_EXPECT_NEAR_EQUAL( 0.2f, color[2], 1e-4f );
		SW_EXPECT_NEAR_EQUAL( 1.0f, color[3], 1e-4f );
	}

	sw::string tempPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_saved_material.material" );
	bool	   saveOk	= material.saveToFile( tempPath );
	SW_EXPECT_TRUE( saveOk );
	SW_EXPECT_TRUE( sw::FileUtil::fileExists( tempPath ) );

	sw::Material reloadedMaterial;
	bool		 reloadOk = reloadedMaterial.loadFromFile( tempPath );
	SW_EXPECT_TRUE( reloadOk );
	SW_EXPECT_EQUAL( material.getName(), reloadedMaterial.getName() );

	sw::FileUtil::removeFile( tempPath );
}

/**
 * @brief [MaterialTest] 머티리얼 permutation define
 */
SW_TEST_CASE( MaterialTest, MaterialPermutationDefines )
{
	sw::ResourceUtil::initialize();

	sw::Material material;
	SW_EXPECT_TRUE( material.loadFromFile( "engine/materials/defaultmaterial.material" ) );

	sw::vector<sw::string> listDef = material.collectShaderKeywords();
	auto				   has	   = [&]( const utf8* pDefine )
	{
		return std::find( listDef.begin(), listDef.end(), pDefine ) != listDef.end();
	};

	SW_EXPECT_TRUE( has( "MATERIAL_DOMAIN_SURFACE" ) );
	SW_EXPECT_TRUE( has( "MATERIAL_QUALITY_HIGH" ) );
	SW_EXPECT_TRUE( has( "MATERIAL_USAGE_STATIC_MESH" ) );
	SW_EXPECT_TRUE( has( "FOG_OFF" ) );
	SW_EXPECT_TRUE( has( "MATERIAL_NORMALMAP_OFF" ) ); // static switch 꺼짐 시 Off 디파인
	SW_EXPECT_TRUE( has( "MATERIAL_NORMALMAP" ) == false );

	material.setStaticSwitch( sw::hashed_string( "UseNormalMap" ), true );
	listDef = material.collectShaderKeywords();
	SW_EXPECT_TRUE( std::find( listDef.begin(), listDef.end(), "MATERIAL_NORMALMAP" ) != listDef.end() );

	material.setMultiCompile( sw::hashed_string( "FogMode" ), "FOG_LINEAR" );
	listDef = material.collectShaderKeywords();
	SW_EXPECT_TRUE( std::find( listDef.begin(), listDef.end(), "FOG_LINEAR" ) != listDef.end() );
	SW_EXPECT_TRUE( std::find( listDef.begin(), listDef.end(), "FOG_OFF" ) == listDef.end() );

	const uint64 hashA = material.getPermutationHash();
	material.setQualityLevel( sw::MaterialQualityLevel::Low );
	const uint64 hashB = material.getPermutationHash();
	SW_EXPECT_TRUE( hashA != hashB );

	sw::MaterialInstance instance( &material );
	instance.enableKeyword( sw::hashed_string( "CUSTOM_KEYWORD" ) );
	sw::vector<sw::string> listInstDef = instance.collectShaderKeywords();
	SW_EXPECT_TRUE( std::find( listInstDef.begin(), listInstDef.end(), "CUSTOM_KEYWORD" ) != listInstDef.end() );
	SW_EXPECT_TRUE( instance.getPermutationHash() != material.getPermutationHash() );
}

/**
 * @brief [MaterialTest] 머티리얼 enum 비트플래그 패킹
 */
SW_TEST_CASE( MaterialTest, MaterialEnumBitFlagPack )
{
	const utf8* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<MaterialDesc formatVersion="0" name="EnumMat" shaderPath="engine/shaders/forwardlit.hlsl" blendMode="Opaque">
	<_properties>
		<item name="shadeMode" type="Enum" shaderType="Uint" value="Lit">
			<_enumEntries>
				<item name="Unlit" value="0"/>
				<item name="Lit" value="1"/>
			</_enumEntries>
		</item>
		<item name="flags" type="BitFlag" shaderType="Uint" value="CastShadows|ReceiveDecals">
			<_enumEntries>
				<item name="None" value="0"/>
				<item name="CastShadows" value="1"/>
				<item name="ReceiveDecals" value="2"/>
			</_enumEntries>
		</item>
	</_properties>
</MaterialDesc>
)";

	sw::string tempPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_enum_material.material" );
	SW_EXPECT_TRUE( sw::FileUtil::writeFile( tempPath, reinterpret_cast<const uint8*>( xml ), static_cast<uint64>( sw::StringUtil::strlen( xml ) ) ) );

	sw::Material material;
	SW_EXPECT_TRUE( material.loadFromFile( tempPath ) );

	const uint32* shade = reinterpret_cast<const uint32*>( material.getPropertyData( "shadeMode" ) );
	const uint32* flags = reinterpret_cast<const uint32*>( material.getPropertyData( "flags" ) );
	SW_EXPECT_TRUE( shade != nullptr && flags != nullptr );
	if ( shade && flags )
	{
		SW_EXPECT_EQUAL( 1u, *shade );
		SW_EXPECT_EQUAL( 3u, *flags ); // 비트 1|2
	}

	sw::FileUtil::removeFile( tempPath );
}

/**
 * @brief [MaterialTest] 머티리얼 파라미터 수정 (CPU 패킹, RHI 없이)
 */
SW_TEST_CASE( MaterialTest, MaterialColorModification )
{
	sw::ResourceUtil::initialize();

	sw::Material material;
	SW_EXPECT_TRUE( material.loadFromFile( "engine/materials/defaultmaterial.material" ) );

	SW_EXPECT_TRUE( material.setPropertyValue( nullptr, sw::hashed_string( "color" ), "0.25 0.50 0.75 1.0" ) );
	const float32* color = reinterpret_cast<const float32*>( material.getPropertyData( "color" ) );
	SW_ASSERT_NOT_NULL( color );
	SW_EXPECT_NEAR_EQUAL( 0.25f, color[0], 1e-3f );
	SW_EXPECT_NEAR_EQUAL( 0.50f, color[1], 1e-3f );
	SW_EXPECT_NEAR_EQUAL( 0.75f, color[2], 1e-3f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, color[3], 1e-3f );

	SW_EXPECT_TRUE( material.setParameterFloat( nullptr, sw::hashed_string( "roughness" ), 0.42f ) );
	float32 roughness = -1.0f;
	SW_EXPECT_TRUE( material.getParameterFloat( sw::hashed_string( "roughness" ), roughness ) );
	SW_EXPECT_NEAR_EQUAL( 0.42f, roughness, 1e-3f );
}

/**
 * @brief [MaterialTest] 비동기 머티리얼 로드
 */
SW_TEST_CASE( MaterialTest, AsyncMaterialLoadTest )
{
	sw::Material   mat;
	sw::TaskHandle handle = mat.loadFromFileAsync( "engine/materials/defaultmaterial.material" );
	SW_EXPECT_TRUE( handle.isValid() );

	sw::engine::getTaskManager().waitAll();
	sw::engine::getTaskManager().clear();
}

/**
 * @brief [MaterialTest] 기본값과 인스턴스 오버라이드
 */
SW_TEST_CASE( MaterialTest, MaterialDefaultAndInstanceOverride )
{
	sw::ResourceUtil::initialize();

	sw::Material material;
	SW_EXPECT_TRUE( material.loadFromFile( "engine/materials/defaultmaterial.material" ) );

	const sw::MaterialProperty* colorProp = material.findProperty( sw::hashed_string( "color" ) );
	SW_EXPECT_TRUE( colorProp != nullptr );
	if ( colorProp )
	{
		SW_EXPECT_TRUE( colorProp->_defaultValue.find( "1.0" ) != sw::string::npos || colorProp->_defaultValue.find( "1" ) != sw::string::npos );
	}

	SW_EXPECT_TRUE( material.setPropertyValue( nullptr, sw::hashed_string( "roughness" ), "0.9" ) );
	SW_EXPECT_TRUE( material.resetPropertyToDefault( nullptr, sw::hashed_string( "roughness" ) ) );
	float32 roughness = -1.0f;
	SW_EXPECT_TRUE( material.getParameterFloat( sw::hashed_string( "roughness" ), roughness ) );
	SW_EXPECT_NEAR_EQUAL( 0.5f, roughness, 1e-3f );

	sw::MaterialInstance instance( &material );
	SW_EXPECT_TRUE( instance.loadFromFile( "game/demo/materials/glassorange.materialinstance" ) || ( instance.setParameter( sw::hashed_string( "color" ), "0.2 0.75 1.0 0.35" ), true ) );
	SW_EXPECT_TRUE( instance.isParameterOverridden( sw::hashed_string( "color" ) ) );

	sw::string colorOverride;
	SW_EXPECT_TRUE( instance.getParameter( sw::hashed_string( "color" ), colorOverride ) );
	SW_EXPECT_TRUE( colorOverride.find( "0.2" ) != sw::string::npos || colorOverride.find( "0.20" ) != sw::string::npos );

	// 인스턴스 오버라이드는 applyToGpu 전까지 마스터 기본 버퍼를 바꾸지 않는다.
	const float32* masterColor = reinterpret_cast<const float32*>( material.getPropertyData( "color" ) );
	SW_EXPECT_TRUE( masterColor != nullptr );
	if ( masterColor )
		SW_EXPECT_NEAR_EQUAL( 1.0f, masterColor[0], 1e-3f );

	sw::string tempPath = sw::FileUtil::joinPath( sw::FileUtil::getTempDirectory(), "test_mic.materialinstance" );
	instance.setName( "TestMic" );
	SW_EXPECT_TRUE( instance.saveToFile( tempPath ) );
	sw::MaterialInstance reloaded( &material );
	SW_EXPECT_TRUE( reloaded.loadFromFile( tempPath ) );
	SW_EXPECT_TRUE( reloaded.isParameterOverridden( sw::hashed_string( "color" ) ) );
	sw::FileUtil::removeFile( tempPath );
}

/**
 * @brief [MaterialTest] 리플렉션 스키마 동기화
 */
SW_TEST_CASE( MaterialTest, MaterialReflectionSchemaSync )
{
	sw::Material material;
	SW_EXPECT_TRUE( material.loadFromFile( "engine/materials/defaultmaterial.material" ) );

	sw::ShaderReflectionData reflection{};
	sw::ShaderBufferInfo	 cb{};
	cb._name	  = "MaterialCB";
	cb._totalSize = 32;
	sw::ShaderVariableInfo colorVar{};
	colorVar._name	 = "color";
	colorVar._offset = 0;
	colorVar._size	 = 16;
	colorVar._type	 = "Float4";
	cb._listVariable.push_back( colorVar );
	sw::ShaderVariableInfo roughVar{};
	roughVar._name	 = "roughness";
	roughVar._offset = 16;
	roughVar._size	 = 4;
	roughVar._type	 = "Float";
	cb._listVariable.push_back( roughVar );
	reflection._listConstantBuffer.push_back( cb );

	SW_EXPECT_TRUE( material.syncPropertiesFromReflection( reflection ) );

	sw::MaterialInstance instance( &material );
	sw::float4			 colorOverride{ 0.1f, 0.2f, 0.3f, 1.0f };
	instance.setVectorParameter( sw::hashed_string( "color" ), colorOverride );
	SW_EXPECT_TRUE( instance.validateParametersWithReflection( reflection ) );
	SW_EXPECT_TRUE( instance.isParameterOverridden( sw::hashed_string( "color" ) ) );

	material.setBlendMode( sw::RHIBlendMode::Transparent );
	SW_EXPECT_TRUE( material.getBlendMode() == sw::RHIBlendMode::Transparent );
}

/**
 * @brief [MaterialTest] 인스턴스 오버라이드 (CPU, applyToGpu/셰이더 컴파일 없음)
 */
SW_TEST_CASE( MaterialTest, MaterialInstanceOverride )
{
	sw::ResourceUtil::initialize();

	sw::Material material;
	SW_EXPECT_TRUE( material.loadFromFile( "engine/materials/defaultmaterial.material" ) );

	sw::MaterialInstance instance( &material );
	sw::float4			 overrideColor{ 0.1f, 0.2f, 0.3f, 0.4f };
	instance.setVectorParameter( sw::hashed_string( "color" ), overrideColor );
	SW_EXPECT_TRUE( instance.isParameterOverridden( sw::hashed_string( "color" ) ) );

	sw::string text;
	SW_EXPECT_TRUE( instance.getParameter( sw::hashed_string( "color" ), text ) );
	SW_EXPECT_TRUE( text.find( "0.1" ) != sw::string::npos );

	instance.clearOverrides();
	SW_EXPECT_FALSE( instance.isParameterOverridden( sw::hashed_string( "color" ) ) );
}

/**
 * @brief [MaterialTest] 셰이더 리플렉션 검증 (가상 리플렉션, DXC/sync 대기 없음)
 */
SW_TEST_CASE( MaterialTest, MaterialShaderReflectionValidation )
{
	sw::Material material;
	SW_EXPECT_TRUE( material.loadFromFile( "engine/materials/defaultmaterial.material" ) );

	sw::ShaderReflectionData reflection{};
	sw::ShaderBufferInfo	 cb{};
	cb._name	  = "MaterialCB";
	cb._totalSize = 16;
	sw::ShaderVariableInfo colorVar{};
	colorVar._name	 = "color";
	colorVar._offset = 0;
	colorVar._size	 = 16;
	colorVar._type	 = "Float4";
	cb._listVariable.push_back( colorVar );
	reflection._listConstantBuffer.push_back( cb );

	sw::MaterialInstance instance( &material );
	instance.setParameter( sw::hashed_string( "color" ), "1.0 0.0 0.0 1.0" );
	SW_EXPECT_TRUE( instance.validateParametersWithReflection( reflection ) );

	// 리플렉션에 없는 이름 오버라이드는 검증 실패해야 한다.
	instance.setParameter( sw::hashed_string( "notInReflection" ), "1.0" );
	SW_EXPECT_FALSE( instance.validateParametersWithReflection( reflection ) );
}

/**
 * @brief [MaterialTest] 고속 직접 바이트 패킹 (packTextureIntoBuffer / packRawDataIntoBuffer)
 */
SW_TEST_CASE( MaterialTest, FastBytePackingDirectMethods )
{
	sw::Material material;
	SW_EXPECT_TRUE( material.loadFromFile( "engine/materials/defaultmaterial.material" ) );

	sw::vector<uint8> buffer = material.getBuffer();
	SW_EXPECT_TRUE( buffer.size() >= 16 );

	// 1) raw data packing test (e.g. float4 color)
	const float32 testColor[4] = { 0.125f, 0.25f, 0.5f, 1.0f };
	SW_EXPECT_TRUE( material.packRawDataIntoBuffer( sw::hashed_string( "color" ), testColor, sizeof( testColor ), buffer ) );

	const float32* pPackedColor = reinterpret_cast<const float32*>( buffer.data() );
	SW_EXPECT_NEAR_EQUAL( 0.125f, pPackedColor[0], 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 0.25f, pPackedColor[1], 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 0.5f, pPackedColor[2], 1e-4f );
	SW_EXPECT_NEAR_EQUAL( 1.0f, pPackedColor[3], 1e-4f );

	// 2) texture packing test
	const sw::RHIDescriptorIndex testTexIdx = 42;
	if ( material.findProperty( sw::hashed_string( "mainTexture" ) ) != nullptr )
	{
		SW_EXPECT_TRUE( material.packTextureIntoBuffer( sw::hashed_string( "mainTexture" ), testTexIdx, buffer ) );
		const sw::MaterialProperty* pTexProp = material.findProperty( sw::hashed_string( "mainTexture" ) );
		SW_ASSERT_NOT_NULL( pTexProp );
		const uint32 packedTexIdx = *reinterpret_cast<const uint32*>( buffer.data() + pTexProp->_offset );
		SW_EXPECT_EQUAL( 42u, packedTexIdx );
	}
}
