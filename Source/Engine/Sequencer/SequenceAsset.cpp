#include "pch.h"

#include "Engine/Sequencer/SequenceAsset.h"

#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Resource/ResourceIO.h"
#include "Engine/Serialization/Format/XMLSerializer.h"

#include <algorithm>

namespace sw
{
    SW_LOG_CALLER( "SequenceAsset" );

    namespace
    {
        /** @brief 프레임 번호가 `kSequenceFrameLimit` 안이면 true 입니다. 밖이면 오류를 남깁니다(@p pWhat 은 칸 이름). */
        bool isFrameWithinLimit( float64 frame, string_view sourceLabel, const utf8* pWhat )
        {
            const float64 limit = static_cast<float64>( kSequenceFrameLimit );
            if ( -limit <= frame && frame <= limit )
                return true;
            SW_LOG_ERROR( "Sequence '%#': %# %# is outside +-%# frames", sourceLabel, pWhat, frame, kSequenceFrameLimit );
            return false;
        }

        /** @brief 채널 하나의 @p frame 키를 찍습니다(있으면 값만 바꾼다). */
        void setCurveKey( FloatCurve& curve, float32 frame, float32 value )
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

        /** @brief 종류 테이블이 값 순서인지 확인합니다. `findItemKindInfo` 가 값으로 바로 찾습니다. */
        constexpr bool isKindTableOrdered()
        {
            for ( size_t kindIndex = 0; kindIndex < SW_COUNT_OF( kArrSequenceItemKindInfo ); ++kindIndex )
            {
                if ( kArrSequenceItemKindInfo[kindIndex]._kind != static_cast<SequenceItemKind>( kindIndex ) )
                    return false;
            }
            return true;
        }

        static_assert( isKindTableOrdered(), "kArrSequenceItemKindInfo must be ordered by SequenceItemKind" );
    } // namespace
} // namespace sw

namespace sw
{
    bool SequenceAsset::loadFromFile( string_view path )
    {
        *this = SequenceAsset{};
        if ( path.empty() )
            return false;

        string text;
        if ( ResourceIO::readTextResource( ResourceID{ path }, text ) == false )
        {
            SW_LOG_WARNING( "Could not read sequence '%#'", path );
            return false;
        }
        return readText( text, path );
    }

    bool SequenceAsset::loadFromDiskFile( string_view diskPath )
    {
        *this = SequenceAsset{};
        if ( diskPath.empty() )
            return false;

        string text;
        if ( ResourceIO::readTextFile( diskPath, text ) == false )
        {
            SW_LOG_WARNING( "Could not read sequence file '%#'", diskPath );
            return false;
        }
        return readText( text, diskPath );
    }

    bool SequenceAsset::saveToFile( string_view path ) const
    {
        if ( path.empty() )
            return false;
        return FileUtil::ensureParentDirectoryExists( path ) && FileUtil::writeTextFile( path, toXML() );
    }

    bool SequenceAsset::parseXML( string_view xmlView )
    {
        return readText( xmlView, "<sequence text>" );
    }

    bool SequenceAsset::readText( string_view xml, string_view sourceLabel )
    {
        // 반쯤 찬 에셋을 남기지 않는다. 실패는 "아무것도 읽지 않았다" 여야 한다.
        *this = SequenceAsset{};
        const string label{ sourceLabel };
        // 키는 대소문자까지 맞아야 하고, 모르는 속성 · 자식 · 모르는 종류 이름 · 읽지 못한 값 · 범위 밖 숫자는 오류다(`readConfigXMLElement` 가 로그로 알린다).
        if ( ConfigManager::readConfigXMLElement( this, *StaticType(), xml, label.c_str() ) == false || validateFile( sourceLabel ) == false )
        {
            *this = SequenceAsset{};
            return false;
        }
        // 커브는 시각 순이어야 한다(`FloatCurve`). 손으로 고친 파일도 같은 순서 · 자동 접선이 되게 다시 센다.
        for ( SequenceKeyTrack& track : _listTrack )
        {
            for ( FloatCurve& channel : track._listChannel )
            {
                channel.sortAndComputeAutoTangents();
            }
        }
        return true;
    }

    bool SequenceAsset::validateFile( string_view sourceLabel ) const
    {
        // 프레임 번호는 `kSequenceFrameLimit` 안이어야 아래의 `+ 1` 과 재생 · 타임라인 쪽의 뺄셈들이 넘치지 않는다.
        bool bValid = isFrameWithinLimit( _frameMin, sourceLabel, "_frameMin" );
        bValid      = isFrameWithinLimit( _frameMax, sourceLabel, "_frameMax" ) && bValid;
        bValid      = isFrameWithinLimit( _cameraPrerollFrames, sourceLabel, "_cameraPrerollFrames" ) && bValid;
        if ( _frameMax <= _frameMin )
        {
            SW_LOG_ERROR( "Sequence '%#': _frameMax %# must be after _frameMin %#", sourceLabel, _frameMax, _frameMin );
            bValid = false;
        }
        for ( const SequenceTrackItem& item : _listItem )
        {
            bValid = isFrameWithinLimit( item._start, sourceLabel, "_start" ) && bValid;
            bValid = isFrameWithinLimit( item._end, sourceLabel, "_end" ) && bValid;
            bValid = isFrameWithinLimit( item._prerollFrames, sourceLabel, "_prerollFrames" ) && bValid;
            if ( item._prerollFrames < SequenceTrackItem::kUseSequencePreroll )
            {
                SW_LOG_ERROR( "Sequence '%#': item '%#' _prerollFrames %# must be 0 or more (-1: the sequence value)", sourceLabel, item._name, item._prerollFrames );
                bValid = false;
            }
            const SequenceItemKindInfo* pKindInfo = findItemKindInfo( item._kind );
            if ( pKindInfo == nullptr )
            {
                SW_LOG_ERROR( "Sequence '%#': item '%#' has no item kind", sourceLabel, item._name );
                bValid = false;
                continue;
            }
            // 스폰은 무엇을 어떤 이름으로 만들지가 필수다 — 빈 채로 두면 재생이 조용히 아무것도 만들지 않는다.
            const bool bSpawnNeedsFields = pKindInfo->_bSpawns && ( item._prefab.empty() || item._targetObject.empty() );
            if ( bSpawnNeedsFields )
            {
                SW_LOG_ERROR( "Sequence '%#': spawn item '%#' needs both _prefab and _targetObject", sourceLabel, item._name );
                bValid = false;
            }
            else if ( pKindInfo->_bNeedsPreset && ( item._preset.empty() || item._targetObject.empty() ) )
            {
                SW_LOG_ERROR( "Sequence '%#': item '%#' needs both a target object and a preset for its kind", sourceLabel, item._name );
                bValid = false;
            }
        }
        for ( const SequenceKeyTrack& track : _listTrack )
        {
            const uint32 channelCount = SequenceKeyTrack::getChannelCount( track._kind );
            if ( channelCount == 0 || track._listChannel.size() != channelCount )
            {
                SW_LOG_ERROR( "Sequence '%#': track '%#' needs %# channel(s) for its kind, has %#", sourceLabel, track._name, channelCount,
                              track._listChannel.size() );
                bValid = false;
            }
            for ( const FloatCurve& channel : track._listChannel )
            {
                for ( const FloatCurveKey& key : channel._listKey )
                {
                    bValid = isFrameWithinLimit( static_cast<float64>( key._time ), sourceLabel, "key _time" ) && bValid;
                }
            }
        }
        return bValid;
    }

    string SequenceAsset::toXML() const
    {
        return XMLSerializer::serialize( this, *StaticType() );
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

    const SequenceTrackItem* SequenceAsset::findCameraCut( int32 frame ) const
    {
        const SequenceTrackItem* pCut = nullptr;
        for ( const SequenceTrackItem& item : _listItem )
        {
            const SequenceItemKindInfo* pInfo            = findItemKindInfo( item._kind );
            const bool                  bNotCuttingFrame = pInfo == nullptr || pInfo->_bCutsCamera == false || frame < item._start || item._end < frame;
            if ( bNotCuttingFrame )
                continue;
            if ( pCut == nullptr || item._start >= pCut->_start )
                pCut = &item;
        }
        return pCut;
    }

    int32 SequenceAsset::getPrerollFrames( const SequenceTrackItem& item ) const
    {
        return item._prerollFrames >= 0 ? item._prerollFrames : _cameraPrerollFrames;
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
            case SequenceTrackKind::RigSlot:
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
            setCurveKey( _listChannel[channelIndex], frame, pArrValue[channelIndex] );
        }
    }

    bool SequenceKeyTrack::setChannelKey( uint32 channelIndex, float32 frame, float32 value )
    {
        if ( channelIndex >= _listChannel.size() )
            return false;
        setCurveKey( _listChannel[channelIndex], frame, value );
        return true;
    }

    bool SequenceKeyTrack::removeKeysAt( float32 frame )
    {
        bool bRemoved = false;
        for ( FloatCurve& channel : _listChannel )
        {
            const size_t removeCount = VectorUtil::removeIf( channel._listKey, [frame]( const FloatCurveKey& key )
            { return MathUtil::abs( key._time - frame ) < kSequenceKeyFrameTolerance; } );
            if ( removeCount > 0 )
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

    std::optional<float32> SequenceKeyTrack::evaluateChannel( uint32 channelIndex, float32 frame ) const
    {
        if ( channelIndex >= _listChannel.size() )
            return std::nullopt;
        return _listChannel[channelIndex].evaluate( frame );
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

namespace sw
{
    SW_ASSET_TYPE( SequenceAsset, SequenceAsset::kAssetType );
} // namespace sw
