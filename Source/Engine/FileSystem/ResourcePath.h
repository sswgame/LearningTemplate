/**
 * @file ResourcePath.h
 * @brief 리소스 경로 `{도메인(선택), 상대 경로}` 형식(`ResourcePath`)과 도메인 이름표 · 우선순위 · 쿠킹 대상 표(`DomainTable`)입니다(R3 · R8). 0 단계에서는 형식만 있고 아직 아무도 쓰지 않습니다.
 * @details 도메인을 비우면 우선순위로 찾고(덮어쓰기), 넣으면 그 도메인만 본다. 만들 때 한 번 정규화(소문자 · `/` · `./` 제거)하고 `..` · 절대 경로 · 드라이브 표기는 거절한다 —
 *          캐시 · GUID 표 · 쿠킹 입력의 키가 이 값(해석이 끝난 것)이라 키 정규화가 한 곳이다. 문자열에서 암시적으로 만들 수 없고, 한 줄 참조(`common/x`)를 두 칸으로 가르는 일은
 *          `DomainTable::split` 만 한다. 상용 엔진: 언리얼 `/Game/…` 패키지 경로, Godot `res://`.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"

#include <optional>

namespace sw
{
    /**
     * @class ResourcePath
     * @brief 정규화된 `{도메인, 상대 경로}` 입니다. 상대 경로는 비어 있지 않다.
     */
    class SW_API ResourcePath
    {
    public:
        /** @brief 도메인 이름 @p domain(비울 수 있다)과 상대 경로 @p relative 로 만듭니다. 상대 경로가 비었거나 `..` · 절대 · 드라이브 표기면 빈 값입니다. */
        [[nodiscard]] static std::optional<ResourcePath> tryMake( string_view domain, string_view relative );

        /** @brief 도메인입니다. 비었으면 우선순위로 찾는다. */
        [[nodiscard]] const string& domain() const { return _domain; }
        /** @brief 도메인 아래 상대 경로입니다(소문자 · `/`). */
        [[nodiscard]] const string& relative() const { return _relative; }
        /** @brief 도메인이 정해졌으면 true 입니다. */
        [[nodiscard]] bool hasDomain() const { return _domain.empty() == false; }
        /** @brief 한 줄 표기입니다(`<도메인>/<상대>` 또는 `<상대>`). 로그 · 데이터 기록용이며 다시 가를 때는 `DomainTable::split` 만 쓴다. */
        [[nodiscard]] string toString() const
        {
            if ( _domain.empty() )
                return _relative;
            return _domain + "/" + _relative;
        }

        [[nodiscard]] bool operator==( const ResourcePath& other ) const { return _domain == other._domain && _relative == other._relative; }
        [[nodiscard]] bool operator!=( const ResourcePath& other ) const { return !( *this == other ); }

    private:
        ResourcePath( string domain, string relative )
            : _domain{ std::move( domain ) }
            , _relative{ std::move( relative ) }
        {
        }

        string _domain;
        string _relative;
    };

    /**
     * @class DomainTable
     * @brief 도메인 표입니다. 기동 때 채우고 이후 읽기만 한다.
     */
    class SW_API DomainTable
    {
    public:
        /** @brief 도메인 하나의 표 줄입니다. */
        struct Entry
        {
            string name;     ///< 소문자 이름(`common` · `game/empty` · `dlc/winter`)
            int32  priority; ///< 작을수록 먼저 찾는다
            bool   bCooked;  ///< 쿠킹 대상이면 true(scratch · 임시는 false)
        };

        /** @brief 도메인을 등록합니다. 이름이 비었거나 이미 있거나 `..` · 절대 · 드라이브 표기를 품으면 false 입니다. */
        [[nodiscard]] bool add( string_view name, int32 priority, bool bCooked );
        /** @brief 이름이 등록된 도메인이면 그 줄을, 아니면 nullptr 입니다. */
        [[nodiscard]] const Entry* find( string_view name ) const;
        /** @brief 우선순위(작은 것 먼저) 순서의 줄입니다. */
        [[nodiscard]] vector<const Entry*> listByPriority() const;
        /**
         * @brief 한 줄 참조 @p reference(`common/x.mat`)를 등록된 도메인 이름의 **최장 일치**(구간 경계 기준)로 가릅니다.
         * @return 도메인이 맞으면 `{도메인, 나머지}`, 어느 도메인도 아니면 도메인 없는 `{상대}` 입니다. 참조가 잘못됐거나(`..` · 절대) 도메인 이름뿐이면 빈 값입니다.
         */
        [[nodiscard]] std::optional<ResourcePath> split( string_view reference ) const;

    private:
        vector<Entry> _listEntry;
    };
} // namespace sw
