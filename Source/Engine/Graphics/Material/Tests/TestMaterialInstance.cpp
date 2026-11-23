#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Material/MaterialPermutation.h"
#include "Engine/Graphics/Material/MaterialRenderProxy.h"
#include "Engine/Graphics/Material/MaterialTypes.h"
#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

// MaterialInstance — 값 덮어쓰기의 사본 재사용과 조회 글, 런타임 스위치 검사와 정적 종류 거절(nogpu).

using namespace sw;

namespace
{
    struct MaterialInstanceTestUtil
    {
        /** @brief define 목록 @p listDefine 에 @p text 가 있는지 봅니다(글 형식이 달라도 내용으로 견준다 — `sw::string` 과 표준 글의 `==` 는 모호하다). */
        template <typename DefineList>
        static bool hasDefine( const DefineList& listDefine, string_view text )
        {
            for ( const auto& define : listDefine )
            {
                if ( string_view( define.c_str() ) == text )
                    return true;
            }
            return false;
        }

        static constexpr const utf8* kMaterial = R"(<?xml version="1.0" encoding="utf-8"?>
<MaterialDef formatVersion="0" name="Sample" shaderPath="engine/shaders/pbr_lit.hlsl" surfaceModel="Lit" alphaMode="Opaque">
	<_properties>
		<item name="_color" type="Color" defaultValue="1 1 1 1"/>
		<item name="_roughness" type="Range" defaultValue="1" min="0.0" max="1.0"/>
	</_properties>
</MaterialDef>
)";

        static constexpr const utf8* kTexturedMaterial = R"(<?xml version="1.0" encoding="utf-8"?>
<MaterialDef formatVersion="0" name="Textured" shaderPath="engine/shaders/pbr_lit.hlsl" surfaceModel="Lit" alphaMode="Opaque">
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
    const MaterialRenderBinding first = instance->getRenderBinding( MaterialRenderInputs::capture() );
    SW_REQUIRE_TRUE( first._data != nullptr );

    instance->setVectorParameter( colorName, float4{ 0.25f, 0.5f, 0.75f, 1.0f } );
    instance->setScalarParameter( valueName, 0.125f );
    const MaterialRenderBinding unchanged = instance->getRenderBinding( MaterialRenderInputs::capture() );
    SW_EXPECT_TRUE( first._data == unchanged._data );

    instance->setScalarParameter( valueName, 0.5f );
    const MaterialRenderBinding changed = instance->getRenderBinding( MaterialRenderInputs::capture() );
    SW_EXPECT_TRUE( unchanged._data != changed._data );
    const MaterialProperty* pRoughness = material->findProperty( valueName );
    SW_REQUIRE_TRUE( pRoughness != nullptr );
    float32 roughness{ 0.0f };
    Memory::copy( &roughness, changed._data->_bytes.data() + pRoughness->_offset, sizeof( roughness ) );
    SW_EXPECT_NEAR( 0.5f, roughness, 1e-6f );
}

/**
 * @brief [MaterialInstanceTest] 값 하나만 바뀐 사본은 그 칸만 다시 쓴다 — 다른 덮어쓰기 · 부모 칸은 앞 사본 그대로이고, 거절된 값의 칸은 앞 값이 아니라 부모 값이다
 * @details 변이: 바뀐 칸만 쓰는 길이 칸을 부모 값으로 되돌리지 않으면 모양이 어긋나 버린 `_color` 가 앞 사본의 값(0.25)으로 남는다. 부모 값이 바뀐 뒤에도 앞 사본을 베끼면 새 부모 값 줄이 진다.
 */
SW_TEST_CASE( MaterialInstanceTest, ChangedValuePatchesOnlyItsProperty )
{
    shared_ptr<Material> material = Material::create();
    SW_REQUIRE_TRUE( material->loadFromXML( MaterialInstanceTestUtil::kMaterial ) );
    shared_ptr<MaterialInstance> instance = MaterialInstance::create( material.get() );
    const hashed_string          colorName( "_color" );
    const hashed_string          roughnessName( "_roughness" );
    const MaterialProperty*      pColor     = material->findProperty( colorName );
    const MaterialProperty*      pRoughness = material->findProperty( roughnessName );
    SW_REQUIRE_TRUE( pColor != nullptr && pRoughness != nullptr );
    const auto readFloat = []( const MaterialRenderBinding& binding, uint32 offset )
    {
        float32 value{ -1.0f };
        if ( offset + sizeof( value ) <= binding._data->_bytes.size() )
            Memory::copy( &value, binding._data->_bytes.data() + offset, sizeof( value ) );
        return value;
    };

    instance->setVectorParameter( colorName, float4{ 0.25f, 0.5f, 0.75f, 1.0f } );
    instance->setScalarParameter( roughnessName, 0.125f );
    const MaterialRenderBinding first = instance->getRenderBinding( MaterialRenderInputs::capture() );
    SW_REQUIRE_TRUE( first._data != nullptr );

    instance->setScalarParameter( roughnessName, 0.5f );
    const MaterialRenderBinding second = instance->getRenderBinding( MaterialRenderInputs::capture() );
    SW_REQUIRE_TRUE( second._data != nullptr );
    SW_EXPECT_NEAR( 0.25f, readFloat( second, pColor->_offset ), 1e-6f );
    SW_EXPECT_NEAR( 0.5f, readFloat( second, pRoughness->_offset ), 1e-6f );
    SW_EXPECT_NEAR( 0.125f, readFloat( first, pRoughness->_offset ), 1e-6f ); // 건넨 사본은 불변이다

    // 부모의 값이 바뀌면 덮지 않은 칸이 따라온다(앞 사본을 베끼지 않는다).
    instance->clearOverrides();
    instance->setScalarParameter( roughnessName, 0.5f );
    SW_REQUIRE_TRUE( material->setParameter( colorName, "0.125 0.5 0.75 1" ) );
    const MaterialRenderBinding third = instance->getRenderBinding( MaterialRenderInputs::capture() );
    SW_REQUIRE_TRUE( third._data != nullptr );
    SW_EXPECT_NEAR( 0.125f, readFloat( third, pColor->_offset ), 1e-6f );
    SW_EXPECT_NEAR( 0.5f, readFloat( third, pRoughness->_offset ), 1e-6f );

    // 모양이 어긋난 값은 알리고 버린다 — 칸은 앞 덮어쓰기가 아니라 부모 값이다.
    instance->setVectorParameter( colorName, float4{ 0.75f, 0.75f, 0.75f, 1.0f } );
    (void)instance->getRenderBinding( MaterialRenderInputs::capture() );
    SW_EXPECT_LOG( "Material.ParameterShapeMismatch" );
    instance->setScalarParameter( colorName, 0.5f );
    const MaterialRenderBinding dropped = instance->getRenderBinding( MaterialRenderInputs::capture() );
    SW_REQUIRE_TRUE( dropped._data != nullptr );
    SW_EXPECT_NEAR( 0.125f, readFloat( dropped, pColor->_offset ), 1e-6f );
    SW_EXPECT_FALSE( instance->isParameterOverridden( colorName ) );
    SW_EXPECT_NEAR( 0.5f, readFloat( dropped, pRoughness->_offset ), 1e-6f );
}

/**
 * @brief [MaterialInstanceTest] 프로퍼티의 자리(`MaterialParameterHandle`)는 값이 바뀌는 동안 그대로이고, 다시 읽어 배치가 바뀌면 낡아 아무것도 내지 않는다(다시 찾으면 새 번호다)
 */
SW_TEST_CASE( MaterialInstanceTest, ParameterHandleFollowsTheLayoutGeneration )
{
    shared_ptr<Material> material = Material::create();
    SW_REQUIRE_TRUE( material->loadFromXML( MaterialInstanceTestUtil::kMaterial ) );
    const hashed_string           roughnessName( "_roughness" );
    const MaterialParameterHandle handle = material->findParameter( roughnessName );
    SW_REQUIRE_TRUE( material->findProperty( handle ) != nullptr );
    SW_EXPECT_EQUAL( string( "_roughness" ), material->findProperty( handle )->_name );
    SW_EXPECT_TRUE( material->findProperty( material->findParameter( hashed_string( "_missing" ) ) ) == nullptr );
    SW_EXPECT_TRUE( material->findProperty( MaterialParameterHandle{} ) == nullptr );

    SW_REQUIRE_TRUE( material->setParameter( roughnessName, "0.25" ) );
    SW_EXPECT_TRUE( material->findProperty( handle ) != nullptr );

    SW_REQUIRE_TRUE( material->loadFromXML( MaterialInstanceTestUtil::kTexturedMaterial ) );
    SW_EXPECT_TRUE( material->findProperty( handle ) == nullptr );
    MaterialParameterHandle renewed = handle;
    const MaterialProperty* pColor  = material->resolveProperty( hashed_string( "_color" ), renewed );
    SW_REQUIRE_TRUE( pColor != nullptr );
    SW_EXPECT_EQUAL( string( "_color" ), pColor->_name );
}

/**
 * @brief [MaterialInstanceTest] 이진으로 든 값도 조회의 글은 공백으로 나눈 최단 왕복 표현이다
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
    (void)instance->getRenderBinding( MaterialRenderInputs::capture() );
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
    fixed_string<constant::buffer::kMax64> expected;
    formatstring( expected.data(), expected.capacity(), "%#=3", kMaterialTextureCountDefine );
    SW_EXPECT_TRUE( MaterialInstanceTestUtil::hasDefine( textured->getCachedShaderDefines(), expected ) );
    shared_ptr<MaterialInstance> instance = MaterialInstance::create( textured.get() );
    SW_EXPECT_TRUE( MaterialInstanceTestUtil::hasDefine( instance->getCachedShaderDefines(), expected ) );

    shared_ptr<Material> plain = Material::create();
    SW_REQUIRE_TRUE( plain->loadFromXML( MaterialInstanceTestUtil::kMaterial ) );
    SW_EXPECT_EQUAL( 0u, plain->getTextureSlotCount() );
    fixed_string<constant::buffer::kMax64> expectedPlain;
    formatstring( expectedPlain.data(), expectedPlain.capacity(), "%#=0", kMaterialTextureCountDefine );
    SW_EXPECT_TRUE( MaterialInstanceTestUtil::hasDefine( plain->getCachedShaderDefines(), expectedPlain ) );
    SW_EXPECT_TRUE( plain->getPermutationHash() != textured->getPermutationHash() );

    SW_EXPECT_TRUE( textured->getTextureSlotCount() <= shaderslot::wgpu::kMaterialTextureBudget );
    SW_EXPECT_TRUE( shaderslot::wgpu::kMaterialTextureBudget + shaderslot::wgpu::kMaterialStageEngineTextureReserve <= shaderslot::wgpu::kStageTextureBudget );
}

/**
 * @brief [MaterialInstanceTest] 패스 PSO 의 칸 0(머티리얼 없는 배치) 위에 머티리얼 변형의 칸 수를 얹으면 같은 자리의 값이 바뀐다 — 같은 이름 define 이 둘이 되지 않는다
 * @details 런타임 PSO 변형과 쿠커가 같은 함수(MaterialPermutation::mergeDefine)로 합쳐 같은 목록 · 같은 차례다.
 */
SW_TEST_CASE( MaterialInstanceTest, TextureCountDefineReplacesPassValue )
{
    vector<string> listDefine{ "SW_PASS_FORWARD", MaterialPermutation::makeTextureCountDefine( 0 ), "SW_PASS_VELOCITY" };
    MaterialPermutation::mergeDefine( listDefine, MaterialPermutation::makeTextureCountDefine( 3 ) );
    MaterialPermutation::mergeDefine( listDefine, "MATERIAL_ALPHA_CUTOFF" );
    SW_REQUIRE_EQUAL( size_t( 4 ), listDefine.size() );
    SW_EXPECT_EQUAL( MaterialPermutation::makeTextureCountDefine( 3 ), listDefine[1] );
    SW_EXPECT_EQUAL( string( "MATERIAL_ALPHA_CUTOFF" ), listDefine[3] );
}

namespace
{
    struct MaterialInstanceSwitchTestUtil
    {
        /** @brief 런타임 스위치(Detail, 끔 키워드 있음) · 에셋 전용 스위치(Wet)를 선언한 부모입니다. */
        static constexpr const utf8* kSwitchMaterial = R"(<?xml version="1.0" encoding="utf-8"?>
<MaterialDef formatVersion="0" name="Switches" shaderPath="engine/shaders/pbr_lit.hlsl" surfaceModel="Lit" alphaMode="Opaque">
	<_properties>
		<item name="_color" type="Color" defaultValue="1 1 1 1"/>
	</_properties>
	<_permutations quality="High" shaderLOD="300" usage="StaticMesh">
		<_staticSwitches>
			<item name="Detail" keyword="MATERIAL_DETAIL" keywordOff="MATERIAL_PLAIN" bEnabled="0" bShaderFeature="0"/>
			<item name="Wet" keyword="MATERIAL_WET" bEnabled="0" bShaderFeature="1"/>
		</_staticSwitches>
	</_permutations>
</MaterialDef>
)";
    };
} // namespace

/**
 * @brief [MaterialInstanceTest] 키워드는 부모가 선언한 런타임 스위치만 받는다 — 모르는 이름 · 에셋 전용 스위치는 오류이고 퍼뮤테이션이 그대로다
 * @details 변이: 이름 검사를 지우면 `MATERIAL_BENCH_CHURN` 같은 쿠킹하지 않은 define 이 퍼뮤테이션에 섞여(배포에서 대체 해시) 해시 줄이 진다.
 */
SW_TEST_CASE( MaterialInstanceTest, KeywordsMustBeDeclaredByParent )
{
    SW_EXPECT_LOG( "MaterialInstance.KeywordUnknown" );
    SW_EXPECT_LOG( "MaterialInstance.KeywordNotRuntime" );
    shared_ptr<Material> material = Material::create();
    SW_REQUIRE_TRUE( material->loadFromXML( MaterialInstanceSwitchTestUtil::kSwitchMaterial ) );
    shared_ptr<MaterialInstance> instance = MaterialInstance::create( material.get() );
    const uint64                 baseHash = instance->getPermutationHash();
    SW_EXPECT_EQUAL( material->getPermutationHash(), baseHash );

    SW_EXPECT_FALSE( instance->enableKeyword( hashed_string( "MATERIAL_BENCH_CHURN" ) ) );
    SW_EXPECT_FALSE( instance->enableKeyword( hashed_string( "Wet" ) ) );
    SW_EXPECT_EQUAL( baseHash, instance->getPermutationHash() );

    shared_ptr<MaterialInstance> orphan = MaterialInstance::create( nullptr );
    SW_EXPECT_FALSE( orphan->enableKeyword( hashed_string( "Detail" ) ) );

    // 이름으로 켜도 키워드로 켜도 같은 스위치 한 칸이고, 부모가 그 스위치를 켠 것과 같은 define · 해시다(쿠커가 굽는 조합).
    SW_EXPECT_TRUE( instance->enableKeyword( hashed_string( "MATERIAL_DETAIL" ) ) );
    SW_EXPECT_TRUE( MaterialInstanceTestUtil::hasDefine( instance->getCachedShaderDefines(), "MATERIAL_DETAIL" ) );
    SW_EXPECT_FALSE( MaterialInstanceTestUtil::hasDefine( instance->getCachedShaderDefines(), "MATERIAL_PLAIN" ) ); // 덮어쓰기는 define 글이 아니라 부모 칸의 값이다
    SW_REQUIRE_TRUE( material->setStaticSwitch( hashed_string( "Detail" ), true ) );
    shared_ptr<MaterialInstance> plain = MaterialInstance::create( material.get() );
    SW_EXPECT_EQUAL( plain->getPermutationHash(), instance->getPermutationHash() );
    SW_EXPECT_TRUE( instance->disableKeyword( hashed_string( "Detail" ) ) );
    SW_EXPECT_FALSE( MaterialInstanceTestUtil::hasDefine( instance->getCachedShaderDefines(), "MATERIAL_DETAIL" ) );
}

namespace
{
    struct MaterialInstanceKeywordValueTestUtil
    {
        /** @brief 키워드를 내는 Bool 칸(`_detailMask` → `MATERIAL_DETAIL_MASK`, 기본 끔)을 선언한 부모입니다. */
        static constexpr const utf8* kKeywordValueMaterial = R"(<?xml version="1.0" encoding="utf-8"?>
<MaterialDef formatVersion="0" name="KeywordValue" shaderPath="engine/shaders/pbr_lit.hlsl" surfaceModel="Lit" alphaMode="Opaque">
	<_properties>
		<item name="_color" type="Color" defaultValue="1 1 1 1"/>
		<item name="_detailMask" type="Bool" defaultValue="0" shaderKeyword="MATERIAL_DETAIL_MASK"/>
	</_properties>
</MaterialDef>
)";
    };
} // namespace

/**
 * @brief [MaterialInstanceTest] 키워드를 내는 Bool 칸은 값 API 가 오류로 거절해 define · 해시가 그대로다. 런타임 전환은 부모가 선언한 런타임 스위치(`enableKeyword`)로, 고정한 값은 자식 머티리얼로만 된다
 * @details 변이: 값 API 가 키워드 칸을 받으면(예전) 쿠킹하지 않은 `MATERIAL_DETAIL_MASK` 조합이 생겨 해시 줄이 진다.
 */
SW_TEST_CASE( MaterialInstanceTest, KeywordValueIsRejected )
{
    SW_EXPECT_LOG( "MaterialInstance.StaticValueRejected" );
    shared_ptr<Material> material = Material::create();
    SW_REQUIRE_TRUE( material->loadFromXML( MaterialInstanceKeywordValueTestUtil::kKeywordValueMaterial ) );
    SW_EXPECT_FALSE( MaterialInstanceTestUtil::hasDefine( material->getCachedShaderDefines(), "MATERIAL_DETAIL_MASK" ) );

    shared_ptr<MaterialInstance> instance = MaterialInstance::create( material.get() );
    const uint64                 baseHash = instance->getPermutationHash();
    SW_EXPECT_FALSE( instance->setParameter( hashed_string( "_detailMask" ), "1" ) );
    instance->setScalarParameter( hashed_string( "_detailMask" ), 1.0f );
    SW_EXPECT_FALSE( instance->isParameterOverridden( hashed_string( "_detailMask" ) ) );
    SW_EXPECT_FALSE( MaterialInstanceTestUtil::hasDefine( instance->getCachedShaderDefines(), "MATERIAL_DETAIL_MASK" ) );
    SW_EXPECT_EQUAL( baseHash, instance->getPermutationHash() );
    SW_EXPECT_TRUE( instance->setParameter( hashed_string( "_color" ), "0.5 0.5 0.5 1" ) ); // 값 칸은 그대로 받는다

    // 런타임 스위치 길은 그대로다 — 부모가 선언한 `bShaderFeature="0"` 스위치는 런타임에 켜고 끈다.
    shared_ptr<Material> switches = Material::create();
    SW_REQUIRE_TRUE( switches->loadFromXML( MaterialInstanceSwitchTestUtil::kSwitchMaterial ) );
    shared_ptr<MaterialInstance> runtime = MaterialInstance::create( switches.get() );
    SW_EXPECT_TRUE( runtime->enableKeyword( hashed_string( "Detail" ) ) );
    SW_EXPECT_TRUE( MaterialInstanceTestUtil::hasDefine( runtime->getCachedShaderDefines(), "MATERIAL_DETAIL" ) );
}

namespace
{
    struct MaterialInstanceReloadTestUtil
    {
        /** @brief 런타임 스위치(Detail, 끔 · 끔 키워드 없음)와 값 칸을 선언한 부모입니다. %# 는 스위치 키워드 · 색 기본값입니다. */
        static string makeParent( const utf8* pKeyword, const utf8* pColor )
        {
            StringBuilder text;
            text.appendFormat( R"(<MaterialDef formatVersion="0" name="Reload" shaderPath="engine/shaders/pbr_lit.hlsl" surfaceModel="Lit" alphaMode="Opaque">
	<_properties>
		<item name="_color" type="Color" defaultValue="%#"/>
		<item name="_roughness" type="Range" defaultValue="1" min="0.0" max="1.0"/>
	</_properties>
	<_permutations quality="High" shaderLOD="300" usage="StaticMesh">
		<_staticSwitches>
			<item name="Detail" keyword="%#" bEnabled="0" bShaderFeature="0"/>
		</_staticSwitches>
	</_permutations>
</MaterialDef>
)",
                               pColor, pKeyword );
            return string( text.view() );
        }
    };
} // namespace

/**
 * @brief [MaterialInstanceTest] 부모를 다시 읽으면 인스턴스의 정의 · 렌더 사본이 새 부모를 따른다 — 부모 자신의 해시가 같아도(덮은 스위치의 키워드 이름만 바뀜) 옛 define 을 쥐지 않는다
 * @details 변이: 인스턴스가 부모 퍼뮤테이션 해시만 보면(예전) 끈 스위치의 키워드 이름 변경은 부모 define 을 바꾸지 않아 `MATERIAL_DETAIL_V2` 줄이 진다.
 *          덮지 않은 값 칸은 부모 바이트 세대로 다시 복사된다(`getRenderBinding`).
 */
SW_TEST_CASE( MaterialInstanceTest, ParentReloadRefreshesInstanceCaches )
{
    shared_ptr<Material> material = Material::create();
    SW_REQUIRE_TRUE( material->loadFromXML( MaterialInstanceReloadTestUtil::makeParent( "MATERIAL_DETAIL", "1 1 1 1" ) ) );
    shared_ptr<MaterialInstance> instance = MaterialInstance::create( material.get() );
    SW_REQUIRE_TRUE( instance->enableKeyword( hashed_string( "Detail" ) ) );
    instance->setScalarParameter( hashed_string( "_roughness" ), 0.25f );
    SW_EXPECT_TRUE( MaterialInstanceTestUtil::hasDefine( instance->getCachedShaderDefines(), "MATERIAL_DETAIL" ) );
    const MaterialRenderBinding before     = instance->getRenderBinding( MaterialRenderInputs::capture() );
    const uint64                parentHash = material->getPermutationHash();
    SW_REQUIRE_TRUE( before._data != nullptr );

    SW_REQUIRE_TRUE( material->loadFromXML( MaterialInstanceReloadTestUtil::makeParent( "MATERIAL_DETAIL_V2", "0.5 0.25 0.125 1" ) ) );
    SW_EXPECT_EQUAL( parentHash, material->getPermutationHash() ); // 부모 자신의 define 은 같다 — 해시만 보면 놓치는 경우다
    SW_EXPECT_TRUE( MaterialInstanceTestUtil::hasDefine( instance->getCachedShaderDefines(), "MATERIAL_DETAIL_V2" ) );
    SW_EXPECT_FALSE( MaterialInstanceTestUtil::hasDefine( instance->getCachedShaderDefines(), "MATERIAL_DETAIL" ) );

    const MaterialRenderBinding after = instance->getRenderBinding( MaterialRenderInputs::capture() );
    SW_REQUIRE_TRUE( after._data != nullptr );
    SW_EXPECT_TRUE( before._data != after._data );
    const MaterialProperty* pColor     = material->findProperty( hashed_string( "_color" ) );
    const MaterialProperty* pRoughness = material->findProperty( hashed_string( "_roughness" ) );
    SW_REQUIRE_TRUE( pColor != nullptr && pRoughness != nullptr );
    float32 arrColor[4]{};
    float32 roughness{ 0.0f };
    Memory::copy( arrColor, after._data->_bytes.data() + pColor->_offset, sizeof( arrColor ) );
    Memory::copy( &roughness, after._data->_bytes.data() + pRoughness->_offset, sizeof( roughness ) );
    SW_EXPECT_NEAR( 0.5f, arrColor[0], 1e-6f ); // 덮지 않은 칸은 새 부모 값
    SW_EXPECT_NEAR( 0.25f, roughness, 1e-6f );  // 덮은 칸은 인스턴스 값
}
