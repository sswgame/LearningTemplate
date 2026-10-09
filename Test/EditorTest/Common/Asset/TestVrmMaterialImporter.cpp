#include "pch.h"

#include "Core/String/hashed_string.h"

#include "Editor/Common/Asset/VrmMaterialImporter.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Serialization/Json/JsonDocument.h"

#include "TestFramework/TestFramework.h"

// VrmMaterialImporterTest — VRM 의 MToon 값(0.x materialProperties · 1.0 VRMC_materials_mtoon)이 엔진 툰 머티리얼 파라미터로 옮겨지는지.

namespace
{
    struct TestVrmMaterialImporterInternal
    {
        /** @brief VRoid 0.x 얼굴 머티리얼 모양의 materialProperties 원소입니다(AvatarSample_D 의 값을 줄인 것). */
        static constexpr const utf8* kVrm0Face = R"({
  "name": "Face", "shader": "VRM/MToon", "renderQueue": 2450,
  "floatProperties": { "_Cutoff": 0.4, "_BlendMode": 1, "_CullMode": 0, "_ShadeShift": -0.3, "_ShadeToony": 0.0,
                       "_ReceiveShadowRate": 0.5, "_BumpScale": 1, "_ShadingGradeRate": 1, "_IndirectLightIntensity": 0.1,
                       "_RimFresnelPower": 3, "_RimLift": 0.1, "_RimLightingMix": 0.5,
                       "_OutlineWidth": 0.075, "_OutlineWidthMode": 1, "_OutlineScaledMaxDistance": 4.63, "_OutlineColorMode": 0,
                       "_OutlineLightingMix": 1, "_OutlineCullMode": 1, "_SrcBlend": 1, "_DstBlend": 0, "_ZWrite": 1, "_DebugMode": 0 },
  "vectorProperties": { "_Color": [1, 1, 1, 1], "_ShadeColor": [0.5, 0.25, 1, 1], "_EmissionColor": [0, 0, 0, 1],
                        "_RimColor": [1, 0.5, 0, 1], "_OutlineColor": [0.2, 0.1, 0.1, 1], "_MainTex": [0, 0, 1, 1] },
  "textureProperties": { "_MainTex": 3, "_ShadeTexture": 3, "_SphereAdd": 5, "_EmissionMap": 4, "_BumpMap": 1 },
  "keywordMap": { "_NORMALMAP": true }, "tagMap": { "RenderType": "TransparentCutout" } })";

        /** @brief VRM 1.0 머티리얼(glTF 머티리얼 + VRMC_materials_mtoon)입니다. */
        static constexpr const utf8* kMtoon1Hair = R"({
  "name": "Hair", "alphaMode": "MASK", "alphaCutoff": 0.3, "doubleSided": true,
  "pbrMetallicRoughness": { "baseColorFactor": [0.2, 0.3, 0.4, 1.0], "baseColorTexture": { "index": 2 } },
  "emissiveFactor": [0.1, 0.2, 0.3], "emissiveTexture": { "index": 6 },
  "extensions": {
    "KHR_materials_emissive_strength": { "emissiveStrength": 2.5 },
    "VRMC_materials_mtoon": { "specVersion": "1.0", "shadeColorFactor": [0.1, 0.1, 0.2], "shadeMultiplyTexture": { "index": 2 },
      "shadingShiftFactor": -0.2, "shadingToonyFactor": 0.95, "matcapFactor": [0.5, 0.5, 0.5], "matcapTexture": { "index": 7 },
      "parametricRimColorFactor": [0.3, 0.2, 0.1], "parametricRimFresnelPowerFactor": 4, "parametricRimLiftFactor": 0.2, "rimLightingMixFactor": 0.25,
      "outlineWidthMode": "screenCoordinates", "outlineWidthFactor": 0.004, "outlineColorFactor": [0.05, 0.0, 0.1], "outlineLightingMixFactor": 0.75,
      "uvAnimationScrollXSpeedFactor": 0.5 } } })";

        static sw::JsonValue parse( sw::JsonDocument& document, const utf8* pText )
        {
            SW_EXPECT_TRUE( document.parse( pText ) );
            return document.getRoot();
        }
    };
} // namespace

/**
 * @brief [VrmMaterialImporterTest] VRM 0.x MToon 값이 툰 머티리얼로 옮겨진다 — 색은 감마 → 선형, 계단은 같은 경계, 외곽선은 cm → m, 블렌드 · 컬 · 텍스처 번호
 */
SW_TEST_CASE( VrmMaterialImporterTest, Vrm0MToonValuesMapToToonParameters )
{
    using Internal = TestVrmMaterialImporterInternal;
    sw::JsonDocument             document;
    sw::JsonDocument             gltfDocument;
    sw::editor::ToonMaterialDesc desc;
    sw::vector<sw::string>       listIgnored;
    sw::string                   error;
    SW_ASSERT_TRUE_MSG( sw::editor::VrmMaterialImporter::readVrm0Material( Internal::parse( document, Internal::kVrm0Face ), Internal::parse( gltfDocument, "{}" ),
                                                                           desc, &listIgnored, error ),
                        error.c_str() );

    SW_EXPECT_STREQ( "Face", desc._name.c_str() );
    // 감마 0.5 → 선형 0.214, 0.25 → 0.0508. 알파는 그대로다.
    SW_EXPECT_NEAR_EQUAL( 0.21404f, desc._shadeColor._x, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 0.05087f, desc._shadeColor._y, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, desc._shadeColor._z, 1e-4f );
    SW_EXPECT_NEAR_EQUAL( 1.0f, desc._rimColor._x, 1e-4f );
    // 0.x 구간 [-0.3, lerp(1, -0.3, 0) = 1] → 1.0 shift = -(-0.3 + 1) / 2 = -0.35, toony = 1 - 1.3 / 2 = 0.35.
    SW_EXPECT_NEAR_EQUAL( -0.35f, desc._shadingShift, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.35f, desc._shadingToony, 1e-5f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, desc._shadowReceive, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, desc._rimFresnelPower, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.1f, desc._rimLift, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, desc._rimLightingMix, 1e-6f );
    // 월드 외곽선 0.075 cm → 0.00075 m, 고정 색(_OutlineColorMode 0)은 빛을 섞지 않는다.
    SW_EXPECT_TRUE( desc._outlineMode == sw::editor::ToonOutlineMode::World );
    SW_EXPECT_NEAR_EQUAL( 0.00075f, desc._outlineWidth, 1e-7f );
    SW_EXPECT_NEAR_EQUAL( 0.0f, desc._outlineLightingMix, 1e-6f );
    SW_EXPECT_TRUE( desc._alphaMode == sw::editor::ToonAlphaMode::Cutout );
    SW_EXPECT_NEAR_EQUAL( 0.4f, desc._alphaCutoff, 1e-6f );
    SW_EXPECT_TRUE( desc._bTwoSided == SW_TRUE );
    SW_EXPECT_EQUAL( 3, desc._baseColorTexture );
    SW_EXPECT_EQUAL( 3, desc._shadeTexture );
    SW_EXPECT_EQUAL( 4, desc._emissiveTexture );
    SW_EXPECT_EQUAL( 5, desc._matcapTexture );

    // 아는데 옮기지 않는 키는 목록에 남는다(노멀 맵). 효과 없는 값(_BumpScale 1 · 블렌드 상태 · _OutlineCullMode 1)은 남지 않는다.
    SW_EXPECT_EQUAL( size_t( 1 ), listIgnored.size() );
    SW_EXPECT_TRUE( listIgnored.empty() == false && listIgnored[0] == "_BumpMap" );
}

/**
 * @brief [VrmMaterialImporterTest] VRM 0.x 의 화면 외곽선은 화면 높이 비율(× 0.005)이고, 0.x 의 `_ShadeToony` 1 은 칼같은 계단(toony 1)이다
 */
SW_TEST_CASE( VrmMaterialImporterTest, Vrm0ScreenOutlineAndHardStep )
{
    using Internal = TestVrmMaterialImporterInternal;
    sw::JsonDocument             document;
    sw::JsonDocument             gltfDocument;
    sw::editor::ToonMaterialDesc desc;
    sw::string                   error;
    const utf8*                  pText = R"({ "name": "Hair", "shader": "VRM/MToon",
      "floatProperties": { "_ShadeShift": 0.2, "_ShadeToony": 1.0, "_OutlineWidth": 0.4, "_OutlineWidthMode": 2, "_OutlineColorMode": 1,
                           "_OutlineLightingMix": 0.6, "_OutlineScaledMaxDistance": 3 } })";
    SW_ASSERT_TRUE_MSG( sw::editor::VrmMaterialImporter::readVrm0Material( Internal::parse( document, pText ), Internal::parse( gltfDocument, "{}" ), desc, nullptr, error ),
                        error.c_str() );
    SW_EXPECT_TRUE( desc._outlineMode == sw::editor::ToonOutlineMode::Screen );
    SW_EXPECT_NEAR_EQUAL( 0.002f, desc._outlineWidth, 1e-7f );
    SW_EXPECT_NEAR_EQUAL( 3.0f, desc._outlineMaxDistance, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.6f, desc._outlineLightingMix, 1e-6f );
    // 구간 [0.2, lerp(1, 0.2, 1) = 0.2] — 폭 0 이면 계단 하나다.
    SW_EXPECT_NEAR_EQUAL( 1.0f, desc._shadingToony, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( -0.2f, desc._shadingShift, 1e-6f );
    SW_EXPECT_TRUE( desc._bTwoSided == SW_FALSE ); // _CullMode 가 없으면 후면 컬링(2)이다
}

/**
 * @brief [VrmMaterialImporterTest] VRM 0.x 의 모르는 키 · 셰이더 · 값은 오류다 — 경고로 넘기면 철자가 틀린 키가 조용히 기본값이 된다
 */
SW_TEST_CASE( VrmMaterialImporterTest, Vrm0UnknownNamesAreErrors )
{
    using Internal = TestVrmMaterialImporterInternal;
    struct ErrorCase
    {
        const utf8* _pText;
        const utf8* _pExpected;
    };
    const ErrorCase kArrCase[] = {
        {       R"({ "name": "A", "shader": "VRM/MToon", "floatProperties": { "_ShadeShiftt": 0 } })",    "_ShadeShiftt"},
        {R"({ "name": "A", "shader": "VRM/MToon", "vectorProperties": { "_Colour": [1, 1, 1, 1] } })",         "_Colour"},
        {       R"({ "name": "A", "shader": "VRM/MToon", "textureProperties": { "_DetailTex": 1 } })",      "_DetailTex"},
        {                           R"({ "name": "A", "shader": "VRM/MToon", "materialVersion": 2 })", "materialVersion"},
        {                                                  R"({ "name": "A", "shader": "Standard" })",        "Standard"},
        {          R"({ "name": "A", "shader": "VRM/MToon", "floatProperties": { "_CullMode": 1 } })",       "_CullMode"},
    };
    for ( const ErrorCase& errorCase : kArrCase )
    {
        sw::JsonDocument             document;
        sw::JsonDocument             gltfDocument;
        sw::editor::ToonMaterialDesc desc;
        sw::string                   error;
        SW_EXPECT_FALSE_MSG( sw::editor::VrmMaterialImporter::readVrm0Material( Internal::parse( document, errorCase._pText ), Internal::parse( gltfDocument, "{}" ),
                                                                                desc, nullptr, error ),
                             errorCase._pExpected );
        SW_EXPECT_TRUE_MSG( error.find( errorCase._pExpected ) != sw::string::npos, ( sw::string( "error does not name the key: " ) + error ).c_str() );
    }
}

/**
 * @brief [VrmMaterialImporterTest] VRM 1.0 VRMC_materials_mtoon 은 이름 · 단위가 엔진과 같아 그대로 옮겨진다(선형 색) — glTF 기본 값(알파 · 양면 · 발광 세기)도 함께
 */
SW_TEST_CASE( VrmMaterialImporterTest, Mtoon1ValuesMapDirectly )
{
    using Internal = TestVrmMaterialImporterInternal;
    sw::JsonDocument             document;
    sw::editor::ToonMaterialDesc desc;
    sw::vector<sw::string>       listIgnored;
    sw::string                   error;
    SW_ASSERT_TRUE_MSG( sw::editor::VrmMaterialImporter::readMtoon1Material( Internal::parse( document, Internal::kMtoon1Hair ), desc, &listIgnored, error ), error.c_str() );

    SW_EXPECT_NEAR_EQUAL( 0.3f, desc._baseColor._y, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.2f, desc._shadeColor._z, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.2f, desc._emissiveColor._y, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 2.5f, desc._emissiveStrength, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( -0.2f, desc._shadingShift, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.95f, desc._shadingToony, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.5f, desc._matcapColor._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.3f, desc._rimColor._x, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 4.0f, desc._rimFresnelPower, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.2f, desc._rimLift, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.25f, desc._rimLightingMix, 1e-6f );
    SW_EXPECT_TRUE( desc._outlineMode == sw::editor::ToonOutlineMode::Screen );
    SW_EXPECT_NEAR_EQUAL( 0.004f, desc._outlineWidth, 1e-7f );
    SW_EXPECT_NEAR_EQUAL( 0.1f, desc._outlineColor._z, 1e-6f );
    SW_EXPECT_NEAR_EQUAL( 0.75f, desc._outlineLightingMix, 1e-6f );
    SW_EXPECT_TRUE( desc._alphaMode == sw::editor::ToonAlphaMode::Cutout );
    SW_EXPECT_NEAR_EQUAL( 0.3f, desc._alphaCutoff, 1e-6f );
    SW_EXPECT_TRUE( desc._bTwoSided == SW_TRUE );
    SW_EXPECT_EQUAL( 2, desc._baseColorTexture );
    SW_EXPECT_EQUAL( 2, desc._shadeTexture );
    SW_EXPECT_EQUAL( 6, desc._emissiveTexture );
    SW_EXPECT_EQUAL( 7, desc._matcapTexture );
    // UV 스크롤은 아는데 옮기지 않는 키다.
    SW_EXPECT_TRUE( listIgnored.size() == 1 && listIgnored[0] == "uvAnimationScrollXSpeedFactor" );

    // 모르는 키는 오류다.
    sw::JsonDocument             badDocument;
    sw::editor::ToonMaterialDesc badDesc;
    const utf8*                  pBad = R"({ "name": "B", "extensions": { "VRMC_materials_mtoon": { "specVersion": "1.0", "shadingToonyFactr": 0.5 } } })";
    SW_EXPECT_FALSE( sw::editor::VrmMaterialImporter::readMtoon1Material( Internal::parse( badDocument, pBad ), badDesc, nullptr, error ) );
    SW_EXPECT_TRUE( error.find( "shadingToonyFactr" ) != sw::string::npos );
}

/**
 * @brief [VrmMaterialImporterTest] 만든 `.material` 이 엔진 툰 머티리얼로 읽힌다 — 값 · 텍스처 경로 · 정적 스위치(외곽선 · 컷오프 · 양면) · 반투명 블렌드
 */
SW_TEST_CASE( VrmMaterialImporterTest, MaterialXmlLoadsAsToonMaterial )
{
    sw::editor::ToonMaterialDesc desc;
    desc._name                                   = "Hair";
    desc._shadingToony                           = 0.75f;
    desc._outlineMode                            = sw::editor::ToonOutlineMode::World;
    desc._outlineWidth                           = 0.002f;
    desc._alphaMode                              = sw::editor::ToonAlphaMode::Cutout;
    desc._bTwoSided                              = SW_TRUE;
    desc._baseColorTexture                       = 1;
    const sw::vector<sw::string> listTexturePath = { "", "game/test/textures/hair.dds" };

    sw::shared_ptr<sw::Material> material = sw::Material::create();
    SW_ASSERT_TRUE( material->loadFromXml( sw::editor::VrmMaterialImporter::makeMaterialXml( desc, listTexturePath ) ) );
    SW_EXPECT_STREQ( "engine/shaders/toon.hlsl", material->getShaderPath().c_str() );
    float32 toony{ 0.0f };
    SW_EXPECT_TRUE( material->getScalarParameter( sw::hashed_string( "shadingToony" ), toony ) );
    SW_EXPECT_NEAR_EQUAL( 0.75f, toony, 1e-5f );
    const sw::MaterialProperty* pBaseMap = material->findProperty( sw::hashed_string( "baseColorMap" ) );
    SW_ASSERT_NOT_NULL( pBaseMap );
    SW_EXPECT_STREQ( "game/test/textures/hair.dds", pBaseMap->_assetPath.c_str() );
    const sw::vector<sw::string>& listDefine = material->getCachedShaderDefines();
    auto                          hasDefine  = [&listDefine]( const utf8* pDefine )
    { return std::find( listDefine.begin(), listDefine.end(), sw::string( pDefine ) ) != listDefine.end(); };
    SW_EXPECT_TRUE( hasDefine( "MATERIAL_OUTLINE" ) );
    SW_EXPECT_TRUE( hasDefine( "MATERIAL_ALPHA_CUTOFF" ) );
    SW_EXPECT_TRUE( hasDefine( "MATERIAL_TWO_SIDED" ) );
    SW_EXPECT_FALSE( hasDefine( "MATERIAL_BLEND_TRANSLUCENT" ) );
    SW_EXPECT_TRUE( material->getBlendMode() == sw::RHIBlendMode::Opaque );

    // 반투명은 블렌드 모드 + MATERIAL_BLEND_TRANSLUCENT 이고 외곽선 패스에는 들어가지 않는다(불투명 목록만 그린다).
    desc._alphaMode                    = sw::editor::ToonAlphaMode::Transparent;
    sw::shared_ptr<sw::Material> glass = sw::Material::create();
    SW_ASSERT_TRUE( glass->loadFromXml( sw::editor::VrmMaterialImporter::makeMaterialXml( desc, listTexturePath ) ) );
    const sw::vector<sw::string>& listGlassDefine = glass->getCachedShaderDefines();
    SW_EXPECT_TRUE( std::find( listGlassDefine.begin(), listGlassDefine.end(), sw::string( "MATERIAL_BLEND_TRANSLUCENT" ) ) != listGlassDefine.end() );
    SW_EXPECT_TRUE( std::find( listGlassDefine.begin(), listGlassDefine.end(), sw::string( "MATERIAL_OUTLINE" ) ) == listGlassDefine.end() );
    SW_EXPECT_TRUE( glass->getBlendMode() == sw::RHIBlendMode::Transparent );
}
