#include "pch.h"

#include "Engine/Sequencer/SequenceAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

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

            static float3 readVec3( const JSONValue& parent, string_view key, const float3& fallback )
            {
                const JSONValue val = parent.get( key );
                if ( val.isObject() == false )
                    return fallback;
                float3 result = fallback;
                result._x     = static_cast<float32>( val.get( "x" ).asFloat( static_cast<float64>( fallback._x ) ) );
                result._y     = static_cast<float32>( val.get( "y" ).asFloat( static_cast<float64>( fallback._y ) ) );
                result._z     = static_cast<float32>( val.get( "z" ).asFloat( static_cast<float64>( fallback._z ) ) );
                return result;
            }

            static void writeVec3( const JSONValue& parent, string_view key, const float3& value )
            {
                const JSONValue obj = parent.set( key );
                obj.setObject();
                obj.set( "x" ).setFloat( static_cast<float64>( value._x ) );
                obj.set( "y" ).setFloat( static_cast<float64>( value._y ) );
                obj.set( "z" ).setFloat( static_cast<float64>( value._z ) );
            }

            /** @brief 종류 표가 값 순서인지 봅니다. `findItemKindInfo` 가 값으로 바로 찾습니다. */
            static constexpr bool isKindTableOrdered()
            {
                for ( size_t kindIndex = 0; kindIndex < SW_COUNT_OF( kArrSequenceItemKindInfo ); ++kindIndex )
                {
                    if ( kArrSequenceItemKindInfo[kindIndex]._kind != static_cast<SequenceItemKind>( kindIndex ) )
                        return false;
                }
                return true;
            }
        };

        static_assert( SequenceAssetInternal::isKindTableOrdered(), "kArrSequenceItemKindInfo must be ordered by SequenceItemKind" );
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "SequenceAsset" );

    bool SequenceAsset::loadFromFile( string_view path )
    {
        *this = SequenceAsset{};
        if ( path.empty() )
            return false;

        JSONDocument doc;
        if ( doc.loadPath( path ) == false )
            return false;
        // 읽은 문서를 **그대로** 읽는다(문자열로 되돌렸다가 다시 파싱하지 않는다).
        return parseRoot( doc.getRoot() );
    }

    bool SequenceAsset::saveToFile( string_view path ) const
    {
        if ( path.empty() )
            return false;
        FileUtil::ensureParentDirectoryExists( path );
        return FileUtil::writeTextFile( path, toJSON() );
    }

    bool SequenceAsset::parseJSON( string_view jsonView )
    {
        JSONDocument doc;
        if ( doc.parse( jsonView ) == false )
        {
            // 반쯤 찬 에셋을 남기지 않는다. 실패는 "아무것도 읽지 않았다" 여야 한다.
            *this = SequenceAsset{};
            return false;
        }
        return parseRoot( doc.getRoot() );
    }

    bool SequenceAsset::parseRoot( const JSONValue& root )
    {
        _listItem.clear();

        // 파일에서 온 프레임 번호는 여기서 한 번 잘라 둔다. 아래의 `+ 1` 과 재생/타임라인 쪽의
        // 뺄셈들이 넘치지 않는 것은 이 잘라 둠에 기댄다. 자세한 이유는 `kSequenceFrameLimit` 참고.
        _frameMin = SequenceAssetInternal::clampFrame( root.get( "frameMin" ).asInt( 0 ) );
        _frameMax = SequenceAssetInternal::clampFrame( root.get( "frameMax" ).asInt( 100 ) );
        _note     = root.get( "note" ).asString();
        if ( _frameMax <= _frameMin )
            _frameMax = _frameMin + 1;

        forEachObjectInArray( root, "items", [this]( const JSONValue& itemJSON, size_t /*itemIndex*/ )
        {
            SequenceTrackItem item{};
            item._name         = itemJSON.get( "name" ).asString();
            item._targetObject = itemJSON.get( "target" ).asString();
            item._start        = SequenceAssetInternal::clampFrame( itemJSON.get( "start" ).asInt( 0 ) );
            item._end          = SequenceAssetInternal::clampFrame( itemJSON.get( "end" ).asInt( 10 ) );
            // 종류는 정수 그대로 둔다 — 모르는 값(새 버전이 쓴 종류)도 다시 쓸 때 잃지 않고, 적용만 하지 않는다.
            const int64 rawKind = itemJSON.get( "type" ).asInt( 0 );
            item._kind          = static_cast<SequenceItemKind>( static_cast<int32>( MathUtil::clamp<int64>( rawKind, MathUtil::kMinInt32, MathUtil::kMaxInt32 ) ) );
            if ( findItemKindInfo( item._kind ) == nullptr )
                SW_LOG_WARNING( "Sequence item '%#' has unknown type %# - it is kept but not applied", item._name, rawKind );
            item._color       = static_cast<uint32>( itemJSON.get( "color" ).asUint( 0xFFAA8080u ) );
            item._translation = SequenceAssetInternal::readVec3( itemJSON, "translation", float3{} );
            item._rotation    = SequenceAssetInternal::readVec3( itemJSON, "rotation", float3{} );
            item._scale       = SequenceAssetInternal::readVec3( itemJSON, "scale", float3{ 1.0f, 1.0f, 1.0f } );
            _listItem.push_back( std::move( item ) );
        } );
        return true;
    }

    string SequenceAsset::toJSON() const
    {
        JSONDocument    doc;
        const JSONValue root = doc.makeObject();
        root.set( "frameMin" ).setInt( _frameMin );
        root.set( "frameMax" ).setInt( _frameMax );
        root.set( "note" ).setString( _note );

        const JSONValue itemsVal = root.set( "items" );
        itemsVal.setArray();
        for ( const SequenceTrackItem& item : _listItem )
        {
            const JSONValue itemJSON = itemsVal.pushBack();
            itemJSON.setObject();
            itemJSON.set( "name" ).setString( item._name );
            itemJSON.set( "target" ).setString( item._targetObject );
            itemJSON.set( "start" ).setInt( item._start );
            itemJSON.set( "end" ).setInt( item._end );
            itemJSON.set( "type" ).setInt( static_cast<int32>( item._kind ) );
            itemJSON.set( "color" ).setUint( item._color );
            SequenceAssetInternal::writeVec3( itemJSON, "translation", item._translation );
            SequenceAssetInternal::writeVec3( itemJSON, "rotation", item._rotation );
            SequenceAssetInternal::writeVec3( itemJSON, "scale", item._scale );
        }

        return doc.dump( 2 );
    }

    const SequenceItemKindInfo* SequenceAsset::findItemKindInfo( SequenceItemKind kind )
    {
        const int32 kindIndex = static_cast<int32>( kind );
        if ( kindIndex < 0 || kindIndex >= static_cast<int32>( SequenceItemKind::Count ) )
            return nullptr;
        return &kArrSequenceItemKindInfo[kindIndex];
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
