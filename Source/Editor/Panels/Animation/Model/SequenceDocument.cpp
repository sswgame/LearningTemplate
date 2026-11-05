#include "pch.h"

#include "Editor/Panels/Animation/Model/SequenceDocument.h"

#include "Core/Container/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Character/PoseModifier/PoseModifierComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/ReflectValue.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Sequencer/SequenceTimelineUtil.h"

namespace sw::editor
{
    namespace
    {
        struct SequenceDocumentInternal
        {
            /** @brief 새 항목의 길이(프레임)입니다. 타임라인이 더 짧으면 타임라인 끝까지입니다. */
            static constexpr int32 kNewItemFrameLength = 10;

            /** @brief 목록에 @p name 이 있으면 true 입니다(항목 · 트랙은 둘 다 `_name` 을 듭니다). */
            template <typename TEntry>
            [[nodiscard]] static bool isNameTaken( const vector<TEntry>& listEntry, string_view name )
            {
                return VectorUtil::containsIf( listEntry, [name]( const TEntry& entry )
                { return name == entry._name; } );
            }

            /** @brief 목록에 없는 `<prefix> <번호>` 입니다. 번호는 목록 길이에서 시작합니다 — 지운 뒤 더해도 남은 것과 겹치지 않습니다. */
            template <typename TEntry>
            [[nodiscard]] static string makeNumberedName( const vector<TEntry>& listEntry, string_view prefix )
            {
                size_t number = listEntry.size();
                string name   = formatToString( "%# %#", prefix, number );
                while ( isNameTaken( listEntry, name ) )
                {
                    ++number;
                    name = formatToString( "%# %#", prefix, number );
                }
                return name;
            }

            /** @brief 고른 번호가 줄어든 목록을 넘으면 끝 항목으로 당깁니다(비면 -1). */
            [[nodiscard]] static int32 clampSelection( int32 selected, size_t count )
            {
                return selected >= static_cast<int32>( count ) ? static_cast<int32>( count ) - 1 : selected;
            }

            /** @brief 값 칸의 표시 배율입니다(회전은 라디안 → 도). */
            [[nodiscard]] static float32 getChannelDisplayScale( const SequenceKeyTrack& track, uint32 channelIndex )
            {
                return SequenceDocument::isRotationChannel( track, channelIndex ) ? MathUtil::kRadianToDegree : 1.0f;
            }
        };
    } // namespace

    SequenceDocument::SequenceDocument()
        : _asset{}
        , _selectedItem{ -1 }
        , _selectedTrack{ -1 }
        , _curveTrack{ -1 }
        , _curveChannel{ 0 }
    {
    }

    void SequenceDocument::restoreAsset( const SequenceAsset& asset )
    {
        _asset         = asset;
        _selectedItem  = SequenceDocumentInternal::clampSelection( _selectedItem, _asset._listItem.size() );
        _selectedTrack = SequenceDocumentInternal::clampSelection( _selectedTrack, _asset._listTrack.size() );
        if ( _curveTrack >= static_cast<int32>( _asset._listTrack.size() ) )
            _curveTrack = -1;
    }

    void SequenceDocument::resetSelection()
    {
        _selectedItem  = -1;
        _selectedTrack = _asset._listTrack.empty() ? -1 : 0;
        _curveTrack    = -1;
    }

    bool SequenceDocument::canReadBack() const
    {
        SequenceAsset readBack;
        return readBack.parseXML( _asset.toXML() );
    }

    bool SequenceDocument::canAddItemKind( SequenceItemKind kind )
    {
        const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( kind );
        return pInfo != nullptr && pInfo->_bSpawns == false && pInfo->_bNeedsPreset == false && pInfo->_bPlaysScenario == false;
    }

    uint32 SequenceDocument::getAddableItemKindCount()
    {
        uint32 count = 0;
        for ( const SequenceItemKindInfo& info : kArrSequenceItemKindInfo )
        {
            if ( canAddItemKind( info._kind ) )
                ++count;
        }
        return count;
    }

    SequenceItemKind SequenceDocument::getAddableItemKind( uint32 addableIndex )
    {
        uint32 remaining = addableIndex;
        for ( const SequenceItemKindInfo& info : kArrSequenceItemKindInfo )
        {
            if ( canAddItemKind( info._kind ) == false )
                continue;
            if ( remaining == 0 )
                return info._kind;
            --remaining;
        }
        return SequenceItemKind::Count;
    }

    int32 SequenceDocument::addItem( SequenceItemKind kind, string_view kindName )
    {
        if ( canAddItemKind( kind ) == false )
            return -1;
        SequenceTrackItem item{};
        item._kind  = kind;
        item._start = _asset._frameMin;
        item._end   = MathUtil::min( _asset._frameMin + SequenceDocumentInternal::kNewItemFrameLength, _asset._frameMax );
        item._color = SequenceAsset::findItemKindInfo( kind )->_defaultColor;
        item._name  = SequenceDocumentInternal::makeNumberedName( _asset._listItem, kindName );
        _asset._listItem.push_back( std::move( item ) );
        return static_cast<int32>( _asset._listItem.size() ) - 1;
    }

    bool SequenceDocument::removeItem( int32 itemIndex )
    {
        if ( itemIndex < 0 || itemIndex >= static_cast<int32>( _asset._listItem.size() ) )
            return false;
        _asset._listItem.erase( _asset._listItem.begin() + itemIndex );
        if ( _selectedItem == itemIndex )
            _selectedItem = -1;
        else if ( _selectedItem > itemIndex )
            --_selectedItem;
        return true;
    }

    int32 SequenceDocument::duplicateItem( int32 itemIndex )
    {
        if ( itemIndex < 0 || itemIndex >= static_cast<int32>( _asset._listItem.size() ) )
            return -1;
        SequenceTrackItem copy = _asset._listItem[static_cast<size_t>( itemIndex )];
        copy._name += " Copy";
        if ( SequenceDocumentInternal::isNameTaken( _asset._listItem, copy._name ) )
            copy._name = SequenceDocumentInternal::makeNumberedName( _asset._listItem, copy._name );
        _asset._listItem.push_back( std::move( copy ) );
        return static_cast<int32>( _asset._listItem.size() ) - 1;
    }

    void SequenceDocument::addTrack( SequenceTrackKind kind, string_view targetObject )
    {
        SequenceKeyTrack track{};
        track._kind         = kind;
        track._targetObject = string{ targetObject };
        const utf8* pPrefix = "Property";
        if ( kind == SequenceTrackKind::Transform )
            pPrefix = "Transform";
        else if ( kind == SequenceTrackKind::RigSlot )
            pPrefix = "Rig Slot";
        track._name = SequenceDocumentInternal::makeNumberedName( _asset._listTrack, pPrefix );
        track.fitChannelsToKind();
        _asset._listTrack.push_back( std::move( track ) );
        _selectedTrack = static_cast<int32>( _asset._listTrack.size() ) - 1;
    }

    bool SequenceDocument::removeSelectedTrack()
    {
        if ( findSelectedTrack() == nullptr )
            return false;
        _asset._listTrack.erase( _asset._listTrack.begin() + _selectedTrack );
        _selectedTrack = -1;
        _curveTrack    = -1;
        return true;
    }

    SequenceKeyTrack* SequenceDocument::findSelectedTrack()
    {
        const bool bValid = 0 <= _selectedTrack && _selectedTrack < static_cast<int32>( _asset._listTrack.size() );
        return bValid ? &_asset._listTrack[static_cast<size_t>( _selectedTrack )] : nullptr;
    }

    const SequenceKeyTrack* SequenceDocument::findSelectedTrack() const
    {
        const bool bValid = 0 <= _selectedTrack && _selectedTrack < static_cast<int32>( _asset._listTrack.size() );
        return bValid ? &_asset._listTrack[static_cast<size_t>( _selectedTrack )] : nullptr;
    }

    SequenceKeyTrack* SequenceDocument::findCurveTrack()
    {
        const bool bValid = 0 <= _curveTrack && _curveTrack < static_cast<int32>( _asset._listTrack.size() );
        return bValid ? &_asset._listTrack[static_cast<size_t>( _curveTrack )] : nullptr;
    }

    FloatCurve* SequenceDocument::findCurveChannel()
    {
        SequenceKeyTrack* pTrack = findCurveTrack();
        if ( pTrack == nullptr || _curveChannel < 0 || _curveChannel >= static_cast<int32>( pTrack->_listChannel.size() ) )
            return nullptr;
        return &pTrack->_listChannel[static_cast<size_t>( _curveChannel )];
    }

    std::optional<int32> SequenceDocument::findPreviousKeyFrame( const vector<float32>& listKeyFrame, int32 frame ) const
    {
        const float32 limit = static_cast<float32>( frame ) - kSequenceKeyFrameTolerance;
        for ( auto it = listKeyFrame.rbegin(); it != listKeyFrame.rend(); ++it )
        {
            if ( *it >= limit )
                continue;
            const std::optional<int32> timelineFrame = findTimelineFrame( *it );
            if ( timelineFrame.has_value() )
                return timelineFrame;
        }
        return std::nullopt;
    }

    std::optional<int32> SequenceDocument::findNextKeyFrame( const vector<float32>& listKeyFrame, int32 frame ) const
    {
        const float32 limit = static_cast<float32>( frame ) + kSequenceKeyFrameTolerance;
        for ( const float32 keyFrame : listKeyFrame )
        {
            if ( keyFrame <= limit )
                continue;
            const std::optional<int32> timelineFrame = findTimelineFrame( keyFrame );
            if ( timelineFrame.has_value() )
                return timelineFrame;
        }
        return std::nullopt;
    }

    int32 SequenceDocument::findFrameAtRatio( float32 ratio ) const
    {
        const float32 clamped = MathUtil::clamp( ratio, 0.0f, 1.0f );
        return _asset._frameMin + static_cast<int32>( MathUtil::round( clamped * static_cast<float32>( getFrameSpan() ) ) );
    }

    float32 SequenceDocument::computeRatioAtFrame( float32 frame ) const
    {
        return ( frame - static_cast<float32>( _asset._frameMin ) ) / static_cast<float32>( getFrameSpan() );
    }

    bool SequenceDocument::isRotationChannel( const SequenceKeyTrack& track, uint32 channelIndex )
    {
        return track._kind == SequenceTrackKind::Transform && channelIndex >= 3 && channelIndex < 6;
    }

    float32 SequenceDocument::computeChannelDisplayValue( const SequenceKeyTrack& track, uint32 channelIndex, float32 frame, float32 currentValue )
    {
        const std::optional<float32> keyedValue = track.evaluateChannel( channelIndex, frame );
        return ( keyedValue.has_value() ? *keyedValue : currentValue ) * SequenceDocumentInternal::getChannelDisplayScale( track, channelIndex );
    }

    bool SequenceDocument::setChannelDisplayValue( SequenceKeyTrack& track, uint32 channelIndex, float32 frame, float32 displayValue )
    {
        return track.setChannelKey( channelIndex, frame, displayValue / SequenceDocumentInternal::getChannelDisplayScale( track, channelIndex ) );
    }

    bool SequenceDocument::readTrackValues( GameObjectManager* pManager, const SequenceKeyTrack& track, float32* pOutArrValue, uint32 valueCapacity )
    {
        GameObject* pTarget               = SequenceTimelineUtil::findTarget( pManager, track._targetObject );
        const bool  bInvalidSampleRequest = pTarget == nullptr || pOutArrValue == nullptr || valueCapacity < SequenceKeyTrack::getChannelCount( track._kind );
        if ( bInvalidSampleRequest )
            return false;
        if ( track._kind == SequenceTrackKind::Transform )
        {
            const SceneComponent* pScene = pTarget->getPrimarySceneComponent();
            if ( pScene == nullptr )
                return false;
            const float3 arrValue[3] = { pScene->getLocalPosition(), pScene->getLocalRotation(), pScene->getLocalScale() };
            for ( uint32 groupIndex = 0; groupIndex < 3; ++groupIndex )
            {
                pOutArrValue[groupIndex * 3 + 0] = arrValue[groupIndex]._x;
                pOutArrValue[groupIndex * 3 + 1] = arrValue[groupIndex]._y;
                pOutArrValue[groupIndex * 3 + 2] = arrValue[groupIndex]._z;
            }
            return true;
        }
        if ( track._kind == SequenceTrackKind::RigSlot )
        {
            const PoseModifierComponent* pModifier = pTarget->getComponent<PoseModifierComponent>();
            if ( pModifier == nullptr )
                return false;
            pOutArrValue[0] = pModifier->getSlotWeight( hashed_string( track._propertyPath.c_str() ) );
            return true;
        }
        if ( track._kind != SequenceTrackKind::Property )
            return false;
        Component*          pComponent{ nullptr };
        const PropertyInfo* pProperty{ nullptr };
        if ( SequenceTimelineUtil::findTrackProperty( pTarget, track, pComponent, pProperty ) == false )
            return false;
        float64 number{ 0.0 };
        if ( ReflectValueUtil::readNumber( ReflectValueUtil::findBuiltinIndex( pProperty->_typeName ), pProperty->getRawPtr( static_cast<const void*>( pComponent ) ), number ) == false )
            return false;
        pOutArrValue[0] = static_cast<float32>( number );
        return true;
    }

    std::optional<int32> SequenceDocument::findTimelineFrame( float32 keyFrame ) const
    {
        // 비교는 실수로 한다 — 파일 · 커브 편집기의 키는 int32 밖일 수 있어 먼저 바꾸면 값이 깨진다.
        const float32 rounded      = MathUtil::round( keyFrame );
        const bool    bInsideRange = static_cast<float32>( _asset._frameMin ) <= rounded && rounded <= static_cast<float32>( _asset._frameMax );
        if ( bInsideRange == false )
            return std::nullopt;
        return static_cast<int32>( rounded );
    }

    int32 SequenceDocument::getFrameSpan() const
    {
        return MathUtil::max( _asset._frameMax - _asset._frameMin, 1 );
    }
} // namespace sw::editor
