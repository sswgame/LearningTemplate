#include "pch.h"

#include "Engine/Sequencer/SequenceAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Utility/Json/JsonDocument.h"

namespace sw
{
    namespace
    {
        struct SequenceAssetInternal
        {
            /**
             * @brief JSON 이 준 프레임 번호를 다룰 수 있는 범위로 자릅니다.
             * @details `asInt` 는 int64 를 줍니다. int32 로 그냥 캐스팅하면 큰 값이 **음수로 접힙니다.**
             *          자르는 이유는 `kSequenceFrameLimit` 에 적혀 있습니다.
             */
            static int32 clampFrame( int64 value )
            {
                const int64 limit = static_cast<int64>( kSequenceFrameLimit );
                return static_cast<int32>( MathUtil::clamp( value, -limit, limit ) );
            }

            static float3 readVec3( const JsonValue& parent, string_view key, const float3& fallback )
            {
                const JsonValue val = parent.get( key );
                if ( val.isObject() == false )
                    return fallback;
                float3 result = fallback;
                result._x     = static_cast<float32>( val.get( "x" ).asFloat( static_cast<float64>( fallback._x ) ) );
                result._y     = static_cast<float32>( val.get( "y" ).asFloat( static_cast<float64>( fallback._y ) ) );
                result._z     = static_cast<float32>( val.get( "z" ).asFloat( static_cast<float64>( fallback._z ) ) );
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
        *this = SequenceAsset{};
        if ( path.empty() )
            return false;

        JsonDocument doc;
        if ( doc.loadPath( path ) == false )
            return false;
        // 읽은 문서를 **그대로** 읽는다. 예전에는 `parseJson( doc.dump( -1 ) )` 이었다.
        // 파일 전체를 문자열로 되돌렸다가 다시 파싱하는, 같은 일을 두 번 하는 경로였다.
        return parseRoot( doc.getRoot() );
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
        JsonDocument doc;
        if ( doc.parse( jsonView ) == false )
        {
            // 반쯤 찬 에셋을 남기지 않는다. 실패는 "아무것도 읽지 않았다" 여야 한다.
            *this = SequenceAsset{};
            return false;
        }
        return parseRoot( doc.getRoot() );
    }

    bool SequenceAsset::parseRoot( const JsonValue& root )
    {
        _listItem.clear();

        // 파일에서 온 프레임 번호는 여기서 한 번 잘라 둔다. 아래의 `+ 1` 과 재생/타임라인 쪽의
        // 뺄셈들이 넘치지 않는 것은 이 잘라 둠에 기댄다. 자세한 이유는 `kSequenceFrameLimit` 참고.
        _frameMin = SequenceAssetInternal::clampFrame( root.get( "frameMin" ).asInt( 0 ) );
        _frameMax = SequenceAssetInternal::clampFrame( root.get( "frameMax" ).asInt( 100 ) );
        _note     = root.get( "note" ).asString();
        if ( _frameMax <= _frameMin )
            _frameMax = _frameMin + 1;

        forEachObjectInArray( root, "items", [this]( const JsonValue& itemJson, size_t /*itemIndex*/ )
        {
            SequenceTrackItem item{};
            item._name         = itemJson.get( "name" ).asString();
            item._targetObject = itemJson.get( "target" ).asString();
            item._start        = SequenceAssetInternal::clampFrame( itemJson.get( "start" ).asInt( 0 ) );
            item._end          = SequenceAssetInternal::clampFrame( itemJson.get( "end" ).asInt( 10 ) );
            item._type         = static_cast<int32>( itemJson.get( "type" ).asInt( 0 ) );
            item._color        = static_cast<uint32>( itemJson.get( "color" ).asUint( 0xFFAA8080u ) );
            item._translation  = SequenceAssetInternal::readVec3( itemJson, "translation", float3{} );
            item._rotation     = SequenceAssetInternal::readVec3( itemJson, "rotation", float3{} );
            item._scale        = SequenceAssetInternal::readVec3( itemJson, "scale", float3{ 1.0f, 1.0f, 1.0f } );
            _listItem.push_back( std::move( item ) );
        } );
        return true;
    }

    string SequenceAsset::toJson() const
    {
        JsonDocument    doc;
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
