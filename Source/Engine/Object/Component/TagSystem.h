/**
 * @file TagSystem.h
 * @brief 태그 집합·질의 API (`TagID` 자체는 `Core/String/TagID.h`).
 *
 * @details 여기 남은 것은 **리플렉션이 필요한 것들**이다 — `PROPERTY` 로 직렬화되는 태그 집합
 *          (`TagContainer`)과 질의(`TagQuery`). `TagID` 는 Core 기능만 쓰는 값 타입이라
 *          `Core/String/TagID.h` 로 내렸다(이유는 그 파일의 @note 참고).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/StringUtil.h"
#include "Core/String/TagID.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    // ------------------------------------------------------------------------------
    // 1) TagContainer — GameObject에 붙는 태그 집합
    // ------------------------------------------------------------------------------
    REFLECT()
    class SW_API TagContainer
    {
    public:
        REFLECT_BODY();
        /** @brief 빈 컨테이너입니다. */
        TagContainer() = default;
        /** @brief 초기 태그 목록으로 만듭니다. */
        TagContainer( std::initializer_list<TagID> tags );

        /** @brief 용량을 예약합니다. */
        void reserve( uint32 capacity ) { _listTag.reserve( capacity ); }

        /** @brief 태그를 추가합니다. */
        void addTag( TagID tag );

        /** @brief 태그를 제거합니다. */
        void removeTag( TagID tag );

        /** @brief 태그 포함 여부를 반환합니다. bExactMatch면 동일 ID만, 아니면 서브태그 허용. */
        bool hasTag( TagID tag, bool bExactMatch = false ) const;

        /** @brief required의 모든 태그를 포함하는지 검사합니다. */
        bool hasAllTags( const TagContainer& required ) const;

        /** @brief other의 태그 중 하나라도 포함하는지 검사합니다. */
        bool hasAnyTag( const TagContainer& other ) const;

        /** @brief required는 모두 포함하고 forbidden은 하나도 없는지 검사합니다. */
        bool matchTags( const TagContainer& required, const TagContainer& forbidden ) const;

        /** @brief 태그 개수를 반환합니다. */
        uint32 getTagCount() const { return static_cast<uint32>( _listTag.size() ); }

        /** @brief 태그를 모두 지웁니다. */
        void clear() { _listTag.clear(); }

        /** @brief 태그 목록을 반환합니다. */
        const vector<TagID>& getTags() const { return _listTag; }

    private:
        PROPERTY()
        vector<TagID> _listTag;
    };

    // ------------------------------------------------------------------------------
    // 2) TagQuery & TagQueryExpr — 복합 불리언 AST 질의 표현식 시스템
    // ------------------------------------------------------------------------------
    enum class TagQueryExprType : uint8
    {
        Undefined = 0,
        AnyTagsMatch,
        AllTagsMatch,
        NoTagsMatch,
        AnyExprMatch, // Logical OR
        AllExprMatch, // Logical AND
        NotExprMatch  // Logical NOT
    };

    struct TagQueryExpr
    {
        TagQueryExprType     _type{ TagQueryExprType::Undefined };
        TagContainer         _tags{};
        vector<TagQueryExpr> _listSubExpr{};

        static TagQueryExpr anyTagsMatch( const TagContainer& tags )
        {
            TagQueryExpr expr;
            expr._type = TagQueryExprType::AnyTagsMatch;
            expr._tags = tags;
            return expr;
        }

        static TagQueryExpr allTagsMatch( const TagContainer& tags )
        {
            TagQueryExpr expr;
            expr._type = TagQueryExprType::AllTagsMatch;
            expr._tags = tags;
            return expr;
        }

        static TagQueryExpr noTagsMatch( const TagContainer& tags )
        {
            TagQueryExpr expr;
            expr._type = TagQueryExprType::NoTagsMatch;
            expr._tags = tags;
            return expr;
        }

        static TagQueryExpr allExprMatch( vector<TagQueryExpr> listSubExpr )
        {
            TagQueryExpr expr;
            expr._type        = TagQueryExprType::AllExprMatch;
            expr._listSubExpr = std::move( listSubExpr );
            return expr;
        }

        static TagQueryExpr anyExprMatch( vector<TagQueryExpr> listSubExpr )
        {
            TagQueryExpr expr;
            expr._type        = TagQueryExprType::AnyExprMatch;
            expr._listSubExpr = std::move( listSubExpr );
            return expr;
        }

        static TagQueryExpr notExprMatch( TagQueryExpr subExpr )
        {
            TagQueryExpr expr;
            expr._type = TagQueryExprType::NotExprMatch;
            expr._listSubExpr.push_back( std::move( subExpr ) );
            return expr;
        }
    };

    class SW_API TagQuery
    {
    public:
        TagQuery() = default;

        static TagQuery createAnyMatch( const TagContainer& tags );
        static TagQuery createAllMatch( const TagContainer& tags );
        static TagQuery createNoMatch( const TagContainer& tags );
        static TagQuery createExpression( const TagQueryExpr& expr );

        bool matches( const TagContainer& container ) const;
        bool isEmpty() const { return _rootExpr._type == TagQueryExprType::Undefined; }
        void clear() { _rootExpr = TagQueryExpr{}; }

        const TagQueryExpr& getRootExpr() const { return _rootExpr; }

    private:
        static bool evalExpr( const TagQueryExpr& expr, const TagContainer& container );

    private:
        TagQueryExpr _rootExpr{};
    };
} // namespace sw
