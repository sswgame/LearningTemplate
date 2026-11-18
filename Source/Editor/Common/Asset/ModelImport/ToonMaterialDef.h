/**
 * @file ToonMaterialDef.h
 * @brief VRM 의 MToon 머티리얼(0.x 는 `extensions.VRM.materialProperties`, 1.0 은 `VRMC_materials_mtoon`)을 엔진 툰 머티리얼(`engine/materials/toon.material`)로 변환합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/VectorMath.h"

#include "Engine/Serialization/Serializer/DataPath.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    class JSONValue;
} // namespace sw

namespace sw::editor
{
    /** @brief 외곽선 두께 모드입니다. MToon 의 outlineWidthMode 에 해당합니다. */
    enum class ToonOutlineMode : uint8
    {
        None,
        World,  ///< 두께가 미터 단위다
        Screen, ///< 두께가 화면 높이에 대한 비율이다. 거리와 무관하다
    };

    /** @brief 알파를 쓰는 방식입니다. glTF 의 alphaMode 와 0.x 의 _BlendMode 에 해당합니다. */
    enum class ToonAlphaMode : uint8
    {
        Opaque,
        Cutout,      ///< 알파 컷오프(alphaMode Mask)
        Transparent, ///< 반투명(alphaMode Blend + MATERIAL_BLEND_TRANSLUCENT)
    };
} // namespace sw::editor

namespace sw::editor
{
    /**
     * @struct ToonMaterialDef
     * @brief 툰 머티리얼 하나의 값입니다. 이름과 의미는 `toon.hlsl` 의 머티리얼 구조체(MToon 1.0 이름)와 같습니다. 색은 선형입니다.
     * @details 텍스처는 glTF `textures[]` 번호이며 -1 이면 없다. 파일 경로는 머티리얼을 만들 때(`makeMaterialXML`) 정한다.
     */
    struct ToonMaterialDef
    {
        string           _name;
        float4           _baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float4           _shadeColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        float4           _emissiveColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        float4           _rimColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        float4           _matcapColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float4           _outlineColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        float32          _shadingShift{ 0.0f };
        float32          _shadingToony{ 0.9f };
        float32          _shadowReceive{ 1.0f };
        float32          _emissiveStrength{ 1.0f };
        float32          _rimFresnelPower{ 5.0f };
        float32          _rimLift{ 0.0f };
        float32          _rimLightingMix{ 1.0f };
        float32          _outlineWidth{ 0.0f };
        float32          _outlineLightingMix{ 1.0f };
        float32          _outlineMaxDistance{ 1000.0f };
        float32          _alphaCutoff{ 0.5f };
        float32          _normalScale{ 1.0f };              ///< glTF normalTexture.scale · 0.x _BumpScale
        float32          _shadingShiftTextureScale{ 1.0f }; ///< MToon shadingShiftTexture.scale
        float32          _uvAnimationScrollXSpeed{ 0.0f };  ///< MToon uvAnimationScrollXSpeedFactor(glTF UV / 초)
        float32          _uvAnimationScrollYSpeed{ 0.0f };  ///< MToon uvAnimationScrollYSpeedFactor(glTF UV / 초, v 는 아래가 +)
        float32          _uvAnimationRotationSpeed{ 0.0f }; ///< MToon uvAnimationRotationSpeedFactor(라디안 / 초, UV 중심 (0.5, 0.5) 기준)
        optional<uint32> _baseColorTexture;
        optional<uint32> _shadeTexture;
        optional<uint32> _emissiveTexture;
        optional<uint32> _matcapTexture;
        optional<uint32> _normalTexture;          ///< glTF normalTexture · 0.x _BumpMap
        optional<uint32> _shadingShiftTexture;    ///< MToon shadingShiftTexture
        optional<uint32> _rimMultiplyTexture;     ///< MToon rimMultiplyTexture · 0.x _RimTexture
        optional<uint32> _outlineWidthTexture;    ///< MToon outlineWidthMultiplyTexture · 0.x _OutlineWidthTexture
        optional<uint32> _uvAnimationMaskTexture; ///< MToon uvAnimationMaskTexture · 0.x _UvAnimMaskTexture(b 채널)
        ToonOutlineMode  _outlineMode{ ToonOutlineMode::None };
        ToonAlphaMode    _alphaMode{ ToonAlphaMode::Opaque };
        uint8            _bTwoSided{ SW_FALSE };

        // VRM 머티리얼 읽기와 엔진 툰 머티리얼 쓰기를 담당합니다. 모든 함수가 static 입니다. 지원하는 키의 기준은 `ToonMaterialDef.cpp` 의 키 표(`kArrVrm0Float` · `kArrVrm0Texture` · `kArrVrm0Color` · `kArrMtoon1Key`)입니다.
        // 모르는 키는 오류다. 철자가 틀린 키가 조용히 기본값이 되지 않게 하는 이 저장소의 규칙이다. 아는데 지원하지 않는 키(텍스처 확장 · 0 이 아닌 texCoord 등)는
        /**
         * @brief VRM 0.x 의 `materialProperties[]` 원소 하나를 읽습니다. @p gltfMaterial 은 같은 번호의 glTF 머티리얼이며, 셰이더가 `VRM_USE_GLTFSHADER` 일 때 씁니다.
         * @details 0.x 의 색은 감마 공간(유니티 색)이므로 선형으로 바꾼다. 계단은 0.x 의 [ShadeShift, lerp(1, ShadeShift, ShadeToony)] 구간을 1.0 의
         *          (shadingShift, shadingToony) 로 옮겨 같은 경계가 되게 한다. 외곽선 두께는 월드에서는 cm 를 m 로, 화면에서는 NDC 의 1 % 단위를 화면 높이 비율(× 0.005)로 바꾼다.
         * @return 읽은 정의. 모르는 키, 셰이더, 값이 있으면 그 이유(대상 앞에 어느 머티리얼인지)입니다.
         */
        [[nodiscard]] static expected<ToonMaterialDef> readVrm0Material( const JSONValue& materialProperty, const JSONValue& gltfMaterial, vector<string>* pOutListIgnored );

        /**
         * @brief VRM 1.0 의 glTF 머티리얼 하나를 읽습니다. `extensions.VRMC_materials_mtoon` 이 있으면 MToon 으로, 없으면 glTF 기본 머티리얼을 평면 툰으로 읽습니다.
         * @details 1.0 의 값은 선형이고 이름이 엔진과 같으므로 그대로 옮긴다. `KHR_materials_emissive_strength` 를 받는다.
         * @return 읽은 정의. 모르는 MToon 키나 값이 있으면 그 이유(대상 앞에 어느 머티리얼인지)입니다.
         */
        [[nodiscard]] static expected<ToonMaterialDef> readMtoon1Material( const JSONValue& gltfMaterial, vector<string>* pOutListIgnored );

        /**
         * @brief 엔진 툰 머티리얼을 템플릿으로 @p desc 의 값을 넣은 `.material` 문자열을 만듭니다. 템플릿을 읽지 못하거나 템플릿에 쓸 프로퍼티가 없으면 빈 문자열입니다.
         * @param listTexturePath glTF 텍스처 번호에서 리소스 경로(DDS)로의 대응입니다. 비어 있는 항목은 그 텍스처가 없는 것으로 씁니다.
         * @param toonTemplate 툰 머티리얼 템플릿의 경로입니다 — 원본은 엔진 기본 에셋 표(`EngineDefaultAssets::_toonMaterialTemplate`). 워커에서도 부르므로 서비스를 찾지 않고 호출한 쪽이 넘긴다.
         */
        static string makeMaterialXML( const ToonMaterialDef& desc, const vector<string>& listTexturePath, const DataPath& toonTemplate );
        /** @brief 머티리얼 XML `_properties` 에서 그 이름의 항목을 찾습니다. 없으면 무효 노드입니다(툰 · PBR 머티리얼 임포트가 함께 쓴다). */
        static XMLNode findMaterialProperty( const XMLNode& properties, string_view name );

        /** @brief 감마(sRGB) 값 하나를 선형으로 변환합니다. */
        static float32 convertGammaToLinear( float32 value );
    };
} // namespace sw::editor
