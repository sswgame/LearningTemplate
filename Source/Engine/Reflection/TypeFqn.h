/**
 * @file TypeFqn.h
 * @brief 타입의 FQN 을 컴파일러 함수 시그니처에서 뽑습니다(`typeFqn<T>`). 레지스트리 없이 쓰는 잎 헤더입니다.
 * @details `ReflectValue.h` 처럼 `TypeRegistry.h` 를 끌지 않는 헤더의 템플릿도 부른다 — 템플릿이 부르는 이름은 정의 앞에 선언돼 있어야 한다
 *          (Linux clang 의 2단계 이름 찾기, Windows 는 같은 규칙을 컴파일러 플래그로 지킨다).
 */
#pragma once
#include "Core/Container/StringUtil.h"
#include "Core/Foundation/Macros.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionConstants.h"

namespace sw
{
    /**
     * @brief 컴파일러 함수 시그니처에서 템플릿 인자 E 의 타입 이름만 뽑아냅니다.
     * @details `[E = Foo::Bar]`(clang/gcc)와 `typeFqn<enum Foo::Bar>`(MSVC) 두 형태를 모두 받습니다.
     *          반환 타입에도 `<>` 가 들어 있으므로 함수 이름을 기준점으로 삼아야 합니다.
     */
    inline hashed_string parseTypeFromSignature( string_view sig )
    {
        string_view inner;

        const size_t equalPos = sig.find( constant::reflection::kSignatureEq );
        if ( equalPos != string_view::npos )
        {
            const size_t eqLength     = StringUtil::strlen( constant::reflection::kSignatureEq );
            inner                     = sig.substr( equalPos + eqLength );
            const size_t semicolonPos = inner.find( ';' );
            const size_t bracketPos   = inner.find( ']' );
            size_t       end          = inner.size();
            if ( semicolonPos != string_view::npos )
                end = semicolonPos;
            if ( bracketPos != string_view::npos && bracketPos < end )
                end = bracketPos;
            inner = inner.substr( 0, end );
        }
        else
        {
            constexpr string_view kMarker       = constant::reflection::kTypeFqnPrefix;
            const size_t          markerPos     = sig.find( kMarker );
            const size_t          closeAnglePos = sig.rfind( '>' );
            const bool            bMarkerFound  = markerPos != string_view::npos && closeAnglePos != string_view::npos && closeAnglePos > markerPos + kMarker.size();
            if ( bMarkerFound )
                inner = sig.substr( markerPos + kMarker.size(), closeAnglePos - markerPos - kMarker.size() );
        }

        inner                                           = StringUtil::trim( inner );
        static constexpr const utf8* kArrTypePrefixes[] = {
            constant::reflection::kEnumClassPrefix,
            constant::reflection::kEnumStructPrefix,
            constant::reflection::kEnumPrefix,
            constant::reflection::kClassPrefix,
            constant::reflection::kStructPrefix,
        };
        for ( const utf8* prefix : kArrTypePrefixes )
        {
            if ( StringUtil::startsWith( inner, prefix ) )
            {
                inner.remove_prefix( StringUtil::strlen( prefix ) );
                break;
            }
        }
        inner = StringUtil::trim( inner );
        if ( inner.empty() )
            return {};
        return hashed_string( inner.data(), static_cast<uint32>( inner.size() ) );
    }

    /** @brief 컴파일러 시그니처에서 enum FQN을 추출합니다. */
    template <typename E>
    hashed_string typeFqn()
    {
        static const hashed_string kFqn = parseTypeFromSignature( SW_FUNCTION_SIGNATURE );
        return kFqn;
    }
} // namespace sw
