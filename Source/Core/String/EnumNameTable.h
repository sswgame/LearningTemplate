/**
 * @file EnumNameTable.h
 * @brief 열거자 이름 테이블입니다. XML 카탈로그가 열거자를 이름으로 적을 때(대소문자 무시) 이름 → 열거자와 열거자 → 이름을 한 테이블로 합니다.
 * @details 카탈로그마다 이름 배열과 `for ( index < 6 )` 같은 개수 상수를 손으로 두면 열거자를 더할 때 개수를 못 따라가 새 이름이 조용히 읽히지 않는다.
 *          테이블은 배열에서 개수를 읽고(`getCount`), 열거자의 끝 값(`Count`)이 있으면 호출한 쪽이 `static_assert( kTable.getCount() == static_cast<size_t>( E::Count ) )` 로 맞춥니다.
 *          열거자는 0 부터 빈틈없이 이어지고 이름 배열의 순서와 같아야 한다(저장 값이 아니라 이름이 파일에 적히는 열거자 전용).
 *          리플렉션 `ENUM()` 으로 옮기지 않는 까닭: 데이터 파일이 `impulse` · `phaseEnter` 처럼 소문자로 적고 있어, 철자가 열거자 이름과 다르다. 옮기면 데이터를 다시 써야 한다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/StringUtil.h"
#include "Core/Container/string.h"

namespace sw
{
    /**
     * @class EnumNameTable
     * @brief 이름 배열을 가리키는 열거자 이름 테이블입니다. 배열은 정적 저장소에 있어야 합니다(`static constexpr const utf8* kArrName[]`).
     */
    template <typename E>
    class EnumNameTable
    {
    public:
        template <size_t Count>
        constexpr EnumNameTable( const utf8* const ( &arrName )[Count] ) noexcept
            : _pArrName{ arrName }
            , _count{ Count }
        {
        }

        constexpr size_t getCount() const noexcept { return _count; }

        /** @brief 이름(대소문자 무시)으로 열거자를 찾습니다. 없으면 false 이고 @p outValue 는 그대로입니다. */
        [[nodiscard]] bool parse( string_view text, E& outValue ) const
        {
            for ( size_t index = 0; index < _count; ++index )
            {
                if ( StringUtil::equals( text, string_view( _pArrName[index] ), true ) )
                {
                    outValue = static_cast<E>( index );
                    return true;
                }
            }
            return false;
        }

        /** @brief 열거자의 이름입니다. 범위 밖이면 "?" 입니다. */
        const utf8* toString( E value ) const noexcept
        {
            const size_t index = static_cast<size_t>( value );
            return index < _count ? _pArrName[index] : "?";
        }

        /** @brief 모든 이름을 @p separator 로 이은 글입니다. 경고에 "(a, b, c)" 로 보일 때 씁니다. */
        string joinNames( string_view separator = ", " ) const
        {
            string result;
            for ( size_t index = 0; index < _count; ++index )
            {
                if ( index > 0 )
                    result.append( separator.data(), separator.size() );
                result += _pArrName[index];
            }
            return result;
        }

    private:
        const utf8* const* _pArrName;
        size_t             _count;
    };
} // namespace sw
