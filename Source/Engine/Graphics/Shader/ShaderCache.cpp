#include "pch.h"

#include "Engine/Graphics/Shader/ShaderCache.h"

#include "Core/Concurrency/mutex.h"
#include "Core/String/StringBuilder.h"

namespace sw
{
	ShaderCache::ShaderCache()
		: _mapCache{}
		, _mutexCache{}
	{
	}

	ShaderCache::~ShaderCache()
	{
		shutdown();
	}

	bool ShaderCache::initialize()
	{
		clearCache();
		return true;
	}

	void ShaderCache::shutdown()
	{
		clearCache();
	}

	ShaderCompileResult ShaderCache::getOrCompile( const ShaderCompileDesc& desc )
	{
		string absPath;
		string cacheKey;
		uint64 currentTimestamp{ 0 };

		absPath = ResourceUtil::getResourcePath( desc._filePath );

		StringBuilder<constant::kMaxBuffer256> sb;
		sb.append( desc._filePath ).append( '_' ).append( desc._entryPoint ).append( '_' ).append( static_cast<int32>( desc._stage ) ).append( '_' ).append( static_cast<int32>( desc._targetFormat ) );
		for ( const auto& def : desc._listDefine )
		{
			sb.append( '_' ).append( def._name ).append( '=' ).append( def._value );
		}
		cacheKey.assign( sb.c_str(), sb.size() );

		if ( absPath.empty() == false )
			currentTimestamp = FileUtil::getFileTimestamp( absPath );

		{
			std::scoped_lock<mutex> lock{ _mutexCache };
			const auto				iter = _mapCache.find( cacheKey );
			if ( iter != _mapCache.end() )
			{
				if ( iter->second._lastTimestamp == currentTimestamp && currentTimestamp != 0 )
					return iter->second._result;
			}
		}

		BLOCK( "캐시 미스: HLSL 컴파일 및 캐시 항목 업데이트" )
		ShaderCompileResult compiledResult = ShaderCompiler::compileHLSL( desc );
		if ( compiledResult._bSuccess )
		{
			std::scoped_lock<mutex> lock{ _mutexCache };
			ShaderCacheEntry		entry{};
			entry._lastTimestamp = currentTimestamp;
			entry._result		 = compiledResult;
			_mapCache.insert_or_assign( std::move( cacheKey ), std::move( entry ) );
		}

		return compiledResult;
	}

	void ShaderCache::clearCache()
	{
		std::scoped_lock<mutex> lock{ _mutexCache };
		_mapCache.clear();
	}
} // namespace sw
