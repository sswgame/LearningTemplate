#include "pch.h"

#include "Editor/Panels/Animation/Model/SequenceModel.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"

#include <algorithm>

namespace sw::editor
{
    namespace
    {
        struct SequenceModelInternal
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
                size_t                                   number = listEntry.size();
                StringBuilder<constant::buffer::kMax256> nameBuilder2;
                nameBuilder2.appendFormat( "%# %#", prefix, number );
                string name{ nameBuilder2.view() };
                while ( isNameTaken( listEntry, name ) )
                {
                    ++number;
                    StringBuilder<constant::buffer::kMax256> nameBuilder;
                    nameBuilder.appendFormat( "%# %#", prefix, number );
                    name = string( nameBuilder.view() );
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
                return SequenceModel::isRotationChannel( track, channelIndex ) ? MathUtil::kRadianToDegree : 1.0f;
            }

            /** @brief 트랙 종류의 새 이름 머리입니다. */
            [[nodiscard]] static const utf8* getTrackNamePrefix( SequenceTrackKind kind )
            {
                switch ( kind )
                {
                    case SequenceTrackKind::Transform:
                        return "Transform";
                    case SequenceTrackKind::Property:
                        return "Property";
                    case SequenceTrackKind::RigSlot:
                        return "Rig Slot";
                    case SequenceTrackKind::CameraLens:
                        return "Lens";
                    case SequenceTrackKind::Count:
                        return "Track";
                }
                return "Track";
            }

            /** @brief 두 키 프레임이 같은 키인지입니다(반 프레임 안). */
            [[nodiscard]] static bool isSameKeyFrame( float32 left, float32 right )
            {
                return MathUtil::abs( left - right ) < kSequenceKeyFrameTolerance;
            }
        };
    } // namespace

    SequenceModel::SequenceModel()
        : _asset{}
        , _itemEdit{}
        , _selectedItem{ -1 }
        , _selectedTrack{ -1 }
        , _curveTrack{ -1 }
        , _curveChannel{ 0 }
        , _selectedBinding{ kSequenceNoBinding }
        , _listSelectedKey{}
        , _bItemDraft{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    void SequenceModel::restoreAsset( const SequenceAsset& asset )
    {
        _asset         = asset;
        _selectedItem  = SequenceModelInternal::clampSelection( _selectedItem, _asset._listItem.size() );
        _selectedTrack = SequenceModelInternal::clampSelection( _selectedTrack, _asset._listTrack.size() );
        if ( _curveTrack >= static_cast<int32>( _asset._listTrack.size() ) )
            _curveTrack = -1;
        if ( _asset.findBinding( _selectedBinding ) == nullptr )
            _selectedBinding = _asset._listBinding.empty() ? kSequenceNoBinding : _asset._listBinding[0]._id;
        // 고른 키는 지금 문서의 프레임을 가리킨다 — 되돌린 문서에는 없을 수 있다.
        _listSelectedKey.clear();
    }

    void SequenceModel::resetSelection()
    {
        _selectedItem    = -1;
        _selectedTrack   = _asset._listTrack.empty() ? -1 : 0;
        _curveTrack      = -1;
        _selectedBinding = _asset._listBinding.empty() ? kSequenceNoBinding : _asset._listBinding[0]._id;
        _bItemDraft      = SW_FALSE;
        _listSelectedKey.clear();
    }

    uint32 SequenceModel::addBinding( string_view name )
    {
        if ( name.empty() )
            return kSequenceNoBinding;
        _selectedBinding = _asset.addBinding( name );
        return _selectedBinding;
    }

    bool SequenceModel::renameBinding( uint32 bindingID, string_view name )
    {
        if ( name.empty() )
            return false;
        for ( SequenceBinding& binding : _asset._listBinding )
        {
            if ( binding._id != bindingID )
                continue;
            binding._name = string{ name };
            return true;
        }
        return false;
    }

    bool SequenceModel::removeBinding( uint32 bindingID )
    {
        if ( _asset.findBinding( bindingID ) == nullptr || _asset.countBindingUses( bindingID ) > 0 )
            return false;
        (void)VectorUtil::removeIf( _asset._listBinding, [bindingID]( const SequenceBinding& binding )
        { return binding._id == bindingID; } );
        if ( _selectedBinding == bindingID )
            _selectedBinding = _asset._listBinding.empty() ? kSequenceNoBinding : _asset._listBinding[0]._id;
        return true;
    }

    bool SequenceModel::isItemAcceptable( const SequenceTrackItem& item ) const
    {
        if ( item.findFieldIssue() != SequenceItemFieldIssue::None )
            return false;
        return item._binding == kSequenceNoBinding || _asset.findBinding( item._binding ) != nullptr;
    }

    int32 SequenceModel::addItem( SequenceItemKind kind, string_view kindName )
    {
        _bItemDraft                       = SW_FALSE;
        const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( kind );
        if ( pInfo == nullptr )
            return -1;
        SequenceTrackItem item{};
        item._kind    = kind;
        item._start   = _asset._frameMin;
        item._end     = MathUtil::min( _asset._frameMin + SequenceModelInternal::kNewItemFrameLength, _asset._frameMax );
        item._color   = pInfo->_defaultColor;
        item._name    = SequenceModelInternal::makeNumberedName( _asset._listItem, kindName );
        item._binding = pInfo->allowsField( SequenceItemField::Binding ) ? _selectedBinding : kSequenceNoBinding;
        // 스폰은 바인딩 하나를 혼자 채운다 — 이미 스폰하는 바인딩이면 초안에서 다른 바인딩을 고른다.
        if ( pInfo->_bSpawns && _asset.findSpawnItem( item._binding ) != nullptr )
            item._binding = kSequenceNoBinding;
        if ( isItemAcceptable( item ) == false )
        {
            _itemEdit   = std::move( item );
            _bItemDraft = SW_TRUE;
            return -1;
        }
        _asset._listItem.push_back( std::move( item ) );
        return static_cast<int32>( _asset._listItem.size() ) - 1;
    }

    bool SequenceModel::isDraftingItem() const
    {
        return _bItemDraft == SW_TRUE;
    }

    int32 SequenceModel::commitItemDraft()
    {
        if ( isDraftingItem() == false || isItemAcceptable( _itemEdit ) == false )
            return -1;
        _asset._listItem.push_back( _itemEdit );
        _bItemDraft   = SW_FALSE;
        _selectedItem = static_cast<int32>( _asset._listItem.size() ) - 1;
        return _selectedItem;
    }

    void SequenceModel::cancelItemDraft()
    {
        _bItemDraft = SW_FALSE;
    }

    bool SequenceModel::hasEditedItem() const
    {
        return isDraftingItem() || hasSelectedItem();
    }

    void SequenceModel::syncItemEdit()
    {
        if ( isDraftingItem() == false && hasSelectedItem() )
            _itemEdit = _asset._listItem[static_cast<size_t>( _selectedItem )];
    }

    bool SequenceModel::commitItemEdit()
    {
        if ( isDraftingItem() || hasSelectedItem() == false )
            return false;
        SequenceTrackItem& item      = _asset._listItem[static_cast<size_t>( _selectedItem )];
        SequenceTrackItem  candidate = item;
        candidate._name              = _itemEdit._name;
        candidate._binding           = _itemEdit._binding;
        // 글 칸은 종류 표의 칸 목록 그대로 옮긴다(칸을 하나 더해도 여기를 고치지 않는다).
        for ( uint32 fieldIndex = 0; fieldIndex < static_cast<uint32>( SequenceItemField::Count ); ++fieldIndex )
        {
            const SequenceItemField field = static_cast<SequenceItemField>( fieldIndex );
            string*                 pTo   = candidate.accessTextField( field );
            string*                 pFrom = _itemEdit.accessTextField( field );
            const bool              bText = pTo != nullptr && pFrom != nullptr;
            if ( bText )
                *pTo = *pFrom;
        }
        const SequenceTrackItem* pSpawnOwner = _asset.findSpawnItem( candidate._binding );
        const bool               bSpawnClash = pSpawnOwner != nullptr && pSpawnOwner != &item && SequenceAsset::findItemKindInfo( candidate._kind )->_bSpawns;
        if ( isItemAcceptable( candidate ) == false || bSpawnClash )
        {
            _itemEdit = item;
            return false;
        }
        item = std::move( candidate );
        return true;
    }

    bool SequenceModel::removeItem( int32 itemIndex )
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

    int32 SequenceModel::duplicateItem( int32 itemIndex )
    {
        if ( itemIndex < 0 || itemIndex >= static_cast<int32>( _asset._listItem.size() ) )
            return -1;
        SequenceTrackItem           copy  = _asset._listItem[static_cast<size_t>( itemIndex )];
        const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( copy._kind );
        if ( pInfo != nullptr && pInfo->_bSpawns )
            return -1; // 두 스폰이 한 바인딩을 채우면 파일이 읽히지 않는다
        copy._name += " Copy";
        if ( SequenceModelInternal::isNameTaken( _asset._listItem, copy._name ) )
            copy._name = SequenceModelInternal::makeNumberedName( _asset._listItem, copy._name );
        _asset._listItem.push_back( std::move( copy ) );
        return static_cast<int32>( _asset._listItem.size() ) - 1;
    }

    void SequenceModel::collectCutLane( vector<int32>& outListItemIndex ) const
    {
        outListItemIndex.clear();
        for ( int32 itemIndex = 0; itemIndex < static_cast<int32>( _asset._listItem.size() ); ++itemIndex )
        {
            const SequenceItemKindInfo* pInfo = SequenceAsset::findItemKindInfo( _asset._listItem[static_cast<size_t>( itemIndex )]._kind );
            if ( pInfo != nullptr && pInfo->_bCutsCamera )
                outListItemIndex.push_back( itemIndex );
        }
        std::stable_sort( outListItemIndex.begin(), outListItemIndex.end(), [this]( int32 left, int32 right )
        { return _asset._listItem[static_cast<size_t>( left )]._start < _asset._listItem[static_cast<size_t>( right )]._start; } );
    }

    int32 SequenceModel::addCameraCut( int32 frame )
    {
        const bool bInvalid = _asset.findBinding( _selectedBinding ) == nullptr || frame < _asset._frameMin || frame > _asset._frameMax;
        if ( bInvalid )
            return -1;
        vector<int32> listCut;
        collectCutLane( listCut );
        int32 end = _asset._frameMax;
        for ( const int32 cutIndex : listCut )
        {
            SequenceTrackItem& cut = _asset._listItem[static_cast<size_t>( cutIndex )];
            if ( cut._start > frame )
            {
                end = MathUtil::min( end, cut._start - 1 );
                continue;
            }
            // 이 프레임을 덮던 컷은 여기 앞에서 끝난다(컷이 시작 프레임에 있으면 새 컷이 그 자리를 갖고 앞 컷은 그대로 — 둘이 같은 프레임에서 시작할 수 없다).
            if ( cut._start < frame && frame <= cut._end )
                cut._end = frame - 1;
            else if ( cut._start == frame )
                return -1;
        }
        SequenceTrackItem item{};
        item._kind    = SequenceItemKind::CameraCut;
        item._binding = _selectedBinding;
        item._start   = frame;
        item._end     = MathUtil::max( end, frame );
        item._color   = SequenceAsset::findItemKindInfo( SequenceItemKind::CameraCut )->_defaultColor;
        item._name    = SequenceModelInternal::makeNumberedName( _asset._listItem, "Cut" );
        _asset._listItem.push_back( std::move( item ) );
        _selectedItem = static_cast<int32>( _asset._listItem.size() ) - 1;
        return _selectedItem;
    }

    bool SequenceModel::addTrack( SequenceTrackKind kind )
    {
        if ( _asset.findBinding( _selectedBinding ) == nullptr || SequenceKeyTrack::getChannelCount( kind ) == 0 )
            return false;
        SequenceKeyTrack track{};
        track._kind    = kind;
        track._binding = _selectedBinding;
        track._name    = SequenceModelInternal::makeNumberedName( _asset._listTrack, SequenceModelInternal::getTrackNamePrefix( kind ) );
        track.fitChannelsToKind();
        _asset._listTrack.push_back( std::move( track ) );
        _selectedTrack = static_cast<int32>( _asset._listTrack.size() ) - 1;
        return true;
    }

    bool SequenceModel::removeSelectedTrack()
    {
        if ( findSelectedTrack() == nullptr )
            return false;
        _asset._listTrack.erase( _asset._listTrack.begin() + _selectedTrack );
        _selectedTrack = -1;
        _curveTrack    = -1;
        _listSelectedKey.clear(); // 트랙 번호가 당겨졌다
        return true;
    }

    SequenceKeyTrack* SequenceModel::findSelectedTrack()
    {
        const bool bValid = 0 <= _selectedTrack && _selectedTrack < static_cast<int32>( _asset._listTrack.size() );
        return bValid ? &_asset._listTrack[static_cast<size_t>( _selectedTrack )] : nullptr;
    }

    const SequenceKeyTrack* SequenceModel::findSelectedTrack() const
    {
        const bool bValid = 0 <= _selectedTrack && _selectedTrack < static_cast<int32>( _asset._listTrack.size() );
        return bValid ? &_asset._listTrack[static_cast<size_t>( _selectedTrack )] : nullptr;
    }

    SequenceKeyTrack* SequenceModel::findCurveTrack()
    {
        const bool bValid = 0 <= _curveTrack && _curveTrack < static_cast<int32>( _asset._listTrack.size() );
        return bValid ? &_asset._listTrack[static_cast<size_t>( _curveTrack )] : nullptr;
    }

    FloatCurve* SequenceModel::findCurveChannel()
    {
        SequenceKeyTrack* pTrack = findCurveTrack();
        if ( pTrack == nullptr || _curveChannel < 0 || _curveChannel >= static_cast<int32>( pTrack->_listChannel.size() ) )
            return nullptr;
        return &pTrack->_listChannel[static_cast<size_t>( _curveChannel )];
    }

    void SequenceModel::selectKey( int32 trackIndex, float32 frame, bool bToggle )
    {
        auto itFound = std::find_if( _listSelectedKey.begin(), _listSelectedKey.end(), [trackIndex, frame]( const SequenceKeyRef& key )
        { return key._track == trackIndex && SequenceModelInternal::isSameKeyFrame( key._frame, frame ); } );
        if ( bToggle )
        {
            if ( itFound != _listSelectedKey.end() )
                _listSelectedKey.erase( itFound );
            else
                _listSelectedKey.push_back( SequenceKeyRef{ trackIndex, frame } );
            return;
        }
        // 고른 키를 다시 누르면 다중 선택을 지킨다(끌기 시작) — 고르지 않은 키를 누르면 그 키만 고른다.
        if ( itFound != _listSelectedKey.end() )
            return;
        _listSelectedKey.clear();
        _listSelectedKey.push_back( SequenceKeyRef{ trackIndex, frame } );
    }

    bool SequenceModel::isKeySelected( int32 trackIndex, float32 frame ) const
    {
        for ( const SequenceKeyRef& key : _listSelectedKey )
        {
            if ( key._track == trackIndex && SequenceModelInternal::isSameKeyFrame( key._frame, frame ) )
                return true;
        }
        return false;
    }

    bool SequenceModel::moveSelectedKeys( int32 frameDelta )
    {
        if ( frameDelta == 0 || _listSelectedKey.empty() )
            return false;
        const float32 delta = static_cast<float32>( frameDelta );
        for ( const SequenceKeyRef& key : _listSelectedKey )
        {
            const float32 target   = MathUtil::round( key._frame + delta );
            const bool    bOutside = key._track < 0 || key._track >= static_cast<int32>( _asset._listTrack.size() ) || target < static_cast<float32>( _asset._frameMin ) ||
                                  target > static_cast<float32>( _asset._frameMax );
            if ( bOutside )
                return false;
        }
        // 옮기는 쪽 끝부터 — 앞의 키가 아직 옮기지 않은 고른 키를 덮지 않는다.
        std::sort( _listSelectedKey.begin(), _listSelectedKey.end(), [frameDelta]( const SequenceKeyRef& left, const SequenceKeyRef& right )
        { return frameDelta > 0 ? left._frame > right._frame : left._frame < right._frame; } );
        bool bMoved = false;
        for ( SequenceKeyRef& key : _listSelectedKey )
        {
            const float32 target = MathUtil::round( key._frame + delta );
            bMoved               = _asset._listTrack[static_cast<size_t>( key._track )].moveKeysAt( key._frame, target ) || bMoved;
            key._frame           = target;
        }
        return bMoved;
    }

    uint32 SequenceModel::removeSelectedKeys()
    {
        uint32 removedCount = 0;
        for ( const SequenceKeyRef& key : _listSelectedKey )
        {
            const bool bValidTrack = 0 <= key._track && key._track < static_cast<int32>( _asset._listTrack.size() );
            if ( bValidTrack && _asset._listTrack[static_cast<size_t>( key._track )].removeKeysAt( key._frame ) )
                ++removedCount;
        }
        _listSelectedKey.clear();
        return removedCount;
    }

    optional<int32> SequenceModel::findPreviousKeyFrame( const vector<float32>& listKeyFrame, int32 frame ) const
    {
        const float32 limit = static_cast<float32>( frame ) - kSequenceKeyFrameTolerance;
        for ( auto it = listKeyFrame.rbegin(); it != listKeyFrame.rend(); ++it )
        {
            if ( *it >= limit )
                continue;
            const optional<int32> timelineFrame = findTimelineFrame( *it );
            if ( timelineFrame.has_value() )
                return timelineFrame;
        }
        return nullopt;
    }

    optional<int32> SequenceModel::findNextKeyFrame( const vector<float32>& listKeyFrame, int32 frame ) const
    {
        const float32 limit = static_cast<float32>( frame ) + kSequenceKeyFrameTolerance;
        for ( const float32 keyFrame : listKeyFrame )
        {
            if ( keyFrame <= limit )
                continue;
            const optional<int32> timelineFrame = findTimelineFrame( keyFrame );
            if ( timelineFrame.has_value() )
                return timelineFrame;
        }
        return nullopt;
    }

    int32 SequenceModel::findFrameAtRatio( float32 ratio ) const
    {
        const float32 clamped = MathUtil::clamp( ratio, 0.0f, 1.0f );
        return _asset._frameMin + static_cast<int32>( MathUtil::round( clamped * static_cast<float32>( getFrameSpan() ) ) );
    }

    float32 SequenceModel::computeRatioAtFrame( float32 frame ) const
    {
        return ( frame - static_cast<float32>( _asset._frameMin ) ) / static_cast<float32>( getFrameSpan() );
    }

    int32 SequenceModel::computeFrameDelta( float32 pixelDelta, float32 stripWidth ) const
    {
        if ( stripWidth <= 0.0f )
            return 0;
        return static_cast<int32>( MathUtil::round( pixelDelta / stripWidth * static_cast<float32>( getFrameSpan() ) ) );
    }

    bool SequenceModel::isRotationChannel( const SequenceKeyTrack& track, uint32 channelIndex )
    {
        const bool bTransformRotation = track._kind == SequenceTrackKind::Transform && channelIndex >= 3 && channelIndex < 6;
        const bool bLensFieldOfView   = track._kind == SequenceTrackKind::CameraLens && channelIndex == 0;
        return bTransformRotation || bLensFieldOfView;
    }

    float32 SequenceModel::computeChannelDisplayValue( const SequenceKeyTrack& track, uint32 channelIndex, float32 frame, float32 currentValue )
    {
        const optional<float32> keyedValue = track.evaluateChannel( channelIndex, frame );
        return ( keyedValue.has_value() ? *keyedValue : currentValue ) * SequenceModelInternal::getChannelDisplayScale( track, channelIndex );
    }

    bool SequenceModel::setChannelDisplayValue( SequenceKeyTrack& track, uint32 channelIndex, float32 frame, float32 displayValue )
    {
        return track.setChannelKey( channelIndex, frame, displayValue / SequenceModelInternal::getChannelDisplayScale( track, channelIndex ) );
    }

    optional<int32> SequenceModel::findTimelineFrame( float32 keyFrame ) const
    {
        // 비교는 실수로 한다 — 파일 · 커브 편집기의 키는 int32 밖일 수 있어 먼저 바꾸면 값이 깨진다.
        const float32 rounded      = MathUtil::round( keyFrame );
        const bool    bInsideRange = static_cast<float32>( _asset._frameMin ) <= rounded && rounded <= static_cast<float32>( _asset._frameMax );
        if ( bInsideRange == false )
            return nullopt;
        return static_cast<int32>( rounded );
    }

    int32 SequenceModel::getFrameSpan() const
    {
        return MathUtil::max( _asset._frameMax - _asset._frameMin, 1 );
    }

    bool SequenceModel::hasSelectedItem() const
    {
        return 0 <= _selectedItem && _selectedItem < static_cast<int32>( _asset._listItem.size() );
    }
} // namespace sw::editor
