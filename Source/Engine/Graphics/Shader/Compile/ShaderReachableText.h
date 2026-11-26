/**
 * @file ShaderReachableText.h
 * @brief 전처리한 셰이더 글에서 진입점이 닿는 선언만 남긴 글입니다 — 셰이더 DDC 의 바이트코드 키가 이것을 해시합니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Macros.h"

namespace sw
{
    /**
     * @struct ShaderReachableText
     * @brief 전처리한 글은 공용 헤더(binding · lighting)를 통째로 펼친 것이라 대부분이 그 진입점이 쓰지 않는 선언이다. 글 전체로 키를 만들면 픽셀 쪽 define 하나가 정점 셰이더의
     *        키까지 갈라 같은 바이트코드를 되풀이해 컴파일한다(엔진 도메인 DX12 쿠킹본 19,739 개의 서로 다른 바이트코드는 2,880 개였다).
     * @details 언리얼 `ShaderMinifier`(전처리한 소스에서 안 쓰는 코드를 걷어 셰이더 작업을 겹쳐 합치는 것)와 같은 자리다. 다른 점은 키에만 쓴다는 것이다 — 컴파일러에는
     *          전처리한 글 그대로를 준다. 걷어 낸 글을 컴파일하면 바이트가 달라지는 셰이더가 있었고, 오류 글의 줄 번호도 잃는다.
     *
     *          선언을 최상위(와 구조체 안 함수)까지 조각내고, 진입점 이름에서 시작해 "쓰인 이름" 이 닿는 조각을 남기는 일을 더 늘지 않을 때까지 되풀이한다.
     *          조각이 남는 조건(이름은 글자로만 견준다 — 겹친 이름은 모두 남긴다):
     *          - 함수(정의 · 선언): 함수 이름이 쓰였다. 구조체 함수면 구조체 이름도 쓰였다.
     *          - 구조체: 이름이 쓰였다(데이터 멤버는 구조체와 함께, 안의 함수는 위 조건).
     *          - 전역 변수 · 자원 · 상수 버퍼: 선언한 이름 하나라도 쓰였다(안 쓰인 자원은 컴파일러도 바이트코드에서 뺀다 — 자원은 모두 register · binding 을 적는다).
     *          - 나머지(`#pragma` · typedef · using · `[[…]]` 가 앞선 선언 · 모르는 꼴)는 늘 남긴다. 괄호가 맞지 않는 글은 전부 남긴다.
     *          문자열 안의 이름(`patchconstantfunc("…")`)도 쓰인 것으로 본다.
     */
    struct SW_API ShaderReachableText
    {
        /**
         * @brief 전처리한 글 @p preprocessed 에서 @p entryPoint 가 닿는 조각을 순서대로 이은 글입니다. `#line` 줄 · 빈칸의 크기 · 빈 줄은 들지 않습니다.
         * @details 닿지 않는 함수의 오류는 글에 드러나지 않는다 — 그 글을 처음 컴파일한 변형에서만 드러난다.
         */
        [[nodiscard]] static string make( string_view preprocessed, string_view entryPoint );
    };
} // namespace sw
