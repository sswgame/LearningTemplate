#include "pch.h"

#include "Engine/Sequencer/SequenceAsset.h"

#include "Core/File/FileUtil.h"

#include "Engine/Utility/Json/JsonDocument.h"
#include "Engine/Utility/Resource/ResourceUtil.h"

namespace sw
{
	namespace
	{
		struct SequenceAssetInternal
		{
			static float3 readVec3( const JsonValue& parent, string_view key, const float3& fallback )
			{
				const JsonValue val = parent.get( key );
				if ( val.isObject() == false )
					return fallback;
				float3 result = fallback;
				result._x	  = static_cast<float32>( val.get( "x" ).asFloat( static_cast<float64>( fallback._x ) ) );
				result._y	  = static_cast<float32>( val.get( "y" ).asFloat( static_cast<float64>( fallback._y ) ) );
				result._z	  = static_cast<float32>( val.get( "z" ).asFloat( static_cast<float64>( fallback._z ) ) );
				return result;
			}

			static void writeVec3( const JsonValue& parent, string_view key, const float3& value )
			{
				const JsonValue obj = parent.set( key );
				obj.setObject();
				obj.set( "x" ).setFloat( static_cast<float64>( value._x ) );
				obj.set( "y" ).setFloat( static_cast<float64>( value._y ) );
				obj.set( "z" ).setFloat( static_cast<float64>( value._z ) );
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	bool SequenceAsset::loadFromFile( string_view path )
	{
		_listItem.clear();
		_note.clear();
		_frameMin = 0;
		_frameMax = 100;
		if ( path.empty() )
			return false;

		JsonDocument doc;
		if ( doc.loadPath( path ) == false )
			return false;
		return parseJson( doc.dump( -1 ) );
	}

	bool SequenceAsset::saveToFile( string_view path ) const
	{
		if ( path.empty() )
			return false;
		const string dir = FileUtil::getDirectoryPart( path );
		if ( dir.empty() == false )
			FileUtil::ensureDirectoryExists( dir );
		return FileUtil::writeTextFile( path, toJson() );
	}

	bool SequenceAsset::parseJson( string_view jsonView )
	{
		_listItem.clear();

		JsonDocument doc;
		if ( doc.parse( jsonView ) == false )
			return false;

		const JsonValue root = doc.root();
		_frameMin			 = static_cast<int32>( root.get( "frameMin" ).asInt( 0 ) );
		_frameMax			 = static_cast<int32>( root.get( "frameMax" ).asInt( 100 ) );
		_note				 = root.get( "note" ).asString();
		if ( _frameMax <= _frameMin )
			_frameMax = _frameMin + 1;

		const JsonValue itemsVal = root.get( "items" );
		if ( itemsVal.isArray() )
		{
			const size_t itemCount = itemsVal.size();
			for ( size_t itemIndex = 0; itemIndex < itemCount; ++itemIndex )
			{
				const JsonValue itemJson = itemsVal.at( itemIndex );
				if ( itemJson.isObject() == false )
					continue;

				SequenceTrackItem item{};
				item._name		   = itemJson.get( "name" ).asString();
				item._targetObject = itemJson.get( "target" ).asString();
				item._start		   = static_cast<int32>( itemJson.get( "start" ).asInt( 0 ) );
				item._end		   = static_cast<int32>( itemJson.get( "end" ).asInt( 10 ) );
				item._type		   = static_cast<int32>( itemJson.get( "type" ).asInt( 0 ) );
				item._color		   = static_cast<uint32>( itemJson.get( "color" ).asUint( 0xFFAA8080u ) );
				item._translation  = SequenceAssetInternal::readVec3( itemJson, "translation", float3{} );
				item._rotation	   = SequenceAssetInternal::readVec3( itemJson, "rotation", float3{} );
				item._scale		   = SequenceAssetInternal::readVec3( itemJson, "scale", float3{ 1.0f, 1.0f, 1.0f } );
				_listItem.push_back( std::move( item ) );
			}
		}
		return true;
	}

	string SequenceAsset::toJson() const
	{
		JsonDocument	doc;
		const JsonValue root = doc.makeObject();
		root.set( "frameMin" ).setInt( _frameMin );
		root.set( "frameMax" ).setInt( _frameMax );
		root.set( "note" ).setString( _note );

		const JsonValue itemsVal = root.set( "items" );
		itemsVal.setArray();
		for ( const SequenceTrackItem& item : _listItem )
		{
			const JsonValue itemJson = itemsVal.pushBack();
			itemJson.setObject();
			itemJson.set( "name" ).setString( item._name );
			itemJson.set( "target" ).setString( item._targetObject );
			itemJson.set( "start" ).setInt( item._start );
			itemJson.set( "end" ).setInt( item._end );
			itemJson.set( "type" ).setInt( item._type );
			itemJson.set( "color" ).setUint( item._color );
			SequenceAssetInternal::writeVec3( itemJson, "translation", item._translation );
			SequenceAssetInternal::writeVec3( itemJson, "rotation", item._rotation );
			SequenceAssetInternal::writeVec3( itemJson, "scale", item._scale );
		}

		return doc.dump( 2 );
	}

	void SequenceAsset::collectActiveItems( int32 frame, vector<const SequenceTrackItem*>& outListItem ) const
	{
		outListItem.clear();
		for ( const SequenceTrackItem& item : _listItem )
		{
			if ( item._start <= frame && frame <= item._end )
				outListItem.push_back( &item );
		}
	}
} // namespace sw
