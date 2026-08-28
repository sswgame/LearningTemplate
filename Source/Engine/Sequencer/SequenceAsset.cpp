#include "pch.h"

#include "Engine/Sequencer/SequenceAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Serialization/Format/JsonSerializer.h"
#include "Engine/Serialization/Format/SimpleJsonWalk.h"

namespace sw
{
	bool SequenceAsset::loadFromFile( string_view path )
	{
		_listItem.clear();
		_note.clear();
		_frameMin = 0;
		_frameMax = 100;
		if ( path.empty() || FileUtil::fileExists( path ) == false )
			return false;

		string json;
		if ( FileUtil::readTextFile( path, json ) == false || json.empty() )
			return false;
		return parseJson( json );
	}

	bool SequenceAsset::saveToFile( string_view path ) const
	{
		if ( path.empty() )
			return false;
		const string dir = FileUtil::getDirectoryPart( path );
		if ( dir.empty() == false )
			FileUtil::ensureDirectoryExists( dir );
		const string json = toJson();
		return FileUtil::writeTextFile( path, json );
	}

	bool SequenceAsset::parseJson( string_view jsonView )
	{
		_listItem.clear();
		const string json{ jsonView };

		parseJsonIntField( json, 0, json.size(), "frameMin", _frameMin );
		parseJsonIntField( json, 0, json.size(), "frameMax", _frameMax );
		parseJsonStringField( json, 0, json.size(), "note", _note );
		if ( _frameMax <= _frameMin )
			_frameMax = _frameMin + 1;

		vector<size_t> listItemObj;
		collectJsonObjectArrayStarts( json, "items", listItemObj );
		const size_t itemsEnd = findJsonArrayEnd( json, "items" );
		for ( const size_t obj : listItemObj )
		{
			SequenceTrackItem item{};
			parseJsonStringField( json, obj, itemsEnd, "name", item._name );
			parseJsonStringField( json, obj, itemsEnd, "target", item._targetObject );
			parseJsonIntField( json, obj, itemsEnd, "start", item._start );
			parseJsonIntField( json, obj, itemsEnd, "end", item._end );
			parseJsonIntField( json, obj, itemsEnd, "type", item._type );
			int32 colorInt{ 0 };
			if ( parseJsonIntField( json, obj, itemsEnd, "color", colorInt ) )
				item._color = static_cast<uint32>( colorInt );
			_listItem.push_back( std::move( item ) );
		}
		return true;
	}

	string SequenceAsset::toJson() const
	{
		StringBuilder<2048> sb;
		sb.append( "{\n" );
		sb.append( "  \"frameMin\": " ).append( _frameMin ).append( ",\n" );
		sb.append( "  \"frameMax\": " ).append( _frameMax ).append( ",\n" );
		sb.append( "  \"note\": \"" ).append( JsonSerializer::escapeString( _note ).c_str() ).append( "\",\n" );
		sb.append( "  \"items\": [\n" );
		for ( size_t itemIndex = 0; itemIndex < _listItem.size(); ++itemIndex )
		{
			const SequenceTrackItem& item = _listItem[itemIndex];
			sb.append( "    { \"name\": \"" )
				.append( JsonSerializer::escapeString( item._name ).c_str() )
				.append( "\", \"target\": \"" )
				.append( JsonSerializer::escapeString( item._targetObject ).c_str() )
				.append( "\", \"start\": " )
				.append( item._start )
				.append( ", \"end\": " )
				.append( item._end )
				.append( ", \"type\": " )
				.append( item._type )
				.append( ", \"color\": " )
				.append( static_cast<int32>( item._color ) )
				.append( " }" );
			if ( itemIndex + 1 < _listItem.size() )
				sb.append( "," );
			sb.append( "\n" );
		}
		sb.append( "  ]\n}\n" );
		return string{ sb.c_str() };
	}

	void SequenceAsset::collectActiveItems( int32 frame, vector<const SequenceTrackItem*>& outItemList ) const
	{
		outItemList.clear();
		for ( const SequenceTrackItem& item : _listItem )
		{
			if ( item._start <= frame && frame <= item._end )
				outItemList.push_back( &item );
		}
	}
} // namespace sw
