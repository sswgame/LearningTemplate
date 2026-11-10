#include "pch.h"

#include "Engine/Sequencer/SequenceAsset.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_set.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Config/ConfigManager.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Serialization/Format/XMLSerializer.h"
#include "Engine/Serialization/Inherit/ResourceComposer.h"

#include <algorithm>

namespace sw
{
    namespace
    {
        constexpr uint16 kFieldBinding  = makeSequenceFieldBit( SequenceItemField::Binding );
        constexpr uint16 kFieldPrefab   = makeSequenceFieldBit( SequenceItemField::Prefab );
        constexpr uint16 kFieldPreset   = makeSequenceFieldBit( SequenceItemField::Preset );
        constexpr uint16 kFieldScenario = makeSequenceFieldBit( SequenceItemField::Scenario );
        constexpr uint16 kFieldClip     = makeSequenceFieldBit( SequenceItemField::Clip );
        constexpr uint16 kFieldSound    = makeSequenceFieldBit( SequenceItemField::Sound );
        constexpr uint16 kFieldText     = makeSequenceFieldBit( SequenceItemField::Text );

        /**
         * @brief 항목 종류 표입니다. **종류마다 한 줄이고 순서는 `SequenceItemKind` 값 순서입니다.** 종류의 칸 · 재생 동작 · 색은 이 줄이 전부다.
         * @details 칸: 필수(`_requiredFields`)는 비면 읽기 오류, 허용(`_allowedFields`) 밖의 칸이 차 있어도 읽기 오류다 — 다른 종류의 칸이 남은 파일은 무엇을 뜻하는지 모른다.
         *          이벤트 · 시나리오는 대상이 없어도 된다(알림만 한다). 소리는 대상이 없으면 화면 소리다. 자막은 대상 바인딩의 표시 이름이 화자다(없으면 화자 없이).
         *          구간 처리기가 있는 종류는 재생 · 미리보기가 들어가기 · 머물기 · 나가기를 그 처리기로 돈다(`SequenceTimelineUtil::applySpans`) — 새 구간 종류는 처리기 하나와 이 줄 하나다.
         */
        constexpr SequenceItemKindInfo kArrSequenceItemKindInfo[] = {
            // 종류, 색, 필수 칸, 허용 칸, 구간 처리기, 대상 켬, 지날 때 알림, 컷, 흔들림, 스폰, 시나리오
            {       SequenceItemKind::Clip, 0xFF80AA80u,                kFieldBinding,                  kFieldBinding,                        nullptr,  true, false, false, false, false, false},
            {      SequenceItemKind::Event, 0xFF8080AAu,                           0u,                  kFieldBinding,                        nullptr, false,  true, false, false, false, false},
            {  SequenceItemKind::CameraCut, 0xFFAA80AAu,                kFieldBinding,   kFieldBinding | kFieldPreset,                        nullptr, false, false,  true, false, false, false},
            {SequenceItemKind::CameraShake, 0xFF80A0C0u,                kFieldBinding,                  kFieldBinding,                        nullptr, false, false, false,  true, false, false},
            {      SequenceItemKind::Spawn, 0xFF80C0A0u, kFieldBinding | kFieldPrefab,   kFieldBinding | kFieldPrefab,                        nullptr, false, false, false, false,  true, false},
            { SequenceItemKind::Appearance, 0xFFC0A070u, kFieldBinding | kFieldPreset,   kFieldBinding | kFieldPreset,                        nullptr, false,  true, false, false, false, false},
            {   SequenceItemKind::Scenario, 0xFF70A0C0u,               kFieldScenario, kFieldBinding | kFieldScenario,                        nullptr, false,  true, false, false, false,  true},
            {  SequenceItemKind::Animation, 0xFFA0A060u,   kFieldBinding | kFieldClip,     kFieldBinding | kFieldClip, &kSequenceAnimationSpanHandler, false, false, false, false, false, false},
            {      SequenceItemKind::Audio, 0xFF6080C0u,                  kFieldSound,    kFieldBinding | kFieldSound,     &kSequenceAudioSpanHandler, false, false, false, false, false, false},
            {   SequenceItemKind::Subtitle, 0xFFB0B0B0u,                   kFieldText,     kFieldBinding | kFieldText,  &kSequenceSubtitleSpanHandler, false, false, false, false, false, false},
        };

        static_assert( SW_COUNT_OF( kArrSequenceItemKindInfo ) == static_cast<size_t>( SequenceItemKind::Count ), "SequenceItemKind 를 늘렸으면 kArrSequenceItemKindInfo 에도 줄을 더할 것" );

        /** @brief 종류 표가 값 순서이고 필수 칸이 허용 칸 안인지 확인합니다. `findItemKindInfo` 가 값으로 바로 찾습니다. */
        constexpr bool isKindTableOrdered()
        {
            for ( size_t kindIndex = 0; kindIndex < SW_COUNT_OF( kArrSequenceItemKindInfo ); ++kindIndex )
            {
                const SequenceItemKindInfo& info = kArrSequenceItemKindInfo[kindIndex];
                if ( info._kind != static_cast<SequenceItemKind>( kindIndex ) || ( info._requiredFields & ~info._allowedFields ) != 0 )
                    return false;
            }
            return true;
        }

        static_assert( isKindTableOrdered(), "kArrSequenceItemKindInfo must be ordered by SequenceItemKind and its required fields must be allowed" );

        /** @brief 키 트랙 종류 표입니다. **종류마다 한 줄이고 순서는 `SequenceTrackKind` 값 순서입니다**(언리얼 Sequencer 의 트랙 에디터 등록 한 줄에 해당). */
        constexpr SequenceTrackKindInfo kArrSequenceTrackKindInfo[] = {
            // 종류, 채널 수, 새 이름 머리, 대상 경로를 쓴다
            { SequenceTrackKind::Transform, kSequenceTransformChannelCount, "Transform", false},
            {  SequenceTrackKind::Property,                              1,  "Property",  true},
            {   SequenceTrackKind::RigSlot,                              1,  "Rig Slot",  true},
            {SequenceTrackKind::CameraLens,      kSequenceLensChannelCount,      "Lens", false},
        };

        static_assert( SW_COUNT_OF( kArrSequenceTrackKindInfo ) == static_cast<size_t>( SequenceTrackKind::Count ), "SequenceTrackKind 를 늘렸으면 kArrSequenceTrackKindInfo 에도 줄을 더할 것" );

        /** @brief 트랙 종류 표가 값 순서이고 채널 수가 값 버퍼 안인지 확인합니다. */
        constexpr bool isTrackKindTableOrdered()
        {
            for ( size_t kindIndex = 0; kindIndex < SW_COUNT_OF( kArrSequenceTrackKindInfo ); ++kindIndex )
            {
                const SequenceTrackKindInfo& info = kArrSequenceTrackKindInfo[kindIndex];
                if ( info._kind != static_cast<SequenceTrackKind>( kindIndex ) || info._channelCount == 0 || info._channelCount > kSequenceMaxChannelCount )
                    return false;
            }
            return true;
        }

        static_assert( isTrackKindTableOrdered(), "kArrSequenceTrackKindInfo must be ordered by SequenceTrackKind with 1..kSequenceMaxChannelCount channels" );

        /** @brief 프레임 번호가 `kSequenceFrameLimit` 안이면 true 입니다. 밖이면 오류를 남깁니다(@p pWhat 은 칸 이름). */
        bool isFrameWithinLimit( float64 frame, string_view sourceLabel, const utf8* pWhat )
        {
            const float64 limit = static_cast<float64>( kSequenceFrameLimit );
            if ( -limit <= frame && frame <= limit )
                return true;
            SW_LOG_ERROR_ID( "Sequence.FrameOutOfRange", "Sequence '%#': %# %# is outside +-%# frames", sourceLabel, pWhat, frame, kSequenceFrameLimit );
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
    } // namespace
} // namespace sw

namespace sw
{
    bool SequenceAsset::loadFromAsset( const AssetID& assetID )
    {
        const string_view path = assetID.view();
        *this                  = SequenceAsset{};
        if ( path.empty() )
            return false;

        string text;
        if ( AssetIO::readTextAsset( assetID, text ) == false )
        {
            SW_LOG_WARNING_ID( "SequenceAsset.CouldNotReadSequence", "Could not read sequence '%#'", path );
            return false;
        }
        return readText( text, path, ResourceContext::forType( *StaticType() ).withAsset( assetID ) );
    }

    bool SequenceAsset::loadFromDiskFile( string_view diskPath )
    {
        *this = SequenceAsset{};
        if ( diskPath.empty() )
            return false;

        string text;
        if ( AssetIO::readTextFile( diskPath, text ) == false )
        {
            SW_LOG_WARNING_ID( "SequenceAsset.CouldNotReadSequence2", "Could not read sequence file '%#'", diskPath );
            return false;
        }
        return readText( text, diskPath, ResourceContext::forType( *StaticType() ) );
    }

    bool SequenceAsset::saveToFile( const AbsolutePath& absPath ) const
    {
        if ( absPath.str().empty() )
            return false;
        return ResourceComposer::saveText( absPath.str(), toXML() );
    }

    bool SequenceAsset::loadFromXMLText( string_view xmlView )
    {
        return readText( xmlView, "<sequence text>", ResourceContext::forType( *StaticType() ) );
    }

    bool SequenceAsset::readText( string_view xml, string_view sourceLabel, const ResourceContext& context )
    {
        // 반쯤 찬 에셋을 남기지 않는다. 실패는 "아무것도 읽지 않았다" 여야 한다.
        *this = SequenceAsset{};
        // 키는 대소문자까지 맞아야 하고, 모르는 속성 · 자식 · 모르는 종류 이름 · 읽지 못한 값 · 범위 밖 숫자는 오류다(`readComposedXMLDocument` 가 로그로 알린다).
        if ( ConfigManager::readComposedXMLDocument( this, *StaticType(), xml, sourceLabel, context ) == false || validateFile( sourceLabel ) == false )
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

    bool SequenceAsset::validateBindings( string_view sourceLabel ) const
    {
        bool                  bValid = true;
        unordered_set<uint32> setID;
        for ( const SequenceBinding& binding : _listBinding )
        {
            if ( binding._id == kSequenceNoBinding || binding._id >= _nextBindingID )
            {
                SW_LOG_ERROR_ID( "SequenceAsset.BindingIDRange", "Sequence '%#': binding '%#' id %# must be 1 or more and below _nextBindingID %#", sourceLabel, binding._name, binding._id, _nextBindingID );
                bValid = false;
            }
            if ( setID.insert( binding._id ).second == false )
            {
                SW_LOG_ERROR_ID( "SequenceAsset.BindingIDDuplicate", "Sequence '%#': binding id %# appears twice ('%#')", sourceLabel, binding._id, binding._name );
                bValid = false;
            }
        }
        return bValid;
    }

    bool SequenceAsset::validateFile( string_view sourceLabel ) const
    {
        // 프레임 번호는 `kSequenceFrameLimit` 안이어야 아래의 `+ 1` 과 재생 · 타임라인 쪽의 뺄셈들이 넘치지 않는다.
        bool bValid = isFrameWithinLimit( _frameMin, sourceLabel, "_frameMin" );
        bValid      = isFrameWithinLimit( _frameMax, sourceLabel, "_frameMax" ) && bValid;
        bValid      = isFrameWithinLimit( _cameraPrerollFrames, sourceLabel, "_cameraPrerollFrames" ) && bValid;
        bValid      = validateBindings( sourceLabel ) && bValid;
        if ( _frameMax <= _frameMin )
        {
            SW_LOG_ERROR_ID( "Sequence.FrameOrder", "Sequence '%#': _frameMax %# must be after _frameMin %#", sourceLabel, _frameMax, _frameMin );
            bValid = false;
        }
        for ( const SequenceTrackItem& item : _listItem )
        {
            bValid = isFrameWithinLimit( item._start, sourceLabel, "_start" ) && bValid;
            bValid = isFrameWithinLimit( item._end, sourceLabel, "_end" ) && bValid;
            bValid = isFrameWithinLimit( item._prerollFrames, sourceLabel, "_prerollFrames" ) && bValid;
            if ( item._prerollFrames < SequenceTrackItem::kUseSequencePreroll )
            {
                SW_LOG_ERROR_ID( "SequenceAsset.SequenceItemPrerollFramesMust", "Sequence '%#': item '%#' _prerollFrames %# must be 0 or more (-1: the sequence value)", sourceLabel, item._name, item._prerollFrames );
                bValid = false;
            }
            SequenceItemField            field = SequenceItemField::Count;
            const SequenceItemFieldIssue issue = item.findFieldIssue( &field );
            switch ( issue )
            {
                case SequenceItemFieldIssue::None:
                {
                    break;
                }
                case SequenceItemFieldIssue::UnknownKind:
                {
                    SW_LOG_ERROR_ID( "SequenceAsset.SequenceItemNoItem", "Sequence '%#': item '%#' has no item kind", sourceLabel, item._name );
                    break;
                }
                case SequenceItemFieldIssue::MissingField:
                case SequenceItemFieldIssue::FieldNotAllowed:
                {
                    SW_LOG_ERROR_ID( "SequenceAsset.SequenceItemField", "Sequence '%#': item '%#' - %# is %# for its kind", sourceLabel, item._name, SequenceTrackItem::getFieldPropertyName( field ),
                                     issue == SequenceItemFieldIssue::MissingField ? "required" : "not allowed" );
                    break;
                }
            }
            bValid = issue == SequenceItemFieldIssue::None && bValid;
            if ( item._binding != kSequenceNoBinding && findBinding( item._binding ) == nullptr )
            {
                SW_LOG_ERROR_ID( "SequenceAsset.ItemBindingUnknown", "Sequence '%#': item '%#' refers to binding %# that the sequence does not have", sourceLabel, item._name, item._binding );
                bValid = false;
            }
        }
        // 스폰 바인딩은 시퀀스가 채운다 — 둘이 같은 바인딩을 스폰하면 어느 오브젝트가 그 바인딩인지 정할 수 없다.
        unordered_set<uint32> setSpawned;
        for ( const SequenceTrackItem& item : _listItem )
        {
            const SequenceItemKindInfo* pInfo = findItemKindInfo( item._kind );
            if ( pInfo == nullptr || pInfo->_bSpawns == false || item._binding == kSequenceNoBinding )
                continue;
            if ( setSpawned.insert( item._binding ).second == false )
            {
                SW_LOG_ERROR_ID( "SequenceAsset.SpawnBindingTwice", "Sequence '%#': two spawn items fill binding %# - each spawned object needs its own binding", sourceLabel, item._binding );
                bValid = false;
            }
        }
        for ( const SequenceKeyTrack& track : _listTrack )
        {
            const uint32 channelCount = SequenceKeyTrack::getChannelCount( track._kind );
            if ( channelCount == 0 || track._listChannel.size() != channelCount )
            {
                SW_LOG_ERROR_ID( "SequenceAsset.SequenceTrackNeedsChannel", "Sequence '%#': track '%#' needs %# channel(s) for its kind, has %#", sourceLabel, track._name, channelCount,
                                 track._listChannel.size() );
                bValid = false;
            }
            if ( findBinding( track._binding ) == nullptr )
            {
                SW_LOG_ERROR_ID( "SequenceAsset.TrackBindingUnknown", "Sequence '%#': track '%#' refers to binding %# that the sequence does not have", sourceLabel, track._name, track._binding );
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

    SequenceItemFieldIssue SequenceTrackItem::findFieldIssue( SequenceItemField* pOutField ) const
    {
        const SequenceItemKindInfo* pKindInfo = SequenceAsset::findItemKindInfo( _kind );
        if ( pKindInfo == nullptr )
            return SequenceItemFieldIssue::UnknownKind;
        // 빈 필수 칸을 두면 재생이 조용히 아무것도 하지 않는다(스폰이 만들지 않고, 시나리오가 부르지 않는다). 남은 다른 종류의 칸은 뜻이 없다.
        for ( uint32 fieldIndex = 0; fieldIndex < static_cast<uint32>( SequenceItemField::Count ); ++fieldIndex )
        {
            const SequenceItemField field   = static_cast<SequenceItemField>( fieldIndex );
            const bool              bFilled = hasField( field );
            SequenceItemFieldIssue  issue   = SequenceItemFieldIssue::None;
            if ( pKindInfo->requiresField( field ) && bFilled == false )
                issue = SequenceItemFieldIssue::MissingField;
            else if ( pKindInfo->allowsField( field ) == false && bFilled )
                issue = SequenceItemFieldIssue::FieldNotAllowed;
            if ( issue == SequenceItemFieldIssue::None )
                continue;
            if ( pOutField != nullptr )
                *pOutField = field;
            return issue;
        }
        return SequenceItemFieldIssue::None;
    }

    bool SequenceTrackItem::hasField( SequenceItemField field ) const
    {
        if ( field == SequenceItemField::Binding )
            return _binding != kSequenceNoBinding;
        const string* const pText = findTextField( field );
        return pText != nullptr && pText->empty() == false;
    }

    string* SequenceTrackItem::accessTextField( SequenceItemField field )
    {
        // 칸 고르기는 const 쪽 한 곳이다(같은 switch 를 두 벌 두지 않는다).
        return const_cast<string*>( static_cast<const SequenceTrackItem*>( this )->findTextField( field ) );
    }

    const string* SequenceTrackItem::findTextField( SequenceItemField field ) const
    {
        switch ( field )
        {
            case SequenceItemField::Prefab:
                return &_prefab;
            case SequenceItemField::Preset:
                return &_preset;
            case SequenceItemField::Scenario:
                return &_scenario;
            case SequenceItemField::Clip:
                return &_clip;
            case SequenceItemField::Sound:
                return &_sound;
            case SequenceItemField::Text:
                return &_text;
            case SequenceItemField::Binding:
            case SequenceItemField::Count:
                return nullptr;
        }
        return nullptr;
    }

    const utf8* SequenceTrackItem::getFieldPropertyName( SequenceItemField field )
    {
        switch ( field )
        {
            case SequenceItemField::Binding:
                return "_binding";
            case SequenceItemField::Prefab:
                return "_prefab";
            case SequenceItemField::Preset:
                return "_preset";
            case SequenceItemField::Scenario:
                return "_scenario";
            case SequenceItemField::Clip:
                return "_clip";
            case SequenceItemField::Sound:
                return "_sound";
            case SequenceItemField::Text:
                return "_text";
            case SequenceItemField::Count:
                return "?";
        }
        return "?";
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

    const SequenceTrackKindInfo* SequenceAsset::findTrackKindInfo( SequenceTrackKind kind )
    {
        const int32 kindIndex = static_cast<int32>( kind );
        if ( kindIndex < 0 || kindIndex >= static_cast<int32>( SequenceTrackKind::Count ) )
            return nullptr;
        return &kArrSequenceTrackKindInfo[kindIndex];
    }

    const SequenceBinding* SequenceAsset::findBinding( uint32 bindingID ) const
    {
        if ( bindingID == kSequenceNoBinding )
            return nullptr;
        for ( const SequenceBinding& binding : _listBinding )
        {
            if ( binding._id == bindingID )
                return &binding;
        }
        return nullptr;
    }

    uint32 SequenceAsset::addBinding( string_view name )
    {
        SequenceBinding binding{};
        binding._id   = _nextBindingID;
        binding._name = string{ name };
        ++_nextBindingID;
        _listBinding.push_back( std::move( binding ) );
        return _listBinding.back()._id;
    }

    uint32 SequenceAsset::countBindingUses( uint32 bindingID ) const
    {
        uint32 useCount = 0;
        for ( const SequenceTrackItem& item : _listItem )
        {
            useCount += item._binding == bindingID ? 1u : 0u;
        }
        for ( const SequenceKeyTrack& track : _listTrack )
        {
            useCount += track._binding == bindingID ? 1u : 0u;
        }
        return useCount;
    }

    const SequenceTrackItem* SequenceAsset::findSpawnItem( uint32 bindingID ) const
    {
        for ( const SequenceTrackItem& item : _listItem )
        {
            const SequenceItemKindInfo* pInfo = findItemKindInfo( item._kind );
            if ( pInfo != nullptr && pInfo->_bSpawns && item._binding == bindingID )
                return &item;
        }
        return nullptr;
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
        const SequenceTrackKindInfo* pInfo = SequenceAsset::findTrackKindInfo( kind );
        return pInfo != nullptr ? pInfo->_channelCount : 0;
    }

    const utf8* SequenceKeyTrack::getChannelName( uint32 channelIndex ) const
    {
        if ( channelIndex >= _listChannel.size() )
            return "?";
        if ( _kind == SequenceTrackKind::Transform && channelIndex < kSequenceTransformChannelCount )
            return kArrSequenceTransformChannelName[channelIndex];
        if ( _kind == SequenceTrackKind::CameraLens && channelIndex < kSequenceLensChannelCount )
            return kArrSequenceLensChannelName[channelIndex];
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

    bool SequenceKeyTrack::moveKeysAt( float32 fromFrame, float32 toFrame )
    {
        if ( MathUtil::abs( fromFrame - toFrame ) < kSequenceKeyFrameTolerance )
            return false;
        bool bMoved = false;
        for ( FloatCurve& channel : _listChannel )
        {
            const auto itFrom = std::find_if( channel._listKey.begin(), channel._listKey.end(), [fromFrame]( const FloatCurveKey& key )
            { return MathUtil::abs( key._time - fromFrame ) < kSequenceKeyFrameTolerance; } );
            if ( itFrom == channel._listKey.end() )
                continue;
            FloatCurveKey moved = *itFrom;
            moved._time         = toFrame;
            channel._listKey.erase( itFrom );
            // 놓은 자리의 키는 옮긴 키가 덮는다.
            (void)VectorUtil::removeIf( channel._listKey, [toFrame]( const FloatCurveKey& key )
            { return MathUtil::abs( key._time - toFrame ) < kSequenceKeyFrameTolerance; } );
            channel._listKey.push_back( moved );
            channel.sortAndComputeAutoTangents();
            bMoved = true;
        }
        return bMoved;
    }

    bool SequenceKeyTrack::hasKeyAt( float32 frame ) const
    {
        for ( uint32 channelIndex = 0; channelIndex < _listChannel.size(); ++channelIndex )
        {
            if ( findChannelKey( channelIndex, frame ).has_value() )
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
        VectorUtil::sort( outListFrame );
        // 채널마다 같은 프레임에 찍힌 키는 키 프레임 하나다.
        const auto itUnique = std::unique( outListFrame.begin(), outListFrame.end(), []( float32 left, float32 right )
        { return MathUtil::abs( left - right ) < kSequenceKeyFrameTolerance; } );
        outListFrame.erase( itUnique, outListFrame.end() );
    }

    optional<float32> SequenceKeyTrack::evaluateChannel( uint32 channelIndex, float32 frame ) const
    {
        if ( channelIndex >= _listChannel.size() )
            return nullopt;
        return _listChannel[channelIndex].evaluate( frame );
    }

    optional<uint32> SequenceKeyTrack::findChannelKey( uint32 channelIndex, float32 frame ) const
    {
        if ( channelIndex >= _listChannel.size() )
            return nullopt;
        const vector<FloatCurveKey>& listKey = _listChannel[channelIndex]._listKey;
        for ( size_t keyIndex = 0; keyIndex < listKey.size(); ++keyIndex )
        {
            if ( MathUtil::abs( listKey[keyIndex]._time - frame ) < kSequenceKeyFrameTolerance )
                return static_cast<uint32>( keyIndex );
        }
        return nullopt;
    }
} // namespace sw

namespace sw
{
    SW_ASSET_TYPE( SequenceAsset, SequenceAsset::kAssetType );
} // namespace sw
