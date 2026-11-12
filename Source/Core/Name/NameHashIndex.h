/**
 * @file NameHashIndex.h
 * @brief 이름 해시를 저장 · 조회 키로 쓰는 표의 공용 충돌 검사 색인입니다(해시 → 처음 든 이름 철자 + 값).
 * @details 해시만 실리는 곳(바이너리 태그의 타입 · 필드 이름, 현지화 실행 표, 기믹 상태의 노드 id)은 같은 해시의 두 이름을 가리지 못해
 *          나중 것이 조용히 이기거나 먼저 찾은 것으로 읽는다. 그런 표는 이 색인으로만 넣는다 — 같은 해시에 **다른 이름**이 오면 오류이고 아무것도 바꾸지 않는다.
 *          언리얼 FName 과 Godot StringName 은 해시로 버킷만 고르고 이름 글(또는 풀 번호)로 같음을 가린다. 여기는 해시가 밖으로 나가므로
 *          표를 지을 때 이름 글을 한 번 비교해 충돌을 등록 오류로 바꾼다. 같음은 `hashed_string` 처럼 대소문자를 무시한다(해시가 대소문자를 무시하므로).
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/StdHeaders.h"
#include "Core/Foundation/Types.h"
#include "Core/String/StringUtil.h"

namespace sw
{
    /** @brief 값 없이 충돌 검사만 하는 색인의 값 자리입니다(`<variant>` 의 monostate 를 끌어오지 않는다). */
    struct NameHashIndexNoValue
    {
    };

    /**
     * @class NameHashIndex
     * @brief 해시 → (이름, 값) 표입니다. 같은 이름(대소문자 무시)은 값을 바꾸고, 같은 해시의 다른 이름은 `AlreadyExists` 오류입니다.
     * @tparam HashT 해시 형식(`uint32` · `uint64`)
     * @tparam ValueT 해시로 찾을 값. 검사만 하면 `NameHashIndexNoValue` 다.
     */
    template <typename HashT, typename ValueT = NameHashIndexNoValue>
    class NameHashIndex
    {
    public:
        /**
         * @brief @p name 을 @p hash 로 넣습니다. 같은 이름이 이미 있으면 값을 바꿉니다(다시 등록 · 핫 리로드).
         * @return 같은 해시에 다른 이름이 있으면 `AlreadyExists`(대상: "먼저 든 이름 / 이 이름")이고 표는 그대로입니다.
         */
        [[nodiscard]] expected<void> assign( string_view name, HashT hash, ValueT value = ValueT{} )
        {
            const auto it = _mapEntry.find( hash );
            if ( it == _mapEntry.end() )
            {
                _mapEntry.emplace( hash, Entry{ string( name ), std::move( value ) } );
                return {};
            }
            if ( StringUtil::equals( string_view{ it->second._name }, name, true ) == false )
            {
                string detail;
                detail.append( it->second._name ).append( " / " ).append( name );
                return unexpected( Error{ ErrorKind::AlreadyExists, "names share a hash", detail } );
            }
            it->second._value = std::move( value );
            return {};
        }

        /** @brief 해시의 값입니다. 없으면 nullptr 입니다. */
        [[nodiscard]] const ValueT* find( HashT hash ) const
        {
            const auto it = _mapEntry.find( hash );
            return it != _mapEntry.end() ? &it->second._value : nullptr;
        }

        void   clear() { _mapEntry.clear(); }
        size_t size() const { return _mapEntry.size(); }

    private:
        struct Entry
        {
            string _name; ///< 처음 든 철자(오류 글과 같음 비교에 쓴다)
            ValueT _value;
        };
        unordered_map<HashT, Entry> _mapEntry;
    };
} // namespace sw
