/**
 * @file Utf8Util.h
 * @brief UTF-8 글자 경계를 다루는 가장 작은 판정 함수입니다(의존이 `Types.h` 뿐이라 `fixed_string.h` · `formatString.h` 같은 낮은 층 헤더도 쓴다).
 * @details 바이트로 자르거나 한 글자 지우는 코드가 연속 바이트 판정을 제각각 쓰면 한 벌만 고쳐지고 나머지가 틀린 채 남는다 — 판정과 물러나기는 여기 하나다.
 *          `StringUtil` 은 이 함수를 `string_view` 로 감싼 `floorToUtf8Boundary` 를 가진다.
 */
#pragma once
#include "Core/Common/Types.h"

namespace sw
{
    /**
     * @struct Utf8Util
     * @brief UTF-8 연속 바이트 판정과 글자 경계로 물러나기입니다.
     */
    struct Utf8Util
    {
        /** @brief UTF-8 글자 하나의 최대 바이트 수입니다. */
        static constexpr uint32 kMaxCharacterBytes = 4;

        /** @brief 글자의 이어지는 바이트(10xxxxxx)인가입니다. 글자의 선두 바이트(ASCII 포함)와 잘못된 바이트(0xF8 이상)는 false 입니다. */
        template <typename TChar>
        [[nodiscard]] static constexpr bool isContinuation( TChar value )
        {
            return ( static_cast<uint8>( value ) & 0xC0u ) == 0x80u;
        }

        /**
         * @brief @p cut 위치에서 자를 때 글자 한가운데가 아니도록 가장 가까운 앞 글자 시작으로 물러난 위치를 돌려줍니다.
         * @details @p pText 는 @p cut 위치를 읽을 수 있어야 한다(`pText[cut]` 이 잘려 나가는 첫 바이트). 잘못된 UTF-8 을 끝없이 거슬러 가지 않도록
         *          이어지는 바이트를 `kMaxCharacterBytes - 1` 번까지만 물린다. 0 아래로는 가지 않는다.
         */
        template <typename TChar>
        [[nodiscard]] static constexpr size_t backOffToCharacterStart( const TChar* pText, size_t cut )
        {
            for ( uint32 step = 0; step + 1 < kMaxCharacterBytes && cut > 0 && isContinuation( pText[cut] ); ++step )
            {
                --cut;
            }
            return cut;
        }
    };
} // namespace sw
