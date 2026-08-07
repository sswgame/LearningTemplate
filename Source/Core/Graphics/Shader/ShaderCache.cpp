/**
 * @file ShaderCache.cpp
 * @brief ShaderCache 구현
 */
#include "ShaderCache.h"
#include "Core/Utility/Resource/ResourceUtil.h"
#include "Core/Utility/String/formatString.h"
#include "Core/Utility/Log/Logger.h"

namespace sw
{
	std::unordered_map<std::string, ShaderCacheEntry> ShaderCache::_s_cacheMap;
	std::mutex										  ShaderCache::_s_cacheMutex;

	ShaderCompileResult ShaderCache::getOrCompile( const ShaderCompileDesc& desc )
	{
		std::lock_guard<std::mutex> lock{ _s_cacheMutex };

		std::string absPath;
		std::string cacheKey;
		uint64		currentTimestamp = 0;

		absPath = ResourceUtil::getResourcePath( desc._filePath );

		utf8 keyBuf[256];
		formatstring( keyBuf, sizeof( keyBuf ), "%#_%#_%#_%#", desc._filePath, desc._entryPoint, static_cast<int>( desc._stage ), static_cast<int>( desc._targetFormat ) );
		cacheKey = keyBuf;

		if ( absPath.empty() == false )
		{
			currentTimestamp = FileUtil::getFileTimestamp( absPath );
		}

		auto iter = _s_cacheMap.find( cacheKey );
		if ( iter != _s_cacheMap.end() )
		{

			if ( iter->second._lastTimestamp == currentTimestamp && currentTimestamp != 0 )
			{
				return iter->second._result;
			}
		}

		BLOCK( "캐시 미스: HLSL 컴파일 및 캐시 항목 업데이트" )
		ShaderCompileResult compiledResult = ShaderCompiler::compileHLSL( desc );
		if ( compiledResult._bSuccess == true )
		{
			ShaderCacheEntry entry{};
			entry._lastTimestamp = currentTimestamp;
			entry._result		 = compiledResult;
			_s_cacheMap.insert_or_assign( std::move( cacheKey ), std::move( entry ) );
		}

		return compiledResult;
	}

	void ShaderCache::clearCache()
	{
		std::lock_guard<std::mutex> lock{ _s_cacheMutex };
		_s_cacheMap.clear();
	}
} // namespace sw
