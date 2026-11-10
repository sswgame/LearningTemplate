/**
 * @file RenderStateEnums.h
 * @brief 렌더 상태 열거(블렌드 · 깊이 · 스텐실 · 래스터)입니다. 머티리얼 · 파이프라인 XML 이 적고 PSO 가 품는 저작 어휘라 RHI 폴더 밖에 둔다.
 * @details 유니티 `UnityEngine.Rendering.CompareFunction` · `CullMode` · `BlendMode` 가 엔진 쪽 형식이고 백엔드가 옮기는 것과 같은 자리다 — 게임이 보는 머티리얼 헤더가
 *          RHI 헤더를 include 하지 않는다(타깃 게이트). 이름에 `RHI` 머리를 붙이지 않는다 — 저작 어휘다(유니티 `CullMode` · `BlendOp` 자리, 옛 `RHI*` 이름은 `_bannedNames`).
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @enum BlendMode
     * @brief 배치를 불투명 목록과 반투명 목록 중 어디로 보내는지의 분류입니다. 블렌드 계수는 여기 없다(`BlendTargetState`).
     * @details 리플렉션 enum 이다. 메시 컴포넌트의 `_blendMode` PROPERTY 가 이 이름으로 저장된다. `ENUM()` 을 빼면 직렬화기가 이름을
     *          몰라 `"null"` 로 적고, 읽을 때 불투명으로 돌아간다(언리얼 `EBlendMode` 도 UENUM 이다). 머티리얼은 `MaterialAlphaMode` 로 고르고 이 값은 거기서 나온다.
     */
    ENUM()
    enum class BlendMode : uint8
    {
        Opaque      = 0,
        Transparent = 1, ///< 반투명 배치 목록으로 간다
    };

    /**
     * @enum FillMode
     * @brief 래스터라이저의 채우기 방식(솔리드, 와이어프레임)입니다.
     */
    ENUM()
    enum class FillMode : uint8
    {
        Solid,    ///< 일반 채우기
        Wireframe ///< 와이어프레임
    };

    /**
     * @enum CullMode
     * @brief 면 컬링 방식입니다.
     */
    ENUM()
    enum class CullMode : uint8
    {
        None,  ///< 컬링 없음(양면)
        Front, ///< 전면 컬링
        Back   ///< 후면 컬링
    };

    /**
     * @enum FrontFace
     * @brief 앞면으로 치는 삼각형의 감김 방향입니다. 기본은 시계 방향이다(D3D 기본값, 네 백엔드가 여기에 맞춘다).
     */
    ENUM()
    enum class FrontFace : uint8
    {
        Clockwise,
        CounterClockwise,
    };

    /**
     * @enum CompareFunc
     * @brief 깊이 · 스텐실 비교 함수입니다. 원본 값(이번 픽셀 · 참조 값)과 저장된 값을 비교합니다.
     */
    ENUM()
    enum class CompareFunc : uint8
    {
        Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always,
    };

    /**
     * @enum StencilOp
     * @brief 스텐실 값을 바꾸는 연산입니다.
     */
    ENUM()
    enum class StencilOp : uint8
    {
        Keep,
        Zero,
        Replace,        ///< 참조 값으로 바꾼다
        IncrementClamp, ///< 1 올리고 255 에서 멈춘다
        DecrementClamp, ///< 1 내리고 0 에서 멈춘다
        Invert,
        IncrementWrap,
        DecrementWrap,
    };

    /**
     * @enum BlendFactor
     * @brief 블렌드 계수입니다. `Src1` 계열(듀얼 소스)은 셰이더 출력 규약이 없어 두지 않는다.
     */
    ENUM()
    enum class BlendFactor : uint8
    {
        Zero,
        One,
        SrcColor,
        InvSrcColor,
        SrcAlpha,
        InvSrcAlpha,
        DstColor,
        InvDstColor,
        DstAlpha,
        InvDstAlpha,
        SrcAlphaSaturate,
    };

    /**
     * @enum BlendOp
     * @brief 블렌드 연산입니다. `Min` · `Max` 는 계수를 보지 않는다(네 API 가 같다).
     */
    ENUM()
    enum class BlendOp : uint8
    {
        Add,
        Subtract,
        ReverseSubtract,
        Min,
        Max,
    };

    /**
     * @enum ColorWriteMask
     * @brief 컬러 첨부에 쓰는 채널입니다. 값이 곧 비트(R 1 · G 2 · B 4 · A 8)이고 D3D · Vulkan 의 쓰기 마스크와 같다.
     * @details 조합마다 이름을 두어 XML 이 글자 그대로 적는다(`RGB`). 깃발 표기를 따로 두지 않아 받는 철자가 이 표 하나다.
     */
    ENUM()
    enum class ColorWriteMask : uint8
    {
        None = 0,
        R    = 1,
        G    = 2,
        RG   = 3,
        B    = 4,
        RB   = 5,
        GB   = 6,
        RGB  = 7,
        A    = 8,
        RA   = 9,
        GA   = 10,
        RGA  = 11,
        BA   = 12,
        RBA  = 13,
        GBA  = 14,
        RGBA = 15,
    };
} // namespace sw
