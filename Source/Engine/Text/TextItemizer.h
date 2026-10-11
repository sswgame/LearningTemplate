/**
 * @file TextItemizer.h
 * @brief 런 나누기(itemization)를 정의합니다. 텍스트를 (면, 방향)이 같은 런으로 끊고, 셰이퍼는 런 하나씩 셰이핑합니다(HarfBuzz 도 같습니다).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Text/ITextShaper.h"

namespace sw
{
    struct FontFaceChain;

    class FontSystem;

    /**
     * @struct TextItemizer
     * @brief 런 나누기와 그것이 쓰는 코드 포인트 분류입니다.
     * @details 면은 연쇄에서 코드 포인트마다 고르고(`FontSystem::findFaceForCodepoint`), 방향은 강한 RTL 문자(히브리, 아랍 범위)에서 바뀐다.
     *          **중립 문자(공백, 숫자, 구두점)는 런을 바꾸지 않는다.** 지금 런의 면에 그 글리프가 있으면 그대로 두고 방향도 앞 런을 따른다
     *          (한글 문장의 공백이 라틴 폰트로 튀어 줄 높이가 흔들리지 않게 하기 위해서다). 방향은 기록만 하고, 눈에 보이는 순서로 뒤집는 것은 배치가 한다.
     */
    struct SW_API TextItemizer
    {
        /** @brief 강한 RTL 문자인지 확인합니다(U+0590..U+08FF, U+FB1D..U+FDFF, U+FE70..U+FEFF). */
        static bool isStrongRightToLeft( uint32 codepoint );
        /** @brief 중립 또는 약한 문자(공백, ASCII 숫자, 구두점, 기호, 일반 구두점 블록, CJK 기호와 구두점)인지 확인합니다. */
        static bool isNeutral( uint32 codepoint );
        /** @brief 폭 없는 문자(ZWSP..RLM, 줄/문단 구분, 방향 제어 U+202A..U+202E, 변이 선택자, 결합 분음 U+0300..U+036F)인지 확인합니다. */
        static bool isZeroWidth( uint32 codepoint );

        /**
         * @brief @p text 를 런으로 나눠 @p outListRun 을 채웁니다(앞을 비웁니다). 같은 면과 방향이 이어지면 한 런입니다.
         * @details 런의 `_text` 는 @p text 의 부분이라 @p text 가 사는 동안만 유효하다. 연쇄가 비면 런이 없다.
         */
        static void itemize( FontSystem& fontSystem, const FontFaceChain& chain, string_view text, vector<ShapingRun>& outListRun );
    };
} // namespace sw
