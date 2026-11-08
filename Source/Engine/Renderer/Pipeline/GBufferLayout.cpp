#include "pch.h"

#include "Engine/Renderer/Pipeline/GBufferLayout.h"

#include "Core/Math/OctahedralNormal.h"

namespace sw
{
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
                case GBufferChannelKind::VelocityU:
                case GBufferChannelKind::VelocityV:
                {
                    // 값 그대로 — 화면 속도는 UV 차이(부호 있는 실수)라 풀 것이 없다.
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
                case GBufferChannelKind::Drawn:
                {
                    // 8 비트에 실린 0/1 이라 가운데에서 가른다(셰이더 읽기와 같다).
                    outListValue.push_back( GBufferDecodedValue{
                        channelSpec._pMeaning, float3{ arrRaw[channel] >= 0.5f ? 1.0f : 0.0f, 0.0f, 0.0f },
                        1
                    } );
                    break;
                }
            }
        }
    }
} // namespace sw
