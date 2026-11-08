#include "pch.h"

#include "Engine/Renderer/Pipeline/GBufferLayout.h"

#include "Core/Math/OctahedralNormal.h"

namespace sw
{
    namespace
    {
        /// 번호 → 이름입니다. 순서는 ShadingModelID 와 같습니다(디버그 표시의 값 줄 이름).
        constexpr const utf8* s_arrShadingModelName[static_cast<uint32>( ShadingModelID::Count )] = {
            "Shading model: None",
            "Shading model: Unlit",
            "Shading model: DefaultLit",
            "Shading model: Subsurface",
            "Shading model: Cloth",
            "Shading model: ClearCoat",
            "Shading model: Hair",
            "Shading model: Eye",
            "Shading model: ThinTranslucent",
            "Shading model: Anisotropic",
        };
    } // namespace

    ShadingModelID GBufferLayout::decodeShadingModel( float32 encoded )
    {
        const float32 clamped = encoded < 0.0f ? 0.0f : ( encoded > 1.0f ? 1.0f : encoded );
        const uint32  id      = static_cast<uint32>( clamped * SW_SHADING_MODEL_ID_SCALE + 0.5f );
        return id < static_cast<uint32>( ShadingModelID::Count ) ? static_cast<ShadingModelID>( id ) : ShadingModelID::Count;
    }

    const utf8* GBufferLayout::getShadingModelName( ShadingModelID id )
    {
        const uint32 index = static_cast<uint32>( id );
        return index < static_cast<uint32>( ShadingModelID::Count ) ? s_arrShadingModelName[index] : "Shading model: (out of table)";
    }

    const GBufferAttachmentSpec* GBufferLayout::findAttachment( string_view name )
    {
        for ( const GBufferAttachmentSpec& spec : kArrAttachment )
        {
            if ( name == string_view( spec._pName ) )
                return &spec;
        }
        return nullptr;
    }

    void GBufferLayout::decodeTexel( const GBufferAttachmentSpec& spec, const float4& raw, vector<GBufferDecodedValue>& outListValue )
    {
        outListValue.clear();
        const float32 arrRaw[4] = { raw._x, raw._y, raw._z, raw._w };
        for ( uint32 channel = 0; channel < spec._channelCount; ++channel )
        {
            const GBufferChannelSpec& channelSpec = spec._arrChannel[channel];
            switch ( channelSpec._kind )
            {
                case GBufferChannelKind::Unused:
                case GBufferChannelKind::OctahedralV:
                    break; // v 는 u 와 함께 노멀 하나로 푼다
                case GBufferChannelKind::Unorm:
                case GBufferChannelKind::Float:
                {
                    outListValue.push_back( GBufferDecodedValue{
                        channelSpec._pMeaning, float3{ arrRaw[channel], 0.0f, 0.0f },
                        1
                    } );
                    break;
                }
                case GBufferChannelKind::OctahedralU:
                {
                    const float32 v = channel + 1 < 4 ? arrRaw[channel + 1] : 0.0f;
                    outListValue.push_back( GBufferDecodedValue{ "Normal", OctahedralNormal::decodeOctant( arrRaw[channel], v ), 3 } );
                    break;
                }
                case GBufferChannelKind::ShadingModel:
                {
                    // 셰이더 읽기와 같은 반올림(번호 / 255). 이름 줄이 모델을 말한다.
                    const ShadingModelID id = decodeShadingModel( arrRaw[channel] );
                    outListValue.push_back( GBufferDecodedValue{
                        getShadingModelName( id ), float3{ static_cast<float32>( id ), 0.0f, 0.0f },
                        1
                    } );
                    break;
                }
            }
        }
    }
} // namespace sw
