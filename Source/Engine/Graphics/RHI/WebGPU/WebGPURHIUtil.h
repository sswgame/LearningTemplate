/**
 * @file WebGPURHIUtil.h
 * @brief WebGPU 백엔드 조각들이 함께 쓰는 변환 표 · 글 뷰 도우미입니다(형식 · 블렌드 · 비교 · 스텐실 · 위상 · 정점 형식).
 * @details 헤더는 `webgpu/webgpu.h`(webgpu-headers — Dawn 과 Emscripten 의 emdawnwebgpu 포트가 같은 헤더를 낸다)다. 판은 붙일 때 고정한다(docs/PlatformTargets.md):
 *          콜백 모양(콜백 정보 구조체 + userdata 둘)과 `WGPUStringView` 는 2025 년 판 기준이다(기억). 판이 바뀌면 이 파일과 콜백을 받는 자리만 고친다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Graphics/RHI/RHIDescriptors.h"
#include "Engine/Graphics/RHI/RHIFormatInfo.h"
#include "Engine/Graphics/RenderState/RenderState.h"

#include <webgpu/webgpu.h>

namespace sw
{
    /** @brief WebGPU 백엔드의 순수 변환입니다. 장치 상태를 모른다 — 장치가 고르는 대체(R16G16_SNORM · 깊이)는 `WebGPURHIDevice::toTextureFormat` 이 한다. */
    struct WebGPURHIUtil
    {
        /** @brief RHIFormat 하나의 WebGPU 형식입니다. 없는 형식은 `Undefined` 다(`supportsTextureFormat` 이 거짓). */
        struct TextureFormatRow
        {
            RHIFormat         _format;
            WGPUTextureFormat _wgpuFormat;
            WGPUFeatureName   _requiredFeature; ///< 이 형식에 필요한 장치 기능(없으면 `WGPUFeatureName_Force32` 을 "필요 없음" 으로 쓴다)
        };

        static constexpr WGPUFeatureName kNoFeature = WGPUFeatureName_Force32;

        // R16G16_SNORM 은 코어에 없다 — `texture-formats-tier1` 이 있으면 그대로, 없으면 장치가 RG16Float 로 만든다(`WebGPURHIDevice::toTextureFormat`).
        // D24S8 은 복사 원본이 될 수 없어 장치가 Depth32FloatStencil8 을 먼저 고른다. R32G32B32_FLOAT 는 텍스처 형식이 없다(Texture2D 가 네 채널로 편다).
        static constexpr TextureFormatRow kArrTextureFormatRow[] = {
            {    RHIFormat::R8G8B8A8_UNORM,          WGPUTextureFormat_RGBA8Unorm,                             kNoFeature},
            {    RHIFormat::B8G8R8A8_UNORM,          WGPUTextureFormat_BGRA8Unorm,                             kNoFeature},
            {RHIFormat::R16G16B16A16_FLOAT,         WGPUTextureFormat_RGBA16Float,                             kNoFeature},
            { RHIFormat::D24_UNORM_S8_UINT, WGPUTextureFormat_Depth24PlusStencil8,                             kNoFeature},
            {   RHIFormat::R32G32B32_FLOAT,           WGPUTextureFormat_Undefined,                             kNoFeature},
            {      RHIFormat::R32G32_FLOAT,           WGPUTextureFormat_RG32Float,                             kNoFeature},
            {         RHIFormat::R32_FLOAT,            WGPUTextureFormat_R32Float,                             kNoFeature},
            {           RHIFormat::Unknown,           WGPUTextureFormat_Undefined,                             kNoFeature},
            {         RHIFormat::BC1_UNORM,        WGPUTextureFormat_BC1RGBAUnorm,   WGPUFeatureName_TextureCompressionBC},
            {         RHIFormat::BC2_UNORM,        WGPUTextureFormat_BC2RGBAUnorm,   WGPUFeatureName_TextureCompressionBC},
            {         RHIFormat::BC3_UNORM,        WGPUTextureFormat_BC3RGBAUnorm,   WGPUFeatureName_TextureCompressionBC},
            {         RHIFormat::BC4_UNORM,           WGPUTextureFormat_BC4RUnorm,   WGPUFeatureName_TextureCompressionBC},
            {         RHIFormat::BC5_UNORM,          WGPUTextureFormat_BC5RGUnorm,   WGPUFeatureName_TextureCompressionBC},
            {         RHIFormat::BC7_UNORM,        WGPUTextureFormat_BC7RGBAUnorm,   WGPUFeatureName_TextureCompressionBC},
            {         RHIFormat::BC6H_UF16,       WGPUTextureFormat_BC6HRGBUfloat,   WGPUFeatureName_TextureCompressionBC},
            {          RHIFormat::R8_UNORM,             WGPUTextureFormat_R8Unorm,                             kNoFeature},
            {      RHIFormat::R16G16_SNORM,           WGPUTextureFormat_RG16Snorm,    WGPUFeatureName_TextureFormatsTier1},
            {    RHIFormat::ASTC_4x4_UNORM,        WGPUTextureFormat_ASTC4x4Unorm, WGPUFeatureName_TextureCompressionASTC},
            {  RHIFormat::ETC2_RGBA8_UNORM,      WGPUTextureFormat_ETC2RGBA8Unorm, WGPUFeatureName_TextureCompressionETC2},
            {     RHIFormat::EAC_R11_UNORM,         WGPUTextureFormat_EACR11Unorm, WGPUFeatureName_TextureCompressionETC2},
            {    RHIFormat::EAC_RG11_UNORM,        WGPUTextureFormat_EACRG11Unorm, WGPUFeatureName_TextureCompressionETC2},
            {RHIFormat::R32G32B32A32_FLOAT,         WGPUTextureFormat_RGBA32Float,                             kNoFeature},
            {      RHIFormat::R16G16_FLOAT,           WGPUTextureFormat_RG16Float,                             kNoFeature},
        };

        /** @brief 표의 줄입니다. 범위 밖이면 `Unknown` 줄입니다. */
        static const TextureFormatRow& findTextureFormatRow( RHIFormat format )
        {
            const size_t index = static_cast<size_t>( format );
            return index < kRHIFormatCount ? kArrTextureFormatRow[index] : kArrTextureFormatRow[static_cast<size_t>( RHIFormat::Unknown )];
        }

        /** @brief 널 종료 글을 WebGPU 글 뷰로 만듭니다. */
        static WGPUStringView toStringView( const utf8* pText ) { return WGPUStringView{ pText, WGPU_STRLEN }; }
        /** @brief 글 뷰를 그대로 넘깁니다(길이를 아는 글). */
        static WGPUStringView toStringView( string_view text ) { return WGPUStringView{ text.data(), text.size() }; }
        /** @brief WebGPU 가 준 글 뷰를 엔진 글로 읽습니다. */
        static string_view fromStringView( WGPUStringView view )
        {
            if ( view.data == nullptr )
                return {};
            return view.length == WGPU_STRLEN ? string_view( view.data ) : string_view( view.data, view.length );
        }

        static WGPUBlendFactor toBlendFactor( BlendFactor factor )
        {
            switch ( factor )
            {
                case BlendFactor::Zero:
                    return WGPUBlendFactor_Zero;
                case BlendFactor::One:
                    return WGPUBlendFactor_One;
                case BlendFactor::SrcColor:
                    return WGPUBlendFactor_Src;
                case BlendFactor::InvSrcColor:
                    return WGPUBlendFactor_OneMinusSrc;
                case BlendFactor::SrcAlpha:
                    return WGPUBlendFactor_SrcAlpha;
                case BlendFactor::InvSrcAlpha:
                    return WGPUBlendFactor_OneMinusSrcAlpha;
                case BlendFactor::DstColor:
                    return WGPUBlendFactor_Dst;
                case BlendFactor::InvDstColor:
                    return WGPUBlendFactor_OneMinusDst;
                case BlendFactor::DstAlpha:
                    return WGPUBlendFactor_DstAlpha;
                case BlendFactor::InvDstAlpha:
                    return WGPUBlendFactor_OneMinusDstAlpha;
                case BlendFactor::SrcAlphaSaturate:
                    return WGPUBlendFactor_SrcAlphaSaturated;
            }
            return WGPUBlendFactor_One;
        }

        static WGPUBlendOperation toBlendOperation( BlendOp op )
        {
            switch ( op )
            {
                case BlendOp::Add:
                    return WGPUBlendOperation_Add;
                case BlendOp::Subtract:
                    return WGPUBlendOperation_Subtract;
                case BlendOp::ReverseSubtract:
                    return WGPUBlendOperation_ReverseSubtract;
                case BlendOp::Min:
                    return WGPUBlendOperation_Min;
                case BlendOp::Max:
                    return WGPUBlendOperation_Max;
            }
            return WGPUBlendOperation_Add;
        }

        static WGPUCompareFunction toCompareFunction( CompareFunc func )
        {
            switch ( func )
            {
                case CompareFunc::Never:
                    return WGPUCompareFunction_Never;
                case CompareFunc::Less:
                    return WGPUCompareFunction_Less;
                case CompareFunc::Equal:
                    return WGPUCompareFunction_Equal;
                case CompareFunc::LessEqual:
                    return WGPUCompareFunction_LessEqual;
                case CompareFunc::Greater:
                    return WGPUCompareFunction_Greater;
                case CompareFunc::NotEqual:
                    return WGPUCompareFunction_NotEqual;
                case CompareFunc::GreaterEqual:
                    return WGPUCompareFunction_GreaterEqual;
                case CompareFunc::Always:
                    return WGPUCompareFunction_Always;
            }
            return WGPUCompareFunction_Always;
        }

        static WGPUStencilOperation toStencilOperation( StencilOp op )
        {
            switch ( op )
            {
                case StencilOp::Keep:
                    return WGPUStencilOperation_Keep;
                case StencilOp::Zero:
                    return WGPUStencilOperation_Zero;
                case StencilOp::Replace:
                    return WGPUStencilOperation_Replace;
                case StencilOp::IncrementClamp:
                    return WGPUStencilOperation_IncrementClamp;
                case StencilOp::DecrementClamp:
                    return WGPUStencilOperation_DecrementClamp;
                case StencilOp::Invert:
                    return WGPUStencilOperation_Invert;
                case StencilOp::IncrementWrap:
                    return WGPUStencilOperation_IncrementWrap;
                case StencilOp::DecrementWrap:
                    return WGPUStencilOperation_DecrementWrap;
            }
            return WGPUStencilOperation_Keep;
        }

        static WGPUCullMode toCullMode( CullMode mode )
        {
            switch ( mode )
            {
                case CullMode::None:
                    return WGPUCullMode_None;
                case CullMode::Front:
                    return WGPUCullMode_Front;
                case CullMode::Back:
                    return WGPUCullMode_Back;
            }
            return WGPUCullMode_Back;
        }

        /** @brief 앞면 감기입니다. WebGPU 의 NDC · 프레임버퍼는 D3D 와 같은 방향이라 그대로 옮긴다(GL 처럼 뒤집지 않는다). */
        static WGPUFrontFace toFrontFace( FrontFace face ) { return face == FrontFace::Clockwise ? WGPUFrontFace_CW : WGPUFrontFace_CCW; }

        /** @brief 위상입니다. 패치 리스트는 WebGPU 에 없다 — 테셀레이션 PSO 는 만들기 전에 거른다(`WebGPURHIResourceFactory::createPipelineState`). */
        static WGPUPrimitiveTopology toTopology( RHIPrimitiveTopology topology )
        {
            switch ( topology )
            {
                case RHIPrimitiveTopology::TriangleList:
                    return WGPUPrimitiveTopology_TriangleList;
                case RHIPrimitiveTopology::LineList:
                    return WGPUPrimitiveTopology_LineList;
                case RHIPrimitiveTopology::PointList:
                    return WGPUPrimitiveTopology_PointList;
                case RHIPrimitiveTopology::PatchList3:
                    break;
            }
            return WGPUPrimitiveTopology_TriangleList;
        }

        static WGPUVertexFormat toVertexFormat( RHIVertexAttributeFormat format )
        {
            switch ( format )
            {
                case RHIVertexAttributeFormat::Float2:
                    return WGPUVertexFormat_Float32x2;
                case RHIVertexAttributeFormat::Float3:
                    return WGPUVertexFormat_Float32x3;
                case RHIVertexAttributeFormat::Float4:
                    return WGPUVertexFormat_Float32x4;
                case RHIVertexAttributeFormat::Uint1:
                    return WGPUVertexFormat_Uint32;
                case RHIVertexAttributeFormat::Snorm8x4:
                    return WGPUVertexFormat_Snorm8x4;
            }
            return WGPUVertexFormat_Float32x4;
        }

        static WGPULoadOp toLoadOp( RHIRenderPassLoadOp op )
        {
            // WebGPU 에는 DontCare 가 없다 — 지우는 것이 가장 싸다(타일 GPU 가 이전 내용을 읽지 않는다).
            return op == RHIRenderPassLoadOp::Load ? WGPULoadOp_Load : WGPULoadOp_Clear;
        }

        static WGPUStoreOp toStoreOp( RHIRenderPassStoreOp op ) { return op == RHIRenderPassStoreOp::Store ? WGPUStoreOp_Store : WGPUStoreOp_Discard; }
    };
    static_assert( isRHIFormatTableInOrder( WebGPURHIUtil::kArrTextureFormatRow ), "WebGPURHIUtil::kArrTextureFormatRow needs one row per RHIFormat, in enumerator order" );
} // namespace sw
