#include "pch.h"

#include "Engine/Graphics/RenderState/RenderState.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Memory/Memory.h"

namespace sw
{
    namespace
    {
        struct RHIRenderStateInternal
        {
            /** @brief 알파 계수로 쓸 수 없는 색 계수이면 true 입니다. */
            static constexpr bool isColorOnlyFactor( RHIBlendFactor factor )
            {
                return factor == RHIBlendFactor::SrcColor || factor == RHIBlendFactor::InvSrcColor || factor == RHIBlendFactor::DstColor || factor == RHIBlendFactor::InvDstColor;
            }

            /** @brief 형식의 바이트가 @p rhs 와 모두 같으면 true 입니다. 패딩이 없는 형식만 받는다(헤더의 static_assert). */
            template <typename T>
            static bool isSameBytes( const T& lhs, const T& rhs ) noexcept
            {
                return Memory::compare( &lhs, &rhs, sizeof( T ) ) == 0;
            }
        };
    } // namespace

    RHIBlendTargetState::RHIBlendTargetState() noexcept
        : _srcColor{ RHIBlendFactor::One }
        , _dstColor{ RHIBlendFactor::Zero }
        , _colorOp{ RHIBlendOp::Add }
        , _srcAlpha{ RHIBlendFactor::One }
        , _dstAlpha{ RHIBlendFactor::Zero }
        , _alphaOp{ RHIBlendOp::Add }
        , _colorWriteMask{ RHIColorWriteMask::RGBA }
        , _bEnableBlend{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    RHIBlendTargetState RHIBlendTargetState::makeStraightAlpha() noexcept
    {
        RHIBlendTargetState target{};
        target._bEnableBlend = SW_TRUE;
        target._srcColor     = RHIBlendFactor::SrcAlpha;
        target._dstColor     = RHIBlendFactor::InvSrcAlpha;
        target._srcAlpha     = RHIBlendFactor::One;
        target._dstAlpha     = RHIBlendFactor::InvSrcAlpha;
        return target;
    }

    RHIBlendTargetState RHIBlendTargetState::makePremultipliedAlpha() noexcept
    {
        RHIBlendTargetState target{};
        target._bEnableBlend = SW_TRUE;
        target._srcColor     = RHIBlendFactor::One;
        target._dstColor     = RHIBlendFactor::InvSrcAlpha;
        target._srcAlpha     = RHIBlendFactor::One;
        target._dstAlpha     = RHIBlendFactor::InvSrcAlpha;
        return target;
    }

    bool RHIBlendTargetState::operator==( const RHIBlendTargetState& other ) const noexcept
    {
        return RHIRenderStateInternal::isSameBytes( *this, other );
    }

    const utf8* RHIBlendTargetState::findInvalidReason() const noexcept
    {
        if ( RHIRenderStateInternal::isColorOnlyFactor( _srcAlpha ) || RHIRenderStateInternal::isColorOnlyFactor( _dstAlpha ) )
            return "an alpha blend factor cannot be a color factor (SrcColor, InvSrcColor, DstColor, InvDstColor)";
        return nullptr;
    }

    RHIFixedRenderState::RHIFixedRenderState() noexcept
        : _depthBias{ 0 }
        , _slopeScaledDepthBias{ 0.0f }
        , _cullMode{ RHICullMode::Back }
        , _fillMode{ RHIFillMode::Solid }
        , _frontFace{ RHIFrontFace::Clockwise }
        , _depthCompare{ RHICompareFunc::LessEqual }
        , _bEnableDepthTest{ SW_TRUE }
        , _bEnableDepthWrite{ SW_TRUE }
        , _bEnableDepthClip{ SW_TRUE }
        , _bEnableStencil{ SW_FALSE }
        , _reservedFlags{ 0 }
        , _stencilRef{ 0 }
        , _stencilReadMask{ 0xFF }
        , _stencilWriteMask{ 0xFF }
        , _stencilFrontFail{ RHIStencilOp::Keep }
        , _stencilFrontDepthFail{ RHIStencilOp::Keep }
        , _stencilFrontPass{ RHIStencilOp::Keep }
        , _stencilFrontCompare{ RHICompareFunc::Always }
        , _stencilBackFail{ RHIStencilOp::Keep }
        , _stencilBackDepthFail{ RHIStencilOp::Keep }
        , _stencilBackPass{ RHIStencilOp::Keep }
        , _stencilBackCompare{ RHICompareFunc::Always }
    {
    }

    bool RHIFixedRenderState::operator==( const RHIFixedRenderState& other ) const noexcept
    {
        return RHIRenderStateInternal::isSameBytes( *this, other );
    }

    const utf8* RHIFixedRenderState::findInvalidReason() const noexcept
    {
        if ( _bEnableDepthWrite != SW_FALSE && _bEnableDepthTest == SW_FALSE )
            return "depth write needs the depth test";
        if ( _bEnableStencil != SW_FALSE && _bEnableDepthTest == SW_FALSE )
            return "the stencil test needs the depth test - it is what binds the depth-stencil attachment (use a depth compare of Always to pass every pixel)";
        const bool bDepthBias = _depthBias != 0 || _slopeScaledDepthBias != 0.0f;
        if ( bDepthBias && _bEnableDepthTest == SW_FALSE )
            return "a depth bias needs the depth test";
        return nullptr;
    }

    bool RHIRenderState::operator==( const RHIRenderState& other ) const noexcept
    {
        return RHIRenderStateInternal::isSameBytes( *this, other );
    }

    uint64 RHIRenderState::computeHash() const noexcept
    {
        return HashUtil::appendBytes64( HashUtil::kFNVOffset64, this, sizeof( RHIRenderState ) );
    }

    void RHIRenderState::setAllBlendTargets( const RHIBlendTargetState& target ) noexcept
    {
        for ( RHIBlendTargetState& entry : _arrBlendTarget )
        {
            entry = target;
        }
    }

    bool RHIRenderState::isAnyBlendEnabled() const noexcept
    {
        for ( const RHIBlendTargetState& entry : _arrBlendTarget )
        {
            if ( entry._bEnableBlend != SW_FALSE )
                return true;
        }
        return false;
    }

    RHIRenderStateOverride::RHIRenderStateOverride() noexcept
        : _values{ makeFilledState( 0 ) }
        , _mask{ makeFilledState( 0 ) }
        , _arrBlendTarget{}
        , _blendTargetCount{ 0 }
        , _arrReserved{ 0, 0, 0 }
    {
    }

    RHIFixedRenderState RHIRenderStateOverride::makeFilledState( uint8 byteValue ) noexcept
    {
        RHIFixedRenderState state{};
        Memory::set( &state, byteValue, sizeof( RHIFixedRenderState ) );
        return state;
    }

    RHIRenderStateOverride RHIRenderStateOverride::makeFromReads( const RHIFixedRenderState& readOverZeros, const RHIFixedRenderState& readOverOnes ) noexcept
    {
        RHIRenderStateOverride result{};
        const uint8*           pZeros  = reinterpret_cast<const uint8*>( &readOverZeros );
        const uint8*           pOnes   = reinterpret_cast<const uint8*>( &readOverOnes );
        uint8*                 pValues = reinterpret_cast<uint8*>( &result._values );
        uint8*                 pMask   = reinterpret_cast<uint8*>( &result._mask );
        for ( size_t byteIndex = 0; byteIndex < sizeof( RHIFixedRenderState ); ++byteIndex )
        {
            const uint8 written = static_cast<uint8>( ~( pZeros[byteIndex] ^ pOnes[byteIndex] ) );
            pMask[byteIndex]    = written;
            pValues[byteIndex]  = static_cast<uint8>( pZeros[byteIndex] & written );
        }
        return result;
    }

    bool RHIRenderStateOverride::hasAny() const noexcept
    {
        return _blendTargetCount != 0 || _mask != makeFilledState( 0 );
    }

    RHIOpaqueDrawBand RHIRenderStateOverride::classifyOpaqueDrawBand() const noexcept
    {
        // 보통 불투명 패스(기본 상태 — 깊이 테스트 · 쓰기, LessEqual, 블렌드 끔) 위에 덮은 결과로 본다 — 덮지 않은 칸은 보통 값이라 띠를 바꾸지 않는다.
        const RHIRenderState state = applyTo( RHIRenderState{}, Scope::FixedAndBlend );
        // 색: 모든 타깃이 RGB 를 다 덮어쓰면(알파만 빼는 것은 덮어쓰기다) 순서와 상관없다. 아무 채널도 쓰지 않는 타깃은 깊이 · 스텐실만 남기는 가리개다.
        bool bReadsTarget = false;
        for ( const RHIBlendTargetState& target : state._arrBlendTarget )
        {
            const uint8 rgbBits  = static_cast<uint8>( target._colorWriteMask ) & static_cast<uint8>( RHIColorWriteMask::RGB );
            const bool  bPartial = target._colorWriteMask != RHIColorWriteMask::None && rgbBits != static_cast<uint8>( RHIColorWriteMask::RGB );
            bReadsTarget         = bReadsTarget || target._bEnableBlend != 0 || bPartial;
        }
        // 깊이: Less · LessEqual 밖의 비교(Equal · Greater · Always …)는 앞에 그려진 깊이에 기댄다. 깊이를 쓰지 않는 불투명은 뒤에 그려진 것이 덮는다(스텐실 마스크는 아래 띠).
        const bool bOrderedDepth = state._depthCompare == RHICompareFunc::Less || state._depthCompare == RHICompareFunc::LessEqual;
        const bool bStencil      = state._bEnableStencil != 0;
        bReadsTarget             = bReadsTarget || bOrderedDepth == false || ( state._bEnableDepthWrite == 0 && bStencil == false );
        // 스텐실: 비교하면 읽는다(뒤 띠), 비교 없이 바꾸기만 하면 마스크다(읽는 띠 앞).
        const bool bStencilReads = bStencil && ( state._stencilFrontCompare != RHICompareFunc::Always || state._stencilBackCompare != RHICompareFunc::Always );
        const bool bStencilWrites =
            bStencil && state._stencilWriteMask != 0 &&
            ( state._stencilFrontPass != RHIStencilOp::Keep || state._stencilBackPass != RHIStencilOp::Keep || state._stencilFrontFail != RHIStencilOp::Keep ||
              state._stencilBackFail != RHIStencilOp::Keep || state._stencilFrontDepthFail != RHIStencilOp::Keep || state._stencilBackDepthFail != RHIStencilOp::Keep );
        RHIOpaqueDrawBand band = RHIOpaqueDrawBand::Ordinary;
        if ( bReadsTarget || bStencilReads )
            band = RHIOpaqueDrawBand::ReadsTarget;
        else if ( bStencilWrites )
            band = RHIOpaqueDrawBand::StencilWrite;
        return band;
    }

    RHIRenderState RHIRenderStateOverride::applyTo( const RHIRenderState& passState, Scope scope ) const noexcept
    {
        // 먼저 범위와 상관없이 바이트를 덮은 뒤, 표가 패스의 것으로 정한 칸을 되돌린다 — 칸을 더해도 덮는 일이 따라오고, 되돌릴 칸만 여기 적는다.
        RHIRenderState merged  = passState;
        const uint8*   pValues = reinterpret_cast<const uint8*>( &_values );
        const uint8*   pMask   = reinterpret_cast<const uint8*>( &_mask );
        uint8*         pMerged = reinterpret_cast<uint8*>( static_cast<RHIFixedRenderState*>( &merged ) );
        for ( size_t byteIndex = 0; byteIndex < sizeof( RHIFixedRenderState ); ++byteIndex )
        {
            pMerged[byteIndex] = static_cast<uint8>( ( pMerged[byteIndex] & static_cast<uint8>( ~pMask[byteIndex] ) ) | pValues[byteIndex] );
        }

        if ( scope == Scope::CullOnly )
        {
            const RHICullMode  cullMode                 = merged._cullMode;
            const RHIFrontFace frontFace                = merged._frontFace;
            static_cast<RHIFixedRenderState&>( merged ) = passState;
            merged._cullMode                            = cullMode;
            merged._frontFace                           = frontFace;
        }
        // 뒤집은 껍질(앞면 컬링)처럼 패스가 정한 컬은 머티리얼이 풀지 않는다(언리얼 Two Sided 도 외곽선 패스의 앞면 컬링을 풀지 않는다). 컬이 없는 패스는 더 켤 수 없다.
        if ( passState._cullMode != RHICullMode::Back )
            merged._cullMode = passState._cullMode;
        // 깊이 테스트는 첨부와 함께 움직인다 — PSO 의 깊이 형식과 렌더 패스가 어긋나지 않게 언제나 패스의 값이다.
        merged._bEnableDepthTest = passState._bEnableDepthTest;
        if ( passState._bEnableDepthTest == SW_FALSE )
        {
            // 붙일 첨부가 없다. 스텐실 · 깊이 쓰기는 받지 않는다.
            merged._bEnableDepthWrite    = passState._bEnableDepthWrite;
            merged._bEnableStencil       = passState._bEnableStencil;
            merged._depthBias            = passState._depthBias;
            merged._slopeScaledDepthBias = passState._slopeScaledDepthBias;
        }

        if ( scope == Scope::FixedAndBlend && _blendTargetCount > 0 )
        {
            for ( uint32 targetIndex = 0; targetIndex < kMaxColorAttachments; ++targetIndex )
            {
                merged._arrBlendTarget[targetIndex] = _arrBlendTarget[_blendTargetCount == 1 ? 0u : ( targetIndex < _blendTargetCount ? targetIndex : _blendTargetCount - 1u )];
            }
        }
        return merged;
    }

    uint64 RHIRenderStateOverride::computeHash() const noexcept
    {
        // 쓰지 않는 블렌드 칸은 기본값이라(읽을 때 그 칸만 채운다) 바이트 그대로 해시해도 같은 값은 같은 해시다.
        return HashUtil::appendBytes64( HashUtil::kFNVOffset64, this, sizeof( RHIRenderStateOverride ) );
    }
} // namespace sw
