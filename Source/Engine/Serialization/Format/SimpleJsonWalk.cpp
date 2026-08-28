#include "pch.h"

#include "Engine/Serialization/Format/SimpleJsonWalk.h"

#include "Core/String/StringUtil.h"

namespace sw
{
	void collectJsonObjectArrayStarts( string_view json, const utf8* pArrayKey, vector<size_t>& outObjStartList )
	{
		outObjStartList.clear();
		if ( pArrayKey == nullptr )
			return;

		const string src{ json };
		const string key	= string( "\"" ) + pArrayKey + "\"";
		const size_t keyPos = src.find( key );
		if ( keyPos == string::npos )
			return;
		const size_t arr = src.find( '[', keyPos );
		const size_t end = src.find( ']', arr );
		if ( arr == string::npos || end == string::npos )
			return;

		size_t cursor = arr;
		while ( true )
		{
			const size_t obj = src.find( '{', cursor );
			if ( obj == string::npos || obj > end )
				break;
			outObjStartList.push_back( obj );
			cursor = src.find( '}', obj );
			if ( cursor == string::npos )
				break;
			++cursor;
		}
	}

	bool parseJsonIntField( string_view json, size_t from, size_t end, const utf8* pKey, int32& outValue )
	{
		if ( pKey == nullptr )
			return false;
		const string src{ json };
		const string key = string( "\"" ) + pKey + "\"";
		const size_t pos = src.find( key, from );
		if ( pos == string::npos || pos > end )
			return false;
		const size_t colon = src.find( ':', pos );
		if ( colon == string::npos || colon > end )
			return false;
		utf8* pEndPtr{ nullptr };
		outValue = static_cast<int32>( StringUtil::strtoll( src.c_str() + colon + 1, &pEndPtr, 10 ) );
		return true;
	}

	bool parseJsonFloatField( string_view json, size_t from, size_t end, const utf8* pKey, float32& outValue )
	{
		if ( pKey == nullptr )
			return false;
		const string src{ json };
		const string key = string( "\"" ) + pKey + "\"";
		const size_t pos = src.find( key, from );
		if ( pos == string::npos || pos > end )
			return false;
		const size_t colon = src.find( ':', pos );
		if ( colon == string::npos || colon > end )
			return false;
		outValue = static_cast<float32>( StringUtil::atof( src.c_str() + colon + 1 ) );
		return true;
	}

	bool parseJsonStringField( string_view json, size_t from, size_t end, const utf8* pKey, string& outValue )
	{
		if ( pKey == nullptr )
			return false;
		const string src{ json };
		const string key = string( "\"" ) + pKey + "\"";
		const size_t pos = src.find( key, from );
		if ( pos == string::npos || pos > end )
			return false;
		const size_t colon = src.find( ':', pos );
		if ( colon == string::npos || colon > end )
			return false;
		const size_t q0 = src.find( '"', colon + 1 );
		const size_t q1 = src.find( '"', q0 + 1 );
		if ( q0 == string::npos || q1 == string::npos || q0 > end || q1 > end )
			return false;
		outValue.assign( src, q0 + 1, q1 - q0 - 1 );
		return true;
	}

	bool parseJsonFloatAfter( string_view json, size_t from, const utf8* pKey, float32& outValue )
	{
		const size_t klen = StringUtil::strlen( pKey );
		if ( klen == 0 || from >= json.size() )
			return false;

		for ( size_t sliceIndex = from; sliceIndex + klen + 2 <= json.size(); ++sliceIndex )
		{
			if ( json[sliceIndex] == '"' && json[sliceIndex + klen + 1] == '"' &&
				 json.substr( sliceIndex + 1, klen ) == string_view{ pKey, klen } )
			{
				const size_t colon = json.find( ':', sliceIndex + klen + 2 );
				if ( colon == string_view::npos )
					return false;
				outValue = static_cast<float32>( StringUtil::atof( json.data() + colon + 1 ) );
				return true;
			}
		}
		return false;
	}

	bool parseJsonIntAfter( string_view json, size_t from, const utf8* pKey, int32& outValue )
	{
		const size_t klen = StringUtil::strlen( pKey );
		if ( klen == 0 || from >= json.size() )
			return false;

		for ( size_t sliceIndex = from; sliceIndex + klen + 2 <= json.size(); ++sliceIndex )
		{
			if ( json[sliceIndex] == '"' && json[sliceIndex + klen + 1] == '"' &&
				 json.substr( sliceIndex + 1, klen ) == string_view{ pKey, klen } )
			{
				const size_t colon = json.find( ':', sliceIndex + klen + 2 );
				if ( colon == string_view::npos )
					return false;
				outValue = StringUtil::atoi( json.data() + colon + 1 );
				return true;
			}
		}
		return false;
	}

	size_t findJsonArrayEnd( string_view json, const utf8* pArrayKey )
	{
		const string src{ json };
		const string key	= string( "\"" ) + pArrayKey + "\"";
		const size_t keyPos = src.find( key );
		if ( keyPos == string::npos )
			return string::npos;
		return src.find( ']', src.find( '[', keyPos ) );
	}
} // namespace sw
