#pragma once
/**
 * @file ShaderCache.h
 * @brief Auto-generated documentation header
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
		/**
		 * @brief getOrCompile 처리를 수행합니다.
		 */
		static ShaderCompileResult getOrCompile( const ShaderCompileDesc& desc );
		/**
		 * @brief clearCache 처리를 수행합니다.
		 */
		static void				   clearCache();

	private:
		static std::unordered_map<std::string, ShaderCacheEntry> _s_cacheMap;
		static std::mutex										 _s_cacheMutex;
	};
}
