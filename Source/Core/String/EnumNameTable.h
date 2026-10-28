/**
 * @file EnumNameTable.h
 * @brief 데이터 파일이 열거자를 이름으로 적을 때 쓰는 이름 표 두 모양입니다 — 순서 표(`EnumNameTable`)와 이름 · 값 쌍 표(`NamedValue` + `NamedValueUtil`).
 * @details 카탈로그마다 이름 배열과 `for ( index < 6 )` 같은 개수 상수를 손으로 두면 열거자를 더할 때 개수를 못 따라가 새 이름이 조용히 읽히지 않는다.
 *          순서 표는 배열에서 개수를 읽고(`getCount`), 열거자의 끝 값(`Count`)이 있으면 호출한 쪽이 `static_assert( kTable.getCount() == static_cast<size_t>( E::Count ) )` 로 맞춥니다.
 *          열거자는 0 부터 빈틈없이 이어지고 이름 배열의 순서와 같아야 한다(저장 값이 아니라 이름이 파일에 적히는 열거자 전용). 순서와 상관없는 표는 `NamedValue` 쌍으로 적는다.
 *          **이름은 철자 그대로 견준다.** 대소문자를 무시하면 이름마다 별칭이 생기고(같은 값을 여러 철자로 적는 데이터가 쌓인다) 리플렉션 `EnumInfo` 의 텍스트 읽기와도 규칙이 갈린다.
 *          리플렉션 `ENUM()` 으로 옮기지 않는 까닭: 데이터 파일이 `impulse` · `phaseEnter` 처럼 소문자로 적고 있어, 철자가 열거자 이름과 다르다. 옮기면 데이터를 다시 써야 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"

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

        /** @brief 이름(철자 그대로)으로 열거자를 찾습니다. 없으면 false 이고 @p outValue 는 그대로입니다. */
        [[nodiscard]] bool parse( string_view text, E& outValue ) const
        {
            for ( size_t index = 0; index < _count; ++index )
            {
                if ( text == string_view( _pArrName[index] ) )
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

namespace sw
{
    /**
     * @struct NamedValue
     * @brief 이름 → 값 표의 한 줄입니다(순서와 상관없는 표). 표는 `static constexpr NamedValue<E> kArrName[] = { { "Name", E::Value }, … }` 로 적는다.
     */
    template <typename T>
    struct NamedValue
    {
        const utf8* _pName;
        T           _value;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct NamedValueUtil
     * @brief `NamedValue` 표를 이름으로 찾습니다. 이름은 철자 그대로 견준다(별칭 없음).
     */
    struct NamedValueUtil
    {
        /** @brief @p name 과 철자가 같은 줄의 값을 @p outValue 에 씁니다. 없으면 false 이고 @p outValue 는 그대로입니다. */
        template <typename T, size_t Count>
        [[nodiscard]] static bool find( const NamedValue<T> ( &arrEntry )[Count], string_view name, T& outValue )
        {
            for ( const NamedValue<T>& entry : arrEntry )
            {
                if ( name == string_view( entry._pName ) )
                {
                    outValue = entry._value;
                    return true;
                }
            }
            return false;
        }
    };
} // namespace sw
