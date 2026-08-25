/**
 * @file TagSystem.h
 * @brief 태그 ID·계층·컨테이너 API
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/String/StringUtil.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) TagID — intern된 문자열 + 해시, 점 계층 (parent.child)
	// ------------------------------------------------------------------------------
	struct SW_API TagID
	{
		uint64		_id{ 0 };
		const utf8* _pString{ nullptr }; ///< 태그 문자열 (리터럴 또는 intern된 문자열)

		/** @brief 빈 태그입니다. */
		constexpr TagID() = default;
		/** @brief ID와 문자열로 태그를 만듭니다. */
		constexpr TagID( uint64 id, const utf8* pStr = nullptr )
			: _id{ id }
			, _pString{ pStr } {}

		/** @brief ID가 같은지 비교합니다. */
		constexpr bool operator==( const TagID& other ) const { return _id == other._id; }
		/** @brief ID가 다른지 비교합니다. */
		constexpr bool operator!=( const TagID& other ) const { return _id != other._id; }
		/** @brief ID 오름차순으로 비교합니다. */
		constexpr bool operator<( const TagID& other ) const { return _id < other._id; }

		/** @brief 유효한 태그인지 반환합니다. */
		constexpr bool isValid() const { return _id != 0; }

		/** @brief parentTag가 자신과 같거나 조상 체인에 있으면 true */
		constexpr bool isSubtagOf( const TagID& parentTag ) const
		{
			if ( _id == parentTag._id )
				return true;

			if ( _pString == nullptr || parentTag._pString == nullptr )
				return false;

			const utf8* pSource = _pString;
			const utf8* pParent = parentTag._pString;
			while ( *pParent != '\0' )
			{
				if ( *pSource != *pParent )
					return false;
				++pSource;
				++pParent;
			}
			return *pSource == '.';
		}
	};

	// ------------------------------------------------------------------------------
	// 2) intern — 런타임은 requestTag, 리터럴은 ""_tag
	// ------------------------------------------------------------------------------
	/** @brief 런타임에 태그를 만들거나 가져옵니다 (문자열 intern). */
	SW_API TagID requestTag( string_view str );

	/** @brief 리터럴에서 컴파일 타임 태그를 만듭니다. */
	constexpr TagID operator""_tag( const utf8* pStr, size_t len )
	{
		uint64 hash = StringUtil::kOffset64;

		for ( size_t charIndex = 0; charIndex < len; ++charIndex )
		{
			hash = ( hash ^ static_cast<uint64>( pStr[charIndex] ) ) * StringUtil::kPrime64;
		}

		return TagID( hash, pStr );
	}

	// ------------------------------------------------------------------------------
	// 3) TagContainer — GameObject에 붙는 태그 집합
	// ------------------------------------------------------------------------------
	class SW_API TagContainer
	{
	public:
		/** @brief 빈 컨테이너입니다. */
		TagContainer() = default;
		/** @brief 초기 태그 목록으로 만듭니다. */
		TagContainer( std::initializer_list<TagID> tags );

		/** @brief 용량을 예약합니다. */
		void reserve( uint32 capacity ) { _listTags.reserve( capacity ); }

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
		uint32 getTagCount() const { return static_cast<uint32>( _listTags.size() ); }

		/** @brief 태그를 모두 지웁니다. */
		void clear() { _listTags.clear(); }

		/** @brief 태그 목록을 반환합니다. */
		const vector<TagID>& getTags() const { return _listTags; }

	private:
		vector<TagID> _listTags;
	};

	// ------------------------------------------------------------------------------
	// 4) TagQuery & TagQueryExpr — 복합 불리언 AST 질의 표현식 시스템
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
		TagQueryExprType	 _type{ TagQueryExprType::Undefined };
		TagContainer		 _tags{};
		vector<TagQueryExpr> _listSubExprs{};

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

		static TagQueryExpr allExprMatch( vector<TagQueryExpr> subExprs )
		{
			TagQueryExpr expr;
			expr._type		   = TagQueryExprType::AllExprMatch;
			expr._listSubExprs = std::move( subExprs );
			return expr;
		}

		static TagQueryExpr anyExprMatch( vector<TagQueryExpr> subExprs )
		{
			TagQueryExpr expr;
			expr._type		   = TagQueryExprType::AnyExprMatch;
			expr._listSubExprs = std::move( subExprs );
			return expr;
		}

		static TagQueryExpr notExprMatch( TagQueryExpr subExpr )
		{
			TagQueryExpr expr;
			expr._type = TagQueryExprType::NotExprMatch;
			expr._listSubExprs.push_back( std::move( subExpr ) );
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
