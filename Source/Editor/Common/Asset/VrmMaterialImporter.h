/**
 * @file VrmMaterialImporter.h
 * @brief VRM 의 MToon 머티리얼(0.x `extensions.VRM.materialProperties` · 1.0 `VRMC_materials_mtoon`)을 엔진 툰 머티리얼(`engine/materials/toon.material`)로 옮깁니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"

namespace sw
{
    class JSONValue;
} // namespace sw

namespace sw::editor
{
    /** @brief 외곽선 두께 모드입니다(MToon outlineWidthMode). */
    enum class ToonOutlineMode : uint8
    {
        None,
        World,  ///< 두께가 미터다
        Screen, ///< 두께가 화면 높이 비율이다(거리와 무관)
    };

    /** @brief 알파를 쓰는 방식입니다(glTF alphaMode · 0.x _BlendMode). */
    enum class ToonAlphaMode : uint8
    {
        Opaque,
        Cutout,      ///< 알파 컷오프(MATERIAL_ALPHA_CUTOFF)
        Transparent, ///< 반투명(blendMode Transparent + MATERIAL_BLEND_TRANSLUCENT)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ToonMaterialDesc
     * @brief 툰 머티리얼 하나의 값입니다. 이름 · 뜻은 `toon.hlsl` 의 머티리얼 구조체(MToon 1.0 이름)와 같습니다. 색은 선형입니다.
     * @details 텍스처는 glTF `textures[]` 번호입니다(-1 = 없음). 파일 경로는 머티리얼을 쓸 때(`makeMaterialXML`) 정합니다.
     */
    struct ToonMaterialDesc
    {
        string          _name;
        float4          _baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float4          _shadeColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        float4          _emissiveColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        float4          _rimColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        float4          _matcapColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float4          _outlineColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        float32         _shadingShift{ 0.0f };
        float32         _shadingToony{ 0.9f };
        float32         _shadowReceive{ 1.0f };
        float32         _emissiveStrength{ 1.0f };
        float32         _rimFresnelPower{ 5.0f };
        float32         _rimLift{ 0.0f };
        float32         _rimLightingMix{ 1.0f };
        float32         _outlineWidth{ 0.0f };
        float32         _outlineLightingMix{ 1.0f };
        float32         _outlineMaxDistance{ 1000.0f };
        float32         _alphaCutoff{ 0.5f };
        int32           _baseColorTexture{ -1 };
        int32           _shadeTexture{ -1 };
        int32           _emissiveTexture{ -1 };
        int32           _matcapTexture{ -1 };
        ToonOutlineMode _outlineMode{ ToonOutlineMode::None };
        ToonAlphaMode   _alphaMode{ ToonAlphaMode::Opaque };
        uint8           _bTwoSided{ SW_FALSE };
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct VrmMaterialImporter
     * @brief VRM 머티리얼 읽기와 엔진 툰 머티리얼 쓰기입니다(전부 static). 지원하는 키는 `Source/Editor/README.md` "VRM 머티리얼" 표입니다.
     * @details **모르는 키는 오류입니다**(이 저장소의 규칙 — 철자가 틀린 키가 조용히 기본값이 되지 않게). 아는데 지원하지 않는 키(노멀 맵 · UV 애니메이션 …)는
     *          @p pOutListIgnored 에 이름을 모읍니다 — 값이 효과가 없는 기본값이면 모으지 않습니다. 부르는 쪽이 경고로 알립니다.
     */
    struct VrmMaterialImporter
    {
        /**
         * @brief VRM 0.x 의 `materialProperties[]` 원소 하나를 읽습니다. @p gltfMaterial 은 같은 번호의 glTF 머티리얼입니다(셰이더가 `VRM_USE_GLTFSHADER` 일 때 씁니다).
         * @details 0.x 의 색은 감마 공간(유니티 색)이라 선형으로 바꿉니다. 계단은 0.x 의 [ShadeShift, lerp(1, ShadeShift, ShadeToony)] 구간을 1.0 의
         *          (shadingShift, shadingToony) 로 옮겨 같은 경계가 되게 하고, 외곽선 두께는 월드 cm → m, 화면은 NDC 의 1 % 단위 → 화면 높이 비율(× 0.005)입니다.
         * @return 모르는 키 · 셰이더 · 값이면 false 이고 @p outError 에 이유를 씁니다.
         */
        [[nodiscard]] static bool readVrm0Material( const JSONValue& materialProperty, const JSONValue& gltfMaterial, ToonMaterialDesc& outDesc,
                                                    vector<string>* pOutListIgnored, string& outError );

        /**
         * @brief VRM 1.0 의 glTF 머티리얼 하나(`extensions.VRMC_materials_mtoon` 이 있으면 MToon, 없으면 glTF 기본 머티리얼을 평면 툰으로)를 읽습니다.
         * @details 1.0 의 값은 선형이고 이름이 엔진과 같아 그대로 옮깁니다. `KHR_materials_emissive_strength` 를 받습니다.
         * @return 모르는 MToon 키 · 값이면 false 이고 @p outError 에 이유를 씁니다.
         */
        [[nodiscard]] static bool readMtoon1Material( const JSONValue& gltfMaterial, ToonMaterialDesc& outDesc, vector<string>* pOutListIgnored, string& outError );

        /**
         * @brief 엔진 툰 머티리얼(`engine/materials/toon.material`)을 틀로 @p desc 의 값을 넣은 `.material` 글을 만듭니다. 틀을 못 읽으면 빈 글입니다.
         * @param listTexturePath glTF 텍스처 번호 → 리소스 경로(DDS). 빈 칸이면 그 텍스처는 없는 것으로 씁니다.
         */
        static string makeMaterialXML( const ToonMaterialDesc& desc, const vector<string>& listTexturePath );

        /** @brief 감마(sRGB) 값 하나를 선형으로 바꿉니다. */
        static float32 convertGammaToLinear( float32 value );
    };
} // namespace sw::editor
