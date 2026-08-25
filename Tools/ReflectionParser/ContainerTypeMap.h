/**
 * @file ContainerTypeMap.h
 * @brief clang 컨테이너 표기 → ContainerKind + wrapper stem (ReflectBuiltins에서 채움).
 */
#pragma once
#include "Core/Common/StdHeaders.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionContainers.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) maps — clang 표기 부분 문자열 → ContainerKind + 래퍼 stem
	// ------------------------------------------------------------------------------
	/** @brief clang 표기 부분 문자열과 컨테이너 종류·래퍼 stem 규칙. */
	struct ContainerTypeRule
	{
		string		  _match;
		ContainerKind _kind = ContainerKind::Sequence;
		string		  _type; ///< Vector | Map | UnorderedMap | …
	};

	class ContainerTypeMap
	{
	public:
		/** @brief 프로세스 전역 컨테이너 맵을 반환합니다. */
		static ContainerTypeMap& instance();

		/** @brief builtins 등록이 완료되었는지 반환합니다. */
		bool isLoaded() const noexcept { return _bLoaded; }

		/** @brief clang 타입 표기에 맞는 규칙을 찾습니다. */
		const ContainerTypeRule* match( const std::string_view clangTypeSpelling ) const;

		/** @brief 매칭 문자열·종류·래퍼 stem 규칙을 등록합니다. */
		void registerRule( const string& match, ContainerKind kind, const string& type );
		/** @brief 종류 철자를 파싱해 규칙을 등록합니다. */
		void registerRule( const string& match, const string& kindSpelling, const string& type );

		/** @brief 등록 규칙을 비웁니다. */
		void clear();
		/** @brief 로드 완료 플래그를 설정합니다. */
		void setLoaded( bool bLoaded ) { _bLoaded = bLoaded; }

	private:
		vector<ContainerTypeRule> _listRules;
		bool					  _bLoaded = false;
	};
} // namespace sw
