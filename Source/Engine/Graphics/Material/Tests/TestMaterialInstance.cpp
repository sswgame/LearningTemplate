#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Material/MaterialRenderProxy.h"
#include "Engine/Graphics/Material/MaterialTypes.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

// MaterialInstance — 값 덮어쓰기의 사본 재사용과 저장 글(nogpu).

using namespace sw;

namespace
{
    struct MaterialInstanceTestUtil
    {
        static constexpr const utf8* kMaterial = R"(<?xml version="1.0" encoding="utf-8"?>
<MaterialDef formatVersion="0" name="Sample" shaderPath="engine/shaders/pbr_lit.hlsl" alphaMode="Opaque">
	<_properties>
		<item name="_color" type="Color" defaultValue="1 1 1 1"/>
		<item name="_roughness" type="Range" defaultValue="1" min="0.0" max="1.0"/>
	</_properties>
</MaterialDef>
)";

        static constexpr const utf8* kTexturedMaterial = R"(<?xml version="1.0" encoding="utf-8"?>
<MaterialDef formatVersion="0" name="Textured" shaderPath="engine/shaders/pbr_lit.hlsl" alphaMode="Opaque">
	<_properties>
		<item name="_color" type="Color" defaultValue="1 1 1 1"/>
		<item name="_albedoMap" type="Texture2D"/>
		<item name="_normalMap" type="Texture2D"/>
		<item name="_ormMap" type="Texture2D"/>
	</_properties>
</MaterialDef>
)";
    };
} // namespace

/**
 * @brief [MaterialInstanceTest] 같은 값을 다시 써도 사본을 새로 만들지 않고, 값이 바뀌면 새 사본에 그 값이 실린다
 */
SW_TEST_CASE( MaterialInstanceTest, SameValueKeepsRenderCopy )
{
    shared_ptr<Material> material = Material::create();
    SW_REQUIRE_TRUE( material->loadFromXML( MaterialInstanceTestUtil::kMaterial ) );
    shared_ptr<MaterialInstance> instance = MaterialInstance::create( material.get() );

    const hashed_string colorName( "_color" );
    const hashed_string valueName( "_roughness" );
    instance->setVectorParameter( colorName, float4{ 0.25f, 0.5f, 0.75f, 1.0f } );
    instance->setScalarParameter( valueName, 0.125f );
    const MaterialRenderBinding first = instance->getRenderBinding();
    SW_REQUIRE_TRUE( first._data != nullptr );

    instance->setVectorParameter( colorName, float4{ 0.25f, 0.5f, 0.75f, 1.0f } );
    instance->setScalarParameter( valueName, 0.125f );
    const MaterialRenderBinding unchanged = instance->getRenderBinding();
    SW_EXPECT_TRUE( first._data == unchanged._data );

    instance->setScalarParameter( valueName, 0.5f );
    const MaterialRenderBinding changed = instance->getRenderBinding();
    SW_EXPECT_TRUE( unchanged._data != changed._data );
    const MaterialProperty* pRoughness = material->findProperty( valueName );
    SW_REQUIRE_TRUE( pRoughness != nullptr );
    float32 roughness{ 0.0f };
    Memory::copy( &roughness, changed._data->_bytes.data() + pRoughness->_offset, sizeof( roughness ) );
    SW_EXPECT_NEAR( 0.5f, roughness, 1e-6f );
}

/**
 * @brief [MaterialInstanceTest] 이진으로 든 값도 조회 · 저장의 글은 예전 형식 그대로다(공백으로 나눈 최단 왕복 표현)
 */
SW_TEST_CASE( MaterialInstanceTest, FloatOverridesKeepTextForm )
{
    shared_ptr<Material> material = Material::create();
    SW_REQUIRE_TRUE( material->loadFromXML( MaterialInstanceTestUtil::kMaterial ) );
    shared_ptr<MaterialInstance> instance = MaterialInstance::create( material.get() );

    instance->setVectorParameter( hashed_string( "_color" ), float4{ 0.25f, 0.5f, 0.75f, 1.5f } );
    instance->setScalarParameter( hashed_string( "_roughness" ), 0.125f );

    string text;
    SW_REQUIRE_TRUE( instance->getParameter( hashed_string( "_color" ), text ) );
    SW_EXPECT_EQUAL( string( "0.25 0.5 0.75 1.5" ), text );
    SW_REQUIRE_TRUE( instance->getParameter( hashed_string( "_roughness" ), text ) );
    SW_EXPECT_EQUAL( string( "0.125" ), text );
    SW_EXPECT_TRUE( instance->isParameterOverridden( hashed_string( "_color" ) ) );

    // 모양이 다른 값은 패킹 때 알리고 버린다.
    instance->setScalarParameter( hashed_string( "_color" ), 0.5f );
    SW_EXPECT_LOG( "Material.ParameterShapeMismatch" );
    (void)instance->getRenderBinding();
    SW_EXPECT_FALSE( instance->isParameterOverridden( hashed_string( "_color" ) ) );
}

/**
 * @brief [MaterialInstanceTest] 텍스처 칸 수(빈 칸 포함)가 셰이더 define MATERIAL_TEXTURE_COUNT 로 가고 인스턴스도 같은 값이다 — 웹 에뮬 셰이더 변형이 그 칸만 선언한다
 * @details 칸 셋 머티리얼의 웹 변형은 머티리얼 칸 t16..t18 만 남긴다(binding_texture.hlsli SW_MATERIAL_TEXTURE_SLOT_LIMIT). 텍스처 없는 머티리얼은 0 칸이다.
 *          웹 머티리얼 칸 예산과 패스 엔진 텍스처 몫의 합은 단계 텍스처 한도 안이다(넘는 머티리얼은 쿠킹이 #error 로 멈춘다).
 */
SW_TEST_CASE( MaterialInstanceTest, TextureSlotCountReachesShaderDefine )
{
    shared_ptr<Material> textured = Material::create();
    SW_REQUIRE_TRUE( textured->loadFromXML( MaterialInstanceTestUtil::kTexturedMaterial ) );
    SW_EXPECT_EQUAL( 3u, textured->getTextureSlotCount() );
    const string expected = string( kMaterialTextureCountDefine ) + "=3";
    SW_EXPECT_TRUE( VectorUtil::contains( textured->getCachedShaderDefines(), expected ) );
    shared_ptr<MaterialInstance> instance = MaterialInstance::create( textured.get() );
    SW_EXPECT_TRUE( VectorUtil::contains( instance->getCachedShaderDefines(), expected ) );

    shared_ptr<Material> plain = Material::create();
    SW_REQUIRE_TRUE( plain->loadFromXML( MaterialInstanceTestUtil::kMaterial ) );
    SW_EXPECT_EQUAL( 0u, plain->getTextureSlotCount() );
    SW_EXPECT_TRUE( VectorUtil::contains( plain->getCachedShaderDefines(), string( kMaterialTextureCountDefine ) + "=0" ) );
    SW_EXPECT_TRUE( plain->getPermutationHash() != textured->getPermutationHash() );

    SW_EXPECT_TRUE( textured->getTextureSlotCount() <= shaderslot::wgpu::kMaterialTextureBudget );
    SW_EXPECT_TRUE( shaderslot::wgpu::kMaterialTextureBudget + shaderslot::wgpu::kMaterialStageEngineTextureReserve <= shaderslot::wgpu::kStageTextureBudget );
}
