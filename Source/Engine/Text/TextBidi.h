/**
 * @file TextBidi.h
 * @brief 양방향 텍스트(UAX #9 단순 버전)를 정의합니다. 코드 포인트마다 수준을 정하고(강한 문자, 숫자, 중립), 줄마다 수준 순서로 뒤집어 눈에 보이는 순서를 냅니다.
 * @details 언리얼 `TextBiDi`(ICU), Godot TextServer(ICU), 유니티 TextCore 에 해당한다. 배치(`TextLayoutEngine`)가 줄을 나눈 **뒤** 줄마다 부른다(L2).
 *          지원하는 것은 강한 L 과 R, 숫자(W7. 앞 강한 문자가 L 이면 L), 중립(N1, N2. 양쪽이 같으면 그 방향, 다르면 문단 방향), 단락 구분(`\n`),
 *          덮어쓰기 한 겹(RLO, LRO … PDF. 안의 글자를 모두 강한 R 이나 L 로 보며, 의사 문화권 `qps-plocm` 이 쓴다), 줄 끝 공백은 문단 수준(L1, 배치가 처리),
 *          RTL 수준의 괄호 거울(L4)이다. 지원하지 않는 것은 중첩된 방향 제어(LRE, RLE, LRI, RLI, FSI, PDI 와 둘 이상의 덮어쓰기), 숫자 앞뒤 기호의 세부 규칙
 *          (W2 ~ W6. 아랍 숫자와 유럽 숫자를 가르지 않는다), 아랍어 연결형(셰이퍼의 일)이다. 복합 문자 빌드(`SW_ENABLE_COMPLEX_TEXT`)는 수준만 SheenBidi(`Text/SheenBidi/`)가 정하고 나머지는 이 파일 그대로다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Text/ITextShaper.h"

namespace sw
{
    /**
     * @struct TextBidi
     * @brief 양방향 수준, 줄 안 재배열, 거울 문자를 담당합니다. 수준은 짝수가 왼쪽에서 오른쪽, 홀수가 오른쪽에서 왼쪽이며, 단순 버전은 0, 1, 2 만 냅니다.
     */
    struct SW_API TextBidi
    {
        /** @brief 문단 방향의 수준입니다(LTR 은 0, RTL 은 1). */
        static uint8 getParagraphLevel( TextDirection paragraphDirection ) { return paragraphDirection == TextDirection::RightToLeft ? 1 : 0; }
        /** @brief 수준이 오른쪽에서 왼쪽(홀수)인가입니다. */
        static bool isRightToLeftLevel( uint8 level ) { return ( level & 1u ) != 0; }

        /**
         * @brief 논리 순서의 코드 포인트 열 @p listCodepoint 의 수준을 @p outListLevel 에 채웁니다(앞을 비우고 같은 길이로 채웁니다).
         * @details 문단 수준(@p paragraphDirection)에 따라 LTR 문단이면 L 0, R 1, 숫자 2 이고, RTL 문단이면 R 1, L 2, 숫자 2 이다(I1, I2).
         *          폭 없는 문자(방향 제어 포함)는 앞 글자의 수준, 단락 구분은 문단 수준이다. 줄 끝 공백(L1)은 줄을 아는 쪽이 문단 수준으로 되돌린다.
         */
        static void resolveLevels( const vector<uint32>& listCodepoint, TextDirection paragraphDirection, vector<uint8>& outListLevel );
        /**
         * @brief 단순 버전의 수준입니다(위 머리말의 지원 범위). 복합 문자 빌드(`SW_ENABLE_COMPLEX_TEXT`)가 아니면 `resolveLevels` 가 이것입니다.
         * @details 복합 문자 빌드는 `Text/SheenBidi/` 의 전체 UAX #9 가 수준을 정한다. 재배열(`reorderVisually`) · 거울 문자는 두 빌드가 같은 코드다.
         */
        static void resolveSimpleLevels( const vector<uint32>& listCodepoint, TextDirection paragraphDirection, vector<uint8>& outListLevel );
        /**
         * @brief 한 줄의 수준 열 @p listLevel 을 눈에 보이는 순서(왼쪽 → 오른쪽)로 바꾼 논리 번호 열을 @p outListIndex 에 채웁니다(L2).
         * @details 가장 높은 수준부터 가장 낮은 홀수 수준까지, 그 수준 이상이 이어진 구간을 차례로 뒤집습니다.
         */
        static void reorderVisually( const vector<uint8>& listLevel, vector<uint32>& outListIndex );
        /** @brief RTL 수준에서 그릴 거울 문자입니다(괄호, 부등호, 홑 화살괄호와 겹 화살괄호 쌍). 짝이 없으면 @p codepoint 그대로입니다. */
        static uint32 getMirroredCodepoint( uint32 codepoint );
    };
} // namespace sw
