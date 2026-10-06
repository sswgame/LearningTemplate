/**
 * @file AnnotationMeta.h
 * @brief AnnotationMeta.txt 를 읽어 REFLECT/ENUM/PROPERTY/FUNCTION 토큰을 필드 바인딩으로 바꿉니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) parse — AnnotationMeta.txt 토큰 → Kind/필드 바인딩
    // ------------------------------------------------------------------------------
    /** @brief 어노테이션 토큰이 가리키는 종류와 대상 필드입니다. */
    struct AnnotationBinding
    {
        enum class Kind : uint8
        {
#define REGISTER_ANNOTATION_KIND( Name, Token ) Name,
#include "Core/Predefined/PredefinedAnnotationKind.xxx"
#undef REGISTER_ANNOTATION_KIND
        };

        string _field; ///< 정규 필드명: ReadOnly, Category, Server, …
        Kind   _kind{ Kind::Flag };
    };
} // namespace sw

namespace sw
{
    /** @brief AnnotationMeta.txt 의 `kind.Field = 별칭…` 한 줄입니다. 필드 표와 대조할 때 씁니다. */
    struct AnnotationMetaEntry
    {
        string            _scope; ///< 섹션 이름: REFLECT | ENUM | PROPERTY | FUNCTION
        AnnotationBinding _binding;
    };
} // namespace sw

namespace sw
{
    /** @brief 철자 토큰을 AnnotationBinding::Kind 로 파싱합니다. */
    inline bool tryParseAnnotationKind( const string_view spelling, AnnotationBinding::Kind& out ) noexcept
    {
#define REGISTER_ANNOTATION_KIND( Name, Token ) \
    if ( spelling == #Token )                   \
    {                                           \
        out = AnnotationBinding::Kind::Name;    \
        return true;                            \
    }
#include "Core/Predefined/PredefinedAnnotationKind.xxx"
#undef REGISTER_ANNOTATION_KIND
        return false;
    }

    /** @brief AnnotationBinding::Kind 의 철자(AnnotationMeta.txt 의 kind 토큰)입니다. */
    inline const utf8* toString( const AnnotationBinding::Kind kind ) noexcept
    {
        switch ( kind )
        {
#define REGISTER_ANNOTATION_KIND( Name, Token ) \
    case AnnotationBinding::Kind::Name:         \
        return #Token;
#include "Core/Predefined/PredefinedAnnotationKind.xxx"
#undef REGISTER_ANNOTATION_KIND
        }
        return "?";
    }

    // ------------------------------------------------------------------------------
    // 2) maps — scope → alias 조회 (bare 플래그 / key= 값)
    // ------------------------------------------------------------------------------
    class AnnotationMeta
    {
    public:
        AnnotationMeta();
        ~AnnotationMeta() = default;

        /** @brief AnnotationMeta.txt 를 로드합니다. */
        bool loadFile( const string_view absPath );
        /** @brief 파일이 로드되었는지 반환합니다. */
        bool isLoaded() const noexcept { return _bLoaded == SW_TRUE; }

        /** @brief 단독 플래그 · 넷 역할 토큰을 조회합니다(scope: REFLECT|ENUM|PROPERTY|FUNCTION). */
        const AnnotationBinding* findBare( const string_view scope, const string_view token ) const;

        /** @brief key= 쪽 바인딩을 조회합니다. */
        const AnnotationBinding* findKey( const string_view scope, const string_view key ) const;

        /** @brief 읽은 `kind.Field` 줄 전부입니다(별칭이 하나라도 있는 줄만). */
        const vector<AnnotationMetaEntry>& getEntries() const noexcept { return _listEntry; }

    private:
        /** @brief 로드된 바인딩을 비웁니다. */
        void clear();
        /** @brief scope·alias 에 바인딩을 추가합니다. */
        void addAlias( const string_view scope, const string_view alias, AnnotationBinding binding );

        static uint64 hashScopeAndKey( string_view scope, string_view key ) noexcept;

        unordered_map<uint64, AnnotationBinding> _mapBare; ///< (scope, alias) 해시 → 바인딩
        unordered_map<uint64, AnnotationBinding> _mapKey;
        vector<AnnotationMetaEntry>              _listEntry;
        uint8                                    _bLoaded  : 1;
        [[maybe_unused]] uint8                   _reserved : 7;
    };
} // namespace sw
