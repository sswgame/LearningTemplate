#include "pch.h"

#include "Editor/Common/Asset/VrmMaterialImporter.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Resource/ResourceIO.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @struct VrmMaterialImporterInternal
         * @brief 키 테이블, 값 읽기, 머티리얼 XML 채우기를 담당합니다.
         */
        struct VrmMaterialImporterInternal
        {
            /** @brief 엔진 툰 머티리얼 템플릿 경로입니다. 프로퍼티 목록과 퍼뮤테이션의 원본은 이 파일 하나입니다. */
            static constexpr string_view kToonMaterialTemplate = "engine/materials/toon.material";

            /** @brief 0.x 원소의 최상위 키입니다. */
            static constexpr string_view kArrVrm0RootKey[] = { "name", "shader", "renderQueue", "floatProperties", "vectorProperties", "textureProperties", "keywordMap", "tagMap" };
            /** @brief 0.x 에서 옮기는 실수 키 하나와, 키가 없을 때의 값(UniVRM 0.x MToon 셰이더의 기본값)입니다. */
            struct Vrm0Float
            {
                string_view _key;
                float32     _default;
            };
            /** @brief `kArrVrm0Float` 의 인덱스입니다. 순서가 테이블과 같아야 한다(아래 static_assert). */
            enum Vrm0FloatSlot : uint32
            {
                Vrm0Cutoff,
                Vrm0BlendMode,
                Vrm0CullMode,
                Vrm0ShadeShift,
                Vrm0ShadeToony,
                Vrm0ReceiveShadowRate,
                Vrm0RimFresnelPower,
                Vrm0RimLift,
                Vrm0RimLightingMix,
                Vrm0OutlineWidth,
                Vrm0OutlineWidthMode,
                Vrm0OutlineScaledMaxDistance,
                Vrm0OutlineColorMode,
                Vrm0OutlineLightingMix,
                Vrm0BumpScale,
                Vrm0UvAnimScrollX,
                Vrm0UvAnimScrollY,
                Vrm0UvAnimRotation,
                Vrm0FloatCount,
            };
            /** @brief 0.x 에서 옮기는 실수 키와 그 기본값입니다. 기본값의 원본은 이 테이블 하나다(읽는 코드에 폴백을 적지 않는다). */
            static constexpr Vrm0Float kArrVrm0Float[] = {
                {                  "_Cutoff", 0.5f},
                {               "_BlendMode", 0.0f},
                {                "_CullMode", 2.0f},
                {              "_ShadeShift", 0.0f},
                {              "_ShadeToony", 0.9f},
                {       "_ReceiveShadowRate", 1.0f},
                {         "_RimFresnelPower", 1.0f},
                {                 "_RimLift", 0.0f},
                {          "_RimLightingMix", 0.0f},
                {            "_OutlineWidth", 0.0f},
                {        "_OutlineWidthMode", 0.0f},
                {"_OutlineScaledMaxDistance", 1.0f},
                {        "_OutlineColorMode", 0.0f},
                {      "_OutlineLightingMix", 1.0f},
                {               "_BumpScale", 1.0f},
                {           "_UvAnimScrollX", 0.0f},
                {           "_UvAnimScrollY", 0.0f},
                {          "_UvAnimRotation", 0.0f},
            };
            static_assert( sizeof( kArrVrm0Float ) / sizeof( kArrVrm0Float[0] ) == Vrm0FloatCount, "Vrm0FloatSlot 과 kArrVrm0Float 의 줄 수가 같아야 한다" );
            /** @brief 화면 외곽선이 아닐 때의 외곽선 최대 거리입니다(`ToonMaterialDesc::_outlineMaxDistance` 의 초기값과 같은 "제한 없음"). */
            static constexpr float32 kUnlimitedOutlineDistance = 1000.0f;
            /**
             * @brief 0.x 에서 알지만 옮기지 않는 실수 키와, 그 키가 효과 없는 값입니다. 다른 값이면 무시 목록에 남깁니다.
             * @details 블렌드 상태(_SrcBlend, _DstBlend, _ZWrite, _AlphaToMask)는 _BlendMode 에서 정해지고, _DebugMode 와 _MToonVersion 은 렌더링과 무관하므로 항상 조용히 넘긴다.
             */
            struct IgnoredFloat
            {
                string_view _key;
                float32     _neutral;
                bool        _bAlwaysSilent;
            };
            static constexpr IgnoredFloat kArrVrm0IgnoredFloat[] = {
                {      "_ShadingGradeRate", 1.0f,  true}, // 셰이딩 그레이드 텍스처가 없으면 효과가 없다
                { "_LightColorAttenuation", 0.0f, false},
                {"_IndirectLightIntensity", 0.1f,  true}, // 환경광이 균일하므로 섞을 것이 없다
                {       "_OutlineCullMode", 1.0f, false}, // 외곽선은 항상 앞면을 컬링한다
                {             "_DebugMode", 0.0f,  true},
                {              "_SrcBlend", 0.0f,  true},
                {              "_DstBlend", 0.0f,  true},
                {                "_ZWrite", 0.0f,  true},
                {           "_AlphaToMask", 0.0f,  true},
                {          "_MToonVersion", 0.0f,  true},
            };
            /** @brief 0.x 에서 옮기는 색 키입니다. */
            static constexpr string_view kArrVrm0Color[] = { "_Color", "_ShadeColor", "_EmissionColor", "_RimColor", "_OutlineColor" };
            /** @brief 0.x 텍스처 키입니다. 옮기는 여덟 개와 알지만 옮기지 않는 것들이 있습니다. 같은 이름의 벡터 키는 텍스처 타일링([x, y, 크기 x, 크기 y])입니다. */
            static constexpr string_view kArrVrm0Texture[]        = { "_MainTex", "_ShadeTexture", "_EmissionMap", "_SphereAdd",
                                                                      "_BumpMap", "_RimTexture", "_OutlineWidthTexture", "_UvAnimMaskTexture" };
            static constexpr size_t      kVrm0TextureCount        = sizeof( kArrVrm0Texture ) / sizeof( kArrVrm0Texture[0] );
            static constexpr string_view kArrVrm0IgnoredTexture[] = { "_ReceiveShadowTexture", "_ShadingGradeTexture" };
            /** @brief 0.x 의 MToon 이 아닌 셰이더입니다. glTF 기본 머티리얼을 평면 툰으로 옮깁니다. */
            static constexpr string_view kArrVrm0FlatShader[] = { "VRM_USE_GLTFSHADER", "VRM/UnlitTexture", "VRM/UnlitCutout", "VRM/UnlitTransparent",
                                                                  "VRM/UnlitTransparentZWrite" };

            /** @brief 1.0 `VRMC_materials_mtoon` 의 키입니다. 옮기는 것과 알지만 옮기지 않는 것이 있습니다. */
            static constexpr string_view kArrMtoon1Key[] = { "specVersion",
                                                             "transparentWithZWrite",
                                                             "renderQueueOffsetNumber",
                                                             "shadeColorFactor",
                                                             "shadeMultiplyTexture",
                                                             "shadingShiftFactor",
                                                             "shadingToonyFactor",
                                                             "giEqualizationFactor",
                                                             "matcapFactor",
                                                             "matcapTexture",
                                                             "parametricRimColorFactor",
                                                             "rimLightingMixFactor",
                                                             "parametricRimFresnelPowerFactor",
                                                             "parametricRimLiftFactor",
                                                             "outlineWidthMode",
                                                             "outlineWidthFactor",
                                                             "outlineColorFactor",
                                                             "outlineLightingMixFactor",
                                                             "shadingShiftTexture",
                                                             "rimMultiplyTexture",
                                                             "outlineWidthMultiplyTexture",
                                                             "uvAnimationMaskTexture",
                                                             "uvAnimationScrollXSpeedFactor",
                                                             "uvAnimationScrollYSpeedFactor",
                                                             "uvAnimationRotationSpeedFactor",
                                                             "extensions",
                                                             "extras" };

            static const IgnoredFloat* findIgnoredFloat( string_view key )
            {
                for ( const IgnoredFloat& entry : kArrVrm0IgnoredFloat )
                {
                    if ( entry._key == key )
                        return &entry;
                }
                return nullptr;
            }

            static void appendIgnored( vector<string>* pOutListIgnored, string_view key )
            {
                if ( pOutListIgnored == nullptr )
                    return;
                const string name{ key };
                if ( VectorUtil::contains( *pOutListIgnored, name ) == false )
                    pOutListIgnored->push_back( name );
            }

            /** @brief 객체의 키를 테이블과 대조합니다. 모르는 키가 있으면 그 이름을 @p outError 에 쓰고 false 를 반환합니다. */
            template <size_t Count>
            [[nodiscard]] static bool checkKeys( const JSONValue& object, const string_view ( &arrKnown )[Count], string_view context, string& outError )
            {
                for ( const string& key : object.getMemberNames() )
                {
                    if ( VectorUtil::contains( arrKnown, key ) == false )
                    {
                        outError = string( context ) + ": unknown key '" + key + "'";
                        return false;
                    }
                }
                return true;
            }

            /** @brief 숫자 @p count 개의 배열을 읽습니다. 모자란 원소는 그대로 둡니다. 배열이 아니거나 숫자가 아니면 false 입니다. */
            [[nodiscard]] static bool readNumbers( const JSONValue& value, float32* pOutValue, uint32 count )
            {
                if ( value.isArray() == false || value.size() < count )
                    return false;
                for ( uint32 index = 0; index < count; ++index )
                {
                    const JSONValue element = value.at( index );
                    if ( element.isNumber() == false )
                        return false;
                    const std::optional<float32> number = element.read<float32>();
                    if ( number.has_value() == false )
                        return false;
                    pOutValue[index] = *number;
                }
                return true;
            }

            /** @brief 없어도 되는 숫자 @p key 를 읽습니다. 없으면 @p inoutValue 를 그대로 두고(정의의 초기값), 숫자가 아니면 이유와 false 입니다. */
            [[nodiscard]] static bool readFloat( const JSONValue& object, string_view key, float32& inoutValue, string& outError )
            {
                const JSONValue value = object.get( key );
                if ( value.isValid() == false || value.isNull() )
                    return true;
                const std::optional<float32> number = value.read<float32>();
                if ( number.has_value() == false )
                {
                    outError = string( key ) + " " + value.dump() + " is not a number";
                    return false;
                }
                inoutValue = *number;
                return true;
            }

            /** @brief 색(원소 3개 또는 4개)을 읽습니다. 3개이면 알파는 1 입니다. */
            [[nodiscard]] static bool readColor( const JSONValue& value, float4& outColor )
            {
                float32    arrValue[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
                const bool bInvalidColorArray = value.isArray() == false || ( value.size() != 3 && value.size() != 4 ) || readNumbers( value, arrValue, static_cast<uint32>( value.size() ) ) == false;
                if ( bInvalidColorArray )
                    return false;
                outColor = float4{ arrValue[0], arrValue[1], arrValue[2], arrValue[3] };
                return true;
            }

            static float4 convertColorToLinear( const float4& gamma )
            {
                return float4{ VrmMaterialImporter::convertGammaToLinear( gamma._x ), VrmMaterialImporter::convertGammaToLinear( gamma._y ),
                               VrmMaterialImporter::convertGammaToLinear( gamma._z ), gamma._w };
            }

            /**
             * @brief glTF textureInfo 의 텍스처 번호를 @p inoutTexture 에 읽습니다. textureInfo 가 없으면 그대로 둡니다(-1, 텍스처 없음).
             *        지원하지 않는 텍스처 확장(KHR_texture_transform)이나 0 이 아닌 texCoord 는 무시 목록에 남깁니다.
             * @return index 가 0 이상의 정수가 아니거나 texCoord 가 정수가 아니면 이유와 false 입니다.
             */
            [[nodiscard]] static bool readTextureIndex( const JSONValue& textureInfo, string_view what, int32& inoutTexture, vector<string>* pOutListIgnored, string& outError )
            {
                if ( textureInfo.isObject() == false )
                    return true;
                const std::optional<int32> index = textureInfo.get( "index" ).read<int32>();
                if ( index.has_value() == false || *index < 0 )
                {
                    outError = string( what ) + ".index " + textureInfo.get( "index" ).dump() + " is not a texture number";
                    return false;
                }
                const JSONValue extensions = textureInfo.get( "extensions" );
                if ( extensions.isObject() )
                {
                    for ( const string& extension : extensions.getMemberNames() )
                    {
                        appendIgnored( pOutListIgnored, extension );
                    }
                }
                const JSONValue texCoord = textureInfo.get( "texCoord" );
                if ( texCoord.isValid() )
                {
                    const std::optional<int32> texCoordIndex = texCoord.read<int32>();
                    if ( texCoordIndex.has_value() == false )
                    {
                        outError = string( what ) + ".texCoord " + texCoord.dump() + " is not a number";
                        return false;
                    }
                    if ( *texCoordIndex != 0 )
                        appendIgnored( pOutListIgnored, "texCoord" );
                }
                inoutTexture = *index;
                return true;
            }

            /**
             * @brief glTF 기본 머티리얼(baseColor, alphaMode, alphaCutoff, doubleSided, emissive)을 읽습니다. 1.0 의 MToon 과 0.x 의 평면 셰이더가 함께 씁니다.
             * @return alphaMode 를 모르면 false 입니다.
             */
            [[nodiscard]] static bool readGltfBase( const JSONValue& gltfMaterial, ToonMaterialDesc& inoutDesc, vector<string>* pOutListIgnored, string& outError )
            {
                if ( gltfMaterial.isObject() == false )
                    return true;
                const JSONValue pbr = gltfMaterial.get( "pbrMetallicRoughness" );
                if ( pbr.isObject() )
                {
                    if ( pbr.has( "baseColorFactor" ) && readColor( pbr.get( "baseColorFactor" ), inoutDesc._baseColor ) == false )
                    {
                        outError = "baseColorFactor is not a color";
                        return false;
                    }
                    if ( readTextureIndex( pbr.get( "baseColorTexture" ), "baseColorTexture", inoutDesc._baseColorTexture, pOutListIgnored, outError ) == false )
                        return false;
                }
                const string alphaMode = gltfMaterial.has( "alphaMode" ) ? gltfMaterial.get( "alphaMode" ).asString() : string( "OPAQUE" );
                if ( alphaMode == "OPAQUE" )
                    inoutDesc._alphaMode = ToonAlphaMode::Opaque;
                else if ( alphaMode == "MASK" )
                    inoutDesc._alphaMode = ToonAlphaMode::Cutout;
                else if ( alphaMode == "BLEND" )
                    inoutDesc._alphaMode = ToonAlphaMode::Transparent;
                else
                {
                    outError = "unknown alphaMode '" + alphaMode + "'";
                    return false;
                }
                // 적지 않은 칸은 `ToonMaterialDesc` 의 초기값(glTF 명세의 기본값)이다.
                bool bTwoSided = inoutDesc._bTwoSided != SW_FALSE;
                if ( readFloat( gltfMaterial, "alphaCutoff", inoutDesc._alphaCutoff, outError ) == false )
                    return false;
                if ( gltfMaterial.readOptionalInto( "doubleSided", bTwoSided ) == false )
                {
                    outError = "doubleSided is not a boolean";
                    return false;
                }
                inoutDesc._bTwoSided = bTwoSided ? SW_TRUE : SW_FALSE;
                if ( gltfMaterial.has( "emissiveFactor" ) && readColor( gltfMaterial.get( "emissiveFactor" ), inoutDesc._emissiveColor ) == false )
                {
                    outError = "emissiveFactor is not a color";
                    return false;
                }
                // 노멀 맵(glTF normalTextureInfo) — 배율은 xy 에 곱한다(셰이더 applyNormalMap).
                const JSONValue normalTexture = gltfMaterial.get( "normalTexture" );
                const bool      bTexturesRead = readTextureIndex( gltfMaterial.get( "emissiveTexture" ), "emissiveTexture", inoutDesc._emissiveTexture, pOutListIgnored, outError ) &&
                                           readTextureIndex( normalTexture, "normalTexture", inoutDesc._normalTexture, pOutListIgnored, outError );
                if ( bTexturesRead == false )
                    return false;
                if ( normalTexture.isObject() && readFloat( normalTexture, "scale", inoutDesc._normalScale, outError ) == false )
                    return false;
                const JSONValue extensions = gltfMaterial.get( "extensions" );
                const JSONValue strength   = extensions.isObject() ? extensions.get( "KHR_materials_emissive_strength" ) : JSONValue{};
                return strength.isObject() == false || readFloat( strength, "emissiveStrength", inoutDesc._emissiveStrength, outError );
            }

            /** @brief 그림자 없이 기본색 하나로 칠하는 평면 툰입니다. 언릿과 glTF 셰이더가 해당합니다. */
            static void makeFlat( ToonMaterialDesc& inoutDesc )
            {
                inoutDesc._shadeColor   = inoutDesc._baseColor;
                inoutDesc._shadeTexture = inoutDesc._baseColorTexture;
                inoutDesc._shadingToony = 1.0f;
                inoutDesc._outlineMode  = ToonOutlineMode::None;
            }

            /** @brief 소수 여섯 자리까지의 문자열입니다. XML 에 쓰는 값입니다. 음의 0 은 0 으로 쓴다(부호를 뒤집은 0 이 "-0" 으로 남지 않게 — -0 + 0 = +0). */
            static string formatNumber( float32 value )
            {
                utf8 arrText[constant::kMaxBuffer32]{};
                (void)snprintf( arrText, sizeof( arrText ), "%.6g", static_cast<float64>( value ) + 0.0 );
                return string( arrText );
            }

            static string formatColor( const float4& color )
            {
                return formatNumber( color._x ) + " " + formatNumber( color._y ) + " " + formatNumber( color._z ) + " " + formatNumber( color._w );
            }

            /** @brief `_properties` 에서 그 이름의 항목을 찾습니다. */
            static XMLNode findProperty( const XMLNode& properties, string_view name )
            {
                for ( XMLNode item = properties.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
                {
                    if ( item.getAttributeText( "name" ) == name )
                        return item;
                }
                return XMLNode{};
            }

            static constexpr size_t kVrm0ColorCount = sizeof( kArrVrm0Color ) / sizeof( kArrVrm0Color[0] );

            /** @brief VRM 0.x `floatProperties` 를 표(`kArrVrm0Float`) 순서로 @p outFloat 에 읽습니다. 적지 않은 키는 표의 기본값이다. 무시하는 키는 중립값에서 벗어났을 때만 알립니다. */
            [[nodiscard]] static bool readVrm0Floats( const JSONValue& materialProperty, const string& context, vector<string>* pOutListIgnored, float32 ( &outFloat )[Vrm0FloatCount],
                                                      string& outError )
            {
                const JSONValue floats = materialProperty.get( "floatProperties" );
                for ( uint32 index = 0; index < Vrm0FloatCount; ++index )
                {
                    outFloat[index] = kArrVrm0Float[index]._default;
                }
                for ( const string& key : floats.isObject() ? floats.getMemberNames() : vector<string>{} )
                {
                    const std::optional<float32> number = floats.get( key ).read<float32>();
                    if ( number.has_value() == false )
                    {
                        outError = context + ": float property '" + key + "' is not a number";
                        return false;
                    }
                    bool bKnown = false;
                    for ( uint32 index = 0; index < Vrm0FloatCount; ++index )
                    {
                        if ( kArrVrm0Float[index]._key != key )
                            continue;
                        outFloat[index] = *number;
                        bKnown          = true;
                    }
                    if ( bKnown )
                        continue;
                    const IgnoredFloat* pIgnored = findIgnoredFloat( key );
                    if ( pIgnored == nullptr )
                    {
                        outError = context + ": unknown float property '" + key + "'";
                        return false;
                    }
                    if ( pIgnored->_bAlwaysSilent == false && MathUtil::abs( *number - pIgnored->_neutral ) > 1e-4f )
                        appendIgnored( pOutListIgnored, key );
                }
                return true;
            }

            /** @brief VRM 0.x `vectorProperties` 의 색을 선형으로 바꿔 표(`kArrVrm0Color`) 순서로 @p outColor 에 읽고, 텍스처 타일링은 항등이 아니면 무시 목록에 올립니다. */
            [[nodiscard]] static bool readVrm0Colors( const JSONValue& materialProperty, const string& context, vector<string>* pOutListIgnored, float4 ( &outColor )[kVrm0ColorCount],
                                                      string& outError )
            {
                const JSONValue vectors = materialProperty.get( "vectorProperties" );
                outColor[0]             = float4{ 1.0f, 1.0f, 1.0f, 1.0f };
                for ( size_t index = 1; index < kVrm0ColorCount; ++index )
                {
                    outColor[index] = float4{ 0.0f, 0.0f, 0.0f, 1.0f };
                }
                for ( const string& key : vectors.isObject() ? vectors.getMemberNames() : vector<string>{} )
                {
                    const JSONValue value  = vectors.get( key );
                    bool            bKnown = false;
                    for ( size_t index = 0; index < kVrm0ColorCount; ++index )
                    {
                        if ( kArrVrm0Color[index] != key )
                            continue;
                        float4 color{};
                        if ( readColor( value, color ) == false )
                        {
                            outError = context + ": vector property '" + key + "' is not a color";
                            return false;
                        }
                        outColor[index] = convertColorToLinear( color );
                        bKnown          = true;
                    }
                    if ( bKnown )
                        continue;
                    if ( VectorUtil::contains( kArrVrm0Texture, key ) == false && VectorUtil::contains( kArrVrm0IgnoredTexture, key ) == false )
                    {
                        outError = context + ": unknown vector property '" + key + "'";
                        return false;
                    }
                    // 텍스처 타일링 [x, y, 크기 x, 크기 y] 는 항상 [0, 0, 1, 1] 이어야 한다. 다르면 지원하지 않는 타일링이다.
                    float32 arrTiling[4]{ 0.0f, 0.0f, 1.0f, 1.0f };
                    if ( readNumbers( value, arrTiling, 4 ) == false )
                    {
                        outError = context + ": texture tiling '" + key + "' is not four numbers";
                        return false;
                    }
                    const bool bIdentity = arrTiling[0] == 0.0f && arrTiling[1] == 0.0f && arrTiling[2] == 1.0f && arrTiling[3] == 1.0f;
                    if ( bIdentity == false )
                        appendIgnored( pOutListIgnored, key + string( " tiling" ) );
                }
                return true;
            }

            /** @brief VRM 0.x `textureProperties` 를 표(`kArrVrm0Texture`) 순서로 glTF 텍스처 번호로 읽습니다. 적지 않은 키는 -1 입니다. */
            [[nodiscard]] static bool readVrm0Textures( const JSONValue& materialProperty, const string& context, vector<string>* pOutListIgnored, int32 ( &outTexture )[kVrm0TextureCount],
                                                        string& outError )
            {
                const JSONValue textures = materialProperty.get( "textureProperties" );
                for ( size_t index = 0; index < kVrm0TextureCount; ++index )
                {
                    outTexture[index] = -1;
                }
                for ( const string& key : textures.isObject() ? textures.getMemberNames() : vector<string>{} )
                {
                    const std::optional<int32> textureIndex = textures.get( key ).read<int32>();
                    if ( textureIndex.has_value() == false )
                    {
                        outError = context + ": texture property '" + key + "' is not a texture index";
                        return false;
                    }
                    bool bKnown = false;
                    for ( size_t index = 0; index < kVrm0TextureCount; ++index )
                    {
                        if ( kArrVrm0Texture[index] != key )
                            continue;
                        outTexture[index] = *textureIndex;
                        bKnown            = true;
                    }
                    if ( bKnown )
                        continue;
                    if ( VectorUtil::contains( kArrVrm0IgnoredTexture, key ) == false )
                    {
                        outError = context + ": unknown texture property '" + key + "'";
                        return false;
                    }
                    appendIgnored( pOutListIgnored, key );
                }
                return true;
            }

            /** @brief 블렌드 모드와 컬 모드를 알파 모드 · 양면으로 옮깁니다. 지원하지 않는 값이면 false 입니다. */
            [[nodiscard]] static bool applyVrm0BlendAndCull( const float32 ( &arrFloat )[Vrm0FloatCount], const string& context, ToonMaterialDesc& inoutDesc, string& outError )
            {
                const int32 blendMode = static_cast<int32>( arrFloat[Vrm0BlendMode] + 0.5f );
                if ( blendMode == 0 )
                    inoutDesc._alphaMode = ToonAlphaMode::Opaque;
                else if ( blendMode == 1 )
                    inoutDesc._alphaMode = ToonAlphaMode::Cutout;
                else if ( blendMode == 2 || blendMode == 3 ) // 3 은 깊이를 쓰는 반투명이다. 엔진의 반투명 패스는 깊이를 쓰지 않는다
                    inoutDesc._alphaMode = ToonAlphaMode::Transparent;
                else
                {
                    outError = context + ": unknown _BlendMode " + to_string( static_cast<int64>( blendMode ) );
                    return false;
                }
                inoutDesc._alphaCutoff = arrFloat[Vrm0Cutoff];
                const int32 cullMode   = static_cast<int32>( arrFloat[Vrm0CullMode] + 0.5f );
                if ( cullMode != 0 && cullMode != 2 )
                {
                    outError = context + ": unsupported _CullMode " + to_string( static_cast<int64>( cullMode ) ) + " (only Off = 0 and Back = 2)";
                    return false;
                }
                inoutDesc._bTwoSided = ( cullMode == 0 ) ? SW_TRUE : SW_FALSE;
                return true;
            }

            /** @brief 외곽선 모드 · 두께 · 최대 거리 · 빛 섞기를 옮깁니다. 지원하지 않는 모드면 false 입니다. */
            [[nodiscard]] static bool applyVrm0Outline( const float32 ( &arrFloat )[Vrm0FloatCount], const string& context, ToonMaterialDesc& inoutDesc, string& outError )
            {
                // 외곽선에서 0.x 의 월드 두께는 cm 단위이고, 화면 두께는 NDC 의 1 % 단위이다(NDC 높이 2 가 화면 높이). 그래서 화면 높이 비율은 × 0.005 다.
                const int32   outlineMode  = static_cast<int32>( arrFloat[Vrm0OutlineWidthMode] + 0.5f );
                const float32 outlineWidth = arrFloat[Vrm0OutlineWidth];
                if ( outlineMode == 0 )
                    inoutDesc._outlineMode = ToonOutlineMode::None;
                else if ( outlineMode == 1 )
                    inoutDesc._outlineMode = ToonOutlineMode::World;
                else if ( outlineMode == 2 )
                    inoutDesc._outlineMode = ToonOutlineMode::Screen;
                else
                {
                    outError = context + ": unknown _OutlineWidthMode " + to_string( static_cast<int64>( outlineMode ) );
                    return false;
                }
                inoutDesc._outlineWidth         = outlineWidth * ( inoutDesc._outlineMode == ToonOutlineMode::Screen ? 0.005f : 0.01f );
                const float32 scaledMaxDistance = arrFloat[Vrm0OutlineScaledMaxDistance];
                inoutDesc._outlineMaxDistance   = scaledMaxDistance > 0.0f && inoutDesc._outlineMode == ToonOutlineMode::Screen ? scaledMaxDistance : kUnlimitedOutlineDistance;
                // _OutlineColorMode 가 0 이면 고정 색이고 빛을 섞지 않는다. 1 이면 섞는다.
                inoutDesc._outlineLightingMix = static_cast<int32>( arrFloat[Vrm0OutlineColorMode] + 0.5f ) == 0 ? 0.0f : arrFloat[Vrm0OutlineLightingMix];
                return true;
            }

            /** @brief 읽은 색 · 텍스처 · 실수를 `ToonMaterialDesc` 에 옮깁니다 — 색 · 텍스처 칸, UV 애니메이션, 블렌드 · 컬, 음영 계단, 림, 외곽선. */
            [[nodiscard]] static bool applyVrm0Mtoon( const float4 ( &arrColor )[kVrm0ColorCount], const int32 ( &arrTexture )[kVrm0TextureCount], const float32 ( &arrFloat )[Vrm0FloatCount],
                                                      const string& context, ToonMaterialDesc& inoutDesc, string& outError )
            {
                inoutDesc._baseColor              = arrColor[0];
                inoutDesc._shadeColor             = arrColor[1];
                inoutDesc._emissiveColor          = arrColor[2];
                inoutDesc._rimColor               = arrColor[3];
                inoutDesc._outlineColor           = arrColor[4];
                inoutDesc._baseColorTexture       = arrTexture[0];
                inoutDesc._shadeTexture           = arrTexture[1];
                inoutDesc._emissiveTexture        = arrTexture[2];
                inoutDesc._matcapTexture          = arrTexture[3];
                inoutDesc._normalTexture          = arrTexture[4];
                inoutDesc._rimMultiplyTexture     = arrTexture[5];
                inoutDesc._outlineWidthTexture    = arrTexture[6];
                inoutDesc._uvAnimationMaskTexture = arrTexture[7];
                inoutDesc._normalScale            = arrFloat[Vrm0BumpScale];
                // UV 애니메이션 — UniVRM 의 0.x → 1.0 이관(VRM10 MigrationMToonMaterial)과 같다: 스크롤 y 는 부호를 뒤집고, 회전은 초당 바퀴 수 × 2π(라디안)이며
                // 방향은 그대로다. three-vrm 의 VRMMaterialsV0CompatPlugin 도 스크롤 y 만 뒤집는다(회전 배율은 UniVRM 을 따른다).
                inoutDesc._uvAnimationScrollXSpeed  = arrFloat[Vrm0UvAnimScrollX];
                inoutDesc._uvAnimationScrollYSpeed  = -arrFloat[Vrm0UvAnimScrollY];
                inoutDesc._uvAnimationRotationSpeed = arrFloat[Vrm0UvAnimRotation] * MathUtil::kTwoPi;

                if ( applyVrm0BlendAndCull( arrFloat, context, inoutDesc, outError ) == false )
                    return false;

                // 계단은 0.x 에서 [shift, lerp( 1, shift, toony )] 를 [0, 1] 로 편다(N·L 기준). 1.0 은 [-1 + toony, 1 - toony] 를 N·L + shift 에 적용한다.
                // 같은 경계가 되도록 1.0 의 shift 는 -(아래 + 위) / 2, toony 는 1 - (위 - 아래) / 2 로 정한다.
                const float32 shadeShift = arrFloat[Vrm0ShadeShift];
                const float32 shadeToony = arrFloat[Vrm0ShadeToony];
                const float32 lower      = shadeShift;
                const float32 upper      = MathUtil::lerp( 1.0f, shadeShift, shadeToony );
                inoutDesc._shadingShift  = -( lower + upper ) * 0.5f;
                inoutDesc._shadingToony  = MathUtil::clamp( 1.0f - ( upper - lower ) * 0.5f, 0.0f, 1.0f );
                inoutDesc._shadowReceive = arrFloat[Vrm0ReceiveShadowRate];

                inoutDesc._rimFresnelPower = arrFloat[Vrm0RimFresnelPower];
                inoutDesc._rimLift         = arrFloat[Vrm0RimLift];
                inoutDesc._rimLightingMix  = arrFloat[Vrm0RimLightingMix];
                inoutDesc._matcapColor     = float4{ 1.0f, 1.0f, 1.0f, 1.0f };

                return applyVrm0Outline( arrFloat, context, inoutDesc, outError );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    float32 VrmMaterialImporter::convertGammaToLinear( float32 value )
    {
        // sRGB 전달 함수(IEC 61966-2-1)의 역함수다. 유니티가 감마 색을 선형으로 바꾸는 식과 같다.
        if ( value <= 0.04045f )
            return value / 12.92f;
        return MathUtil::pow( ( value + 0.055f ) / 1.055f, 2.4f );
    }

    bool VrmMaterialImporter::readVrm0Material( const JSONValue& materialProperty, const JSONValue& gltfMaterial, ToonMaterialDesc& outDesc,
                                                vector<string>* pOutListIgnored, string& outError )
    {
        using Internal = VrmMaterialImporterInternal;
        outDesc        = ToonMaterialDesc{};
        if ( materialProperty.isObject() == false )
        {
            outError = "materialProperties element is not an object";
            return false;
        }
        outDesc._name        = materialProperty.get( "name" ).asString();
        const string context = "VRM 0.x material '" + outDesc._name + "'";
        const string shader  = materialProperty.get( "shader" ).asString();
        if ( Internal::checkKeys( materialProperty, Internal::kArrVrm0RootKey, context, outError ) == false )
            return false;

        // MToon 이 아닌 셰이더(언릿, glTF 셰이더)는 glTF 머티리얼을 평면 툰으로 옮긴다.
        if ( VectorUtil::contains( Internal::kArrVrm0FlatShader, shader ) )
        {
            if ( Internal::readGltfBase( gltfMaterial, outDesc, pOutListIgnored, outError ) == false )
            {
                outError = context + ": " + outError;
                return false;
            }
            Internal::makeFlat( outDesc );
            return true;
        }
        if ( shader != "VRM/MToon" )
        {
            outError = context + ": unsupported shader '" + shader + "'";
            return false;
        }

        float32    arrFloat[Internal::Vrm0FloatCount]{};
        float4     arrColor[Internal::kVrm0ColorCount]{};
        int32      arrTexture[Internal::kVrm0TextureCount]{};
        const bool bRead = Internal::readVrm0Floats( materialProperty, context, pOutListIgnored, arrFloat, outError ) &&
                           Internal::readVrm0Colors( materialProperty, context, pOutListIgnored, arrColor, outError ) &&
                           Internal::readVrm0Textures( materialProperty, context, pOutListIgnored, arrTexture, outError );
        if ( bRead == false )
            return false;
        return Internal::applyVrm0Mtoon( arrColor, arrTexture, arrFloat, context, outDesc, outError );
    }

    bool VrmMaterialImporter::readMtoon1Material( const JSONValue& gltfMaterial, ToonMaterialDesc& outDesc, vector<string>* pOutListIgnored, string& outError )
    {
        using Internal       = VrmMaterialImporterInternal;
        outDesc              = ToonMaterialDesc{};
        outDesc._name        = gltfMaterial.get( "name" ).asString();
        const string context = "VRM 1.0 material '" + outDesc._name + "'";
        if ( Internal::readGltfBase( gltfMaterial, outDesc, pOutListIgnored, outError ) == false )
        {
            outError = context + ": " + outError;
            return false;
        }
        const JSONValue extensions = gltfMaterial.get( "extensions" );
        const JSONValue mtoon      = extensions.isObject() ? extensions.get( "VRMC_materials_mtoon" ) : JSONValue{};
        if ( mtoon.isObject() == false )
        {
            // MToon 이 없는 머티리얼(KHR_materials_unlit, PBR)은 평면 툰으로 처리한다.
            Internal::makeFlat( outDesc );
            return true;
        }
        if ( Internal::checkKeys( mtoon, Internal::kArrMtoon1Key, context + ": VRMC_materials_mtoon", outError ) == false )
            return false;

        // 키가 없으면 `ToonMaterialDesc` 의 초기값이다 — 그 값이 1.0 의 기본값(VRMC_materials_mtoon 스키마)이다.
        auto readFactor = [&]( string_view key, float4& inoutColor ) -> bool
        { return mtoon.has( key ) == false || Internal::readColor( mtoon.get( key ), inoutColor ); };
        const bool bInvalidColorFactor = readFactor( "shadeColorFactor", outDesc._shadeColor ) == false || readFactor( "matcapFactor", outDesc._matcapColor ) == false ||
                                         readFactor( "parametricRimColorFactor", outDesc._rimColor ) == false || readFactor( "outlineColorFactor", outDesc._outlineColor ) == false;
        if ( bInvalidColorFactor )
        {
            outError = context + ": a color factor is not three or four numbers";
            return false;
        }
        // 셰이딩 시프트 텍스처는 shadingShiftTextureInfo(index · texCoord · scale)다.
        const JSONValue shiftTexture  = mtoon.get( "shadingShiftTexture" );
        const bool      bTexturesRead = Internal::readTextureIndex( mtoon.get( "shadeMultiplyTexture" ), "shadeMultiplyTexture", outDesc._shadeTexture, pOutListIgnored, outError ) &&
                                   Internal::readTextureIndex( mtoon.get( "matcapTexture" ), "matcapTexture", outDesc._matcapTexture, pOutListIgnored, outError ) &&
                                   Internal::readTextureIndex( mtoon.get( "rimMultiplyTexture" ), "rimMultiplyTexture", outDesc._rimMultiplyTexture, pOutListIgnored, outError ) &&
                                   Internal::readTextureIndex( mtoon.get( "outlineWidthMultiplyTexture" ), "outlineWidthMultiplyTexture", outDesc._outlineWidthTexture, pOutListIgnored,
                                                               outError ) &&
                                   Internal::readTextureIndex( mtoon.get( "uvAnimationMaskTexture" ), "uvAnimationMaskTexture", outDesc._uvAnimationMaskTexture, pOutListIgnored,
                                                               outError ) &&
                                   Internal::readTextureIndex( shiftTexture, "shadingShiftTexture", outDesc._shadingShiftTexture, pOutListIgnored, outError ) &&
                                   ( shiftTexture.isObject() == false || Internal::readFloat( shiftTexture, "scale", outDesc._shadingShiftTextureScale, outError ) );
        const bool bFactorsRead = Internal::readFloat( mtoon, "shadingShiftFactor", outDesc._shadingShift, outError ) &&
                                  Internal::readFloat( mtoon, "shadingToonyFactor", outDesc._shadingToony, outError ) &&
                                  Internal::readFloat( mtoon, "rimLightingMixFactor", outDesc._rimLightingMix, outError ) &&
                                  Internal::readFloat( mtoon, "parametricRimFresnelPowerFactor", outDesc._rimFresnelPower, outError ) &&
                                  Internal::readFloat( mtoon, "parametricRimLiftFactor", outDesc._rimLift, outError ) &&
                                  Internal::readFloat( mtoon, "outlineWidthFactor", outDesc._outlineWidth, outError ) &&
                                  Internal::readFloat( mtoon, "outlineLightingMixFactor", outDesc._outlineLightingMix, outError ) &&
                                  Internal::readFloat( mtoon, "uvAnimationScrollXSpeedFactor", outDesc._uvAnimationScrollXSpeed, outError ) &&
                                  Internal::readFloat( mtoon, "uvAnimationScrollYSpeedFactor", outDesc._uvAnimationScrollYSpeed, outError ) &&
                                  Internal::readFloat( mtoon, "uvAnimationRotationSpeedFactor", outDesc._uvAnimationRotationSpeed, outError );
        if ( bTexturesRead == false || bFactorsRead == false )
        {
            outError = context + ": " + outError;
            return false;
        }

        const string outlineMode = mtoon.has( "outlineWidthMode" ) ? mtoon.get( "outlineWidthMode" ).asString() : string( "none" );
        if ( outlineMode == "none" )
            outDesc._outlineMode = ToonOutlineMode::None;
        else if ( outlineMode == "worldCoordinates" )
            outDesc._outlineMode = ToonOutlineMode::World;
        else if ( outlineMode == "screenCoordinates" )
            outDesc._outlineMode = ToonOutlineMode::Screen;
        else
        {
            outError = context + ": unknown outlineWidthMode '" + outlineMode + "'";
            return false;
        }
        return true;
    }

    string VrmMaterialImporter::makeMaterialXML( const ToonMaterialDesc& desc, const vector<string>& listTexturePath )
    {
        using Internal = VrmMaterialImporterInternal;
        string templateText;
        if ( ResourceIO::readTextResource( Internal::kToonMaterialTemplate, templateText ) == false )
            return {};
        XMLDocument document;
        if ( document.parse( templateText ) == false )
            return {};
        const XMLNode root       = document.getRoot( "MaterialDesc" );
        const XMLNode properties = root.findChild( "_properties" );
        if ( root.isValid() == false || properties.isValid() == false )
            return {};

        root.setAttribute( "name", string_view( desc._name.empty() ? "Toon" : desc._name.c_str() ) );
        root.setAttribute( "blendMode", desc._alphaMode == ToonAlphaMode::Transparent ? "Transparent" : "Opaque" );

        // 템플릿에 없는 프로퍼티는 조용히 건너뛰지 않는다 — 템플릿과 이 표가 어긋나면 값이 기본값으로 남은 머티리얼이 나온다.
        bool bMissingProperty = false;
        auto setValue         = [&]( string_view name, const string& value )
        {
            const XMLNode item = Internal::findProperty( properties, name );
            if ( item.isValid() == false )
            {
                bMissingProperty = true;
                return;
            }
            item.setAttribute( "defaultValue", string_view( value ) );
        };
        auto setTexture = [&]( string_view name, int32 textureIndex )
        {
            const XMLNode item = Internal::findProperty( properties, name );
            if ( item.isValid() == false )
            {
                bMissingProperty = true;
                return;
            }
            const bool bInRange          = 0 <= textureIndex && static_cast<size_t>( textureIndex ) < listTexturePath.size();
            const bool bCanAssignTexture = bInRange && listTexturePath[static_cast<size_t>( textureIndex )].empty() == false;
            if ( bCanAssignTexture )
                item.setAttribute( "assetPath", string_view( listTexturePath[static_cast<size_t>( textureIndex )] ) );
        };
        setValue( "_baseColor", Internal::formatColor( desc._baseColor ) );
        setValue( "_shadeColor", Internal::formatColor( desc._shadeColor ) );
        setValue( "_emissiveColor", Internal::formatColor( desc._emissiveColor ) );
        setValue( "_rimColor", Internal::formatColor( desc._rimColor ) );
        setValue( "_matcapColor", Internal::formatColor( desc._matcapColor ) );
        setValue( "_outlineColor", Internal::formatColor( desc._outlineColor ) );
        setValue( "_shadingShift", Internal::formatNumber( desc._shadingShift ) );
        setValue( "_shadingToony", Internal::formatNumber( desc._shadingToony ) );
        setValue( "_shadowReceive", Internal::formatNumber( desc._shadowReceive ) );
        setValue( "_emissiveStrength", Internal::formatNumber( desc._emissiveStrength ) );
        setValue( "_rimFresnelPower", Internal::formatNumber( desc._rimFresnelPower ) );
        setValue( "_rimLift", Internal::formatNumber( desc._rimLift ) );
        setValue( "_rimLightingMix", Internal::formatNumber( desc._rimLightingMix ) );
        setValue( "_outlineWidth", Internal::formatNumber( desc._outlineWidth ) );
        setValue( "_outlineWidthMode", desc._outlineMode == ToonOutlineMode::Screen ? "1" : "0" );
        setValue( "_outlineLightingMix", Internal::formatNumber( desc._outlineLightingMix ) );
        setValue( "_outlineMaxDistance", Internal::formatNumber( desc._outlineMaxDistance ) );
        setValue( "_alphaCutoff", Internal::formatNumber( desc._alphaCutoff ) );
        setValue( "_normalScale", Internal::formatNumber( desc._normalScale ) );
        setValue( "_shadingShiftTextureScale", Internal::formatNumber( desc._shadingShiftTextureScale ) );
        setValue( "_uvAnimationScrollSpeedX", Internal::formatNumber( desc._uvAnimationScrollXSpeed ) );
        setValue( "_uvAnimationScrollSpeedY", Internal::formatNumber( desc._uvAnimationScrollYSpeed ) );
        setValue( "_uvAnimationRotationSpeed", Internal::formatNumber( desc._uvAnimationRotationSpeed ) );
        setTexture( "_baseColorMap", desc._baseColorTexture );
        setTexture( "_shadeMap", desc._shadeTexture );
        setTexture( "_emissiveMap", desc._emissiveTexture );
        setTexture( "_matcapMap", desc._matcapTexture );
        setTexture( "_normalMap", desc._normalTexture );
        setTexture( "_shadingShiftMap", desc._shadingShiftTexture );
        setTexture( "_rimMultiplyMap", desc._rimMultiplyTexture );
        setTexture( "_outlineWidthMap", desc._outlineWidthTexture );
        setTexture( "_uvAnimationMaskMap", desc._uvAnimationMaskTexture );
        if ( bMissingProperty )
            return {};

        const XMLNode permutations = root.findChild( "_permutations" );
        const XMLNode switches     = permutations.findChild( "_staticSwitches" );
        for ( XMLNode item = switches.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
        {
            const string_view name     = item.getAttributeText( "name" );
            bool              bEnabled = false;
            if ( name == "Outline" )
                bEnabled = desc._outlineMode != ToonOutlineMode::None && desc._outlineWidth > 0.0f && desc._alphaMode != ToonAlphaMode::Transparent;
            else if ( name == "AlphaCutoff" )
                bEnabled = desc._alphaMode == ToonAlphaMode::Cutout;
            else if ( name == "TwoSided" )
                bEnabled = desc._bTwoSided == SW_TRUE;
            item.setAttribute( "bEnabled", bEnabled ? "1" : "0" );
            // 임포트한 머티리얼의 스위치는 에셋이 정한 값이다. 템플릿(engine toon)이 런타임 스위치로 둔 것도 그 상태만 쿠킹한다.
            item.setAttribute( "bShaderFeature", "1" );
        }
        if ( desc._alphaMode == ToonAlphaMode::Transparent )
            permutations.findChild( "_alwaysDefines" ).appendChild( "item" ).setValue( "MATERIAL_BLEND_TRANSLUCENT" );
        return document.saveToString();
    }
} // namespace sw::editor
