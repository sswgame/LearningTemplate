/**
 * @file RHIDxRenderState.h
 * @brief 렌더 상태 값(블렌드 · 비교 · 스텐실)을 D3D 값으로 옮깁니다(DX11, DX12 공유).
 * @details D3D11 과 D3D12 의 상태 열거는 이름 앞머리만 다르고 값이 같다 — 아래 `static_assert` 가 그것을 확인하고 변환은 한 벌로 둔다. `TDxEnum` 은 그 백엔드의 열거(`D3D11_BLEND` · `D3D12_BLEND` …)다.
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/EnginePlatformHeaders.h"
#include "Engine/Graphics/RHI/RHITypes.h"

#if defined( SW_PLATFORM_WINDOWS )

namespace sw
{
    static_assert( static_cast<int32>( D3D11_BLEND_ZERO ) == D3D12_BLEND_ZERO && static_cast<int32>( D3D11_BLEND_ONE ) == D3D12_BLEND_ONE && static_cast<int32>( D3D11_BLEND_SRC_COLOR ) == D3D12_BLEND_SRC_COLOR &&
                       static_cast<int32>( D3D11_BLEND_INV_SRC_COLOR ) == D3D12_BLEND_INV_SRC_COLOR && static_cast<int32>( D3D11_BLEND_SRC_ALPHA ) == D3D12_BLEND_SRC_ALPHA &&
                       static_cast<int32>( D3D11_BLEND_INV_SRC_ALPHA ) == D3D12_BLEND_INV_SRC_ALPHA && static_cast<int32>( D3D11_BLEND_DEST_COLOR ) == D3D12_BLEND_DEST_COLOR &&
                       static_cast<int32>( D3D11_BLEND_INV_DEST_COLOR ) == D3D12_BLEND_INV_DEST_COLOR && static_cast<int32>( D3D11_BLEND_DEST_ALPHA ) == D3D12_BLEND_DEST_ALPHA &&
                       static_cast<int32>( D3D11_BLEND_INV_DEST_ALPHA ) == D3D12_BLEND_INV_DEST_ALPHA && static_cast<int32>( D3D11_BLEND_SRC_ALPHA_SAT ) == D3D12_BLEND_SRC_ALPHA_SAT,
                   "D3D11_BLEND and D3D12_BLEND must share values" );
    static_assert( static_cast<int32>( D3D11_BLEND_OP_ADD ) == D3D12_BLEND_OP_ADD && static_cast<int32>( D3D11_BLEND_OP_SUBTRACT ) == D3D12_BLEND_OP_SUBTRACT &&
                       static_cast<int32>( D3D11_BLEND_OP_REV_SUBTRACT ) == D3D12_BLEND_OP_REV_SUBTRACT && static_cast<int32>( D3D11_BLEND_OP_MIN ) == D3D12_BLEND_OP_MIN && static_cast<int32>( D3D11_BLEND_OP_MAX ) == D3D12_BLEND_OP_MAX,
                   "D3D11_BLEND_OP and D3D12_BLEND_OP must share values" );
    static_assert( static_cast<int32>( D3D11_COMPARISON_NEVER ) == D3D12_COMPARISON_FUNC_NEVER && static_cast<int32>( D3D11_COMPARISON_LESS ) == D3D12_COMPARISON_FUNC_LESS &&
                       static_cast<int32>( D3D11_COMPARISON_EQUAL ) == D3D12_COMPARISON_FUNC_EQUAL && static_cast<int32>( D3D11_COMPARISON_LESS_EQUAL ) == D3D12_COMPARISON_FUNC_LESS_EQUAL &&
                       static_cast<int32>( D3D11_COMPARISON_GREATER ) == D3D12_COMPARISON_FUNC_GREATER && static_cast<int32>( D3D11_COMPARISON_NOT_EQUAL ) == D3D12_COMPARISON_FUNC_NOT_EQUAL &&
                       static_cast<int32>( D3D11_COMPARISON_GREATER_EQUAL ) == D3D12_COMPARISON_FUNC_GREATER_EQUAL && static_cast<int32>( D3D11_COMPARISON_ALWAYS ) == D3D12_COMPARISON_FUNC_ALWAYS,
                   "D3D11_COMPARISON_FUNC and D3D12_COMPARISON_FUNC must share values" );
    static_assert( static_cast<int32>( D3D11_STENCIL_OP_KEEP ) == D3D12_STENCIL_OP_KEEP && static_cast<int32>( D3D11_STENCIL_OP_ZERO ) == D3D12_STENCIL_OP_ZERO && static_cast<int32>( D3D11_STENCIL_OP_REPLACE ) == D3D12_STENCIL_OP_REPLACE &&
                       static_cast<int32>( D3D11_STENCIL_OP_INCR_SAT ) == D3D12_STENCIL_OP_INCR_SAT && static_cast<int32>( D3D11_STENCIL_OP_DECR_SAT ) == D3D12_STENCIL_OP_DECR_SAT && static_cast<int32>( D3D11_STENCIL_OP_INVERT ) == D3D12_STENCIL_OP_INVERT &&
                       static_cast<int32>( D3D11_STENCIL_OP_INCR ) == D3D12_STENCIL_OP_INCR && static_cast<int32>( D3D11_STENCIL_OP_DECR ) == D3D12_STENCIL_OP_DECR,
                   "D3D11_STENCIL_OP and D3D12_STENCIL_OP must share values" );

    /** @brief 렌더 상태 값을 D3D 값으로 옮깁니다. `default:` 를 두지 않는다 — 상태를 더하면 `-Werror=switch` 가 여기를 가리킨다. */
    struct RHIDxRenderState
    {
        template <typename TDxEnum>
        static TDxEnum toBlend( RHIBlendFactor factor )
        {
            switch ( factor )
            {
                case RHIBlendFactor::Zero:
                    return static_cast<TDxEnum>( D3D12_BLEND_ZERO );
                case RHIBlendFactor::One:
                    return static_cast<TDxEnum>( D3D12_BLEND_ONE );
                case RHIBlendFactor::SrcColor:
                    return static_cast<TDxEnum>( D3D12_BLEND_SRC_COLOR );
                case RHIBlendFactor::InvSrcColor:
                    return static_cast<TDxEnum>( D3D12_BLEND_INV_SRC_COLOR );
                case RHIBlendFactor::SrcAlpha:
                    return static_cast<TDxEnum>( D3D12_BLEND_SRC_ALPHA );
                case RHIBlendFactor::InvSrcAlpha:
                    return static_cast<TDxEnum>( D3D12_BLEND_INV_SRC_ALPHA );
                case RHIBlendFactor::DstColor:
                    return static_cast<TDxEnum>( D3D12_BLEND_DEST_COLOR );
                case RHIBlendFactor::InvDstColor:
                    return static_cast<TDxEnum>( D3D12_BLEND_INV_DEST_COLOR );
                case RHIBlendFactor::DstAlpha:
                    return static_cast<TDxEnum>( D3D12_BLEND_DEST_ALPHA );
                case RHIBlendFactor::InvDstAlpha:
                    return static_cast<TDxEnum>( D3D12_BLEND_INV_DEST_ALPHA );
                case RHIBlendFactor::SrcAlphaSaturate:
                    return static_cast<TDxEnum>( D3D12_BLEND_SRC_ALPHA_SAT );
            }
            return static_cast<TDxEnum>( D3D12_BLEND_ONE );
        }

        template <typename TDxEnum>
        static TDxEnum toBlendOp( RHIBlendOp op )
        {
            switch ( op )
            {
                case RHIBlendOp::Add:
                    return static_cast<TDxEnum>( D3D12_BLEND_OP_ADD );
                case RHIBlendOp::Subtract:
                    return static_cast<TDxEnum>( D3D12_BLEND_OP_SUBTRACT );
                case RHIBlendOp::ReverseSubtract:
                    return static_cast<TDxEnum>( D3D12_BLEND_OP_REV_SUBTRACT );
                case RHIBlendOp::Min:
                    return static_cast<TDxEnum>( D3D12_BLEND_OP_MIN );
                case RHIBlendOp::Max:
                    return static_cast<TDxEnum>( D3D12_BLEND_OP_MAX );
            }
            return static_cast<TDxEnum>( D3D12_BLEND_OP_ADD );
        }

        template <typename TDxEnum>
        static TDxEnum toCompare( RHICompareFunc func )
        {
            switch ( func )
            {
                case RHICompareFunc::Never:
                    return static_cast<TDxEnum>( D3D12_COMPARISON_FUNC_NEVER );
                case RHICompareFunc::Less:
                    return static_cast<TDxEnum>( D3D12_COMPARISON_FUNC_LESS );
                case RHICompareFunc::Equal:
                    return static_cast<TDxEnum>( D3D12_COMPARISON_FUNC_EQUAL );
                case RHICompareFunc::LessEqual:
                    return static_cast<TDxEnum>( D3D12_COMPARISON_FUNC_LESS_EQUAL );
                case RHICompareFunc::Greater:
                    return static_cast<TDxEnum>( D3D12_COMPARISON_FUNC_GREATER );
                case RHICompareFunc::NotEqual:
                    return static_cast<TDxEnum>( D3D12_COMPARISON_FUNC_NOT_EQUAL );
                case RHICompareFunc::GreaterEqual:
                    return static_cast<TDxEnum>( D3D12_COMPARISON_FUNC_GREATER_EQUAL );
                case RHICompareFunc::Always:
                    return static_cast<TDxEnum>( D3D12_COMPARISON_FUNC_ALWAYS );
            }
            return static_cast<TDxEnum>( D3D12_COMPARISON_FUNC_LESS_EQUAL );
        }

        template <typename TDxEnum>
        static TDxEnum toStencilOp( RHIStencilOp op )
        {
            switch ( op )
            {
                case RHIStencilOp::Keep:
                    return static_cast<TDxEnum>( D3D12_STENCIL_OP_KEEP );
                case RHIStencilOp::Zero:
                    return static_cast<TDxEnum>( D3D12_STENCIL_OP_ZERO );
                case RHIStencilOp::Replace:
                    return static_cast<TDxEnum>( D3D12_STENCIL_OP_REPLACE );
                case RHIStencilOp::IncrementClamp:
                    return static_cast<TDxEnum>( D3D12_STENCIL_OP_INCR_SAT );
                case RHIStencilOp::DecrementClamp:
                    return static_cast<TDxEnum>( D3D12_STENCIL_OP_DECR_SAT );
                case RHIStencilOp::Invert:
                    return static_cast<TDxEnum>( D3D12_STENCIL_OP_INVERT );
                case RHIStencilOp::IncrementWrap:
                    return static_cast<TDxEnum>( D3D12_STENCIL_OP_INCR );
                case RHIStencilOp::DecrementWrap:
                    return static_cast<TDxEnum>( D3D12_STENCIL_OP_DECR );
            }
            return static_cast<TDxEnum>( D3D12_STENCIL_OP_KEEP );
        }
    };
} // namespace sw

#endif
