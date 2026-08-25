/**
 * @file ShaderCache.h
 * @brief 컴파일 결과 캐시
 * @note ResourceManager가 아닙니다. 셰이더 바이트코드는 RHI/컴파일러 수명이며 팩 에셋 인스턴스와 분리합니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Concurrency/mutex.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"

#include "Engine/Graphics/Shader/ShaderCompiler.h"

namespace sw

{
	/// @brief 디스크 캐시 한 항목 (바이트코드 + 해시)
	struct ShaderCacheEntry
	{
		uint64				_lastTimestamp{ 0 };
		ShaderCompileResult _result;
	};

	/// @brief 컴파일 결과 디스크 캐시
	class SW_API ShaderCache
	{
	public:
		/** @brief 캐시에 있으면 반환하고, 없거나 파일이 바뀌었으면 컴파일 후 캐시합니다. */
		static ShaderCompileResult getOrCompile( const ShaderCompileDesc& desc );
		/** @brief 컴파일 캐시를 비웁니다. */
		static void clearCache();

	private:
		static unordered_map<string, ShaderCacheEntry> _s_mapCache;
		static mutex								   _s_mutexCache;
	};
} // namespace sw
