/**
 * @file UILayoutDump.h
 * @brief 배치 결과를 사각형 목록 텍스트로 씁니다. 레이아웃 테스트와 결정성 골든이 비교하는 형식입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace sw
{
    class WidgetTree;

    /**
     * @struct UILayoutDump
     * @brief 트리를 문서 순서로 한 줄씩 씁니다. 한 줄의 형식은 `<깊이 × 공백 둘><이름> <x> <y> <w> <h>` 이며, 소수 둘째 자리까지 쓰고 렌더 변환 전의 레이아웃 사각형입니다.
     * @details 이름이 없는 위젯은 리플렉션 타입 이름을 쓴다. Collapsed 위젯은 `<이름> collapsed` 한 줄이고 자식은 쓰지 않는다.
     */
    struct SW_API UILayoutDump
    {
        /**
         * @brief 덤프 텍스트를 만듭니다.
         * @param physicalScale 곱할 배율입니다. 1 이면 UI 단위이고, UI 배율을 넘기면 물리 픽셀 사각형입니다.
         */
        static string makeDump( const WidgetTree& tree, float32 physicalScale = 1.0f );
    };
} // namespace sw
