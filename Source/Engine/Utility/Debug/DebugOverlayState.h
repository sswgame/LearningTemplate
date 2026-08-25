/**
 * @file DebugOverlayState.h
 * @brief Cross-module debug overlay (game writes keys, Editor reads).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/String/hashed_string.h"

namespace sw
{
	/**
	 * @brief 장르 중립 디버그 오버레이 상태.
	 * @details float32 gauge / 짧은 문자열을 hashed_string 키로 기록합니다.
	 */
	struct SW_API DebugOverlayState
	{
		unordered_map<hashed_string, float32> _mapFloats;
		unordered_map<hashed_string, string>  _mapStrings;
		uint8								  _bVisible : 1;
		[[maybe_unused]] uint8				  _reserved : 7;

		DebugOverlayState()
			: _bVisible{ 1 }
			, _reserved{ 0 } {}

		/** @brief float 값을 넣습니다. */
		void setFloat( hashed_string key, float32 value );
		/** @brief float 값을 읽습니다. */
		float32 getFloat( hashed_string key, float32 defaultValue = 0.0f ) const;
		/** @brief 문자열 값을 넣습니다. */
		void setString( hashed_string key, string_view value );
		/** @brief 문자열 값을 읽습니다. */
		string getString( hashed_string key ) const;
		/** @brief 비웁니다. */
		void clear();
	};
} // namespace sw
