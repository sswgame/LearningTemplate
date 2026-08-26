/**
 * @file ParserUtil.h
 * @brief ReflectionParser 경로 조합·공통 토큰 분할
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/String/StringUtil.h"

namespace sw
{
	// ------------------------------------------------------------------------------
	// 1) emit — 생성 경로 조합 (.gen.cpp / .gen.h)
	// ------------------------------------------------------------------------------
	/**
	 * @brief 출력 디렉터리 + stem + 확장자(.gen.cpp / .gen.h)
	 * @note FileUtil::normalizePath는 Windows에서 소문자화하므로 CMake OUTPUT과 맞추기 위해 사용하지 않음
	 */
	inline string makeGeneratedPath( const string& outputDir, const string& sourceFilePath,
									 const string_view extension )
	{
		string fileName = FileUtil::removeExtension( FileUtil::getFileNamePart( sourceFilePath ) );
		fileName += extension;
		return FileUtil::joinPath( outputDir, fileName );
	}

	// ------------------------------------------------------------------------------
	// 2) parse — 템플릿 인자 토큰 분할
	// ------------------------------------------------------------------------------
	/** @brief `<>` 밖의 `,` 만 분할하고 각 토큰을 trim 합니다. */
	inline vector<string> splitCommaRespectingAngles( string_view inner )
	{
		vector<string> out;
		int32		   depth	  = 0;
		size_t		   tokenStart = 0;

		for ( size_t index = 0; index < inner.size(); ++index )
		{
			const utf8 c = inner[index];
			if ( c == '<' )
				++depth;
			else if ( c == '>' )
				--depth;

			if ( c == ',' && depth == 0 )
			{
				if ( index > tokenStart )
				{
					string_view token = StringUtil::trim( inner.substr( tokenStart, index - tokenStart ) );
					if ( token.empty() == false )
						out.emplace_back( token );
				}
				tokenStart = index + 1;
			}
		}
		if ( inner.size() > tokenStart )
		{
			string_view token = StringUtil::trim( inner.substr( tokenStart ) );
			if ( token.empty() == false )
				out.emplace_back( token );
		}
		return out;
	}
} // namespace sw
