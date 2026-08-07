#pragma once

/**
 * @file ParserUtil.h
 * @brief ReflectionParser 공통 경로/텍스트 헬퍼 (Core FileUtil/StringBuilder 래핑)
 */

#include "Core/CoreMinimal.h"
#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/String/StringBuilder.h"
#include "Core/Utility/String/StringUtil.h"

namespace sw::tool
{
	inline void normalizeSlashesPreserveCase( std::string& path )
	{
		for ( utf8& c : path )
		{
			if ( c == '\\' )
				c = '/';
		}
	}

	/**
	 * @brief 출력 디렉터리 + stem 기반 gen 경로 조합
	 * @note FileUtil::normalizePath는 Windows에서 소문자화하므로 CMake OUTPUT과 맞추기 위해 사용하지 않음
	 */
	inline std::string makeGeneratedCppPath( const std::string& outputDir, const std::string& sourceFilePath )
	{
		const std::string stem = FileUtil::removeExtension( FileUtil::getFileNamePart( sourceFilePath ) );

		StringBuilder<constant::kMaxBuffer1024> path;
		path.append( outputDir );
		if ( outputDir.empty() == false )
		{
			const utf8 last = outputDir.back();
			if ( last != '/' && last != '\\' )
				path.append( '/' );
		}
		path.append( stem );
		path.append( ".gen.cpp" );

		std::string result( path.view() );
		normalizeSlashesPreserveCase( result );
		return result;
	}

	inline std::string readTextFile( const std::string& path )
	{
		std::vector<uint8> data;
		if ( FileUtil::readFile( path, data ) == false )
			return {};
		return std::string( reinterpret_cast<const utf8*>( data.data() ), data.size() );
	}

	inline bool writeTextFile( const std::string& path, const std::string& content )
	{
		return FileUtil::writeFile( path, reinterpret_cast<const uint8*>( content.data() ), static_cast<uint64>( content.size() ) );
	}

	inline bool containsKeyword( const std::string_view text, const std::string_view keyword )
	{
		if ( text.empty() || keyword.empty() )
			return false;
		return text.find( keyword ) != std::string_view::npos;
	}
}
