#include "pch.h"

#include "Engine/Sequencer/SequenceAsset.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

#include <algorithm>

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

            /** @brief 정수 종류 값을 int32 로 자릅니다. 모르는 값도 지우지 않고 그대로 둔다(다시 쓸 때 잃지 않는다). */
            static int32 clampKindValue( int64 rawKind )
            {
                return static_cast<int32>( MathUtil::clamp<int64>( rawKind, MathUtil::kMinInt32, MathUtil::kMaxInt32 ) );
            }

            static void readCurve( const JSONValue& channelJSON, FloatCurve& outCurve )
            {
                outCurve._listKey.clear();
                const float64 limit = static_cast<float64>( kSequenceFrameLimit );
                forEachObjectInArray( channelJSON, "keys", [&outCurve, limit]( const JSONValue& keyJSON, size_t /*keyIndex*/ )
                {
                    FloatCurveKey key{};
                    key._time                    = static_cast<float32>( MathUtil::clamp( keyJSON.get( "time" ).asFloat( 0.0 ), -limit, limit ) );
                    key._value                   = static_cast<float32>( keyJSON.get( "value" ).asFloat( 0.0 ) );
                    key._arriveTangent           = static_cast<float32>( keyJSON.get( "arriveTangent" ).asFloat( 0.0 ) );
                    key._leaveTangent            = static_cast<float32>( keyJSON.get( "leaveTangent" ).asFloat( 0.0 ) );
                    const int64 rawInterpolation = keyJSON.get( "interpolation" ).asInt( static_cast<int64>( CurveInterpolation::Cubic ) );
                    key._interpolation           = ( rawInterpolation >= 0 && rawInterpolation <= static_cast<int64>( CurveInterpolation::Cubic ) )
                                                     ? static_cast<CurveInterpolation>( rawInterpolation )
                                                     : CurveInterpolation::Cubic;
                    key._bAutoTangent            = keyJSON.get( "autoTangent" ).asBool( true );
                    outCurve._listKey.push_back( key );
                } );
                outCurve.sortAndComputeAutoTangents();
            }

            static void writeCurve( const JSONValue& channelJSON, const FloatCurve& curve )
            {
                channelJSON.setObject();
                const JSONValue keysJSON = channelJSON.set( "keys" );
                keysJSON.setArray();
                for ( const FloatCurveKey& key : curve._listKey )
                {
                    const JSONValue keyJSON = keysJSON.pushBack();
                    keyJSON.setObject();
                    keyJSON.set( "time" ).setFloat( static_cast<float64>( key._time ) );
                    keyJSON.set( "value" ).setFloat( static_cast<float64>( key._value ) );
                    keyJSON.set( "arriveTangent" ).setFloat( static_cast<float64>( key._arriveTangent ) );
                    keyJSON.set( "leaveTangent" ).setFloat( static_cast<float64>( key._leaveTangent ) );
                    keyJSON.set( "interpolation" ).setInt( static_cast<int32>( key._interpolation ) );
                    keyJSON.set( "autoTangent" ).setBool( key._bAutoTangent );
                }
            }

            /** @brief 채널 하나의 @p frame 키를 찍습니다(있으면 값만 바꾼다). */
            static void setCurveKey( FloatCurve& curve, float32 frame, float32 value )
            {
                for ( FloatCurveKey& key : curve._listKey )
                {
                    if ( MathUtil::abs( key._time - frame ) < kSequenceKeyFrameTolerance )
                    {
                        key._value = value;
                        curve.sortAndComputeAutoTangents();
                        return;
                    }
                }
                (void)curve.addKey( frame, value );
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
        _listTrack.clear();

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
            item._kind          = static_cast<SequenceItemKind>( SequenceAssetInternal::clampKindValue( rawKind ) );
            if ( findItemKindInfo( item._kind ) == nullptr )
                SW_LOG_WARNING( "Sequence item '%#' has unknown type %# - it is kept but not applied", item._name, rawKind );
            item._color = static_cast<uint32>( itemJSON.get( "color" ).asUint( 0xFFAA8080u ) );
            _listItem.push_back( std::move( item ) );
        } );

        forEachObjectInArray( root, "tracks", [this]( const JSONValue& trackJSON, size_t /*trackIndex*/ )
        {
            SequenceKeyTrack track{};
            track._name         = trackJSON.get( "name" ).asString();
            track._targetObject = trackJSON.get( "target" ).asString();
            track._propertyPath = trackJSON.get( "property" ).asString();
            const int64 rawKind = trackJSON.get( "type" ).asInt( 0 );
            track._kind         = static_cast<SequenceTrackKind>( SequenceAssetInternal::clampKindValue( rawKind ) );
            if ( SequenceKeyTrack::getChannelCount( track._kind ) == 0 )
                SW_LOG_WARNING( "Sequence track '%#' has unknown type %# - it is kept but not applied", track._name, rawKind );
            forEachObjectInArray( trackJSON, "channels", [&track]( const JSONValue& channelJSON, size_t /*channelIndex*/ )
            {
                FloatCurve curve{};
                SequenceAssetInternal::readCurve( channelJSON, curve );
                track._listChannel.push_back( std::move( curve ) );
            } );
            track.fitChannelsToKind();
            _listTrack.push_back( std::move( track ) );
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
        }

        const JSONValue tracksVal = root.set( "tracks" );
        tracksVal.setArray();
        for ( const SequenceKeyTrack& track : _listTrack )
        {
            const JSONValue trackJSON = tracksVal.pushBack();
            trackJSON.setObject();
            trackJSON.set( "name" ).setString( track._name );
            trackJSON.set( "target" ).setString( track._targetObject );
            trackJSON.set( "type" ).setInt( static_cast<int32>( track._kind ) );
            trackJSON.set( "property" ).setString( track._propertyPath );
            const JSONValue channelsVal = trackJSON.set( "channels" );
            channelsVal.setArray();
            for ( const FloatCurve& channel : track._listChannel )
            {
                SequenceAssetInternal::writeCurve( channelsVal.pushBack(), channel );
            }
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

    bool SequenceAsset::hasCameraCut() const
    {
        for ( const SequenceTrackItem& item : _listItem )
        {
            const SequenceItemKindInfo* pInfo = findItemKindInfo( item._kind );
            if ( pInfo != nullptr && pInfo->_bCutsCamera )
                return true;
        }
        return false;
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

namespace sw
{
    uint32 SequenceKeyTrack::getChannelCount( SequenceTrackKind kind )
    {
        switch ( kind )
        {
            case SequenceTrackKind::Transform:
            {
                return kSequenceTransformChannelCount;
            }
            case SequenceTrackKind::Property:
            {
                return 1;
            }
            default:
            {
                return 0;
            }
        }
    }

    const utf8* SequenceKeyTrack::getChannelName( uint32 channelIndex ) const
    {
        if ( channelIndex >= _listChannel.size() )
            return "?";
        if ( _kind == SequenceTrackKind::Transform && channelIndex < kSequenceTransformChannelCount )
            return kArrSequenceTransformChannelName[channelIndex];
        return _propertyPath.empty() ? "Value" : _propertyPath.c_str();
    }

    void SequenceKeyTrack::fitChannelsToKind()
    {
        const uint32 channelCount = getChannelCount( _kind );
        if ( channelCount != 0 )
            _listChannel.resize( channelCount );
    }

    void SequenceKeyTrack::setKey( float32 frame, const float32* pArrValue, uint32 valueCount )
    {
        if ( pArrValue == nullptr )
            return;
        const uint32 channelCount = static_cast<uint32>( _listChannel.size() );
        for ( uint32 channelIndex = 0; channelIndex < channelCount && channelIndex < valueCount; ++channelIndex )
        {
            SequenceAssetInternal::setCurveKey( _listChannel[channelIndex], frame, pArrValue[channelIndex] );
        }
    }

    bool SequenceKeyTrack::setChannelKey( uint32 channelIndex, float32 frame, float32 value )
    {
        if ( channelIndex >= _listChannel.size() )
            return false;
        SequenceAssetInternal::setCurveKey( _listChannel[channelIndex], frame, value );
        return true;
    }

    bool SequenceKeyTrack::removeKeysAt( float32 frame )
    {
        bool bRemoved = false;
        for ( FloatCurve& channel : _listChannel )
        {
            const size_t keyCountBefore = channel._listKey.size();
            const auto   itRemoved      = std::remove_if( channel._listKey.begin(), channel._listKey.end(), [frame]( const FloatCurveKey& key )
                   { return MathUtil::abs( key._time - frame ) < kSequenceKeyFrameTolerance; } );
            channel._listKey.erase( itRemoved, channel._listKey.end() );
            if ( channel._listKey.size() != keyCountBefore )
            {
                bRemoved = true;
                channel.sortAndComputeAutoTangents();
            }
        }
        return bRemoved;
    }

    bool SequenceKeyTrack::hasKeyAt( float32 frame ) const
    {
        for ( uint32 channelIndex = 0; channelIndex < _listChannel.size(); ++channelIndex )
        {
            if ( findChannelKey( channelIndex, frame ) != invalid_index::kUint32 )
                return true;
        }
        return false;
    }

    void SequenceKeyTrack::collectKeyFrames( vector<float32>& outListFrame ) const
    {
        outListFrame.clear();
        for ( const FloatCurve& channel : _listChannel )
        {
            for ( const FloatCurveKey& key : channel._listKey )
            {
                outListFrame.push_back( key._time );
            }
        }
        std::sort( outListFrame.begin(), outListFrame.end() );
        // 채널마다 같은 프레임에 찍힌 키는 키 프레임 하나다.
        const auto itUnique = std::unique( outListFrame.begin(), outListFrame.end(), []( float32 left, float32 right )
        { return MathUtil::abs( left - right ) < kSequenceKeyFrameTolerance; } );
        outListFrame.erase( itUnique, outListFrame.end() );
    }

    float32 SequenceKeyTrack::evaluateChannel( uint32 channelIndex, float32 frame, float32 fallback ) const
    {
        if ( channelIndex >= _listChannel.size() )
            return fallback;
        return _listChannel[channelIndex].evaluate( frame, fallback );
    }

    uint32 SequenceKeyTrack::findChannelKey( uint32 channelIndex, float32 frame ) const
    {
        if ( channelIndex >= _listChannel.size() )
            return invalid_index::kUint32;
        const vector<FloatCurveKey>& listKey = _listChannel[channelIndex]._listKey;
        for ( size_t keyIndex = 0; keyIndex < listKey.size(); ++keyIndex )
        {
            if ( MathUtil::abs( listKey[keyIndex]._time - frame ) < kSequenceKeyFrameTolerance )
                return static_cast<uint32>( keyIndex );
        }
        return invalid_index::kUint32;
    }
} // namespace sw
