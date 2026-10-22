#include "pch.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Memory/Memory.h"
#include "Core/Task/TaskManager.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Material/MaterialUtil.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflection.h"
#include "Engine/Resource/ResourcePaths.h"

#include "TestFramework/Harness/TestHostRuntime.h"
#include "TestFramework/TestEnvironment.h"
#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// 1) MaterialTest — 로드·저장·데이터·인스턴스
// ------------------------------------------------------------------------------
/**
 * @brief [MaterialTest] 머티리얼 로드 및 저장
 */
SW_TEST_CASE( MaterialTest, MaterialLoadAndSave )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    sw::shared_ptr<sw::Material> material = sw::Material::create();
    bool                         loadOk   = material->loadFromFile( ::test::getEngineDefaultAssets()._defaultMaterial.c_str() );
    if ( loadOk == false )
        loadOk = material->loadFromFile( "materials/defaultmaterial.material" );
    SW_EXPECT_TRUE( loadOk );

    SW_EXPECT_EQUAL( sw::string( "DefaultMaterial" ), material->getName() );
    SW_EXPECT_EQUAL( sw::string( test::getEngineDefaultAssets()._shaderForwardLit.get() ), material->getShaderPath() );

    const float32* color = reinterpret_cast<const float32*>( material->getParameterData( "color" ) );
    SW_EXPECT_TRUE( color != nullptr );
    if ( color )
    {
        // 흰색이어야 한다. 셰이더가 정점 색에 이 값을 실제로 곱하므로 폴백 머티리얼이 색을 입히면 안 된다.
        SW_EXPECT_NEAR_EQUAL( 1.0f, color[0], 1e-4f );
        SW_EXPECT_NEAR_EQUAL( 1.0f, color[1], 1e-4f );
        SW_EXPECT_NEAR_EQUAL( 1.0f, color[2], 1e-4f );
        SW_EXPECT_NEAR_EQUAL( 1.0f, color[3], 1e-4f );
    }

    sw::string tempPath = test::makeTempPath( "test_saved_material.material" );
    bool       saveOk   = material->saveToFile( tempPath );
    SW_EXPECT_TRUE( saveOk );
    SW_EXPECT_TRUE( sw::FileUtil::exists( tempPath ) );

    sw::shared_ptr<sw::Material> reloadedMaterial = sw::Material::create();
    bool                         reloadOk         = reloadedMaterial->loadFromFile( ::test::makeTempResourceID( tempPath ) );
    SW_EXPECT_TRUE( reloadOk );
    SW_EXPECT_EQUAL( material->getName(), reloadedMaterial->getName() );
}

/**
 * @brief [MaterialTest] 사용 define 은 열거자 이름에서 만든다 — 낱말마다 밑줄(PostProcess → POST_PROCESS), 약어는 한 낱말(UI)
 */
SW_TEST_CASE( MaterialTest, UsageDefinesComeFromEnumeratorNames )
{
    sw::vector<sw::string> listDefine;
    sw::MaterialUtil::appendUsageDefines( sw::MaterialUsageFlags::PostProcess | sw::MaterialUsageFlags::UI | sw::MaterialUsageFlags::SkeletalMesh, listDefine );
    SW_ASSERT_EQUAL( listDefine.size(), size_t{ 3 } );
    SW_EXPECT_STREQ( listDefine[0].c_str(), "MATERIAL_USAGE_SKELETAL_MESH" );
    SW_EXPECT_STREQ( listDefine[1].c_str(), "MATERIAL_USAGE_UI" );
    SW_EXPECT_STREQ( listDefine[2].c_str(), "MATERIAL_USAGE_POST_PROCESS" );
}

/**
 * @brief [MaterialTest] 머티리얼 permutation define
 */
SW_TEST_CASE( MaterialTest, MaterialPermutationDefines )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_EXPECT_TRUE( material->loadFromFile( ::test::getEngineDefaultAssets()._defaultMaterial.c_str() ) );

    sw::vector<sw::string> listDef = material->getCachedShaderDefines();
    auto                   has     = [&]( const utf8* pDefine )
    {
        return std::find( listDef.begin(), listDef.end(), pDefine ) != listDef.end();
    };

    SW_EXPECT_TRUE( has( "MATERIAL_DOMAIN_SURFACE" ) );
    SW_EXPECT_TRUE( has( "MATERIAL_QUALITY_HIGH" ) );
    SW_EXPECT_TRUE( has( "MATERIAL_USAGE_STATIC_MESH" ) );
    SW_EXPECT_TRUE( has( "FOG_OFF" ) );

    material->setStaticSwitch( sw::hashed_string( "MATERIAL_TEST_SWITCH" ), true );
    listDef = material->getCachedShaderDefines();
    SW_EXPECT_TRUE( std::find( listDef.begin(), listDef.end(), "MATERIAL_TEST_SWITCH" ) != listDef.end() );

    material->setMultiCompile( sw::hashed_string( "FogMode" ), "FOG_LINEAR" );
    listDef = material->getCachedShaderDefines();
    SW_EXPECT_TRUE( std::find( listDef.begin(), listDef.end(), "FOG_LINEAR" ) != listDef.end() );
    SW_EXPECT_TRUE( std::find( listDef.begin(), listDef.end(), "FOG_OFF" ) == listDef.end() );

    const uint64 hashA = material->getPermutationHash();
    material->setQualityLevel( sw::MaterialQualityLevel::Low );
    const uint64 hashB = material->getPermutationHash();
    SW_EXPECT_TRUE( hashA != hashB );

    sw::shared_ptr<sw::MaterialInstance> instance = sw::MaterialInstance::create( material.get() );
    instance->enableKeyword( sw::hashed_string( "CUSTOM_KEYWORD" ) );
    sw::vector<sw::string> listInstDef = instance->getCachedShaderDefines();
    SW_EXPECT_TRUE( std::find( listInstDef.begin(), listInstDef.end(), "CUSTOM_KEYWORD" ) != listInstDef.end() );
    SW_EXPECT_TRUE( instance->getPermutationHash() != material->getPermutationHash() );
}

/**
 * @brief [MaterialTest] 꺼진 정적 스위치는 `keywordOff` 를, 켜면 `keyword` 를 define 으로 낸다
 * @details 지금 이 경로를 쓰는 에셋이 없다 — 안 쓰이는 경로가 조용히 썩지 않게 테스트가 든다.
 */
SW_TEST_CASE( MaterialTest, StaticSwitchOffKeywordWhenDisabled )
{
    const utf8*      xml      = R"(<?xml version="1.0" encoding="utf-8"?>
<MaterialDesc formatVersion="0" name="SwitchMat" shaderPath="engine/shaders/forwardlit.hlsl" blendMode="Opaque">
	<_permutations quality="High" shaderLOD="300" usage="StaticMesh">
		<_staticSwitches>
			<item name="UseDetail" keyword="MATERIAL_DETAIL" keywordOff="MATERIAL_DETAIL_OFF" bEnabled="0" bShaderFeature="1"/>
		</_staticSwitches>
	</_permutations>
</MaterialDesc>
)";
    const sw::string tempPath = test::makeTempPath( "test_switch_material.material" );
    SW_ASSERT_TRUE( sw::FileUtil::writeFile( tempPath, reinterpret_cast<const uint8*>( xml ), static_cast<uint64>( sw::StringUtil::strlen( xml ) ) ) );

    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_ASSERT_TRUE( material->loadFromFile( ::test::makeTempResourceID( tempPath ) ) );

    auto has = [&material]( const utf8* pDefine )
    {
        const sw::vector<sw::string>& listDefine = material->getCachedShaderDefines();
        return std::find( listDefine.begin(), listDefine.end(), pDefine ) != listDefine.end();
    };
    SW_EXPECT_TRUE( has( "MATERIAL_DETAIL_OFF" ) );
    SW_EXPECT_FALSE( has( "MATERIAL_DETAIL" ) );

    material->setStaticSwitch( sw::hashed_string( "UseDetail" ), true );
    SW_EXPECT_TRUE( has( "MATERIAL_DETAIL" ) );
    SW_EXPECT_FALSE( has( "MATERIAL_DETAIL_OFF" ) );
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

    sw::string tempPath = test::makeTempPath( "test_enum_material.material" );
    SW_EXPECT_TRUE( sw::FileUtil::writeFile( tempPath, reinterpret_cast<const uint8*>( xml ), static_cast<uint64>( sw::StringUtil::strlen( xml ) ) ) );

    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_EXPECT_TRUE( material->loadFromFile( ::test::makeTempResourceID( tempPath ) ) );

    const uint32* shade = reinterpret_cast<const uint32*>( material->getParameterData( "shadeMode" ) );
    const uint32* flags = reinterpret_cast<const uint32*>( material->getParameterData( "flags" ) );
    SW_EXPECT_TRUE( shade != nullptr && flags != nullptr );
    if ( shade && flags )
    {
        SW_EXPECT_EQUAL( 1u, *shade );
        SW_EXPECT_EQUAL( 3u, *flags ); // 비트 1|2
    }
}

/**
 * @brief [MaterialTest] 머티리얼 파라미터 수정 (CPU 패킹, RHI 없이)
 */
SW_TEST_CASE( MaterialTest, MaterialColorModification )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_EXPECT_TRUE( material->loadFromFile( ::test::getEngineDefaultAssets()._defaultMaterial.c_str() ) );

    SW_EXPECT_TRUE( material->setParameter( nullptr, sw::hashed_string( "color" ), "0.25 0.50 0.75 1.0" ) );
    const float32* color = reinterpret_cast<const float32*>( material->getParameterData( "color" ) );
    SW_ASSERT_NOT_NULL( color );
    SW_EXPECT_NEAR_EQUAL( 0.25f, color[0], 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.50f, color[1], 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, color[2], 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, color[3], 1e-3f );

    SW_EXPECT_TRUE( material->setScalarParameter( nullptr, sw::hashed_string( "roughness" ), 0.42f ) );
    float32 roughness = -1.0f;
    SW_EXPECT_TRUE( material->getScalarParameter( sw::hashed_string( "roughness" ), roughness ) );
    SW_EXPECT_NEAR_EQUAL( 0.42f, roughness, 1e-3f );
}

/**
 * @brief [MaterialTest] 비동기 머티리얼 로드
 */
SW_TEST_CASE( MaterialTest, AsyncMaterialLoadTest )
{
    sw::shared_ptr<sw::Material> mat    = sw::Material::create();
    sw::TaskHandle               handle = mat->loadFromFileAsync( ::test::getEngineDefaultAssets()._defaultMaterial.c_str() );
    SW_EXPECT_TRUE( handle.isValid() );

    sw::engine::getSubsystem<sw::TaskManager>().waitAll();
    sw::engine::getSubsystem<sw::TaskManager>().clear();
}

/**
 * @brief [MaterialTest] 기본값과 인스턴스 오버라이드
 */
SW_TEST_CASE( MaterialTest, MaterialDefaultAndInstanceOverride )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_EXPECT_TRUE( material->loadFromFile( ::test::getEngineDefaultAssets()._defaultMaterial.c_str() ) );

    const sw::MaterialProperty* colorProp = material->findProperty( sw::hashed_string( "color" ) );
    SW_EXPECT_TRUE( colorProp != nullptr );
    if ( colorProp )
        SW_EXPECT_TRUE( colorProp->_defaultValue.find( "1.0" ) != sw::string::npos || colorProp->_defaultValue.find( "1" ) != sw::string::npos );

    SW_EXPECT_TRUE( material->setParameter( nullptr, sw::hashed_string( "roughness" ), "0.9" ) );
    SW_EXPECT_TRUE( material->resetParameterToDefault( nullptr, sw::hashed_string( "roughness" ) ) );
    float32 roughness = -1.0f;
    SW_EXPECT_TRUE( material->getScalarParameter( sw::hashed_string( "roughness" ), roughness ) );
    SW_EXPECT_NEAR_EQUAL( 0.5f, roughness, 1e-3f );

    sw::shared_ptr<sw::MaterialInstance> instance = sw::MaterialInstance::create( material.get() );
    instance->setParameter( sw::hashed_string( "color" ), "0.2 0.75 1.0 0.35" );
    SW_EXPECT_TRUE( instance->isParameterOverridden( sw::hashed_string( "color" ) ) );

    sw::string colorOverride;
    SW_EXPECT_TRUE( instance->getParameter( sw::hashed_string( "color" ), colorOverride ) );
    SW_EXPECT_TRUE( colorOverride.find( "0.2" ) != sw::string::npos || colorOverride.find( "0.20" ) != sw::string::npos );

    // 인스턴스 오버라이드는 updateRHI 전까지 마스터 기본 버퍼를 바꾸지 않는다.
    const float32* masterColor = reinterpret_cast<const float32*>( material->getParameterData( "color" ) );
    SW_EXPECT_TRUE( masterColor != nullptr );
    if ( masterColor )
        SW_EXPECT_NEAR_EQUAL( 1.0f, masterColor[0], 1e-3f );

    sw::string tempPath = test::makeTempPath( "test_mic.materialinstance" );
    instance->setName( "TestMic" );
    SW_EXPECT_TRUE( instance->saveToFile( tempPath ) );
    sw::shared_ptr<sw::MaterialInstance> reloaded = sw::MaterialInstance::create( material.get() );
    SW_EXPECT_TRUE( reloaded->loadFromFile( ::test::makeTempResourceID( tempPath ) ) );
    SW_EXPECT_TRUE( reloaded->isParameterOverridden( sw::hashed_string( "color" ) ) );
}

/**
 * @brief [MaterialTest] 리플렉션 스키마 동기화
 */
SW_TEST_CASE( MaterialTest, MaterialReflectionSchemaSync )
{
    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_EXPECT_TRUE( material->loadFromFile( ::test::getEngineDefaultAssets()._defaultMaterial.c_str() ) );

    sw::ShaderReflectionData reflection{};
    sw::ShaderBufferInfo     cb{};
    cb._name      = "MaterialCB";
    cb._totalSize = 32;
    sw::ShaderVariableInfo colorVar{};
    colorVar._name   = "color";
    colorVar._offset = 0;
    colorVar._size   = 16;
    colorVar._type   = "Float4";
    cb._listVariable.push_back( colorVar );
    // 셰이더 구조체는 XML(color · roughness · metallic · albedoMap)과 순서가 다르다 — 텍스처 인덱스가 먼저다.
    sw::ShaderVariableInfo albedoVar{};
    albedoVar._name   = "albedoMap";
    albedoVar._offset = 16;
    albedoVar._size   = 4;
    albedoVar._type   = "Uint";
    cb._listVariable.push_back( albedoVar );
    sw::ShaderVariableInfo roughVar{};
    roughVar._name   = "roughness";
    roughVar._offset = 20;
    roughVar._size   = 4;
    roughVar._type   = "Float";
    cb._listVariable.push_back( roughVar );
    sw::ShaderVariableInfo metallicVar{};
    metallicVar._name   = "metallic";
    metallicVar._offset = 24;
    metallicVar._size   = 4;
    metallicVar._type   = "Float";
    cb._listVariable.push_back( metallicVar );
    reflection._listConstantBuffer.push_back( cb );

    SW_EXPECT_TRUE( material->syncPropertiesFromReflection( reflection ) );
    // 텍스처 프로퍼티도 리플렉션 오프셋을 받는다 — XML 순서로 쌓은 자리(20)에 남으면 셰이더의 albedoMap 칸이 0(첫 실제 텍스처)이 된다.
    const sw::MaterialProperty* pAlbedo = material->findProperty( sw::hashed_string( "albedoMap" ) );
    SW_ASSERT_NOT_NULL( pAlbedo );
    SW_EXPECT_EQUAL( 16u, pAlbedo->_offset );
    const sw::vector<uint8>& bytes = material->getBuffer();
    SW_ASSERT_TRUE( bytes.size() >= 24u );
    uint32  albedoWord{ 0 };
    float32 roughness{ 0.0f };
    sw::Memory::copy( &albedoWord, bytes.data() + 16, sizeof( albedoWord ) );
    sw::Memory::copy( &roughness, bytes.data() + 20, sizeof( roughness ) );
    SW_EXPECT_EQUAL( sw::kInvalidDescriptorIndex, albedoWord );
    SW_EXPECT_NEAR_EQUAL( 0.5f, roughness, 1e-6f );

    sw::shared_ptr<sw::MaterialInstance> instance = sw::MaterialInstance::create( material.get() );
    sw::float4                           colorOverride{ 0.1f, 0.2f, 0.3f, 1.0f };
    instance->setVectorParameter( sw::hashed_string( "color" ), colorOverride );
    SW_EXPECT_TRUE( instance->validateParametersWithReflection( reflection ) );
    SW_EXPECT_TRUE( instance->isParameterOverridden( sw::hashed_string( "color" ) ) );

    material->setBlendMode( sw::RHIBlendMode::Transparent );
    SW_EXPECT_TRUE( material->getBlendMode() == sw::RHIBlendMode::Transparent );
}

/**
 * @brief [MaterialTest] 인스턴스 오버라이드 (CPU, updateRHI/셰이더 컴파일 없음)
 */
SW_TEST_CASE( MaterialTest, MaterialInstanceOverride )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );

    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_EXPECT_TRUE( material->loadFromFile( ::test::getEngineDefaultAssets()._defaultMaterial.c_str() ) );

    sw::shared_ptr<sw::MaterialInstance> instance = sw::MaterialInstance::create( material.get() );
    sw::float4                           overrideColor{ 0.1f, 0.2f, 0.3f, 0.4f };
    instance->setVectorParameter( sw::hashed_string( "color" ), overrideColor );
    SW_EXPECT_TRUE( instance->isParameterOverridden( sw::hashed_string( "color" ) ) );

    sw::string text;
    SW_EXPECT_TRUE( instance->getParameter( sw::hashed_string( "color" ), text ) );
    SW_EXPECT_TRUE( text.find( "0.1" ) != sw::string::npos );

    instance->clearOverrides();
    SW_EXPECT_FALSE( instance->isParameterOverridden( sw::hashed_string( "color" ) ) );
}

/**
 * @brief [MaterialTest] 셰이더 리플렉션 검증 (가상 리플렉션, DXC/sync 대기 없음)
 */
SW_TEST_CASE( MaterialTest, MaterialShaderReflectionValidation )
{
    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_EXPECT_TRUE( material->loadFromFile( ::test::getEngineDefaultAssets()._defaultMaterial.c_str() ) );

    sw::ShaderReflectionData reflection{};
    sw::ShaderBufferInfo     cb{};
    cb._name      = "MaterialCB";
    cb._totalSize = 16;
    sw::ShaderVariableInfo colorVar{};
    colorVar._name   = "color";
    colorVar._offset = 0;
    colorVar._size   = 16;
    colorVar._type   = "Float4";
    cb._listVariable.push_back( colorVar );
    reflection._listConstantBuffer.push_back( cb );

    sw::shared_ptr<sw::MaterialInstance> instance = sw::MaterialInstance::create( material.get() );
    instance->setParameter( sw::hashed_string( "color" ), "1.0 0.0 0.0 1.0" );
    SW_EXPECT_TRUE( instance->validateParametersWithReflection( reflection ) );

    // 리플렉션에 없는 이름 오버라이드는 검증 실패해야 한다.
    instance->setParameter( sw::hashed_string( "notInReflection" ), "1.0" );
    SW_EXPECT_FALSE( instance->validateParametersWithReflection( reflection ) );
}

/**
 * @brief [MaterialTest] 고속 직접 바이트 패킹 (packTextureIntoBuffer / packRawDataIntoBuffer)
 */
SW_TEST_CASE( MaterialTest, FastBytePackingDirectMethods )
{
    sw::shared_ptr<sw::Material> material = sw::Material::create();
    // 아래 전부가 이 둘에 달려 있다 — 약한 기대로 넘기면 빈 버퍼를 reinterpret_cast 해서 읽는다.
    // (리소스 루트를 못 찾는 작업 폴더에서 돌리면 그렇게 세그폴트한다.)
    SW_ASSERT_TRUE( material->loadFromFile( ::test::getEngineDefaultAssets()._defaultMaterial.c_str() ) );

    sw::vector<uint8> buffer = material->getBuffer();
    SW_ASSERT_TRUE( buffer.size() >= 16 );

    // 1) raw data packing test (e.g. float4 color)
    const float32 testColor[4] = { 0.125f, 0.25f, 0.5f, 1.0f };
    SW_EXPECT_TRUE( material->packRawDataIntoBuffer( sw::hashed_string( "color" ), testColor, sizeof( testColor ), buffer ) );

    const float32* pPackedColor = reinterpret_cast<const float32*>( buffer.data() );
    SW_EXPECT_NEAR_EQUAL( 0.125f, pPackedColor[0], 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, pPackedColor[1], 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pPackedColor[2], 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pPackedColor[3], 1e-4f );

    // 2) texture packing test
    const sw::RHIDescriptorIndex testTexIdx = 42;
    if ( material->findProperty( sw::hashed_string( "mainTexture" ) ) != nullptr )
    {
        SW_EXPECT_TRUE( material->packTextureIntoBuffer( sw::hashed_string( "mainTexture" ), testTexIdx, buffer ) );
        const sw::MaterialProperty* pTexProp = material->findProperty( sw::hashed_string( "mainTexture" ) );
        SW_ASSERT_NOT_NULL( pTexProp );
        const uint32 packedTexIdx = *reinterpret_cast<const uint32*>( buffer.data() + pTexProp->_offset );
        SW_EXPECT_EQUAL( 42u, packedTexIdx );
    }
}

/**
 * @brief [MaterialTest] ShaderReflection에 의한 런타임 CBuffer 레이아웃 동적 재배치 및 오프셋 동기화 검증
 */
SW_TEST_CASE( MaterialTest, ShaderReflectionDynamicLayoutReorderAndOffsetSync )
{
    sw::shared_ptr<sw::Material> material = sw::Material::create();

    // 1) 셰이더 리플렉션으로 CBuffer 변수 순서 및 오프셋 정의: roughness(0B), tint(16B), specular(32B), albedoIndex(48B)
    sw::ShaderReflectionData reflection{};
    sw::ShaderBufferInfo     cb{};
    cb._name      = "MaterialCB";
    cb._totalSize = 64;

    sw::ShaderVariableInfo varRoughness{};
    varRoughness._name   = "roughness";
    varRoughness._type   = "Float";
    varRoughness._offset = 0;
    varRoughness._size   = 4;
    cb._listVariable.push_back( varRoughness );

    sw::ShaderVariableInfo varTint{};
    varTint._name   = "tint";
    varTint._type   = "Float4";
    varTint._offset = 16;
    varTint._size   = 16;
    cb._listVariable.push_back( varTint );

    sw::ShaderVariableInfo varSpecular{};
    varSpecular._name   = "specular";
    varSpecular._type   = "Float";
    varSpecular._offset = 32;
    varSpecular._size   = 4;
    cb._listVariable.push_back( varSpecular );

    sw::ShaderVariableInfo varAlbedoIdx{};
    varAlbedoIdx._name   = "albedoTex";
    varAlbedoIdx._type   = "Uint";
    varAlbedoIdx._offset = 48;
    varAlbedoIdx._size   = 4;
    cb._listVariable.push_back( varAlbedoIdx );

    reflection._listConstantBuffer.push_back( cb );

    // 2) 리플렉션 데이터 동기화
    SW_EXPECT_TRUE( material->syncPropertiesFromReflection( reflection ) );

    // 3) 오프셋 및 타입 자동 갱신 검증
    const sw::MaterialProperty* pPropRoughness = material->findProperty( sw::hashed_string( "roughness" ) );
    const sw::MaterialProperty* pPropTint      = material->findProperty( sw::hashed_string( "tint" ) );
    const sw::MaterialProperty* pPropSpecular  = material->findProperty( sw::hashed_string( "specular" ) );
    const sw::MaterialProperty* pPropAlbedo    = material->findProperty( sw::hashed_string( "albedoTex" ) );

    SW_ASSERT_NOT_NULL( pPropRoughness );
    SW_ASSERT_NOT_NULL( pPropTint );
    SW_ASSERT_NOT_NULL( pPropSpecular );
    SW_ASSERT_NOT_NULL( pPropAlbedo );

    SW_EXPECT_EQUAL( 0u, pPropRoughness->_offset );
    SW_EXPECT_EQUAL( 16u, pPropTint->_offset );
    SW_EXPECT_EQUAL( 32u, pPropSpecular->_offset );
    SW_EXPECT_EQUAL( 48u, pPropAlbedo->_offset );

    // 4) 새 오프셋에 맞춘 실제 바이트 버퍼 패킹 검증
    sw::vector<uint8> buffer = material->getBuffer();
    SW_EXPECT_TRUE( buffer.size() >= 64 );

    const float32 roughnessVal = 0.75f;
    const float32 tintVal[4]   = { 0.2f, 0.4f, 0.6f, 1.0f };
    const float32 specularVal  = 0.5f;
    const uint32  bindlessIdx  = 123u;

    SW_EXPECT_TRUE( material->packRawDataIntoBuffer( sw::hashed_string( "roughness" ), &roughnessVal, sizeof( roughnessVal ), buffer ) );
    SW_EXPECT_TRUE( material->packRawDataIntoBuffer( sw::hashed_string( "tint" ), tintVal, sizeof( tintVal ), buffer ) );
    SW_EXPECT_TRUE( material->packRawDataIntoBuffer( sw::hashed_string( "specular" ), &specularVal, sizeof( specularVal ), buffer ) );
    SW_EXPECT_TRUE( material->packTextureIntoBuffer( sw::hashed_string( "albedoTex" ), bindlessIdx, buffer ) );

    const float32  packedRoughness = *reinterpret_cast<const float32*>( buffer.data() + 0 );
    const float32* pPackedTint     = reinterpret_cast<const float32*>( buffer.data() + 16 );
    const float32  packedSpecular  = *reinterpret_cast<const float32*>( buffer.data() + 32 );
    const uint32   packedAlbedoIdx = *reinterpret_cast<const uint32*>( buffer.data() + 48 );

    SW_EXPECT_NEAR_EQUAL( 0.75f, packedRoughness, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.2f, pPackedTint[0], 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.4f, pPackedTint[1], 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.6f, pPackedTint[2], 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pPackedTint[3], 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, packedSpecular, 1e-4f );
    SW_EXPECT_EQUAL( 123u, packedAlbedoIdx );
}

/**
 * @brief [MaterialTest] 셰이더 슬롯 변경 시 Non-Bindless 바인딩 유효성 검증
 */
SW_TEST_CASE( MaterialTest, ShaderReflectionSlotChangeValidation )
{
    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_EXPECT_TRUE( material->loadFromFile( ::test::getEngineDefaultAssets()._defaultMaterial.c_str() ) );

    // 1) 리플렉션에 슬롯 변경(t0 -> t2)이 발생했을 때의 리소스 바인딩 정보 구성
    sw::ShaderReflectionData reflection{};
    sw::ShaderBufferInfo     cb{};
    cb._name      = "MaterialCB";
    cb._totalSize = 16;
    sw::ShaderVariableInfo colorVar{};
    colorVar._name   = "color";
    colorVar._offset = 0;
    colorVar._size   = 16;
    colorVar._type   = "Float4";
    cb._listVariable.push_back( colorVar );
    reflection._listConstantBuffer.push_back( cb );

    sw::ShaderReflectedBinding texBinding{};
    texBinding._name          = "mainTexture";
    texBinding._type          = "Texture2D";
    texBinding._bindPoint     = 2; // t2 슬롯으로 변경
    texBinding._registerSpace = 0;
    reflection._listResource.push_back( texBinding );

    // 2) 인스턴스 파라미터 오버라이드 후 검증
    sw::shared_ptr<sw::MaterialInstance> instance = sw::MaterialInstance::create( material.get() );
    instance->setParameter( sw::hashed_string( "color" ), "0.5 0.5 0.5 1.0" );
    SW_EXPECT_TRUE( instance->validateParametersWithReflection( reflection ) );

    // 리플렉션 리소스 바인딩 목록에 포함된 mainTexture 파라미터 슬롯 반영 확인
    SW_EXPECT_EQUAL( size_t( 1 ), reflection._listResource.size() );
    SW_EXPECT_EQUAL( 2u, reflection._listResource[0]._bindPoint );
}

/**
 * @brief [MaterialTest] 셰이더 리플렉션 고속 핫리로드 레이아웃 변이 스트레스 테스트 (200회 반복 동적 재배치)
 */
SW_TEST_CASE( MaterialTest, ShaderReflectionRapidHotReloadStressTest )
{
    sw::shared_ptr<sw::Material> material = sw::Material::create();

    // 고정된 머티리얼 프로퍼티 세트 (실제 셰이더 편집 시 변수 순서 및 패딩 변경 시뮬레이션)
    const sw::string arrPropNames[] = {
        "roughness", "metallic", "specular", "tintColor", "emissiveColor", "albedoTex", "normalTex" };
    constexpr uint32 kTotalProps = 7;

    constexpr uint32 kIterations = 200;
    for ( uint32 iterIndex = 0; iterIndex < kIterations; ++iterIndex )
    {
        sw::ShaderReflectionData reflection{};
        sw::ShaderBufferInfo     cb{};
        cb._name = "MaterialCB";

        uint32 currentOffset = 0;

        struct ExpectedVar
        {
            sw::string _name;
            uint32     _offset{ 0 };
            uint32     _size{ 0 };
            float32    _testValue{ 0.0f };
        };
        sw::vector<ExpectedVar> listExpected;

        // 회차별로 프로퍼티 순서 셔플/순환
        for ( uint32 propIndex = 0; propIndex < kTotalProps; ++propIndex )
        {
            const uint32           shuffledIndex = ( propIndex + iterIndex ) % kTotalProps;
            const sw::string&      propName      = arrPropNames[shuffledIndex];
            sw::ShaderVariableInfo var{};
            var._name = propName;

            if ( propName == "tintColor" || propName == "emissiveColor" )
            {
                var._type     = "Float4";
                var._size     = 16;
                currentOffset = sw::MathUtil::align( currentOffset, 16u );
            }
            else if ( propName == "albedoTex" || propName == "normalTex" )
            {
                var._type     = "Uint";
                var._size     = 4;
                currentOffset = sw::MathUtil::align( currentOffset, 4u );
            }
            else
            {
                var._type     = "Float";
                var._size     = 4;
                currentOffset = sw::MathUtil::align( currentOffset, 4u );
            }

            var._offset = currentOffset;
            cb._listVariable.push_back( var );

            ExpectedVar expected{};
            expected._name      = var._name;
            expected._offset    = var._offset;
            expected._size      = var._size;
            expected._testValue = static_cast<float32>( ( iterIndex + 1 ) * 10 + propIndex );
            listExpected.push_back( expected );

            currentOffset += var._size;
        }
        cb._totalSize = sw::MathUtil::align( currentOffset, 16u );
        reflection._listConstantBuffer.push_back( cb );

        // 런타임 동적 핫리로드 동기화
        SW_EXPECT_TRUE( material->syncPropertiesFromReflection( reflection ) );

        sw::vector<uint8> buffer = material->getBuffer();
        SW_EXPECT_TRUE( buffer.size() >= cb._totalSize );
        SW_EXPECT_TRUE( buffer.size() % 256 == 0 ); // 256B 정렬 패딩 검증

        // 데이터 패킹 및 오프셋 무결성 검증
        for ( const ExpectedVar& expected : listExpected )
        {
            const sw::MaterialProperty* pProp = material->findProperty( sw::hashed_string( expected._name.c_str() ) );
            SW_ASSERT_NOT_NULL( pProp );
            SW_EXPECT_EQUAL( expected._offset, pProp->_offset );
            SW_EXPECT_EQUAL( expected._size, pProp->_size );

            if ( expected._size == 4 )
            {
                SW_EXPECT_TRUE( material->packRawDataIntoBuffer( sw::hashed_string( expected._name.c_str() ), &expected._testValue, sizeof( float32 ), buffer ) );
                const float32 val = *reinterpret_cast<const float32*>( buffer.data() + expected._offset );
                SW_EXPECT_NEAR_EQUAL( expected._testValue, val, 1e-4f );
            }
        }
    }
}

/**
 * @brief [MaterialTest] 멀티스레드 대규모 바인드리스 디스크립터 인덱싱 및 인스턴스 오버라이드 스트레스 테스트
 */
SW_TEST_CASE( MaterialTest, BindlessDescriptorHeapMultiThreadedStressTest )
{
    sw::shared_ptr<sw::Material> material = sw::Material::create();

    // CBuffer에 다중 텍스처 인덱스 필드 구성
    sw::ShaderReflectionData reflection{};
    sw::ShaderBufferInfo     cb{};
    cb._name      = "MaterialCB";
    cb._totalSize = 64;

    for ( uint32 slotIndex = 0; slotIndex < 8; ++slotIndex )
    {
        sw::ShaderVariableInfo var{};
        var._name   = ( "texSlot_" + std::to_string( slotIndex ) ).c_str();
        var._type   = "Uint";
        var._offset = slotIndex * 4;
        var._size   = 4;
        cb._listVariable.push_back( var );
    }
    reflection._listConstantBuffer.push_back( cb );
    SW_EXPECT_TRUE( material->syncPropertiesFromReflection( reflection ) );

    constexpr uint32        kThreadCount        = 8;
    constexpr uint32        kInstancesPerThread = 64;
    sw::vector<std::thread> listThread;
    std::atomic<uint32>     successCount{ 0 };

    for ( uint32 threadIndex = 0; threadIndex < kThreadCount; ++threadIndex )
    {
        listThread.emplace_back( [&material, threadIndex, &successCount]()
        {
            for ( uint32 instIndex = 0; instIndex < kInstancesPerThread; ++instIndex )
            {
                sw::shared_ptr<sw::MaterialInstance> instance   = sw::MaterialInstance::create( material.get() );
                sw::vector<uint8>                    instBuffer = material->getBuffer();

                const uint32 baseDescriptor = ( threadIndex * 1000 ) + ( instIndex * 8 );
                for ( uint32 slotIndex = 0; slotIndex < 8; ++slotIndex )
                {
                    const sw::string             propName      = ( "texSlot_" + std::to_string( slotIndex ) ).c_str();
                    const sw::RHIDescriptorIndex descriptorIdx = baseDescriptor + slotIndex;

                    if ( material->packTextureIntoBuffer( sw::hashed_string( propName.c_str() ), descriptorIdx, instBuffer ) )
                    {
                        const uint32 readBack = *reinterpret_cast<const uint32*>( instBuffer.data() + ( slotIndex * 4 ) );
                        if ( readBack == descriptorIdx )
                            successCount.fetch_add( 1, std::memory_order_relaxed );
                    }
                }
            }
        } );
    }

    for ( auto& t : listThread )
    {
        if ( t.joinable() )
            t.join();
    }

    constexpr uint32 kExpectedTotalPackOperations = kThreadCount * kInstancesPerThread * 8;
    SW_EXPECT_EQUAL( kExpectedTotalPackOperations, successCount.load() );
}

/**
 * @brief [MaterialTest] 복합 4x4 행렬, 경계 패딩 및 16바이트 정렬 CBuffer 패킹 정밀 스트레스 테스트
 */
SW_TEST_CASE( MaterialTest, ComplexMatrixAndArrayCbufferPackingStressTest )
{
    sw::shared_ptr<sw::Material> material = sw::Material::create();

    sw::ShaderReflectionData reflection{};
    sw::ShaderBufferInfo     cb{};
    cb._name      = "MaterialCB";
    cb._totalSize = 160;

    // 1) float3 + float (16B)
    sw::ShaderVariableInfo varVec3{};
    varVec3._name   = "lightDir";
    varVec3._type   = "Float3";
    varVec3._offset = 0;
    varVec3._size   = 12;
    cb._listVariable.push_back( varVec3 );

    sw::ShaderVariableInfo varIntensity{};
    varIntensity._name   = "intensity";
    varIntensity._type   = "Float";
    varIntensity._offset = 12;
    varIntensity._size   = 4;
    cb._listVariable.push_back( varIntensity );

    // 2) float4x4 worldMatrix (64B at offset 16)
    sw::ShaderVariableInfo varMatWorld{};
    varMatWorld._name   = "worldMatrix";
    varMatWorld._type   = "Float4x4";
    varMatWorld._offset = 16;
    varMatWorld._size   = 64;
    cb._listVariable.push_back( varMatWorld );

    // 3) float4x4 viewProjMatrix (64B at offset 80)
    sw::ShaderVariableInfo varMatVp{};
    varMatVp._name   = "viewProjMatrix";
    varMatVp._type   = "Float4x4";
    varMatVp._offset = 80;
    varMatVp._size   = 64;
    cb._listVariable.push_back( varMatVp );

    // 4) uint4 bindlessIndices (16B at offset 144)
    sw::ShaderVariableInfo varTexIndices{};
    varTexIndices._name   = "texIndices";
    varTexIndices._type   = "Uint4";
    varTexIndices._offset = 144;
    varTexIndices._size   = 16;
    cb._listVariable.push_back( varTexIndices );

    reflection._listConstantBuffer.push_back( cb );
    SW_EXPECT_TRUE( material->syncPropertiesFromReflection( reflection ) );

    sw::vector<uint8> buffer = material->getBuffer();
    SW_EXPECT_TRUE( buffer.size() >= 160 );

    // 정밀 데이터 주입
    const float32 lightDir[3] = { 0.577f, -0.577f, 0.577f };
    const float32 intensity   = 3.5f;

    const sw::float4x4 testWorld = sw::float4x4{
        1.0f, 0.0f, 0.0f, 10.0f,
        0.0f, 2.0f, 0.0f, 20.0f,
        0.0f, 0.0f, 3.0f, 30.0f,
        0.0f, 0.0f, 0.0f, 1.0f };

    const sw::float4x4 testVp = sw::float4x4{
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.8f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.1f, 1.0f,
        0.0f, 0.0f, 1.0f, 0.0f };

    const uint32 texIndices[4] = { 101, 102, 103, 104 };

    SW_EXPECT_TRUE( material->packRawDataIntoBuffer( sw::hashed_string( "lightDir" ), lightDir, sizeof( lightDir ), buffer ) );
    SW_EXPECT_TRUE( material->packRawDataIntoBuffer( sw::hashed_string( "intensity" ), &intensity, sizeof( intensity ), buffer ) );
    SW_EXPECT_TRUE( material->packRawDataIntoBuffer( sw::hashed_string( "worldMatrix" ), &testWorld, sizeof( testWorld ), buffer ) );
    SW_EXPECT_TRUE( material->packRawDataIntoBuffer( sw::hashed_string( "viewProjMatrix" ), &testVp, sizeof( testVp ), buffer ) );
    SW_EXPECT_TRUE( material->packRawDataIntoBuffer( sw::hashed_string( "texIndices" ), texIndices, sizeof( texIndices ), buffer ) );

    // 오프셋별 역직렬화 정밀 바이트 비교
    const float32* pReadLightDir = reinterpret_cast<const float32*>( buffer.data() + 0 );
    SW_EXPECT_NEAR_EQUAL( 0.577f, pReadLightDir[0], 1e-3f );
    SW_EXPECT_NEAR_EQUAL( -0.577f, pReadLightDir[1], 1e-3f );
    SW_EXPECT_NEAR_EQUAL( 0.577f, pReadLightDir[2], 1e-3f );

    const float32 readIntensity = *reinterpret_cast<const float32*>( buffer.data() + 12 );
    SW_EXPECT_NEAR_EQUAL( 3.5f, readIntensity, 1e-4f );

    const sw::float4x4* pReadWorld = reinterpret_cast<const sw::float4x4*>( buffer.data() + 16 );
    SW_EXPECT_NEAR_EQUAL( 10.0f, pReadWorld->_14, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 20.0f, pReadWorld->_24, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 30.0f, pReadWorld->_34, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, pReadWorld->_44, 1e-4f );

    const sw::float4x4* pReadVp = reinterpret_cast<const sw::float4x4*>( buffer.data() + 80 );
    SW_EXPECT_NEAR_EQUAL( 0.5f, pReadVp->_11, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.8f, pReadVp->_22, 1e-4f );

    const uint32* pReadTex = reinterpret_cast<const uint32*>( buffer.data() + 144 );
    SW_EXPECT_EQUAL( 101u, pReadTex[0] );
    SW_EXPECT_EQUAL( 102u, pReadTex[1] );
    SW_EXPECT_EQUAL( 103u, pReadTex[2] );
    SW_EXPECT_EQUAL( 104u, pReadTex[3] );
}

/**
 * @brief [MaterialTest] 어긋난 크기로 써서 **옆 프로퍼티를 덮지 않는다**
 * @details 칸 크기는 **셰이더 리플렉션**이 정하고(`ShaderVariableInfo::_size`), 쓰는 크기는
 *          **머티리얼 XML** 의 `shaderType` 이 정한다. `syncPropertiesFromReflection` 이 둘을
 *          대부분 재매핑으로 맞춰 주지만 **고칠 수 없는 조합**에서는 경고만 남기고 `shaderType` 을
 *          그대로 둔다. 여기서는 리플렉션이 **타입 이름 없이 5바이트**를 보고하는 경우를 만든다 —
 *          낡거나 깨진 리플렉션 매니페스트에서 나올 수 있는 모양이다. XML 은 `ChannelMask` +
 *          `shaderType="Float4"`, 즉 쓰는 쪽은 16바이트다.
 *
 *          `writeNumericValue` 뿐 아니라 직접 `Memory::copy` 하는 형제 경로들(Bool · Enum · BitFlag ·
 *          ChannelMask · Texture · Range)도 `packSize < need` 를 봐야 한다. 건너뛰면 5바이트 칸에 16바이트를
 *          쓰고 **그 뒤에 놓인 프로퍼티의 값을 덮는다** — 상수버퍼 안이라 메모리 오류로는 안 잡히고, 화면에서
 *          "엉뚱한 색" 으로만 나타난다.
 *
 *          그래서 검사는 "터지는가" 가 아니라 **옆 값이 살아 있는가** 로 한다.
 */
SW_TEST_CASE( MaterialTest, PackingDoesNotClobberTheNextPropertySlot )
{
    SW_TEST_SUPPRESS_LOGS();

    sw::shared_ptr<sw::Material> material = sw::Material::create();

    // `_tint` 를 **먼저** 적는다 — 패킹은 목록 순서대로 돌므로, 뒤에 오는 `_mask` 가 덮으면 진다.
    const sw::string xml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<MaterialDesc formatVersion=\"0\" name=\"MismatchProbe\" shaderPath=\"engine/shaders/forwardlit.hlsl\">"
        "  <_properties>"
        "    <item name=\"_tint\" type=\"Float\" shaderType=\"Float\" defaultValue=\"0.25\"/>"
        "    <item name=\"_mask\" type=\"ChannelMask\" shaderType=\"Float4\" defaultValue=\"rgba\"/>"
        "  </_properties>"
        "</MaterialDesc>";
    SW_ASSERT_TRUE( material->loadFromXML( xml ) );

    // 리플렉션이 말하는 것: `_mask` 는 타입 이름 없이 5바이트(재매핑이 실패하는 조합),
    // `_tint` 는 그 바로 뒤 8바이트 자리의 float 하나다.
    sw::ShaderReflectionData reflection{};
    sw::ShaderBufferInfo     cb{};
    cb._name      = "MaterialCB";
    cb._totalSize = 12;

    sw::ShaderVariableInfo varMask{};
    varMask._name   = "_mask";
    varMask._type   = "";
    varMask._offset = 0;
    varMask._size   = 5;
    cb._listVariable.push_back( varMask );

    sw::ShaderVariableInfo varTint{};
    varTint._name   = "_tint";
    varTint._type   = "Float";
    varTint._offset = 8;
    varTint._size   = 4;
    cb._listVariable.push_back( varTint );

    reflection._listConstantBuffer.push_back( cb );

    // 재매핑에 실패하므로 sync 는 false 이고 바이트는 비어 GPU 에 올라가지 않는다 — 덮인 옆 값이 화면에 나가는 길이 없다.
    SW_EXPECT_FALSE( material->syncPropertiesFromReflection( reflection ) );
    SW_EXPECT_TRUE_MSG( material->getBuffer().empty(), "레이아웃 오류 뒤에 바이트가 남았습니다" );
}

/**
 * @brief [MaterialTest] 머티리얼의 불리언이 아닌 글은 읽기 오류다 — 기본값이나 false 로 대신하지 않는다
 * @details 머티리얼의 불리언 글은 모두 `MaterialUtil::parseBoolToken` 하나를 지나(필드 · 파라미터 값 · 키워드 define) 읽지 못하면
 *          이름과 함께 오류를 남기고 로드가 실패한다. 어떤 값으로 대신하든 텍스처의 `bSrgb="ture"` 가 데이터와 다른 값이 된다.
 */
SW_TEST_CASE( MaterialTest, UnreadableBooleanFailsTheLoad )
{
    const sw::string fieldXML =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<MaterialDesc formatVersion=\"0\" name=\"BoolProbe\" shaderPath=\"engine/shaders/forwardlit.hlsl\">"
        "  <_properties>"
        "    <item name=\"_albedo\" type=\"Texture2D\" bSrgb=\"ture\"/>"
        "  </_properties>"
        "</MaterialDesc>";
    const sw::string valueXML =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<MaterialDesc formatVersion=\"0\" name=\"BoolProbe\" shaderPath=\"engine/shaders/forwardlit.hlsl\">"
        "  <_properties>"
        "    <item name=\"_flip\" type=\"Bool\" shaderType=\"Uint\" defaultValue=\"maybe\"/>"
        "  </_properties>"
        "</MaterialDesc>";
    sw::shared_ptr<sw::Material> material = sw::Material::create();

    test::ScopedLogCollector logs;
    {
        SW_TEST_DEFENSIVE_SCOPE( "non-boolean material text" );
        SW_EXPECT_FALSE( material->loadFromXML( fieldXML ) );
        SW_EXPECT_FALSE( material->loadFromXML( valueXML ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "Material value 'bSrgb' has an unreadable boolean 'ture'" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "Material value '_flip' has an unreadable boolean 'maybe'" ) >= 1, logs.joined().c_str() );
}

/**
 * @brief [MaterialTest] 머티리얼의 숫자가 아닌 글은 읽기 오류다 — 0 으로 패킹하지 않는다
 * @details Range 와 숫자 벡터는 패커(`MaterialUtil::packPropertyIntoBuffer`)가 못 읽는 글을 거절하고, 로드는 같은 패커로 확인해 멈춘다.
 */
SW_TEST_CASE( MaterialTest, UnreadableNumberFailsTheLoad )
{
    const sw::string rangeXML =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<MaterialDesc formatVersion=\"0\" name=\"NumberProbe\" shaderPath=\"engine/shaders/forwardlit.hlsl\">"
        "  <_properties>"
        "    <item name=\"roughness\" type=\"Range\" shaderType=\"Float\" defaultValue=\"half\" min=\"0\" max=\"1\"/>"
        "  </_properties>"
        "</MaterialDesc>";
    const sw::string vectorXML =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<MaterialDesc formatVersion=\"0\" name=\"NumberProbe\" shaderPath=\"engine/shaders/forwardlit.hlsl\">"
        "  <_properties>"
        "    <item name=\"color\" type=\"Color\" shaderType=\"Float4\" defaultValue=\"1 red 1 1\"/>"
        "  </_properties>"
        "</MaterialDesc>";
    sw::shared_ptr<sw::Material> material = sw::Material::create();

    test::ScopedLogCollector logs;
    {
        SW_TEST_DEFENSIVE_SCOPE( "non-numeric material text" );
        SW_EXPECT_FALSE( material->loadFromXML( rangeXML ) );
        SW_EXPECT_FALSE( material->loadFromXML( vectorXML ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "Material parameter 'roughness' has an unreadable number 'half'" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "has an unreadable number 'red'" ) == 1, logs.joined().c_str() );
}

/**
 * @brief [MaterialTest] 셰이더 구조체에 있는데 머티리얼이 적지 않은 필드는 동기화 오류다 — 0 으로 두지 않는다
 * @details 텍스처 인덱스 필드의 0 은 Vulkan 의 첫 실제 텍스처라, 적지 않은 필드를 0 으로 두면 엉뚱한 텍스처를 읽는다.
 */
SW_TEST_CASE( MaterialTest, ShaderFieldMissingFromTheMaterialFailsTheSync )
{
    const sw::string xml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<MaterialDesc formatVersion=\"0\" name=\"FieldProbe\" shaderPath=\"engine/shaders/forwardlit.hlsl\">"
        "  <_properties>"
        "    <item name=\"color\" type=\"Color\" shaderType=\"Float4\" defaultValue=\"1 1 1 1\"/>"
        "  </_properties>"
        "</MaterialDesc>";
    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_ASSERT_TRUE( material->loadFromXML( xml ) );

    sw::ShaderReflectionData reflection{};
    sw::ShaderBufferInfo     element{};
    element._name      = "g_SwMaterials";
    element._totalSize = 20;
    sw::ShaderVariableInfo varColor{};
    varColor._name   = "color";
    varColor._type   = "Float4";
    varColor._offset = 0;
    varColor._size   = 16;
    element._listVariable.push_back( varColor );
    sw::ShaderVariableInfo varAlbedo{};
    varAlbedo._name   = "albedoMap";
    varAlbedo._type   = "Uint";
    varAlbedo._offset = 16;
    varAlbedo._size   = 4;
    element._listVariable.push_back( varAlbedo );
    reflection._listStructuredElement.push_back( element );

    test::ScopedLogCollector logs;
    {
        SW_TEST_DEFENSIVE_SCOPE( "material missing a shader field" );
        SW_EXPECT_FALSE( material->syncPropertiesFromReflection( reflection ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "does not list shader field 'albedoMap'" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE( material->getBuffer().empty() );
}

/**
 * @brief [MaterialTest] 머티리얼에만 있는 필드(셰이더 구조체에 없는 프로퍼티)도 동기화 오류다 — 바이트를 비워 올리지 않는다
 */
SW_TEST_CASE( MaterialTest, MaterialFieldMissingFromTheShaderFailsTheSync )
{
    const sw::string xml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<MaterialDesc formatVersion=\"0\" name=\"ExtraProbe\" shaderPath=\"engine/shaders/forwardlit.hlsl\">"
        "  <_properties>"
        "    <item name=\"color\" type=\"Color\" shaderType=\"Float4\" defaultValue=\"1 1 1 1\"/>"
        "    <item name=\"ghost\" type=\"Float\" shaderType=\"Float\" defaultValue=\"1\"/>"
        "  </_properties>"
        "</MaterialDesc>";
    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_ASSERT_TRUE( material->loadFromXML( xml ) );

    sw::ShaderReflectionData reflection{};
    sw::ShaderBufferInfo     element{};
    element._name      = "g_SwMaterials";
    element._totalSize = 16;
    sw::ShaderVariableInfo varColor{};
    varColor._name = "color";
    varColor._type = "Float4";
    varColor._size = 16;
    element._listVariable.push_back( varColor );
    reflection._listStructuredElement.push_back( element );

    test::ScopedLogCollector logs;
    {
        SW_TEST_DEFENSIVE_SCOPE( "material lists a field the shader lacks" );
        SW_EXPECT_FALSE( material->syncPropertiesFromReflection( reflection ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "lists property 'ghost'" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE( material->getBuffer().empty() );
}

/**
 * @brief [MaterialTest] 머티리얼 스키마가 없는 리플렉션은 오류다 — XML 순서 바이트로 남지 않는다
 */
SW_TEST_CASE( MaterialTest, ReflectionWithoutMaterialSchemaFailsTheSync )
{
    const sw::string xml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<MaterialDesc formatVersion=\"0\" name=\"NoSchemaProbe\" shaderPath=\"engine/shaders/forwardlit.hlsl\">"
        "  <_properties>"
        "    <item name=\"color\" type=\"Color\" shaderType=\"Float4\" defaultValue=\"1 1 1 1\"/>"
        "  </_properties>"
        "</MaterialDesc>";
    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_ASSERT_TRUE( material->loadFromXML( xml ) );

    sw::ShaderReflectionData reflection{};
    sw::ShaderBufferInfo     passCb{};
    passCb._name = "PassCB";
    sw::ShaderVariableInfo var{};
    var._name = "viewProj";
    var._type = "Float4x4";
    var._size = 64;
    passCb._listVariable.push_back( var );
    reflection._listConstantBuffer.push_back( passCb );

    test::ScopedLogCollector logs;
    {
        SW_TEST_DEFENSIVE_SCOPE( "reflection without material schema" );
        SW_EXPECT_FALSE( material->syncPropertiesFromReflection( reflection ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "머티리얼 스키마가 없습니다" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE( material->getBuffer().empty() );
}

/**
 * @brief [MaterialTest] 저장소의 모든 .material 이 자기 셰이더의 머티리얼 구조체와 필드가 일치한다(셰이더에만 · XML 에만 있는 필드 없음)
 * @details 일치하지 않으면 로드 뒤 첫 동기화에서 오류가 나고 바이트가 비어 화면에서 사라진다. 그 오류를 렌더 전에 한 번에 모아 보여 준다.
 */
SW_TEST_CASE( MaterialTest, EveryRepositoryMaterialMatchesItsShaderStruct )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    const sw::string engineDir = sw::ResourcePaths::findLooseDomainFolder( "engine", "materials" ).getDiskPath();
    if ( engineDir.empty() )
        SW_TEST_SKIP( "Resource/engine/materials 를 찾지 못했습니다" );
    const sw::string       resourceRoot = sw::FileUtil::getDirectoryPart( sw::FileUtil::getDirectoryPart( engineDir ) );
    sw::vector<sw::string> listFile;
    SW_ASSERT_TRUE( sw::FileUtil::collectFiles( resourceRoot, ".material", listFile, true ) );

    uint32     checked{ 0 };
    sw::string failures;
    for ( const sw::string& path : listFile )
    {
        sw::string xml;
        if ( sw::FileUtil::readTextFile( path, xml ) == false )
            continue;
        sw::shared_ptr<sw::Material> material = sw::Material::create();
        if ( material->loadFromXML( xml ) == false || material->getDesc()._shaderPath.empty() )
            continue;
        sw::ShaderReflectionData reflection{};
        if ( sw::Material::reflectMaterialSchema( material->getDesc()._shaderPath, sw::ShaderTargetFormat::SPIRV_Vulkan, reflection ) == false )
            SW_TEST_SKIP( "셰이더 리플렉션을 구하지 못했습니다(App.exe --cook-shaders 필요)" );
        if ( material->syncPropertiesFromReflection( reflection ) == false )
            failures.append( path ).append( ", " );
        ++checked;
    }
    SW_EXPECT_TRUE_MSG( checked > 0, "머티리얼을 하나도 검사하지 못했다" );
    SW_EXPECT_TRUE_MSG( failures.empty(), failures.c_str() );
}

/**
 * @brief [MaterialTest] 없는 머티리얼 파일은 GPU 에 올리지 않는다 — 빈 기본값으로 계속하지 않는다
 * @details 못 읽은 머티리얼을 빈 값으로 올리면 화면에서 빠진 것을 알 수 없다. 대신 그릴 것(누락 머티리얼)은 부르는 쪽이 정한다.
 */
SW_TEST_CASE( MaterialTest, MissingMaterialFileFailsLoad )
{
    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_TEST_DEFENSIVE_SCOPE( "missing material file" );
    SW_EXPECT_FALSE( material->loadFromFile( "engine/materials/does_not_exist.material" ) );
    SW_EXPECT_FALSE( material->isRHIValid() );
}

/**
 * @brief [MaterialTest] 다시 로드한 머티리얼은 셰이더 레이아웃을 잊는다 — 원소 stride 를 비우고 바이트 세대를 올린다
 * @details 리플렉션으로 레이아웃을 맞춘 뒤 같은 머티리얼을 다시 로드하면(에셋 핫 리로드 · 에디터 미리보기) 프로퍼티가 XML 순서로 다시
 *          쌓인다. "맞췄다" 는 표시가 남으면 다시 맞추지 않아 **옛 stride 와 XML 순서 바이트**가 함께 GPUScene 에 올라간다 —
 *          셰이더가 color 를 읽는 자리에 roughness 가 들어간다. 실제 리플렉션으로 다시 맞추는 것은
 *          `RenderPassGPUTest.ReloadedMaterialIsLaidOutByTheShaderAgain` 이 본다.
 */
SW_TEST_CASE( MaterialTest, ReloadForgetsTheShaderLayout )
{
    SW_TEST_SUPPRESS_LOGS();

    // XML 은 roughness 를 먼저 적고, 셰이더(SwMaterialData)는 color 를 먼저 둔다.
    const sw::string xml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<MaterialDesc formatVersion=\"0\" name=\"ReloadProbe\" shaderPath=\"engine/shaders/forwardlit.hlsl\">"
        "  <_properties>"
        "    <item name=\"roughness\" type=\"Range\" shaderType=\"Float\" defaultValue=\"0.5\"/>"
        "    <item name=\"color\" type=\"Color\" shaderType=\"Float4\" defaultValue=\"0 0 0 1\"/>"
        "  </_properties>"
        "</MaterialDesc>";
    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_ASSERT_TRUE( material->loadFromXML( xml ) );

    sw::ShaderReflectionData reflection{};
    sw::ShaderBufferInfo     element{};
    element._name      = "g_SwMaterials";
    element._totalSize = 20;
    sw::ShaderVariableInfo varColor{};
    varColor._name   = "color";
    varColor._type   = "Float4";
    varColor._offset = 0;
    varColor._size   = 16;
    element._listVariable.push_back( varColor );
    sw::ShaderVariableInfo varRoughness{};
    varRoughness._name   = "roughness";
    varRoughness._type   = "Float";
    varRoughness._offset = 16;
    varRoughness._size   = 4;
    element._listVariable.push_back( varRoughness );
    reflection._listStructuredElement.push_back( element );
    SW_ASSERT_TRUE( material->syncPropertiesFromReflection( reflection ) );
    SW_ASSERT_EQUAL( 20u, material->getElementStride() );

    const uint32 syncedGeneration = material->getBufferGeneration();
    SW_ASSERT_TRUE( material->loadFromXML( xml ) ); // 핫 리로드 — XML 순서로 다시 쌓인다

    const sw::MaterialProperty* pColor = material->findProperty( sw::hashed_string( "color" ) );
    SW_ASSERT_NOT_NULL( pColor );
    SW_EXPECT_EQUAL( 16u, pColor->_offset ); // 다시 맞추기 전에는 XML 순서다
    SW_EXPECT_TRUE_MSG( material->getElementStride() == 0, "옛 stride 가 XML 순서 바이트와 함께 남았습니다 — 다시 맞추지 않습니다" );
    SW_EXPECT_TRUE_MSG( material->getBufferGeneration() != syncedGeneration, "바이트가 바뀌었는데 세대가 그대로입니다 — 인스턴스가 옛 복사본을 씁니다" );
}

/**
 * @brief [MaterialTest] 인스턴스의 텍스처 덮어쓰기는 에셋 경로다 — 저장하면 `assetPath` 로 나가고, 읽으면 다시 덮어쓰기가 된다
 * @details 덮어쓰기를 날 디스크립터 인덱스로 들면 값(`value`)으로 저장되고, 파일의 `assetPath` 를 읽고 버리면 .materialinstance 에 적은 텍스처가
 *          조용히 부모 것으로 남는다. GPU 에 닿는 것은 `RenderPassGPUTest.InstanceOverridesReachTheGPUOnEveryBackend` 가 본다.
 */
SW_TEST_CASE( MaterialTest, InstanceTextureOverrideRoundTripsAsAnAssetPath )
{
    SW_ASSERT_TRUE( sw::ResourcePaths::initialize() );
    sw::shared_ptr<sw::Material> parent = sw::Material::create();
    SW_ASSERT_TRUE( parent->loadFromFile( "engine/materials/benchtextured.material" ) );

    sw::shared_ptr<sw::MaterialInstance> instance = sw::MaterialInstance::create( parent.get() );
    // 덮어쓰지 않았으면 부모 프로퍼티의 경로다.
    SW_EXPECT_TRUE( instance->getTextureParameter( sw::hashed_string( "albedoMap" ) ) == "engine/textures/test/checker.dds" );
    instance->setTextureParameter( sw::hashed_string( "albedoMap" ), "engine/textures/perlin.dds" );
    SW_EXPECT_TRUE( instance->isParameterOverridden( sw::hashed_string( "albedoMap" ) ) );

    const sw::string path = test::makeTempPath( "textureoverride.materialinstance" );
    SW_ASSERT_TRUE( instance->saveToFile( path ) );
    sw::string text;
    SW_ASSERT_TRUE( sw::FileUtil::readTextFile( path, text ) );
    SW_EXPECT_TRUE_MSG( text.find( "assetPath=\"engine/textures/perlin.dds\"" ) != sw::string::npos, text.c_str() );

    sw::shared_ptr<sw::MaterialInstance> reloaded = sw::MaterialInstance::create( parent.get() );
    SW_ASSERT_TRUE( reloaded->loadFromFile( ::test::makeTempResourceID( path ) ) );
    SW_EXPECT_TRUE_MSG( reloaded->getTextureParameter( sw::hashed_string( "albedoMap" ) ) == "engine/textures/perlin.dds", text.c_str() );
    SW_EXPECT_TRUE( reloaded->isParameterOverridden( sw::hashed_string( "albedoMap" ) ) );

    // 지우면 다시 부모 경로다.
    reloaded->setTextureParameter( sw::hashed_string( "albedoMap" ), "" );
    SW_EXPECT_TRUE( reloaded->getTextureParameter( sw::hashed_string( "albedoMap" ) ) == "engine/textures/test/checker.dds" );
    SW_EXPECT_FALSE( reloaded->isParameterOverridden( sw::hashed_string( "albedoMap" ) ) );
}

/**
 * @brief [MaterialTest] 머티리얼의 모르는 enum · 플래그 글은 경고하고 값을 그대로 둔다 — 0 이 되거나 토큰이 빠지지 않는다
 * @details 프로퍼티 값(파일의 `_enumEntries` · 리플렉션 `enumType`)과 `_permutations` 의 `usage` 가 같은 규칙이다: 모르는 이름 · 오타 · 표식 값
 *          (`Count`)이 하나라도 있으면 읽지 않는다. 에셋 글은 전역 이름 표에 넣지 않는다(`hashed_string::findInterned`).
 */
SW_TEST_CASE( MaterialTest, UnknownEnumTextKeepsTheValueAndSaysSo )
{
    const sw::string xml =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<MaterialDesc formatVersion=\"0\" name=\"EnumProbe\" shaderPath=\"engine/shaders/forwardlit.hlsl\">"
        "  <_permutations quality=\"High\" usage=\"Instanced | ZqUsageTypoProbe\"/>"
        "  <_properties>"
        "    <item name=\"shadeMode\" type=\"Enum\" shaderType=\"Uint\" value=\"Lit\">"
        "      <_enumEntries><item name=\"Unlit\" value=\"0\"/><item name=\"Lit\" value=\"1\"/></_enumEntries>"
        "    </item>"
        "    <item name=\"flags\" type=\"BitFlag\" shaderType=\"Uint\" value=\"CastShadows|ReceiveDecals\">"
        "      <_enumEntries><item name=\"CastShadows\" value=\"1\"/><item name=\"ReceiveDecals\" value=\"2\"/></_enumEntries>"
        "    </item>"
        "    <item name=\"usage\" type=\"BitFlag\" shaderType=\"Uint\" enumType=\"sw::MaterialUsageFlags\" value=\"StaticMesh|Instanced\"/>"
        "    <item name=\"quality\" type=\"Enum\" shaderType=\"Uint\" enumType=\"sw::MaterialQualityLevel\" value=\"Epic\"/>"
        "  </_properties>"
        "</MaterialDesc>";
    sw::shared_ptr<sw::Material> material = sw::Material::create();

    test::ScopedLogCollector logs;
    {
        // `_permutations` 의 usage 에 모르는 토큰이 있으면 로드 오류다 — 아는 토큰만 남기거나 기본값으로 계속하지 않는다.
        SW_TEST_DEFENSIVE_SCOPE( "unknown material enum text" );
        SW_EXPECT_FALSE( material->loadFromXML( xml ) );
    }
    SW_EXPECT_TRUE_MSG( logs.countContaining( "ZqUsageTypoProbe" ) >= 1, logs.joined().c_str() );
    SW_ASSERT_TRUE( material->loadFromXML( sw::StringUtil::replace( xml, " | ZqUsageTypoProbe", "" ) ) );
    SW_EXPECT_TRUE( material->getPermutations()._usage == sw::MaterialUsageFlags::Instanced );

    const auto readUint = [&]( const utf8* pName ) -> uint32
    {
        const uint32* pValue = reinterpret_cast<const uint32*>( material->getParameterData( pName ) );
        return pValue != nullptr ? *pValue : 0xFFFFFFFFu;
    };
    SW_ASSERT_EQUAL( 1u, readUint( "shadeMode" ) );
    SW_ASSERT_EQUAL( 3u, readUint( "flags" ) );
    SW_ASSERT_EQUAL( 5u, readUint( "usage" ) ); // StaticMesh(1) | Instanced(4)
    SW_ASSERT_EQUAL( 3u, readUint( "quality" ) );

    // 오타 · 모르는 토큰 · 표식 값은 쓰지 않는다 — 앞의 값이 남고, 이름과 함께 경고한다.
    {
        SW_TEST_DEFENSIVE_SCOPE( "unknown material enum text" );
        SW_EXPECT_FALSE( material->setParameter( nullptr, sw::hashed_string( "shadeMode" ), "Lt" ) );
        SW_EXPECT_FALSE( material->setParameter( nullptr, sw::hashed_string( "flags" ), "CastShadows|ReceiveDecalz" ) );
        SW_EXPECT_FALSE( material->setParameter( nullptr, sw::hashed_string( "usage" ), "Decal | ZqFlagTypoProbe" ) );
        SW_EXPECT_FALSE( material->setParameter( nullptr, sw::hashed_string( "quality" ), "ZqQualityTypoProbe" ) );
        SW_EXPECT_FALSE( material->setParameter( nullptr, sw::hashed_string( "quality" ), "Count" ) );
        SW_EXPECT_FALSE( material->setParameter( nullptr, sw::hashed_string( "quality" ), "Med" ) ); // 줄인 이름은 열거자가 아니다 — 이름은 하나다
    }
    SW_EXPECT_EQUAL( 1u, readUint( "shadeMode" ) );
    SW_EXPECT_EQUAL( 3u, readUint( "flags" ) );
    SW_EXPECT_EQUAL( 5u, readUint( "usage" ) );
    SW_EXPECT_EQUAL( 3u, readUint( "quality" ) );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "Material parameter 'shadeMode' has an unknown enum value 'Lt'" ) == 1, logs.joined().c_str() );
    SW_EXPECT_TRUE_MSG( logs.countContaining( "'quality' has an unknown enum value 'Count'" ) == 1, logs.joined().c_str() );
    // 실패한 글은 프로퍼티 값으로도 남지 않는다(저장하면 옛 값이 나간다).
    const sw::MaterialProperty* pShade = material->findProperty( sw::hashed_string( "shadeMode" ) );
    SW_ASSERT_NOT_NULL( pShade );
    SW_EXPECT_TRUE( pShade->_value == "Lit" );

    // 아는 이름 · 숫자 · 쉼표 구분은 그대로 읽는다.
    SW_EXPECT_TRUE( material->setParameter( nullptr, sw::hashed_string( "shadeMode" ), "unlit" ) );
    SW_EXPECT_EQUAL( 0u, readUint( "shadeMode" ) );
    SW_EXPECT_TRUE( material->setParameter( nullptr, sw::hashed_string( "flags" ), "ReceiveDecals, 1" ) );
    SW_EXPECT_EQUAL( 3u, readUint( "flags" ) );
    SW_EXPECT_TRUE( material->setParameter( nullptr, sw::hashed_string( "usage" ), "Decal" ) );
    SW_EXPECT_EQUAL( 16u, readUint( "usage" ) );
    SW_EXPECT_TRUE( material->setParameter( nullptr, sw::hashed_string( "quality" ), "Medium" ) );
    SW_EXPECT_EQUAL( 1u, readUint( "quality" ) );

    // 에셋 글은 전역 이름 표에 들어가지 않는다.
    SW_EXPECT_TRUE( sw::hashed_string::findInterned( "ZqUsageTypoProbe" ).empty() );
    SW_EXPECT_TRUE( sw::hashed_string::findInterned( "ZqFlagTypoProbe" ).empty() );
    SW_EXPECT_TRUE( sw::hashed_string::findInterned( "ZqQualityTypoProbe" ).empty() );
}

/**
 * @brief [MaterialTest] 머티리얼 · 인스턴스의 형식 판정은 `AssetManager` 서비스 없이 돈다 — 이 빌드보다 새 판은 서비스가 없어도 assert 없이 거절한다
 * @details 형식 판정은 씬 · 프리팹과 같은 `AssetXMLFormat::isCurrentXMLVersion`, 판 번호 쓰기 · 읽기는 상태 없는 정적 함수다.
 *          머티리얼 본문의 enum 글 해석은 리플렉션(`TypeRegistry`)이 필요하므로 이 테스트는 그 앞에서 끝나는 경로만 본다.
 */
SW_TEST_CASE( MaterialTest, FormatCheckDoesNotNeedTheAssetManager )
{
    const sw::string futureMaterialPath = test::makeTempPath( "future.material" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( futureMaterialPath, "<MaterialDesc formatVersion=\"999\" name=\"Future\"/>" ) );
    const sw::string futureInstancePath = test::makeTempPath( "future.materialinstance" );
    SW_ASSERT_TRUE( sw::FileUtil::writeTextFile( futureInstancePath, "<MaterialInstanceDesc formatVersion=\"999\" name=\"Future\"/>" ) );

    SW_ASSERT_TRUE( sw::engine::areEngineServicesBound() );
    {
        const test::ScopedUnboundEngineServices unbound;
        SW_ASSERT_FALSE( sw::engine::areEngineServicesBound() );

        test::ScopedDefensiveTestLog expected( "material files newer than this build" );
        sw::shared_ptr<sw::Material> material = sw::Material::create();
        SW_EXPECT_FALSE( material->loadFromFile( ::test::makeTempResourceID( futureMaterialPath ) ) );
        sw::shared_ptr<sw::MaterialInstance> instance = sw::MaterialInstance::create( material.get() );
        SW_EXPECT_FALSE( instance->loadFromFile( ::test::makeTempResourceID( futureInstancePath ) ) );
    }
    SW_EXPECT_TRUE( sw::engine::areEngineServicesBound() );
}
