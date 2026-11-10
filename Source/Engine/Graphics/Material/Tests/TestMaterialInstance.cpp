#include "pch.h"

#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Material/MaterialRenderProxy.h"

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
