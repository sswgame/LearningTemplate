#include "pch.h"

#include "Engine/Utility/File/KeyValueFile.h"

namespace sw
{

	bool KeyValueFile::parse( string_view text, KeyValueMap& mapOut, KeyValueParseOptions opt )
	{
		mapOut.clear();
		forEachContentLine(
			text,
			[&]( string_view line )
		{
			if ( opt._bSkipSemicolonComments != 0 && line.front() == ';' )
				return;
			if ( opt._bSkipBracketSections != 0 && line.front() == '[' )
				return;

			const size_t eq = line.find( '=' );
			if ( eq == string_view::npos || eq == 0 )
				return;

			string_view key = StringUtil::trim( line.substr( 0, eq ) );
			string_view val = StringUtil::trim( line.substr( eq + 1 ) );
			if ( key.empty() )
				return;
			mapOut.emplace( string( key ), string( val ) );
		},
			opt._commentChar );

		return mapOut.empty() == false;
	}

	bool KeyValueFile::loadFile( string_view absPath, KeyValueMap& mapOut, KeyValueParseOptions opt )
	{
		string text;
		if ( FileUtil::readTextFile( absPath, text ) == false )
			return false;
		return parse( text, mapOut, opt );
	}

	bool KeyValueFile::loadResource( string_view relativePath, KeyValueMap& mapOut, KeyValueParseOptions opt,
									 string* pOutAbsPath )
	{
		string text;
		string absPath;
		if ( ResourceUtil::readTextResource( relativePath, text, &absPath ) == false )
			return false;
		if ( pOutAbsPath != nullptr )
			*pOutAbsPath = std::move( absPath );
		return parse( text, mapOut, opt );
	}

	const utf8* KeyValueFile::get( const KeyValueMap& mapData, string_view key, const utf8* pFallback )
	{
		if ( key.empty() )
			return pFallback != nullptr ? pFallback : "";
		const KeyValueMap::const_iterator it = mapData.find( string( key ) );
		if ( it == mapData.end() )
			return pFallback;
		return it->second.c_str();
	}

	int32 KeyValueFile::getInt( const KeyValueMap& mapData, string_view key, int32 fallback )
	{
		const utf8* pV = get( mapData, key, nullptr );
		if ( pV == nullptr || pV[0] == '\0' )
			return fallback;
		return StringUtil::atoi( pV );
	}

	float32 KeyValueFile::getFloat( const KeyValueMap& mapData, string_view key, float32 fallback )
	{
		const utf8* pV = get( mapData, key, nullptr );
		if ( pV == nullptr || pV[0] == '\0' )
			return fallback;
		return static_cast<float32>( StringUtil::atof( pV ) );
	}
} // namespace sw
