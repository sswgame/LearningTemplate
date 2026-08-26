/**
 * @file AnnotationMeta.h
 * @brief AnnotationMeta.txt — REFLECT/PROPERTY/FUNCTION 토큰 → 필드 바인딩.
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/unordered_map.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) parse — AnnotationMeta.txt 토큰 → Kind/필드 바인딩
	// ------------------------------------------------------------------------------
	/** @brief 어노테이션 토큰이 가리키는 종류와 대상 필드. */
	struct AnnotationBinding
	{
		enum class Kind : uint8
		{
#define REGISTER_ANNOTATION_KIND( Name, Token ) Name,
#include "Core/Predefined/PredefinedAnnotationKind.xxx"
#undef REGISTER_ANNOTATION_KIND
		};

		Kind   _kind = Kind::Flag;
		string _field; ///< 정규 필드명: ReadOnly, Category, Server, …
	};

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

	// ------------------------------------------------------------------------------
	// 2) maps — scope → alias 조회 (bare 플래그 / key= 값)
	// ------------------------------------------------------------------------------
	class AnnotationMeta
	{
	public:
		/** @brief 프로세스 전역 어노테이션 메타를 반환합니다. */
		static AnnotationMeta& instance();

		/** @brief AnnotationMeta.txt 를 로드합니다. */
		bool loadFile( const string_view absPath );
		/** @brief 파일이 로드되었는지 반환합니다. */
		bool isLoaded() const noexcept { return _bLoaded; }

		/** @brief 단독 플래그/넷롤 토큰을 조회합니다 (scope: REFLECT|PROPERTY|FUNCTION). */
		const AnnotationBinding* findBare( const string_view scope, const string_view token ) const;

		/** @brief key= 쪽 바인딩을 조회합니다. */
		const AnnotationBinding* findKey( const string_view scope, const string_view key ) const;

	private:
		/** @brief 로드된 바인딩을 비웁니다. */
		void clear();
		/** @brief scope·alias 에 바인딩을 추가합니다. */
		void addAlias( const string& scope, const string& alias, AnnotationBinding binding );

		using ScopeMap = unordered_map<string, AnnotationBinding>;
		unordered_map<string, ScopeMap> _mapBare; ///< scope → alias → 바인딩
		unordered_map<string, ScopeMap> _mapKeys;
		bool							_bLoaded = false;
	};
} // namespace sw
