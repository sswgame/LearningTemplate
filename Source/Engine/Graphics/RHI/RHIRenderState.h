/**
 * @file RHIRenderState.h
 * @brief 패스와 머티리얼이 함께 쓰는 렌더 상태 값 형식입니다. 블렌드 타깃 · 깊이 · 래스터 · 스텐실을 한 벌로 묶어 `RHIPipelineStateDesc` 가 품습니다.
 * @details 칸을 더할 곳은 이 파일 하나다 — XML 읽기 · 쓰기는 리플렉션이, 해시 · 비교 · 합치기는 바이트가 따라온다. 그래서 형식에 패딩이 없어야 한다(아래 static_assert).
 *          언리얼의 `FGraphicsPipelineStateInitializer` 의 상태 칸과 유니티 ShaderLab 의 렌더 상태 블록(Blend · ZTest · Cull · Stencil)을 한 자리에 모은 것이다(기억에 의존한 비교).
 *          마스크 · 참조 값이 면마다 있지 않고 하나인 것은 D3D 가 그렇기 때문이다(언리얼 · 유니티도 같다).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Foundation/EngineDefines.h"
#include "Engine/Graphics/RHI/RHIEnums.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @brief beginRenderPass 와 PSO 가 동시에 바인딩할 수 있는 컬러 RT 의 최대 개수입니다(MRT).
     * @details G버퍼 판 3 이 여섯 장(알베도 · 노멀 · 머티리얼 · 속도 · 사용자 칸 · 장면 색)이라 8 이다 — DX11 · DX12 · GL(최소 8) · WebGPU(기본 한도 8)가 보장하고,
     *          Vulkan 은 장치 한도(`maxColorAttachments`, 최소 4)를 기동 때 확인한다.
     */
    inline constexpr uint32 kMaxColorAttachments = 8;

    /**
     * @struct RHIBlendTargetState
     * @brief 컬러 첨부 하나의 블렌드입니다(색 · 알파 분리, 쓰기 마스크). XML 은 프리셋 이름(`engine/renderstate/blendpresets.xml`) 또는 이 칸들로 적습니다.
     */
    REFLECT()
    struct SW_API RHIBlendTargetState
    {
        REFLECT_BODY();

        PROPERTY()
        RHIBlendFactor _srcColor;
        PROPERTY()
        RHIBlendFactor _dstColor;
        PROPERTY()
        RHIBlendOp _colorOp;
        PROPERTY()
        RHIBlendFactor _srcAlpha;
        PROPERTY()
        RHIBlendFactor _dstAlpha;
        PROPERTY()
        RHIBlendOp _alphaOp;
        PROPERTY()
        RHIColorWriteMask _colorWriteMask;
        PROPERTY()
        uint8                  _bEnableBlend  : 1; ///< 꺼져 있으면 계수를 보지 않고 덮어쓴다
        [[maybe_unused]] uint8 _reservedFlags : 7;

        /** @brief 불투명(블렌드 꺼짐, 모든 채널 쓰기)으로 만듭니다. */
        RHIBlendTargetState() noexcept;
        /** @brief 곧은 알파입니다(색 SrcAlpha / InvSrcAlpha, 알파 One / InvSrcAlpha). 코드가 짓는 PSO 가 쓰며 데이터의 `Transparent` 프리셋과 같아야 한다(테스트). */
        [[nodiscard]] static RHIBlendTargetState makeStraightAlpha() noexcept;
        /** @brief 프리멀티플라이 알파입니다(색 · 알파 One / InvSrcAlpha). 코드가 짓는 PSO 가 쓰며 데이터의 `Premultiplied` 프리셋과 같아야 한다(테스트). */
        [[nodiscard]] static RHIBlendTargetState makePremultipliedAlpha() noexcept;

        /** @brief 칸이 모두 같으면 true 입니다. */
        [[nodiscard]] bool operator==( const RHIBlendTargetState& other ) const noexcept;
        [[nodiscard]] bool operator!=( const RHIBlendTargetState& other ) const noexcept { return ( *this == other ) == false; }
        /** @brief 받아 줄 수 없는 조합의 이유입니다. 맞으면 nullptr 입니다(알파 계수 자리에 색 계수를 적은 것 — D3D 가 PSO 를 거절한다). */
        [[nodiscard]] const utf8* findInvalidReason() const noexcept;
    };
    static_assert( sizeof( RHIBlendTargetState ) == 8, "the blend target is hashed and compared as bytes - it must have no padding" );

    /**
     * @struct RHIFixedRenderState
     * @brief 렌더 타깃 수와 상관없는 렌더 상태입니다(깊이 · 래스터 · 스텐실). 파이프라인 XML 의 패스와 머티리얼 XML 의 루트에 같은 칸으로 적습니다.
     * @details 기본값은 패스의 일반적인 값이다(뒷면 컬링, 깊이 테스트 · 쓰기, LessEqual, 깊이 클립, 스텐실 꺼짐). 칸을 더하면 패딩이 생기지 않게 `_arrReserved` 로 맞춘다.
     */
    REFLECT()
    struct SW_API RHIFixedRenderState
    {
        REFLECT_BODY();

        /**
         * @brief 깊이 바이어스입니다. 깊이 테스트 전에 래스터 깊이에 더합니다(음수면 카메라 쪽). 둘 다 0 이면 없습니다.
         * @details 상수 칸은 깊이 형식의 최소 단위(D24 는 2^-24) 배수이고 기울기 칸은 삼각형의 최대 깊이 기울기에 곱한다. D3D `DepthBias` · `SlopeScaledDepthBias`,
         *          Vulkan `depthBiasConstantFactor` · `depthBiasSlopeFactor`, GL `glPolygonOffset( factor, units )` 가 같은 식이다. 같은 삼각형을 선으로 겹쳐 그릴 때
         *          (보기 모드 ShadedWireframe) 선이 면과 z-fighting 하지 않게 한다.
         */
        PROPERTY()
        int32 _depthBias;
        PROPERTY()
        float32 _slopeScaledDepthBias;

        PROPERTY()
        RHICullMode _cullMode;
        PROPERTY()
        RHIFillMode _fillMode;
        PROPERTY()
        RHIFrontFace _frontFace;
        PROPERTY()
        RHICompareFunc _depthCompare; ///< LessEqual 이 기본이다. Less 로 두면 깊이 프리패스 뒤 같은 깊이를 다시 그리는 기본 패스가 모두 탈락한다

        PROPERTY()
        uint8 _bEnableDepthTest : 1; ///< 깊이 첨부를 붙이는지이기도 하다(꺼지면 PSO 의 깊이 형식이 없다). 머티리얼은 덮을 수 없다 — 항상 통과는 `_depthCompare` 의 `Always` 다
        PROPERTY()
        uint8 _bEnableDepthWrite : 1;
        PROPERTY()
        uint8 _bEnableDepthClip : 1; ///< 끄면 가까운 · 먼 평면 밖의 기하를 평면에 눌러 그린다(Vulkan 은 장치 기능 `depthClamp` 이 필요하다)
        PROPERTY()
        uint8                  _bEnableStencil : 1; ///< 스텐실 테스트. 깊이 테스트가 켜져 있어야 첨부가 있다
        [[maybe_unused]] uint8 _reservedFlags  : 4;

        PROPERTY()
        uint8 _stencilRef;
        PROPERTY()
        uint8 _stencilReadMask;
        PROPERTY()
        uint8 _stencilWriteMask;

        PROPERTY()
        RHIStencilOp _stencilFrontFail;
        PROPERTY()
        RHIStencilOp _stencilFrontDepthFail;
        PROPERTY()
        RHIStencilOp _stencilFrontPass;
        PROPERTY()
        RHICompareFunc _stencilFrontCompare;
        PROPERTY()
        RHIStencilOp _stencilBackFail;
        PROPERTY()
        RHIStencilOp _stencilBackDepthFail;
        PROPERTY()
        RHIStencilOp _stencilBackPass;
        PROPERTY()
        RHICompareFunc _stencilBackCompare;

        /** @brief 패스의 일반적인 값(클래스 설명)으로 만듭니다. */
        RHIFixedRenderState() noexcept;

        /** @brief 칸이 모두 같으면 true 입니다. */
        [[nodiscard]] bool operator==( const RHIFixedRenderState& other ) const noexcept;
        [[nodiscard]] bool operator!=( const RHIFixedRenderState& other ) const noexcept { return ( *this == other ) == false; }
        /** @brief 받아 줄 수 없는 조합의 이유입니다. 맞으면 nullptr 입니다. 패스와 머티리얼의 합친 결과에 함께 쓰는 규칙이다. */
        [[nodiscard]] const utf8* findInvalidReason() const noexcept;
    };
    static_assert( sizeof( RHIFixedRenderState ) == 24, "the fixed render state is hashed, compared and overlaid as bytes - it must have no padding" );

    /**
     * @struct RHIRenderState
     * @brief PSO 하나의 렌더 상태 전체입니다(`RHIPipelineStateDesc::_renderState`). 네 백엔드가 이것만 읽습니다.
     */
    struct SW_API RHIRenderState : RHIFixedRenderState
    {
        RHIBlendTargetState _arrBlendTarget[kMaxColorAttachments]; ///< 렌더 타깃마다의 블렌드. 쓰지 않는 칸은 기본값이다

        /** @brief 칸이 모두 같으면 true 입니다. */
        [[nodiscard]] bool operator==( const RHIRenderState& other ) const noexcept;
        [[nodiscard]] bool operator!=( const RHIRenderState& other ) const noexcept { return ( *this == other ) == false; }
        /** @brief 칸 전체의 해시입니다. 칸을 더해도 따라온다. */
        [[nodiscard]] uint64 computeHash() const noexcept;
        /** @brief 모든 렌더 타깃의 블렌드를 @p target 으로 둡니다. */
        void setAllBlendTargets( const RHIBlendTargetState& target ) noexcept;
        /** @brief 한 타깃이라도 블렌드가 켜져 있으면 true 입니다. */
        [[nodiscard]] bool isAnyBlendEnabled() const noexcept;
    };
    static_assert( sizeof( RHIRenderState ) == sizeof( RHIFixedRenderState ) + sizeof( RHIBlendTargetState ) * kMaxColorAttachments, "RHIRenderState is hashed as bytes - it must have no padding" );

    /**
     * @struct RHIRenderStateOverride
     * @brief 패스의 렌더 상태 위에 머티리얼이 적은 칸만 덮는 값입니다. 적지 않은 칸은 패스의 것이 남습니다.
     * @details 어느 칸을 적었는지는 칸마다 "적지 않음" 값을 두는 대신 비트 마스크로 든다. 같은 XML 을 0 으로 채운 값과 1 로 채운 값 위에 읽어 두 값이 같은 비트가 곧 적은 칸이다
     *          (`makeFromReads`). 그래서 enum 에 `Inherit` 같은 값이 들어가 백엔드까지 새지 않고, 칸을 더해도 읽기 · 합치기 · 해시 · 쓰기가 그대로 따라온다.
     *          언리얼 머티리얼 인스턴스의 Base Property Overrides(칸마다 켜는 체크) 자리다.
     */
    struct SW_API RHIRenderStateOverride
    {
        /**
         * @enum Scope
         * @brief 패스 종류가 머티리얼에게 내주는 칸의 범위입니다(표: `docs/agents/RenderPipelineXml.md`, 고르는 쪽 `RenderPassTraitFlag::kMaterialRenderState` 등).
         */
        enum class Scope : uint8
        {
            CullOnly,      ///< 컬 · 앞면 방향만(그림자 · 깊이 프리패스 — 모양이 색 패스와 같아야 한다)
            FixedState,    ///< 렌더 타깃과 상관없는 칸 모두(G버퍼 — 첨부 셋의 블렌드는 머티리얼의 것이 아니다)
            FixedAndBlend, ///< 블렌드까지 모두(포워드 · 반투명)
        };

        RHIFixedRenderState _values;                               ///< 적은 칸의 값. 적지 않은 칸의 비트는 0 이다
        RHIFixedRenderState _mask;                                 ///< 적은 칸의 비트가 1 이다
        RHIBlendTargetState _arrBlendTarget[kMaxColorAttachments]; ///< `_blendTargetCount` 칸만 쓴다
        uint8               _blendTargetCount;                     ///< 0 이면 패스의 블렌드를 그대로 쓴다. 1 이면 모든 타깃에 같은 값이다
        uint8               _arrReserved[3];

        /** @brief 아무것도 덮지 않는 값으로 만듭니다. */
        RHIRenderStateOverride() noexcept;

        /**
         * @brief 같은 XML 을 0 으로 채운 값(@p readOverZeros)과 1 로 채운 값(@p readOverOnes) 위에 읽은 결과로 만듭니다(`makeFilledState`). 두 값이 같은 비트가 적은 칸입니다.
         */
        [[nodiscard]] static RHIRenderStateOverride makeFromReads( const RHIFixedRenderState& readOverZeros, const RHIFixedRenderState& readOverOnes ) noexcept;
        /** @brief 모든 바이트가 @p byteValue 인 값입니다(`makeFromReads` 의 읽을 바탕). */
        [[nodiscard]] static RHIFixedRenderState makeFilledState( uint8 byteValue ) noexcept;
        /** @brief 적은 칸이 하나라도 있거나 블렌드를 덮으면 true 입니다. */
        [[nodiscard]] bool hasAny() const noexcept;
        /** @brief `_bEnableDepthTest` 를 적었으면 true 입니다(비트필드라 주소를 받지 않는다). */
        [[nodiscard]] bool hasDepthTestField() const noexcept { return _mask._bEnableDepthTest != 0; }
        /**
         * @brief @p passState 위에 덮은 값을 돌려줍니다. 패스 → 머티리얼 순서입니다.
         * @details 표의 규칙: 범위(@p scope) 밖 칸은 패스의 것이다. 패스가 뒷면 컬링이 아니면 컬은 패스의 것이다(뒤집은 껍질 외곽선은 양면 머티리얼도 앞면을 잘라야 외곽선이다).
         *          깊이 첨부가 없는 패스(`_bEnableDepthTest` 꺼짐)는 깊이 · 스텐실 칸을 받지 않는다 — 붙일 첨부가 없다. 깊이 테스트 칸 자체는 언제나 패스의 것이다(첨부와 함께 움직인다).
         */
        [[nodiscard]] RHIRenderState applyTo( const RHIRenderState& passState, Scope scope ) const noexcept;
        /** @brief 덮는 칸 · 값의 해시입니다. 머티리얼의 배치 키와 PSO 변형 키에 들어갑니다. */
        [[nodiscard]] uint64 computeHash() const noexcept;
    };
    static_assert( sizeof( RHIRenderStateOverride ) == sizeof( RHIFixedRenderState ) * 2 + sizeof( RHIBlendTargetState ) * kMaxColorAttachments + 4, "the override is hashed as bytes - it must have no padding" );
} // namespace sw
