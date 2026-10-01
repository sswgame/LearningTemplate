/**
 * @file TypeNameMap.h
 * @brief ReflectionParser용 clang 표기 → canonical 맵 (ReflectBuiltins에서 채움).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) maps — clang 표기 alias → canonical
    // ------------------------------------------------------------------------------
    class TypeNameMap
    {
    public:
        TypeNameMap();
        ~TypeNameMap() = default;

        /** @brief builtins 등록이 완료되었는지 반환합니다. */
        bool isLoaded() const noexcept { return _bLoaded == SW_TRUE; }

        /** @brief clang 수식어를 제거한 뒤 alias → canonical 로 정규화합니다. */
        string normalize( const string& clangSpelling ) const;

        /** @brief 이 표기(수식어를 떼고, `sw::` 를 떼고)가 표에 있는 이름인지 — 내장 스칼라 · 문자열 · 수학 타입(`int32` · `string` · `float3` …). */
        bool isKnown( const string& clangSpelling ) const;

        /** @brief canonical·네임스페이스·별칭을 맵에 등록합니다. */
        void registerEntry( const string& canonical, const string& nameSpace,
                            const vector<string>& aliases );

        /** @brief 정규화 전에 떼어 낼 clang 수식어(`const ` · `class ` …)를 정합니다. parser_config 의 `type_strip_prefixes` 입니다. */
        void setStripPrefixes( const vector<string>& listPrefix ) { _listStripPrefix = listPrefix; }

        /** @brief 등록 항목을 비웁니다. */
        void clear();
        /** @brief 로드 완료 플래그를 설정합니다. */
        void setLoaded( bool bLoaded ) { _bLoaded = bLoaded ? SW_TRUE : SW_FALSE; }

    private:
        /** @brief alias 키를 canonical 에 연결합니다. */
        void addKey( const string& key, const string& canonical );

        /** @brief clang 수식어(const/class 등)와 참조를 제거합니다. */
        string_view stripDecorations( string_view spelling ) const;

        unordered_map<string, string> _mapAliasToCanonical;
        vector<string>                _listStripPrefix;
        uint8                         _bLoaded  : 1;
        [[maybe_unused]] uint8        _reserved : 7;
    };

} // namespace sw
