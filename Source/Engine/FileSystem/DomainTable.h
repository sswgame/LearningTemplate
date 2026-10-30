/**
 * @file DomainTable.h
 * @brief 도메인 이름표 · 우선순위 · 쿠킹 대상을 한 표로 둔 형식입니다(R3 · R8). 0 단계에서는 형식만 있고 아직 아무도 쓰지 않습니다.
 * @details 도메인(`engine` · `common` · `editor` · `game/<팩>` · `dlc/<이름>` · scratch · 붙인 디스크 도메인)을 한 곳에 등록한다.
 *          데이터의 한 줄 참조(`common/x`)를 `{도메인, 상대}` 로 가르는 일은 `split` 의 **최장 일치** 한 곳에서만 한다 — 디스크 폴더가 있는지, 글자가 `game/` 인지로 추측하지 않는다.
 *          우선순위 번호가 작을수록 먼저 찾는다(덮어쓰기). 상용 엔진: 언리얼 마운트 지점 · pak 우선순위.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"

#include "Engine/FileSystem/ResourcePath.h"

#include <optional>

namespace sw
{
    /** @brief 도메인 하나의 표 줄입니다. */
    struct DomainEntry
    {
        string name;     ///< 소문자 이름(`common` · `game/empty` · `dlc/winter`)
        int32  priority; ///< 작을수록 먼저 찾는다
        bool   bCooked;  ///< 쿠킹 대상이면 true(scratch · 임시는 false)
    };

    /**
     * @class DomainTable
     * @brief 도메인 표입니다. 기동 때 채우고 이후 읽기만 한다.
     */
    class SW_API DomainTable
    {
    public:
        /** @brief 도메인을 등록합니다. 이름이 비었거나 이미 있거나 `..` · 절대 · 드라이브 표기를 품으면 false 입니다. */
        [[nodiscard]] bool add( string_view name, int32 priority, bool bCooked );
        /** @brief 이름이 등록된 도메인이면 그 줄을, 아니면 nullptr 입니다. */
        [[nodiscard]] const DomainEntry* find( string_view name ) const;
        /** @brief 우선순위(작은 것 먼저) 순서의 줄입니다. */
        [[nodiscard]] vector<const DomainEntry*> listByPriority() const;
        /**
         * @brief 한 줄 참조 @p reference(`common/x.mat`)를 등록된 도메인 이름의 **최장 일치**(구간 경계 기준)로 가릅니다.
         * @return 도메인이 맞으면 `{도메인, 나머지}`, 어느 도메인도 아니면 도메인 없는 `{상대}` 입니다. 참조가 잘못됐거나(`..` · 절대) 도메인 이름뿐이면 빈 값입니다.
         */
        [[nodiscard]] std::optional<ResourcePath> split( string_view reference ) const;

    private:
        vector<DomainEntry> _listEntry;
    };
} // namespace sw
