#include "pch.h"

#include "Editor/Panels/MaterialPreviewShading.h"

#include "Core/Container/StringUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/string_splitter.h"

#include "Engine/Graphics/Material/MaterialTypes.h"

namespace sw::editor
{
    namespace
    {
        struct MaterialPreviewShadingInternal
        {
            /** @brief 주변광 세기입니다. */
            static constexpr float32 kAmbient = 0.12f;
            /** @brief 평균을 낼 때 한 변의 표본 수입니다. */
            static constexpr int32 kSampleSide = 24;

            static float32 toLinear( float32 srgb )
            {
                return srgb <= 0.04045f ? srgb / 12.92f : MathUtil::pow( ( srgb + 0.055f ) / 1.055f, 2.4f );
            }

            static float32 toSrgb( float32 linear )
            {
                const float32 clamped = MathUtil::clamp( linear, 0.0f, 1.0f );
                return clamped <= 0.0031308f ? clamped * 12.92f : 1.055f * MathUtil::pow( clamped, 1.0f / 2.4f ) - 0.055f;
            }

            static const MaterialProperty* findByNames( const vector<MaterialProperty>& listProperty, std::initializer_list<string_view> listName )
            {
                for ( const string_view name : listName )
                {
                    for ( const MaterialProperty& prop : listProperty )
                    {
                        if ( StringUtil::equals( string_view{ prop._name }, name, true ) )
                            return &prop;
                    }
                }
                return nullptr;
            }

            static const string& readValueText( const MaterialProperty& prop ) { return prop._value.empty() ? prop._defaultValue : prop._value; }

            /** @brief 색 값을 선형 색으로 읽습니다. sRGB 로 적힌 값(`bSrgb`)은 풉니다. */
            static float3 readColor( const MaterialProperty& prop )
            {
                float32 arrValue[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
                (void)MaterialPreviewShading::parseFloats( readValueText( prop ), arrValue, 4 ); // 미리보기 표시 — 못 읽은 칸 · 모자란 칸은 0 이다
                if ( prop._bSrgb == SW_FALSE )
                    return float3{ arrValue[0], arrValue[1], arrValue[2] };
                return float3{ toLinear( arrValue[0] ), toLinear( arrValue[1] ), toLinear( arrValue[2] ) };
            }

            static float32 readScalar( const MaterialProperty& prop, float32 fallback )
            {
                float32 value = fallback;
                if ( MaterialPreviewShading::parseFloats( readValueText( prop ), &value, 1 ) == 0 )
                    return fallback;
                return value;
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    uint32 MaterialPreviewShading::parseFloats( string_view text, float32* pOut, uint32 count )
    {
        if ( pOut == nullptr || count == 0 )
            return 0;
        for ( uint32 index = 0; index < count; ++index )
        {
            pOut[index] = 0.0f;
        }
        if ( text.empty() )
            return 0;
        const string_splitter splitter( text, { ",", " ", "\t" } );
        uint32                filled{ 0 };
        for ( const string_view token : splitter.getSplitList() )
        {
            if ( filled >= count )
                break;
            if ( token.empty() )
                continue;
            (void)StringUtil::parseFloat( token, pOut[filled] ); // 화면 표시 — 못 읽은 칸은 0
            ++filled;
        }
        return filled;
    }

    MaterialPreviewInputs MaterialPreviewShading::readInputs( const vector<MaterialProperty>& listProperty )
    {
        MaterialPreviewInputs   inputs{};
        const MaterialProperty* pBase = MaterialPreviewShadingInternal::findByNames( listProperty, { "color", "baseColor", "albedo", "albedoColor" } );
        if ( pBase == nullptr )
        {
            for ( const MaterialProperty& prop : listProperty )
            {
                if ( prop._type == MaterialPropertyType::Color )
                {
                    pBase = &prop;
                    break;
                }
            }
        }
        if ( pBase != nullptr )
            inputs._baseColor = MaterialPreviewShadingInternal::readColor( *pBase );
        if ( const MaterialProperty* pEmissive = MaterialPreviewShadingInternal::findByNames( listProperty, { "emissive", "emission", "emissiveColor" } ) )
            inputs._emissive = MaterialPreviewShadingInternal::readColor( *pEmissive );
        if ( const MaterialProperty* pRoughness = MaterialPreviewShadingInternal::findByNames( listProperty, { "roughness" } ) )
            inputs._roughness = MathUtil::clamp( MaterialPreviewShadingInternal::readScalar( *pRoughness, 0.5f ), 0.0f, 1.0f );
        if ( const MaterialProperty* pMetallic = MaterialPreviewShadingInternal::findByNames( listProperty, { "metallic", "metalness" } ) )
            inputs._metallic = MathUtil::clamp( MaterialPreviewShadingInternal::readScalar( *pMetallic, 0.0f ), 0.0f, 1.0f );
        return inputs;
    }

    float3 MaterialPreviewShading::shadePoint( const MaterialPreviewInputs& inputs, float32 nx, float32 ny )
    {
        const float32 radiusSq = nx * nx + ny * ny;
        if ( radiusSq > 1.0f )
            return float3{ 0.0f, 0.0f, 0.0f };

        // 화면 좌표(아래가 +y)를 오른손 법선(위가 +y, 보는 쪽이 +z)으로 바꾼다.
        const float32 normalX = nx;
        const float32 normalY = -ny;
        const float32 normalZ = MathUtil::sqrt( 1.0f - radiusSq );

        // 빛은 왼쪽 위 앞에서 온다(언리얼 · 유니티 미리보기의 기본 구도).
        const float32 lightLength = MathUtil::sqrt( 0.5f * 0.5f + 0.7f * 0.7f + 0.6f * 0.6f );
        const float32 lightX      = -0.5f / lightLength;
        const float32 lightY      = 0.7f / lightLength;
        const float32 lightZ      = 0.6f / lightLength;
        const float32 diffuse     = MathUtil::max( 0.0f, normalX * lightX + normalY * lightY + normalZ * lightZ );

        // 블린-퐁 하이라이트 — 거칠수록 넓고 약하다. 금속은 하이라이트가 기본색을 띤다.
        float32       halfX      = lightX;
        float32       halfY      = lightY;
        float32       halfZ      = lightZ + 1.0f;
        const float32 halfLength = MathUtil::sqrt( halfX * halfX + halfY * halfY + halfZ * halfZ );
        halfX /= halfLength;
        halfY /= halfLength;
        halfZ /= halfLength;
        const float32 smoothness = 1.0f - inputs._roughness;
        const float32 exponent   = 2.0f + smoothness * smoothness * 254.0f;
        const float32 specular   = MathUtil::pow( MathUtil::max( 0.0f, normalX * halfX + normalY * halfY + normalZ * halfZ ), exponent ) * ( 0.04f + 0.96f * smoothness ) *
                                 ( diffuse > 0.0f ? 1.0f : 0.0f );

        const float32 diffuseWeight = 1.0f - inputs._metallic;
        float32       arrChannel[3]{ inputs._baseColor._x, inputs._baseColor._y, inputs._baseColor._z };
        const float32 arrEmissive[3]{ inputs._emissive._x, inputs._emissive._y, inputs._emissive._z };
        for ( uint32 channel = 0; channel < 3; ++channel )
        {
            const float32 base         = arrChannel[channel];
            const float32 specularTint = 0.04f + ( base - 0.04f ) * inputs._metallic;
            const float32 linear       = base * diffuseWeight * ( MaterialPreviewShadingInternal::kAmbient + diffuse ) + specular * specularTint * 4.0f + arrEmissive[channel];
            arrChannel[channel]        = MaterialPreviewShadingInternal::toSrgb( linear );
        }
        return float3{ arrChannel[0], arrChannel[1], arrChannel[2] };
    }

    float32 MaterialPreviewShading::computeMeanRedMinusBlue( const MaterialPreviewInputs& inputs )
    {
        constexpr int32 kSide = MaterialPreviewShadingInternal::kSampleSide;
        float32         sum{ 0.0f };
        uint32          sampleCount{ 0 };
        for ( int32 sampleY = 0; sampleY < kSide; ++sampleY )
        {
            for ( int32 sampleX = 0; sampleX < kSide; ++sampleX )
            {
                const float32 nx = ( static_cast<float32>( sampleX ) + 0.5f ) / static_cast<float32>( kSide ) * 2.0f - 1.0f;
                const float32 ny = ( static_cast<float32>( sampleY ) + 0.5f ) / static_cast<float32>( kSide ) * 2.0f - 1.0f;
                if ( nx * nx + ny * ny > 1.0f )
                    continue;
                const float3 color = shadePoint( inputs, nx, ny );
                sum += ( color._x - color._z ) * 255.0f;
                ++sampleCount;
            }
        }
        return sampleCount > 0 ? sum / static_cast<float32>( sampleCount ) : 0.0f;
    }
} // namespace sw::editor
