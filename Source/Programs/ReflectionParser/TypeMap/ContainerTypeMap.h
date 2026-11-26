/**
 * @file ContainerTypeMap.h
 * @brief clang 컨테이너 표기 → ContainerKind + wrapper stem (ReflectBuiltins에서 채움).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionContainers.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) maps — clang 표기 부분 문자열 → ContainerKind + 래퍼 stem
    // ------------------------------------------------------------------------------
    /** @brief clang 표기 부분 문자열과 컨테이너 종류·래퍼 stem 규칙. */
    struct ContainerTypeRule
    {
        string        _match;
        string        _type; ///< Vector | Map | UnorderedMap | …
        ContainerKind _kind{ ContainerKind::Sequence };
        uint8         _bState{ SW_FALSE }; ///< Roles 에 State — `RuntimeState` 칸이 될 수 있다(순서가 정해지고 원소를 제자리에서 쓸 수 있는 시퀀스)
        uint8         _bFixed{ SW_FALSE }; ///< Roles 에 Fixed — 크기가 고정이다(고정 배열 · 열거자 배열)
    };
} // namespace sw

namespace sw
{
    class ContainerTypeMap
    {
    public:
        ContainerTypeMap();
        ~ContainerTypeMap() = default;

        /** @brief builtins 등록이 완료되었는지 반환합니다. */
        bool isLoaded() const noexcept { return _bLoaded == SW_TRUE; }

        /** @brief clang 타입 표기에 맞는 규칙을 찾습니다. */
        const ContainerTypeRule* match( const string_view clangTypeSpelling ) const;

        /** @brief 정규 이름(`array` · `vector` …)이 같은 규칙입니다. 없으면 nullptr 입니다. */
        const ContainerTypeRule* findByMatch( string_view match ) const;
        /** @brief 래퍼 줄기(`Vector` · `Array` …)가 같은 첫 규칙입니다. 같은 줄기의 규칙은 종류 · 역할이 같다(`registerRule` 이 본다). 없으면 nullptr 입니다. */
        const ContainerTypeRule* findByWrapperStem( string_view wrapperStem ) const;

        /**
         * @brief 규칙을 등록합니다. 매칭 · 래퍼가 비었거나, 같은 래퍼 줄기의 앞 규칙과 종류 · 역할이 다르면 이유를 알리고 false 입니다.
         * @details 래퍼 하나가 두 모양을 가질 수 없다 — 생성 코드는 래퍼 형식에서 종류 · 역할을 컴파일 때 다시 대조한다(`ContainerWrapperOf::matchesTable`).
         */
        [[nodiscard]] bool registerRule( const ContainerTypeRule& rule );

        /** @brief 등록 규칙을 비웁니다. */
        void clear();
        /** @brief 로드 완료 플래그를 설정합니다. */
        void setLoaded( bool bLoaded ) { _bLoaded = bLoaded ? SW_TRUE : SW_FALSE; }

    private:
        vector<ContainerTypeRule> _listRule;
        uint8                     _bLoaded  : 1;
        [[maybe_unused]] uint8    _reserved : 7;
    };
} // namespace sw
