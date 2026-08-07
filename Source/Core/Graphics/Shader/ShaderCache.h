#pragma once
/**
 * @file ShaderCache.h
 * @brief 컴파일 결과 캐시
 */

#include "Core/CoreMinimal.h"
#include "Core/Graphics/Shader/ShaderCompiler.h"

namespace sw
{
	struct ShaderCacheEntry
	{
		uint64				_lastTimestamp = 0;
		ShaderCompileResult _result;
	};

	class SW_API ShaderCache
	{
	public:
		/** @brief 캐시에 있으면 반환하고, 없거나 파일이 바뀌었으면 컴파일 후 캐시합니다. */
		static ShaderCompileResult getOrCompile( const ShaderCompileDesc& desc );
		/** @brief 컴파일 캐시를 비웁니다. */
		static void				   clearCache();

	private:
		static std::unordered_map<std::string, ShaderCacheEntry> _s_cacheMap;
		static std::mutex										 _s_cacheMutex;
	};
}
