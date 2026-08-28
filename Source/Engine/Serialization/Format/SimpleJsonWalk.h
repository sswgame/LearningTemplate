/**
 * @file SimpleJsonWalk.h
 * @brief 도구 애셋 JSON을 위한 최소 필드 파서 (객체 배열 순회)
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	/** @brief JSON 객체 배열의 각 `{` 오프셋을 채웁니다. */
	SW_API void collectJsonObjectArrayStarts( string_view json, const utf8* pArrayKey, vector<size_t>& outObjStartList );
	/** @brief 구간에서 int 필드를 읽습니다. */
	SW_API bool parseJsonIntField( string_view json, size_t from, size_t end, const utf8* pKey, int32& outValue );
	/** @brief 구간에서 float 필드를 읽습니다. */
	SW_API bool parseJsonFloatField( string_view json, size_t from, size_t end, const utf8* pKey, float32& outValue );
	/** @brief 구간에서 string 필드를 읽습니다. */
	SW_API bool parseJsonStringField( string_view json, size_t from, size_t end, const utf8* pKey, string& outValue );
	/** @brief 키 다음 첫 float를 읽습니다. */
	SW_API bool parseJsonFloatAfter( string_view json, size_t from, const utf8* pKey, float32& outValue );
	/** @brief 키 다음 첫 int를 읽습니다. */
	SW_API bool parseJsonIntAfter( string_view json, size_t from, const utf8* pKey, int32& outValue );
	/** @brief 배열 닫는 `]` 오프셋을 찾습니다. */
	SW_API size_t findJsonArrayEnd( string_view json, const utf8* pArrayKey );
} // namespace sw
