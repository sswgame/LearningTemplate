/**
 * @file ReflectionIntellisense.h
 * @brief 편집기(clangd)에서만 리플렉션 어노테이션 인자를 C++ 식으로 풀어, 키 완성 · 호버 · 오타 오류를 줍니다.
 * @details `ReflectionMacros.h` 가 `SW_REFLECT_INTELLISENSE`(`.clangd` 가 정의)일 때만 include 합니다. Visual Studio IntelliSense 는 아직 쓰지 않습니다.
 *          컴파일러 빌드와 ReflectionParser 는 이 파일을 읽지 않으므로 빌드 산출물과 생성 코드는 바뀌지 않습니다.
 *
 *          REFLECT · PROPERTY · ENUM 은 그 자리에 `static_assert` 둘을 둡니다.
 *          - 첫째는 부르지 않는 람다 안의 검사용 구조체이고, 인자를 그 함수 본문의 `use( 인자 )` 로 풉니다. 구조체가 키를 정적 멤버로 가진
 *            `attr::*Keys` 를 상속하므로 인자 위치에서 키가 먼저 찾아집니다(완성 · 호버). 값은 뒤에 선언될 이름(열거자 `Invalid = Unknown`,
 *            메서드 `Validate = validateFoo`)일 수 있어 모르는 이름은 템플릿 인스턴스화로 미룹니다(MS 호환 모드의 dependent base 찾기).
 *          - 둘째는 인자 글자를 키 목록(`kArr*Key`)과 대조해 키 오타를 잡습니다. REFLECT_CONTAINER 는 둘째 인자가 래퍼 이름이라 대조하지 않습니다.
 *            인자가 두 번 펼쳐지므로 clangd 호버는 이 셋에서 키를 가리키지 못합니다. 키 설명은 완성 목록이 보여 줍니다.
 *          FUNCTION 은 값에 모르는 이름이 오지 않으므로 미루지 않고(STRICT), 오타는 그 자리의 "undeclared identifier" 오류이며 호버도 됩니다.
 *          키 목록은 생성 파일 `ReflectionAnnotationKeys.h` 이고 `AnnotationMeta.txt` 에서 만듭니다.
 *          값 형식은 검사하지 않습니다. 값이 매크로보다 **앞에** 선언된 메서드 이름이면 그 이름이 식이 될 수 없어 편집기가 오류로 보입니다.
 *          빌드에는 영향이 없습니다.
 */
#pragma once
#include "Core/Common/Types.h"

#include "Engine/Reflection/ReflectionAnnotationKeys.h"

namespace sw::reflect::attr
{
    /** @brief 인자 식을 받기만 합니다. 부르지 않습니다. */
    template <typename... T>
    constexpr bool use( const T&... ) noexcept
    {
        return true;
    }

    /** @brief 인자 글자 사이의 공백인지. */
    constexpr bool isAnnotationSpace( const utf8 c ) noexcept
    {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    }

    /** @brief [pBegin, pEnd) 가 키 목록(끝은 nullptr)에 있는지. */
    constexpr bool hasAnnotationKey( const utf8* pBegin, const utf8* pEnd, const utf8* const* ppKey ) noexcept
    {
        for ( ; *ppKey != nullptr; ++ppKey )
        {
            const utf8* pText = pBegin;
            const utf8* pKey  = *ppKey;
            while ( pText != pEnd && *pKey != '\0' && *pText == *pKey )
            {
                ++pText;
                ++pKey;
            }
            if ( pText == pEnd && *pKey == '\0' )
                return true;
        }
        return false;
    }

    /** @brief `#__VA_ARGS__` 글자에서 쉼표로 나뉜 인자마다 첫 이름(`Key` 또는 `Key = 값` 의 Key)이 키 목록에 있는지. */
    constexpr bool hasOnlyAnnotationKeys( const utf8* pText, const utf8* const* ppKey ) noexcept
    {
        while ( *pText != '\0' )
        {
            while ( isAnnotationSpace( *pText ) || *pText == ',' )
            {
                ++pText;
            }
            if ( *pText == '\0' )
                break;
            const utf8* pBegin = pText;
            while ( *pText != '\0' && isAnnotationSpace( *pText ) == false && *pText != '=' && *pText != ',' )
            {
                ++pText;
            }
            if ( hasAnnotationKey( pBegin, pText, ppKey ) == false )
                return false;
            int32 depth   = 0;
            bool  bQuoted = false;
            while ( *pText != '\0' && ( bQuoted || depth != 0 || *pText != ',' ) )
            {
                if ( bQuoted && *pText == '\\' && pText[1] != '\0' )
                    ++pText;
                else if ( *pText == '"' )
                    bQuoted = !bQuoted;
                else if ( bQuoted == false && *pText == '(' )
                    ++depth;
                else if ( bQuoted == false && *pText == ')' )
                    --depth;
                ++pText;
            }
        }
        return true;
    }
} // namespace sw::reflect::attr

// 검사용 구조체는 부르지 않는 제네릭 람다 안의 지역 클래스다. 이름 없는 람다라 같은 범위에 몇 개가 있어도 겹치지 않는다
// (__COUNTER__ 로 지은 이름은 clangd 가 프리앰블과 본 파일을 따로 세어 겹친다).
#define SW_REFLECT_ATTR_STRICT_OPEN( keys ) \
    static_assert( ( (void)[] {                                                                  \
                         struct SwReflectAttrCheck : ::sw::reflect::attr::keys                   \
                         {                                                                      \
                             [[maybe_unused]] static void check()                                              \
                             {                                                                  \
                                 (void)::sw::reflect::attr::use(
#define SW_REFLECT_ATTR_STRICT_CLOSE \
    );                               \
    }                                \
    }                                \
    ;                                \
    },                               \
                     true ),         \
                   "" );

// 지역 클래스가 람다 인자 타입(dependent)을 상속하므로 모르는 이름은 MS 호환 모드의 dependent base 찾기로 미뤄진다.
// 인자를 사이에 끼우는 여는 · 닫는 조각으로 나눈 것은 빈 인자를 `...` 뒤 자리에 넘기지 않기 위해서다(C++17 에서 경고).
#define SW_REFLECT_ATTR_DEFERRED_OPEN( keys )                                                         \
    _Pragma( "clang diagnostic push" ) _Pragma( "clang diagnostic ignored \"-Wmicrosoft-template\"" )         \
    static_assert( ( (void)[]( auto swDeferred ) {                                                            \
                         struct SwReflectAttrCheck : ::sw::reflect::attr::keys, decltype( swDeferred )        \
                         {                                                                                    \
                             [[maybe_unused]] static void check()                                                            \
                             {                                                                                \
                                 (void)::sw::reflect::attr::use(
#define SW_REFLECT_ATTR_DEFERRED_CLOSE \
    );                                 \
    }                                  \
    }                                  \
    ;                                  \
    },                                 \
                     true ),           \
                   "" );               \
    _Pragma( "clang diagnostic pop" )

#define SW_REFLECT_ATTR_KEY_CHECK_REFLECT( ... ) \
    static_assert( ::sw::reflect::attr::hasOnlyAnnotationKeys( #__VA_ARGS__, ::sw::reflect::attr::kArrReflectKey ), "unknown REFLECT key (Source/Core/Predefined/AnnotationMeta.txt)" );
#define SW_REFLECT_ATTR_KEY_CHECK_PROPERTY( ... ) \
    static_assert( ::sw::reflect::attr::hasOnlyAnnotationKeys( #__VA_ARGS__, ::sw::reflect::attr::kArrPropertyKey ), "unknown PROPERTY key (Source/Core/Predefined/AnnotationMeta.txt)" );
#define SW_REFLECT_ATTR_KEY_CHECK_FUNCTION( ... ) \
    static_assert( ::sw::reflect::attr::hasOnlyAnnotationKeys( #__VA_ARGS__, ::sw::reflect::attr::kArrFunctionKey ), "unknown FUNCTION key (Source/Core/Predefined/AnnotationMeta.txt)" );
#define SW_REFLECT_ATTR_KEY_CHECK_ENUM( ... ) \
    static_assert( ::sw::reflect::attr::hasOnlyAnnotationKeys( #__VA_ARGS__, ::sw::reflect::attr::kArrEnumKey ), "unknown ENUM key (Source/Core/Predefined/AnnotationMeta.txt)" );

#undef REFLECT
#undef PROPERTY
#undef FUNCTION
#undef ENUM
#undef REFLECT_CONTAINER

#define REFLECT( ... )                           \
    SW_REFLECT_ATTR_DEFERRED_OPEN( ReflectKeys ) \
    __VA_ARGS__ SW_REFLECT_ATTR_DEFERRED_CLOSE   \
    SW_REFLECT_ATTR_KEY_CHECK_REFLECT( __VA_ARGS__ )
#define ENUM( ... )                            \
    SW_REFLECT_ATTR_DEFERRED_OPEN( EnumKeys )  \
    __VA_ARGS__ SW_REFLECT_ATTR_DEFERRED_CLOSE \
    SW_REFLECT_ATTR_KEY_CHECK_ENUM( __VA_ARGS__ )
#define PROPERTY( ... )                           \
    SW_REFLECT_ATTR_DEFERRED_OPEN( PropertyKeys ) \
    __VA_ARGS__ SW_REFLECT_ATTR_DEFERRED_CLOSE    \
    SW_REFLECT_ATTR_KEY_CHECK_PROPERTY( __VA_ARGS__ )
#define FUNCTION( ... )                         \
    SW_REFLECT_ATTR_STRICT_OPEN( FunctionKeys ) \
    __VA_ARGS__ SW_REFLECT_ATTR_STRICT_CLOSE
#define REFLECT_CONTAINER( ... ) SW_REFLECT_ATTR_DEFERRED_OPEN( ContainerKeys ) __VA_ARGS__ SW_REFLECT_ATTR_DEFERRED_CLOSE
