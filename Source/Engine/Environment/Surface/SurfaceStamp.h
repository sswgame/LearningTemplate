/**
 * @file SurfaceStamp.h
 * @brief 표면 변형 층의 도장 하나입니다 — 발자국 · 끌린 자국 · 바퀴 자국 · 파묻힘 테두리 · 회전하는 깔때기가 모양 셋(`SurfaceStampShape`)과 칸 몇 개로 갈린다.
 * @details 도장은 데이터다. 표면 종류 표의 발자국(`SurfaceKindDef::_footprint`), 도장 컴포넌트(`SurfaceStampComponent`), 지역 프로필의 반응 모듈이 같은 형식을 품고
 *          씬의 변형 층(`SurfaceDeformSystem`)에 찍는다. 모양 · 크기 · 깊이 · 테두리 · 메워지는 속도 · 회전은 모두 칸이다(눈은 깊고 느리게, 모래는 얕고 빠르게) —
 *          코드에 지형 이름이 없다. 높이는 지면 기준 미터(음수 = 꺼짐, 양수 = 솟음)이고 충돌 · 내비는 고치지 않는다(보이는 것만).
 *          언리얼 런타임 가상 텍스처 · 렌더 타깃에 브러시를 그리는 눈 자국, 유니티 CustomRenderTexture 도장의 자리다.
 *
 *          XML 예(`ReflectedXMLFile`, 다른 형식 안에 칸으로 들어간다):
 *          @code
 *          <_footprint _shape="Print" _size="0.14,0.28" _depth="0.06" _rimHeight="0.01" _rimWidth="0.03" _fillSpeed="0.0025"/>
 *          @endcode
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 도장의 모양입니다. 크기 칸(`SurfaceStampDef::_size`)의 뜻이 모양마다 다르다. */
    ENUM()
    enum class SurfaceStampShape : uint8
    {
        Print = 0, ///< 타원(너비 × 길이) — 발자국 · 파묻힘 테두리(원)
        Trail,     ///< 두 점을 잇는 띠(너비, 끝은 둥글다) — 끌린 자국 · 바퀴 자국. 한 점에 찍으면 길이 `_size.y` 의 띠다
        Funnel,    ///< 가운데가 가장 깊은 깔때기(지름 `_size.x`)에 나선 홈 — 빨려드는 구멍. 붙잡은 도장이면 `_spinSpeed` 로 돈다
    };

    /**
     * @struct SurfaceStampDef
     * @brief 도장 하나의 데이터입니다(파일 머리말 참고). 깊이와 테두리가 모두 0 이면 찍지 않는 빈 도장이다(`isSet`).
     */
    REFLECT()
    struct SW_API SurfaceStampDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Print: ellipse (footprint, sink rim). Trail: band between two points (drag, wheel track). Funnel: cone with spiral grooves (whirl)" )
        SurfaceStampShape _shape{ SurfaceStampShape::Print };
        PROPERTY( Units = m, Tooltip = "Width and length (Print, Trail); Funnel uses x as its diameter" )
        float2 _size{ 0.0f, 0.0f };
        PROPERTY( Min = 0.0, Units = m, Tooltip = "How far the ground sinks at the deepest point" )
        float32 _depth{ 0.0f };
        PROPERTY( Min = 0.0, Units = m, Tooltip = "Height of the pushed-up rim around the stamp (0: no rim)" )
        float32 _rimHeight{ 0.0f };
        PROPERTY( Min = 0.0, Units = m, Tooltip = "Width of the pushed-up rim outside the stamp edge" )
        float32 _rimWidth{ 0.0f };
        PROPERTY( Min = 0.0, Units = "m/s", Tooltip = "How fast the ground fills back to flat (snow slow, sand fast)" )
        float32 _fillSpeed{ 0.0f };
        PROPERTY( Units = "rad/s", Tooltip = "Turn speed of a held Funnel's grooves (0: still)" )
        float32 _spinSpeed{ 0.0f };
        PROPERTY( Min = 0, Max = 16, Tooltip = "Spiral grooves of a Funnel (0: plain cone)" )
        uint32 _armCount{ 0 };

        /** @brief 무엇이든 바꾸는 도장인지입니다(깊이나 테두리가 있다). */
        bool isSet() const { return _depth > 0.0f || _rimHeight > 0.0f; }

        /**
         * @brief 칸 사이의 규칙을 봅니다 — 찍는 도장은 크기와 메워지는 속도가 있어야 하고(없으면 영영 남는다) 테두리는 높이와 폭이 함께 있다.
         * @details 틀리면 `Surface.Stamp*` 오류를 @p path · @p owner 와 함께 남기고 false 입니다. 범위(음수 금지)는 PROPERTY 가 읽을 때 본다.
         */
        [[nodiscard]] bool validate( string_view path, string_view owner ) const;

        /**
         * @brief 도장 중심 기준 자리(@p right — 도장의 오른쪽, @p forward — 도장의 앞, m)의 높이(m, 음수 = 꺼짐)입니다. 도장 밖은 0 이다.
         * @param length Trail 이 두 점 사이에 찍힐 때의 길이(m). 0 이면 `_size.y` 다
         * @param phase  Funnel 홈의 회전(라디안) — 붙잡은 도장이 시간에 따라 올린다
         */
        float32 computeHeight( float32 right, float32 forward, float32 length, float32 phase ) const;
        /** @brief 도장 중심에서 바뀌는 칸이 있을 수 있는 가장 먼 거리(m)입니다(테두리 포함). 격자가 이 반경 안만 훑는다. */
        float32 computeReach( float32 length ) const;
    };

    /**
     * @struct SurfaceStampHandle
     * @brief 붙잡은 도장(`SurfaceDeformSystem::holdStamp`) 하나를 가리킵니다. 기본값은 아무것도 가리키지 않는다(`isValid`).
     * @details 번호와 세대를 함께 들어, 놓은 칸을 다른 도장이 다시 쓴 뒤 옛 핸들로 옮기거나 놓아도 새 도장에 닿지 않는다.
     */
    struct SW_API SurfaceStampHandle
    {
        uint32 _slot{ 0 };
        uint32 _generation{ 0 }; ///< 0 이면 비었다 — 칸의 세대는 1 부터다

        bool isValid() const { return _generation != 0; }
    };
} // namespace sw
