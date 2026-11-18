/**
 * @file URLTextUtil.h
 * @brief 주소의 경로 · 쿼리에 인코딩 없이 그대로 싣는 글(티켓 · 비밀 키 · 상품 id · 토픽 이름)의 글자 검사입니다. 키트 서버의 제공자들이 함께 씁니다.
 * @details 외부에서 온 글을 주소에 이어 붙이면 `&` · `/` · `?` 로 다른 인자 · 경로를 끼워 넣을 수 있다. 그래서 제공자는 보내기 전에 허용 글자만 있는지 본다 —
 *          제공자마다 같은 순회를 따로 적으면 한쪽만 느슨해지므로 판정은 여기 하나다(허용 기호는 제공자의 규칙이라 호출부가 적는다). 헤더만 있어 내보내는 기호가 없다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @struct URLTextUtil @brief 주소에 그대로 싣는 글의 허용 글자 검사입니다. 상태가 없습니다. */
    struct URLTextUtil
    {
        /** @brief @p text 가 비지 않았고 모든 글자가 ASCII 영숫자이거나 @p marks 안의 기호면 true 입니다. */
        static bool isSafeText( string_view text, string_view marks )
        {
            if ( text.empty() )
                return false;
            for ( const utf8 character : text )
            {
                const bool bAlphaNumeric = ( character >= '0' && character <= '9' ) || ( character >= 'a' && character <= 'z' ) || ( character >= 'A' && character <= 'Z' );
                if ( bAlphaNumeric == false && marks.find( character ) == string_view::npos )
                    return false;
            }
            return true;
        }
    };
} // namespace sw
