#include "pch.h"

#include "Editor/Common/Asset/VrmMaterialImporter.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Json/JsonDocument.h"
#include "Engine/Utility/Xml/XmlDocument.h"

namespace sw::editor
{
    namespace
    {
        /**
         * @struct VrmMaterialImporterInternal
         * @brief 키 표 · 값 읽기 · 머티리얼 XML 채우기입니다.
         */
        struct VrmMaterialImporterInternal
        {
            /** @brief 엔진 툰 머티리얼 틀입니다. 프로퍼티 목록 · 퍼뮤테이션은 이 파일 하나가 정본입니다. */
            static constexpr string_view kToonMaterialTemplate = "engine/materials/toon.material";

            /** @brief 0.x 원소의 최상위 키입니다. */
            static constexpr string_view kArrVrm0RootKey[] = { "name", "shader", "renderQueue", "floatProperties", "vectorProperties", "textureProperties", "keywordMap", "tagMap" };
            /** @brief 0.x 에서 옮기는 실수 키입니다. */
            static constexpr string_view kArrVrm0Float[] = { "_Cutoff", "_BlendMode", "_CullMode",
                                                             "_ShadeShift", "_ShadeToony", "_ReceiveShadowRate",
                                                             "_RimFresnelPower", "_RimLift", "_RimLightingMix",
                                                             "_OutlineWidth", "_OutlineWidthMode", "_OutlineScaledMaxDistance",
                                                             "_OutlineColorMode", "_OutlineLightingMix" };
            /**
             * @brief 0.x 에서 알지만 옮기지 않는 실수 키와 그 키가 효과 없는 값입니다. 다른 값이면 무시 목록에 남깁니다.
             * @details 블렌드 상태(_SrcBlend · _DstBlend · _ZWrite · _AlphaToMask)는 _BlendMode 에서 나오고 _DebugMode · _MToonVersion 은 그림과 무관해 늘 조용히 넘긴다.
             */
            struct IgnoredFloat
            {
                string_view _key;
                float32     _neutral;
                bool        _bAlwaysSilent;
            };
            static constexpr IgnoredFloat kArrVrm0IgnoredFloat[] = {
                {             "_BumpScale", 1.0f, false}, // 노멀 맵 없음
                {      "_ShadingGradeRate", 1.0f,  true}, // 셰이딩 그레이드 텍스처가 없으면 효과가 없다
                { "_LightColorAttenuation", 0.0f, false},
                {"_IndirectLightIntensity", 0.1f,  true}, // 환경광이 균일해 섞을 것이 없다
                {       "_OutlineCullMode", 1.0f, false}, // 외곽선은 늘 앞면 컬링
                {         "_UvAnimScrollX", 0.0f, false},
                {         "_UvAnimScrollY", 0.0f, false},
                {        "_UvAnimRotation", 0.0f, false},
                {             "_DebugMode", 0.0f,  true},
                {              "_SrcBlend", 0.0f,  true},
                {              "_DstBlend", 0.0f,  true},
                {                "_ZWrite", 0.0f,  true},
                {           "_AlphaToMask", 0.0f,  true},
                {          "_MToonVersion", 0.0f,  true},
            };
            /** @brief 0.x 에서 옮기는 색 키입니다. */
            static constexpr string_view kArrVrm0Color[] = { "_Color", "_ShadeColor", "_EmissionColor", "_RimColor", "_OutlineColor" };
            /** @brief 0.x 텍스처 키 — 옮기는 넷과 알지만 옮기지 않는 것들입니다. 같은 이름의 벡터 키는 텍스처 타일링([x, y, 크기 x, 크기 y])입니다. */
            static constexpr string_view kArrVrm0Texture[]        = { "_MainTex", "_ShadeTexture", "_EmissionMap", "_SphereAdd" };
            static constexpr string_view kArrVrm0IgnoredTexture[] = { "_BumpMap", "_ReceiveShadowTexture", "_ShadingGradeTexture", "_RimTexture", "_OutlineWidthTexture",
                                                                      "_UvAnimMaskTexture" };
            /** @brief 0.x 의 MToon 이 아닌 셰이더 — glTF 기본 머티리얼을 평면 툰으로 옮깁니다. */
            static constexpr string_view kArrVrm0FlatShader[] = { "VRM_USE_GLTFSHADER", "VRM/UnlitTexture", "VRM/UnlitCutout", "VRM/UnlitTransparent",
                                                                  "VRM/UnlitTransparentZWrite" };

            /** @brief 1.0 `VRMC_materials_mtoon` 의 키 — 옮기는 것과 알지만 옮기지 않는 것들입니다. */
            static constexpr string_view kArrMtoon1Key[]        = { "specVersion",
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
                                                                    "extensions",
                                                                    "extras" };
            static constexpr string_view kArrMtoon1IgnoredKey[] = { "shadingShiftTexture",
                                                                    "rimMultiplyTexture",
                                                                    "outlineWidthMultiplyTexture",
                                                                    "uvAnimationMaskTexture",
                                                                    "uvAnimationScrollXSpeedFactor",
                                                                    "uvAnimationScrollYSpeedFactor",
                                                                    "uvAnimationRotationSpeedFactor" };

            template <size_t Count>
            static bool contains( const string_view ( &arrKey )[Count], string_view key )
            {
                for ( const string_view known : arrKey )
                {
                    if ( known == key )
                        return true;
                }
                return false;
            }

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
                if ( std::find( pOutListIgnored->begin(), pOutListIgnored->end(), name ) == pOutListIgnored->end() )
                    pOutListIgnored->push_back( name );
            }

            /** @brief 객체의 키를 표와 대조합니다. 모르는 키가 있으면 그 이름을 @p outError 에 쓰고 false 입니다. */
            template <size_t Count>
            [[nodiscard]] static bool checkKeys( const JsonValue& object, const string_view ( &arrKnown )[Count], string_view context, string& outError )
            {
                for ( const string& key : object.getMemberNames() )
                {
                    if ( contains( arrKnown, key ) == false )
                    {
                        outError = string( context ) + ": unknown key '" + key + "'";
                        return false;
                    }
                }
                return true;
            }

            /** @brief 숫자 @p count 개의 배열을 읽습니다. 모자란 칸은 그대로 둡니다. 배열이 아니거나 숫자가 아니면 false 입니다. */
            [[nodiscard]] static bool readNumbers( const JsonValue& value, float32* pOutValue, uint32 count )
            {
                if ( value.isArray() == false || value.size() < count )
                    return false;
                for ( uint32 index = 0; index < count; ++index )
                {
                    const JsonValue element = value.at( index );
                    if ( element.isNumber() == false )
                        return false;
                    pOutValue[index] = static_cast<float32>( element.asFloat() );
                }
                return true;
            }

            /** @brief 색(3 또는 4 칸)을 읽습니다. 3 칸이면 알파는 1 입니다. */
            [[nodiscard]] static bool readColor( const JsonValue& value, float4& outColor )
            {
                float32 arrValue[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
                if ( value.isArray() == false || ( value.size() != 3 && value.size() != 4 ) || readNumbers( value, arrValue, static_cast<uint32>( value.size() ) ) == false )
                    return false;
                outColor = float4{ arrValue[0], arrValue[1], arrValue[2], arrValue[3] };
                return true;
            }

            static float4 convertColorToLinear( const float4& gamma )
            {
                return float4{ VrmMaterialImporter::convertGammaToLinear( gamma._x ), VrmMaterialImporter::convertGammaToLinear( gamma._y ),
                               VrmMaterialImporter::convertGammaToLinear( gamma._z ), gamma._w };
            }

            /** @brief glTF textureInfo 의 텍스처 번호입니다. 없으면 -1 입니다. 지원하지 않는 텍스처 확장(KHR_texture_transform)이 있으면 무시 목록에 남깁니다. */
            static int32 readTextureIndex( const JsonValue& textureInfo, vector<string>* pOutListIgnored )
            {
                if ( textureInfo.isObject() == false || textureInfo.get( "index", false ).isNumber() == false )
                    return -1;
                const JsonValue extensions = textureInfo.get( "extensions", false );
                if ( extensions.isObject() )
                {
                    for ( const string& extension : extensions.getMemberNames() )
                    {
                        appendIgnored( pOutListIgnored, extension );
                    }
                }
                if ( textureInfo.get( "texCoord", false ).asInt( 0 ) != 0 )
                    appendIgnored( pOutListIgnored, "texCoord" );
                return static_cast<int32>( textureInfo.get( "index", false ).asInt( -1 ) );
            }

            /**
             * @brief glTF 기본 머티리얼(baseColor · alphaMode · alphaCutoff · doubleSided · emissive)을 읽습니다. 1.0 의 MToon 과 0.x 의 평면 셰이더가 함께 씁니다.
             * @return alphaMode 를 모르면 false 입니다.
             */
            [[nodiscard]] static bool readGltfBase( const JsonValue& gltfMaterial, ToonMaterialDesc& inoutDesc, vector<string>* pOutListIgnored, string& outError )
            {
                if ( gltfMaterial.isObject() == false )
                    return true;
                const JsonValue pbr = gltfMaterial.get( "pbrMetallicRoughness", false );
                if ( pbr.isObject() )
                {
                    if ( pbr.has( "baseColorFactor", false ) && readColor( pbr.get( "baseColorFactor", false ), inoutDesc._baseColor ) == false )
                    {
                        outError = "baseColorFactor is not a color";
                        return false;
                    }
                    inoutDesc._baseColorTexture = readTextureIndex( pbr.get( "baseColorTexture", false ), pOutListIgnored );
                }
                const string alphaMode = gltfMaterial.has( "alphaMode", false ) ? gltfMaterial.get( "alphaMode", false ).asString() : string( "OPAQUE" );
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
                inoutDesc._alphaCutoff = static_cast<float32>( gltfMaterial.get( "alphaCutoff", false ).asFloat( 0.5 ) );
                inoutDesc._bTwoSided   = gltfMaterial.get( "doubleSided", false ).asBool( false ) ? SW_TRUE : SW_FALSE;
                if ( gltfMaterial.has( "emissiveFactor", false ) && readColor( gltfMaterial.get( "emissiveFactor", false ), inoutDesc._emissiveColor ) == false )
                {
                    outError = "emissiveFactor is not a color";
                    return false;
                }
                inoutDesc._emissiveTexture = readTextureIndex( gltfMaterial.get( "emissiveTexture", false ), pOutListIgnored );
                if ( gltfMaterial.has( "normalTexture", false ) )
                    appendIgnored( pOutListIgnored, "normalTexture" );
                const JsonValue extensions = gltfMaterial.get( "extensions", false );
                const JsonValue strength   = extensions.isObject() ? extensions.get( "KHR_materials_emissive_strength", false ) : JsonValue{};
                if ( strength.isObject() )
                    inoutDesc._emissiveStrength = static_cast<float32>( strength.get( "emissiveStrength", false ).asFloat( 1.0 ) );
                return true;
            }

            /** @brief 그림자 없이 기본색 하나로 칠하는 평면 툰입니다(언릿 · glTF 셰이더). */
            static void makeFlat( ToonMaterialDesc& inoutDesc )
            {
                inoutDesc._shadeColor   = inoutDesc._baseColor;
                inoutDesc._shadeTexture = inoutDesc._baseColorTexture;
                inoutDesc._shadingToony = 1.0f;
                inoutDesc._outlineMode  = ToonOutlineMode::None;
            }

            /** @brief 소수 여섯 자리까지의 글입니다(XML 에 쓰는 값). */
            static string formatNumber( float32 value )
            {
                utf8 arrText[constant::kMaxBuffer32]{};
                (void)snprintf( arrText, sizeof( arrText ), "%.6g", static_cast<float64>( value ) );
                return string( arrText );
            }

            static string formatColor( const float4& color )
            {
                return formatNumber( color._x ) + " " + formatNumber( color._y ) + " " + formatNumber( color._z ) + " " + formatNumber( color._w );
            }

            /** @brief `_properties` 에서 이름의 항목입니다. */
            static XmlNode findProperty( const XmlNode& properties, string_view name )
            {
                for ( XmlNode item = properties.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
                {
                    if ( item.getAttributeText( "name" ) == name )
                        return item;
                }
                return XmlNode{};
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    float32 VrmMaterialImporter::convertGammaToLinear( float32 value )
    {
        // sRGB 전달 함수(IEC 61966-2-1)의 역. 유니티가 감마 색을 선형으로 바꾸는 식과 같다.
        if ( value <= 0.04045f )
            return value / 12.92f;
        return MathUtil::pow( ( value + 0.055f ) / 1.055f, 2.4f );
    }

    bool VrmMaterialImporter::readVrm0Material( const JsonValue& materialProperty, const JsonValue& gltfMaterial, ToonMaterialDesc& outDesc,
                                                vector<string>* pOutListIgnored, string& outError )
    {
        using Internal = VrmMaterialImporterInternal;
        outDesc        = ToonMaterialDesc{};
        if ( materialProperty.isObject() == false )
        {
            outError = "materialProperties element is not an object";
            return false;
        }
        outDesc._name        = materialProperty.get( "name", false ).asString();
        const string context = "VRM 0.x material '" + outDesc._name + "'";
        const string shader  = materialProperty.get( "shader", false ).asString();
        if ( Internal::checkKeys( materialProperty, Internal::kArrVrm0RootKey, context, outError ) == false )
            return false;

        // MToon 이 아닌 셰이더(언릿 · glTF 셰이더)는 glTF 머티리얼을 평면 툰으로 옮긴다.
        if ( Internal::contains( Internal::kArrVrm0FlatShader, shader ) )
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

        // 실수
        const JsonValue floats = materialProperty.get( "floatProperties", false );
        float32         arrFloat[sizeof( Internal::kArrVrm0Float ) / sizeof( Internal::kArrVrm0Float[0] )]{};
        bool            arrHasFloat[sizeof( Internal::kArrVrm0Float ) / sizeof( Internal::kArrVrm0Float[0] )]{};
        auto            findFloat = [&]( string_view key, float32 fallback ) -> float32
        {
            for ( size_t index = 0; index < sizeof( Internal::kArrVrm0Float ) / sizeof( Internal::kArrVrm0Float[0] ); ++index )
            {
                if ( Internal::kArrVrm0Float[index] == key )
                    return arrHasFloat[index] ? arrFloat[index] : fallback;
            }
            return fallback;
        };
        for ( const string& key : floats.isObject() ? floats.getMemberNames() : vector<string>{} )
        {
            const JsonValue value = floats.get( key, false );
            if ( value.isNumber() == false )
            {
                outError = context + ": float property '" + key + "' is not a number";
                return false;
            }
            bool bKnown = false;
            for ( size_t index = 0; index < sizeof( Internal::kArrVrm0Float ) / sizeof( Internal::kArrVrm0Float[0] ); ++index )
            {
                if ( Internal::kArrVrm0Float[index] != key )
                    continue;
                arrFloat[index]    = static_cast<float32>( value.asFloat() );
                arrHasFloat[index] = true;
                bKnown             = true;
            }
            if ( bKnown )
                continue;
            const Internal::IgnoredFloat* pIgnored = Internal::findIgnoredFloat( key );
            if ( pIgnored == nullptr )
            {
                outError = context + ": unknown float property '" + key + "'";
                return false;
            }
            if ( pIgnored->_bAlwaysSilent == false && MathUtil::abs( static_cast<float32>( value.asFloat() ) - pIgnored->_neutral ) > 1e-4f )
                Internal::appendIgnored( pOutListIgnored, key );
        }

        // 색
        const JsonValue vectors                                                                            = materialProperty.get( "vectorProperties", false );
        float4          arrColor[sizeof( Internal::kArrVrm0Color ) / sizeof( Internal::kArrVrm0Color[0] )] = {
            float4{1.0f, 1.0f, 1.0f, 1.0f},
            float4{0.0f, 0.0f, 0.0f, 1.0f},
            float4{0.0f, 0.0f, 0.0f, 1.0f},
            float4{0.0f, 0.0f, 0.0f, 1.0f},
            float4{0.0f, 0.0f, 0.0f, 1.0f}
        };
        for ( const string& key : vectors.isObject() ? vectors.getMemberNames() : vector<string>{} )
        {
            const JsonValue value  = vectors.get( key, false );
            bool            bKnown = false;
            for ( size_t index = 0; index < sizeof( Internal::kArrVrm0Color ) / sizeof( Internal::kArrVrm0Color[0] ); ++index )
            {
                if ( Internal::kArrVrm0Color[index] != key )
                    continue;
                float4 color{};
                if ( Internal::readColor( value, color ) == false )
                {
                    outError = context + ": vector property '" + key + "' is not a color";
                    return false;
                }
                arrColor[index] = Internal::convertColorToLinear( color );
                bKnown          = true;
            }
            if ( bKnown )
                continue;
            if ( Internal::contains( Internal::kArrVrm0Texture, key ) == false && Internal::contains( Internal::kArrVrm0IgnoredTexture, key ) == false )
            {
                outError = context + ": unknown vector property '" + key + "'";
                return false;
            }
            // 텍스처 타일링 [x, y, 크기 x, 크기 y] — 늘 [0, 0, 1, 1] 이다. 다르면 지원하지 않는 타일링이다.
            float32 arrTiling[4]{ 0.0f, 0.0f, 1.0f, 1.0f };
            if ( Internal::readNumbers( value, arrTiling, 4 ) == false )
            {
                outError = context + ": texture tiling '" + key + "' is not four numbers";
                return false;
            }
            const bool bIdentity = arrTiling[0] == 0.0f && arrTiling[1] == 0.0f && arrTiling[2] == 1.0f && arrTiling[3] == 1.0f;
            if ( bIdentity == false )
                Internal::appendIgnored( pOutListIgnored, key + string( " tiling" ) );
        }

        // 텍스처(glTF 텍스처 번호)
        const JsonValue textures = materialProperty.get( "textureProperties", false );
        int32           arrTexture[4]{ -1, -1, -1, -1 };
        for ( const string& key : textures.isObject() ? textures.getMemberNames() : vector<string>{} )
        {
            const JsonValue value = textures.get( key, false );
            if ( value.isNumber() == false )
            {
                outError = context + ": texture property '" + key + "' is not a texture index";
                return false;
            }
            bool bKnown = false;
            for ( size_t index = 0; index < 4; ++index )
            {
                if ( Internal::kArrVrm0Texture[index] != key )
                    continue;
                arrTexture[index] = static_cast<int32>( value.asInt( -1 ) );
                bKnown            = true;
            }
            if ( bKnown )
                continue;
            if ( Internal::contains( Internal::kArrVrm0IgnoredTexture, key ) == false )
            {
                outError = context + ": unknown texture property '" + key + "'";
                return false;
            }
            Internal::appendIgnored( pOutListIgnored, key );
        }

        outDesc._baseColor        = arrColor[0];
        outDesc._shadeColor       = arrColor[1];
        outDesc._emissiveColor    = arrColor[2];
        outDesc._rimColor         = arrColor[3];
        outDesc._outlineColor     = arrColor[4];
        outDesc._baseColorTexture = arrTexture[0];
        outDesc._shadeTexture     = arrTexture[1];
        outDesc._emissiveTexture  = arrTexture[2];
        outDesc._matcapTexture    = arrTexture[3];

        // 블렌드 · 컬
        const int32 blendMode = static_cast<int32>( findFloat( "_BlendMode", 0.0f ) + 0.5f );
        if ( blendMode == 0 )
            outDesc._alphaMode = ToonAlphaMode::Opaque;
        else if ( blendMode == 1 )
            outDesc._alphaMode = ToonAlphaMode::Cutout;
        else if ( blendMode == 2 || blendMode == 3 ) // 3 = 깊이를 쓰는 반투명 — 엔진 반투명 패스는 깊이를 쓰지 않는다
            outDesc._alphaMode = ToonAlphaMode::Transparent;
        else
        {
            outError = context + ": unknown _BlendMode " + to_string( static_cast<int64>( blendMode ) );
            return false;
        }
        outDesc._alphaCutoff = findFloat( "_Cutoff", 0.5f );
        const int32 cullMode = static_cast<int32>( findFloat( "_CullMode", 2.0f ) + 0.5f );
        if ( cullMode != 0 && cullMode != 2 )
        {
            outError = context + ": unsupported _CullMode " + to_string( static_cast<int64>( cullMode ) ) + " (only Off = 0 and Back = 2)";
            return false;
        }
        outDesc._bTwoSided = ( cullMode == 0 ) ? SW_TRUE : SW_FALSE;

        // 계단 — 0.x 는 [shift, lerp( 1, shift, toony )] 를 [0, 1] 로 편다(N·L 기준). 1.0 은 [-1 + toony, 1 - toony] 를 N·L + shift 에 건다.
        // 같은 경계가 되게: 1.0 shift = -(아래 + 위) / 2, toony = 1 - (위 - 아래) / 2.
        const float32 shadeShift = findFloat( "_ShadeShift", 0.0f );
        const float32 shadeToony = findFloat( "_ShadeToony", 0.9f );
        const float32 lower      = shadeShift;
        const float32 upper      = MathUtil::lerp( 1.0f, shadeShift, shadeToony );
        outDesc._shadingShift    = -( lower + upper ) * 0.5f;
        outDesc._shadingToony    = MathUtil::clamp( 1.0f - ( upper - lower ) * 0.5f, 0.0f, 1.0f );
        outDesc._shadowReceive   = findFloat( "_ReceiveShadowRate", 1.0f );

        outDesc._rimFresnelPower = findFloat( "_RimFresnelPower", 1.0f );
        outDesc._rimLift         = findFloat( "_RimLift", 0.0f );
        outDesc._rimLightingMix  = findFloat( "_RimLightingMix", 0.0f );
        outDesc._matcapColor     = float4{ 1.0f, 1.0f, 1.0f, 1.0f };

        // 외곽선 — 0.x 월드 두께는 cm, 화면 두께는 NDC 의 1 % 단위(NDC 높이 2 = 화면 높이)라 화면 높이 비율은 × 0.005 다.
        const int32   outlineMode  = static_cast<int32>( findFloat( "_OutlineWidthMode", 0.0f ) + 0.5f );
        const float32 outlineWidth = findFloat( "_OutlineWidth", 0.0f );
        if ( outlineMode == 0 )
            outDesc._outlineMode = ToonOutlineMode::None;
        else if ( outlineMode == 1 )
            outDesc._outlineMode = ToonOutlineMode::World;
        else if ( outlineMode == 2 )
            outDesc._outlineMode = ToonOutlineMode::Screen;
        else
        {
            outError = context + ": unknown _OutlineWidthMode " + to_string( static_cast<int64>( outlineMode ) );
            return false;
        }
        outDesc._outlineWidth       = outlineWidth * ( outDesc._outlineMode == ToonOutlineMode::Screen ? 0.005f : 0.01f );
        outDesc._outlineMaxDistance = findFloat( "_OutlineScaledMaxDistance", 1.0f ) > 0.0f && outDesc._outlineMode == ToonOutlineMode::Screen
                                        ? findFloat( "_OutlineScaledMaxDistance", 1.0f )
                                        : 1000.0f;
        // _OutlineColorMode 0 = 고정 색(빛을 섞지 않는다), 1 = 섞는다.
        outDesc._outlineLightingMix = static_cast<int32>( findFloat( "_OutlineColorMode", 0.0f ) + 0.5f ) == 0 ? 0.0f : findFloat( "_OutlineLightingMix", 1.0f );
        return true;
    }

    bool VrmMaterialImporter::readMtoon1Material( const JsonValue& gltfMaterial, ToonMaterialDesc& outDesc, vector<string>* pOutListIgnored, string& outError )
    {
        using Internal       = VrmMaterialImporterInternal;
        outDesc              = ToonMaterialDesc{};
        outDesc._name        = gltfMaterial.get( "name", false ).asString();
        const string context = "VRM 1.0 material '" + outDesc._name + "'";
        if ( Internal::readGltfBase( gltfMaterial, outDesc, pOutListIgnored, outError ) == false )
        {
            outError = context + ": " + outError;
            return false;
        }
        const JsonValue extensions = gltfMaterial.get( "extensions", false );
        const JsonValue mtoon      = extensions.isObject() ? extensions.get( "VRMC_materials_mtoon", false ) : JsonValue{};
        if ( mtoon.isObject() == false )
        {
            // MToon 이 없는 머티리얼(KHR_materials_unlit · PBR)은 평면 툰이다.
            Internal::makeFlat( outDesc );
            return true;
        }
        for ( const string& key : mtoon.getMemberNames() )
        {
            if ( Internal::contains( Internal::kArrMtoon1Key, key ) )
                continue;
            if ( Internal::contains( Internal::kArrMtoon1IgnoredKey, key ) == false )
            {
                outError = context + ": unknown VRMC_materials_mtoon key '" + key + "'";
                return false;
            }
            Internal::appendIgnored( pOutListIgnored, key );
        }

        // 1.0 의 기본값(VRMC_materials_mtoon 스키마) — 키가 없으면 이 값이다.
        auto readFloat = [&]( string_view key, float32 fallback ) -> float32
        { return static_cast<float32>( mtoon.get( key, false ).asFloat( static_cast<float64>( fallback ) ) ); };
        auto readFactor = [&]( string_view key, const float4& fallback, float4& outColor ) -> bool
        {
            outColor = fallback;
            return mtoon.has( key, false ) == false || Internal::readColor( mtoon.get( key, false ), outColor );
        };
        if ( readFactor( "shadeColorFactor", float4{ 0.0f, 0.0f, 0.0f, 1.0f }, outDesc._shadeColor ) == false ||
             readFactor( "matcapFactor", float4{ 1.0f, 1.0f, 1.0f, 1.0f }, outDesc._matcapColor ) == false ||
             readFactor( "parametricRimColorFactor", float4{ 0.0f, 0.0f, 0.0f, 1.0f }, outDesc._rimColor ) == false ||
             readFactor( "outlineColorFactor", float4{ 0.0f, 0.0f, 0.0f, 1.0f }, outDesc._outlineColor ) == false )
        {
            outError = context + ": a color factor is not three or four numbers";
            return false;
        }
        outDesc._shadeTexture       = Internal::readTextureIndex( mtoon.get( "shadeMultiplyTexture", false ), pOutListIgnored );
        outDesc._matcapTexture      = Internal::readTextureIndex( mtoon.get( "matcapTexture", false ), pOutListIgnored );
        outDesc._shadingShift       = readFloat( "shadingShiftFactor", 0.0f );
        outDesc._shadingToony       = readFloat( "shadingToonyFactor", 0.9f );
        outDesc._rimLightingMix     = readFloat( "rimLightingMixFactor", 1.0f );
        outDesc._rimFresnelPower    = readFloat( "parametricRimFresnelPowerFactor", 5.0f );
        outDesc._rimLift            = readFloat( "parametricRimLiftFactor", 0.0f );
        outDesc._outlineWidth       = readFloat( "outlineWidthFactor", 0.0f );
        outDesc._outlineLightingMix = readFloat( "outlineLightingMixFactor", 1.0f );

        const string outlineMode = mtoon.has( "outlineWidthMode", false ) ? mtoon.get( "outlineWidthMode", false ).asString() : string( "none" );
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

    string VrmMaterialImporter::makeMaterialXml( const ToonMaterialDesc& desc, const vector<string>& listTexturePath )
    {
        using Internal = VrmMaterialImporterInternal;
        string templateText;
        if ( ResourceUtil::readTextResource( Internal::kToonMaterialTemplate, templateText ) == false )
            return {};
        XmlDocument document;
        if ( document.parse( templateText ) == false )
            return {};
        const XmlNode root       = document.getRoot( "MaterialDesc" );
        const XmlNode properties = root.findChild( "_properties" );
        if ( root.isValid() == false || properties.isValid() == false )
            return {};

        root.setAttribute( "name", string_view( desc._name.empty() ? "Toon" : desc._name.c_str() ) );
        root.setAttribute( "blendMode", desc._alphaMode == ToonAlphaMode::Transparent ? "Transparent" : "Opaque" );

        auto setValue = [&]( string_view name, const string& value )
        {
            const XmlNode item = Internal::findProperty( properties, name );
            if ( item.isValid() )
                item.setAttribute( "defaultValue", string_view( value ) );
        };
        auto setTexture = [&]( string_view name, int32 textureIndex )
        {
            const XmlNode item     = Internal::findProperty( properties, name );
            const bool    bInRange = 0 <= textureIndex && static_cast<size_t>( textureIndex ) < listTexturePath.size();
            if ( item.isValid() && bInRange && listTexturePath[static_cast<size_t>( textureIndex )].empty() == false )
                item.setAttribute( "assetPath", string_view( listTexturePath[static_cast<size_t>( textureIndex )] ) );
        };
        setValue( "baseColor", Internal::formatColor( desc._baseColor ) );
        setValue( "shadeColor", Internal::formatColor( desc._shadeColor ) );
        setValue( "emissiveColor", Internal::formatColor( desc._emissiveColor ) );
        setValue( "rimColor", Internal::formatColor( desc._rimColor ) );
        setValue( "matcapColor", Internal::formatColor( desc._matcapColor ) );
        setValue( "outlineColor", Internal::formatColor( desc._outlineColor ) );
        setValue( "shadingShift", Internal::formatNumber( desc._shadingShift ) );
        setValue( "shadingToony", Internal::formatNumber( desc._shadingToony ) );
        setValue( "shadowReceive", Internal::formatNumber( desc._shadowReceive ) );
        setValue( "emissiveStrength", Internal::formatNumber( desc._emissiveStrength ) );
        setValue( "rimFresnelPower", Internal::formatNumber( desc._rimFresnelPower ) );
        setValue( "rimLift", Internal::formatNumber( desc._rimLift ) );
        setValue( "rimLightingMix", Internal::formatNumber( desc._rimLightingMix ) );
        setValue( "outlineWidth", Internal::formatNumber( desc._outlineWidth ) );
        setValue( "outlineWidthMode", desc._outlineMode == ToonOutlineMode::Screen ? "1" : "0" );
        setValue( "outlineLightingMix", Internal::formatNumber( desc._outlineLightingMix ) );
        setValue( "outlineMaxDistance", Internal::formatNumber( desc._outlineMaxDistance ) );
        setValue( "alphaCutoff", Internal::formatNumber( desc._alphaCutoff ) );
        setTexture( "baseColorMap", desc._baseColorTexture );
        setTexture( "shadeMap", desc._shadeTexture );
        setTexture( "emissiveMap", desc._emissiveTexture );
        setTexture( "matcapMap", desc._matcapTexture );

        const XmlNode permutations = root.findChild( "_permutations" );
        const XmlNode switches     = permutations.findChild( "_staticSwitches" );
        for ( XmlNode item = switches.findChild( "item" ); item; item = item.findNextSibling( "item" ) )
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
            // 임포트한 머티리얼의 스위치는 에셋이 정한 값이다 — 틀(engine toon)이 런타임 스위치로 둔 것도 그 상태만 쿠킹한다.
            item.setAttribute( "bShaderFeature", "1" );
        }
        if ( desc._alphaMode == ToonAlphaMode::Transparent )
            permutations.findChild( "_alwaysDefines" ).appendChild( "item" ).setValue( "MATERIAL_BLEND_TRANSLUCENT" );
        return document.saveToString();
    }
} // namespace sw::editor
