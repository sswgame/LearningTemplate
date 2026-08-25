/**
 * @file TypeNameMap.h
 * @brief ReflectionParser용 clang 표기 → canonical 맵 (ReflectBuiltins에서 채움).
 */
#pragma once
#include "Core/Common/StdHeaders.h"
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
		/** @brief 프로세스 전역 타입명 맵을 반환합니다. */
		static TypeNameMap& instance();

		/** @brief builtins 등록이 완료되었는지 반환합니다. */
		bool isLoaded() const { return _bLoaded; }

		/** @brief clang 수식어를 제거한 뒤 alias → canonical 로 정규화합니다. */
		string normalize( const string& clangSpelling ) const;

		/** @brief canonical·네임스페이스·별칭을 맵에 등록합니다. */
		void registerEntry( const string& canonical, const string& nameSpace,
							const vector<string>& aliases );

		/** @brief 등록 항목을 비웁니다. */
		void clear();
		/** @brief 로드 완료 플래그를 설정합니다. */
		void setLoaded( bool bLoaded ) { _bLoaded = bLoaded; }

	private:
		/** @brief alias 키를 canonical 에 연결합니다. */
		void addKey( const string& key, const string& canonical );

		unordered_map<string, string> _aliasToCanonical;
		bool						  _bLoaded = false;
	};

	// ------------------------------------------------------------------------------
	// 2) parse — 전역 맵으로 clang 표기 정규화
	// ------------------------------------------------------------------------------
	/** @brief 전역 TypeNameMap 으로 clang 표기를 canonical 로 정규화합니다. */
	string normalizeTypeName( const string& clangSpelling );
} // namespace sw
