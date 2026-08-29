/**
 * @file ShaderCache.h
 * @brief 컴파일 결과 캐시 매니저
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

	/// @brief 컴파일 결과 디스크 캐시 매니저
	class SW_API ShaderCache
	{
	public:
		ShaderCache();
		~ShaderCache();

		ShaderCache( const ShaderCache& )			 = delete;
		ShaderCache& operator=( const ShaderCache& ) = delete;

		/** @brief 셰이더 캐시를 초기화합니다. */
		bool initialize();
		/** @brief 셰이더 캐시를 정리하고 종료합니다. */
		void shutdown();

		/** @brief 캐시에 있으면 반환하고, 없거나 파일이 바뀌었으면 컴파일 후 캐시합니다. */
		ShaderCompileResult getOrCompile( const ShaderCompileDesc& desc );
		/** @brief 컴파일 캐시를 비웁니다. */
		void clearCache();

	private:
		unordered_map<string, ShaderCacheEntry> _mapCache;
		mutex									_mutexCache;
	};
} // namespace sw
