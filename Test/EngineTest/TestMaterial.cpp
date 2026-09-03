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
    bool         loadOk = material.loadFromFile( "engine/materials/defaultmaterial.material" );
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
    bool       saveOk   = material.saveToFile( tempPath );
    SW_EXPECT_TRUE( saveOk );
    SW_EXPECT_TRUE( sw::FileUtil::fileExists( tempPath ) );

    sw::Material reloadedMaterial;
    bool         reloadOk = reloadedMaterial.loadFromFile( tempPath );
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
    auto                   has     = [&]( const utf8* pDefine )
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
    instance.setParameter( sw::hashed_string( "color" ), "0.2 0.75 1.0 0.35" );
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
    sw::ShaderBufferInfo     cb{};
    cb._name      = "MaterialCB";
    cb._totalSize = 32;
    sw::ShaderVariableInfo colorVar{};
    colorVar._name   = "color";
    colorVar._offset = 0;
    colorVar._size   = 16;
    colorVar._type   = "Float4";
    cb._listVariable.push_back( colorVar );
    sw::ShaderVariableInfo roughVar{};
    roughVar._name   = "roughness";
    roughVar._offset = 16;
    roughVar._size   = 4;
    roughVar._type   = "Float";
    cb._listVariable.push_back( roughVar );
    reflection._listConstantBuffer.push_back( cb );

    SW_EXPECT_TRUE( material.syncPropertiesFromReflection( reflection ) );

    sw::MaterialInstance instance( &material );
    sw::float4           colorOverride{ 0.1f, 0.2f, 0.3f, 1.0f };
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
    sw::float4           overrideColor{ 0.1f, 0.2f, 0.3f, 0.4f };
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

/**
 * @brief [MaterialTest] ShaderReflection에 의한 런타임 CBuffer 레이아웃 동적 재배치 및 오프셋 동기화 검증
 */
SW_TEST_CASE( MaterialTest, ShaderReflectionDynamicLayoutReorderAndOffsetSync )
{
    sw::Material material;

    // 1) 셰이더 리플렉션으로 CBuffer 변수 순서 및 오프셋 정의: roughness(0B), tint(16B), specular(32B), albedoIndex(48B)
    sw::ShaderReflectionData reflection{};
    sw::ShaderBufferInfo     cb{};
    cb._name      = "CustomMaterialCB";
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
    SW_EXPECT_TRUE( material.syncPropertiesFromReflection( reflection ) );

    // 3) 오프셋 및 타입 자동 갱신 검증
    const sw::MaterialProperty* pPropRoughness = material.findProperty( sw::hashed_string( "roughness" ) );
    const sw::MaterialProperty* pPropTint      = material.findProperty( sw::hashed_string( "tint" ) );
    const sw::MaterialProperty* pPropSpecular  = material.findProperty( sw::hashed_string( "specular" ) );
    const sw::MaterialProperty* pPropAlbedo    = material.findProperty( sw::hashed_string( "albedoTex" ) );

    SW_ASSERT_NOT_NULL( pPropRoughness );
    SW_ASSERT_NOT_NULL( pPropTint );
    SW_ASSERT_NOT_NULL( pPropSpecular );
    SW_ASSERT_NOT_NULL( pPropAlbedo );

    SW_EXPECT_EQUAL( 0u, pPropRoughness->_offset );
    SW_EXPECT_EQUAL( 16u, pPropTint->_offset );
    SW_EXPECT_EQUAL( 32u, pPropSpecular->_offset );
    SW_EXPECT_EQUAL( 48u, pPropAlbedo->_offset );

    // 4) 새 오프셋에 맞춘 실제 바이트 버퍼 패킹 검증
    sw::vector<uint8> buffer = material.getBuffer();
    SW_EXPECT_TRUE( buffer.size() >= 64 );

    const float32 roughnessVal = 0.75f;
    const float32 tintVal[4]   = { 0.2f, 0.4f, 0.6f, 1.0f };
    const float32 specularVal  = 0.5f;
    const uint32  bindlessIdx  = 123u;

    SW_EXPECT_TRUE( material.packRawDataIntoBuffer( sw::hashed_string( "roughness" ), &roughnessVal, sizeof( roughnessVal ), buffer ) );
    SW_EXPECT_TRUE( material.packRawDataIntoBuffer( sw::hashed_string( "tint" ), tintVal, sizeof( tintVal ), buffer ) );
    SW_EXPECT_TRUE( material.packRawDataIntoBuffer( sw::hashed_string( "specular" ), &specularVal, sizeof( specularVal ), buffer ) );
    SW_EXPECT_TRUE( material.packTextureIntoBuffer( sw::hashed_string( "albedoTex" ), bindlessIdx, buffer ) );

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
    sw::Material material;
    SW_EXPECT_TRUE( material.loadFromFile( "engine/materials/defaultmaterial.material" ) );

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

    sw::ShaderResourceBinding texBinding{};
    texBinding._name          = "mainTexture";
    texBinding._type          = "Texture2D";
    texBinding._bindPoint     = 2; // t2 슬롯으로 변경
    texBinding._registerSpace = 0;
    reflection._listResource.push_back( texBinding );

    // 2) 인스턴스 파라미터 오버라이드 후 검증
    sw::MaterialInstance instance( &material );
    instance.setParameter( sw::hashed_string( "color" ), "0.5 0.5 0.5 1.0" );
    SW_EXPECT_TRUE( instance.validateParametersWithReflection( reflection ) );

    // 리플렉션 리소스 바인딩 목록에 포함된 mainTexture 파라미터 슬롯 반영 확인
    SW_EXPECT_EQUAL( size_t( 1 ), reflection._listResource.size() );
    SW_EXPECT_EQUAL( 2u, reflection._listResource[0]._bindPoint );
}

/**
 * @brief [MaterialTest] 셰이더 리플렉션 고속 핫리로드 레이아웃 변이 스트레스 테스트 (200회 반복 동적 재배치)
 */
SW_TEST_CASE( MaterialTest, ShaderReflectionRapidHotReloadStressTest )
{
    sw::Material material;

    // 고정된 머티리얼 프로퍼티 세트 (실제 셰이더 편집 시 변수 순서 및 패딩 변경 시뮬레이션)
    const sw::string arrPropNames[] = {
        "roughness", "metallic", "specular", "tintColor", "emissiveColor", "albedoTex", "normalTex" };
    constexpr uint32 kTotalProps = 7;

    constexpr uint32 kIterations = 200;
    for ( uint32 iterIndex = 0; iterIndex < kIterations; ++iterIndex )
    {
        sw::ShaderReflectionData reflection{};
        sw::ShaderBufferInfo     cb{};
        cb._name = "StressDynamicCB";

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
        SW_EXPECT_TRUE( material.syncPropertiesFromReflection( reflection ) );

        sw::vector<uint8> buffer = material.getBuffer();
        SW_EXPECT_TRUE( buffer.size() >= cb._totalSize );
        SW_EXPECT_TRUE( buffer.size() % 256 == 0 ); // 256B 정렬 패딩 검증

        // 데이터 패킹 및 오프셋 무결성 검증
        for ( const ExpectedVar& expected : listExpected )
        {
            const sw::MaterialProperty* pProp = material.findProperty( sw::hashed_string( expected._name.c_str() ) );
            SW_ASSERT_NOT_NULL( pProp );
            SW_EXPECT_EQUAL( expected._offset, pProp->_offset );
            SW_EXPECT_EQUAL( expected._size, pProp->_size );

            if ( expected._size == 4 )
            {
                SW_EXPECT_TRUE( material.packRawDataIntoBuffer( sw::hashed_string( expected._name.c_str() ), &expected._testValue, sizeof( float32 ), buffer ) );
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
    sw::Material material;

    // CBuffer에 다중 텍스처 인덱스 필드 구성
    sw::ShaderReflectionData reflection{};
    sw::ShaderBufferInfo     cb{};
    cb._name      = "MultiBindlessCB";
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
    SW_EXPECT_TRUE( material.syncPropertiesFromReflection( reflection ) );

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
                sw::MaterialInstance instance( &material );
                sw::vector<uint8>    instBuffer = material.getBuffer();

                const uint32 baseDescriptor = ( threadIndex * 1000 ) + ( instIndex * 8 );
                for ( uint32 slotIndex = 0; slotIndex < 8; ++slotIndex )
                {
                    const sw::string             propName      = ( "texSlot_" + std::to_string( slotIndex ) ).c_str();
                    const sw::RHIDescriptorIndex descriptorIdx = baseDescriptor + slotIndex;

                    if ( material.packTextureIntoBuffer( sw::hashed_string( propName.c_str() ), descriptorIdx, instBuffer ) )
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
    sw::Material material;

    sw::ShaderReflectionData reflection{};
    sw::ShaderBufferInfo     cb{};
    cb._name      = "ComplexMatrixCB";
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
    SW_EXPECT_TRUE( material.syncPropertiesFromReflection( reflection ) );

    sw::vector<uint8> buffer = material.getBuffer();
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

    SW_EXPECT_TRUE( material.packRawDataIntoBuffer( sw::hashed_string( "lightDir" ), lightDir, sizeof( lightDir ), buffer ) );
    SW_EXPECT_TRUE( material.packRawDataIntoBuffer( sw::hashed_string( "intensity" ), &intensity, sizeof( intensity ), buffer ) );
    SW_EXPECT_TRUE( material.packRawDataIntoBuffer( sw::hashed_string( "worldMatrix" ), &testWorld, sizeof( testWorld ), buffer ) );
    SW_EXPECT_TRUE( material.packRawDataIntoBuffer( sw::hashed_string( "viewProjMatrix" ), &testVp, sizeof( testVp ), buffer ) );
    SW_EXPECT_TRUE( material.packRawDataIntoBuffer( sw::hashed_string( "texIndices" ), texIndices, sizeof( texIndices ), buffer ) );

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
