#include "pch.h"

#include "GameFramework/Plugin/Editor/CharacterEditor/Model/PairedAnimationEditModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Serialization/Inherit/ResourceComposer.h"

namespace sw::editor
{
    namespace
    {
        struct PairedAnimationEditModelInternal
        {
            /** @brief 값이 유한하고 0 이상이 아니면 오류입니다(대상은 칸 이름). */
            static expected<void> checkNonNegative( float32 value, const utf8* pField )
            {
                if ( MathUtil::isFinite( value ) && value >= 0.0f )
                    return {};
                return unexpected( Error{ ErrorKind::InvalidArgument, "Value must be finite and 0 or more", pField } );
            }

            /** @brief 연속 편집을 합칠 키입니다(필드 + 인덱스). */
            static string makeCoalesceKey( const utf8* pField, uint32 index )
            {
                utf8 arrKey[constant::buffer::kMax64];
                formatstring( arrKey, constant::buffer::kMax64, "%#:%#", pField, index );
                return string( arrKey );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    PairedAnimationEditModel::PairedAnimationEditModel()
        : _content{}
        , _state{ PairedAnimationDef::makeXMLText( PairedAnimationContentDef{} ) }
        , _scrubTime{ 0.0f }
    {
    }

    bool PairedAnimationEditModel::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        PairedAnimationDef loaded;
        if ( loaded.loadFromXMLText( xmlText, sourceName ) == false )
            return false;
        _content = loaded.getContent();
        _state.reset( saveToXMLText() );
        _scrubTime = 0.0f;
        return true;
    }

    bool PairedAnimationEditModel::loadFromFile( string_view absolutePath )
    {
        string text;
        if ( FileUtil::readTextFile( absolutePath, text ) == false )
        {
            SW_LOG_WARNING_ID( "PairedAnimationEditModel.PairedAnimationFileCould", "Paired animation file could not be read: %#", string( absolutePath ).c_str() );
            return false;
        }
        return loadFromXMLText( text, absolutePath );
    }

    expected<void> PairedAnimationEditModel::saveDocument( const AbsolutePath& absPath )
    {
        if ( isValid() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "The paired animation breaks a rule (see the log) - fix it before saving" } );
        const string text = saveToXMLText();
        if ( ResourceComposer::saveText( absPath.str(), text ) == false )
            return unexpected( Error{ ErrorKind::ExternalFailure, "The file could not be written", absPath.str() } );
        _state.markSaved( text );
        return {};
    }

    bool PairedAnimationEditModel::isValid() const
    {
        PairedAnimationDef check;
        return check.loadFromXMLText( saveToXMLText(), "paired animation editor" );
    }

    expected<void> PairedAnimationEditModel::setDuration( float32 seconds, bool bContinuous )
    {
        if ( expected<void> check = PairedAnimationEditModelInternal::checkNonNegative( seconds, "Duration" ); check.has_value() == false )
            return check;
        if ( seconds <= 0.0f )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Duration must be positive" } );
        recordUndo( bContinuous ? string_view( "duration" ) : string_view{} );
        _content._duration = seconds;
        setScrubTime( _scrubTime ); // 줄었으면 스크럽 시각도 자른다
        return {};
    }

    expected<void> PairedAnimationEditModel::setRoleStart( uint32 roleIndex, float32 seconds, bool bContinuous )
    {
        if ( expected<void> check = checkRoleIndex( roleIndex ).and_then( [seconds]()
        { return PairedAnimationEditModelInternal::checkNonNegative( seconds, "Start" ); } );
             check.has_value() == false )
            return check;
        if ( _content._listRole[roleIndex]._bAnchor )
            return unexpected( Error{ ErrorKind::InvalidArgument, "The anchor role starts at 0", _content._listRole[roleIndex]._name.view() } );
        recordUndo( bContinuous ? PairedAnimationEditModelInternal::makeCoalesceKey( "start", roleIndex ) : string{} );
        _content._listRole[roleIndex]._startSeconds = seconds;
        return {};
    }

    expected<void> PairedAnimationEditModel::setRoleBlend( uint32 roleIndex, float32 enterSeconds, float32 exitSeconds, bool bContinuous )
    {
        const expected<void> check = checkRoleIndex( roleIndex )
                                         .and_then( [enterSeconds]()
        { return PairedAnimationEditModelInternal::checkNonNegative( enterSeconds, "Enter blend" ); } )
                                         .and_then( [exitSeconds]()
        { return PairedAnimationEditModelInternal::checkNonNegative( exitSeconds, "Exit blend" ); } );
        if ( check.has_value() == false )
            return check;
        recordUndo( bContinuous ? PairedAnimationEditModelInternal::makeCoalesceKey( "blend", roleIndex ) : string{} );
        _content._listRole[roleIndex]._enterBlendSeconds = enterSeconds;
        _content._listRole[roleIndex]._exitBlendSeconds  = exitSeconds;
        return {};
    }

    expected<void> PairedAnimationEditModel::setRoleOffset( uint32 roleIndex, const float3& offset, float32 yawDegrees, bool bContinuous )
    {
        if ( expected<void> check = checkRoleIndex( roleIndex ); check.has_value() == false )
            return check;
        PairedAnimationRoleDef& role     = _content._listRole[roleIndex];
        const bool              bAligned = role._bAnchor == false && role._align != PairedAnimationAlign::None;
        if ( bAligned == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Role does not align (align is none or it is the anchor)", role._name.view() } );
        const bool bFinite = MathUtil::isFinite( offset._x ) && MathUtil::isFinite( offset._y ) && MathUtil::isFinite( offset._z ) && MathUtil::isFinite( yawDegrees );
        if ( bFinite == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Offset and yaw must be finite" } );
        recordUndo( bContinuous ? PairedAnimationEditModelInternal::makeCoalesceKey( "offset", roleIndex ) : string{} );
        role._offset     = offset;
        role._yawRadians = MathUtil::wrapAngle( yawDegrees * MathUtil::kDegreeToRadian );
        return {};
    }

    expected<void> PairedAnimationEditModel::setRoleOffsetFromWorld( uint32 roleIndex, const float3& anchorPosition, float32 anchorYaw, const float3& rolePosition, float32 roleYaw,
                                                                     bool bContinuous )
    {
        // `PairedAnimationDef::computeAlignTarget` 의 역 — 앵커 요를 되돌려 앵커 기준 자리로 만든다.
        const quaternion unturn = quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, -anchorYaw );
        const float3     offset = float3::transform( rolePosition - anchorPosition, unturn );
        const float32    yaw    = MathUtil::wrapAngle( roleYaw - anchorYaw );
        return setRoleOffset( roleIndex, offset, yaw * MathUtil::kRadianToDegree, bContinuous );
    }

    expected<void> PairedAnimationEditModel::setRoleWarpEnd( uint32 roleIndex, float32 seconds, bool bContinuous )
    {
        if ( expected<void> check = checkRoleIndex( roleIndex ).and_then( [seconds]()
        { return PairedAnimationEditModelInternal::checkNonNegative( seconds, "Warp end" ); } );
             check.has_value() == false )
            return check;
        PairedAnimationRoleDef& role = _content._listRole[roleIndex];
        if ( role._bAnchor || role._align != PairedAnimationAlign::Warp )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Role does not warp", role._name.view() } );
        recordUndo( bContinuous ? PairedAnimationEditModelInternal::makeCoalesceKey( "warpEnd", roleIndex ) : string{} );
        role._warpEndSeconds = seconds;
        return {};
    }

    expected<void> PairedAnimationEditModel::addNotify( const hashed_string& name, const hashed_string& role, float32 time )
    {
        if ( name.empty() )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Notify name is empty" } );
        const bool bTimeInRange = MathUtil::isFinite( time ) && 0.0f <= time && time <= _content._duration;
        if ( bTimeInRange == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Notify time must be within the timeline" } );
        bool bRoleKnown = false;
        for ( const PairedAnimationRoleDef& roleDef : _content._listRole )
        {
            bRoleKnown = bRoleKnown || roleDef._name == role;
        }
        if ( bRoleKnown == false )
            return unexpected( Error{ ErrorKind::NotFound, "Role is not in this paired animation", role.view() } );
        recordUndo( string_view{} );
        _content._listNotify.push_back( PairedAnimationNotifyDef{ name, role, time } );
        return {};
    }

    expected<void> PairedAnimationEditModel::removeNotify( uint32 notifyIndex )
    {
        if ( expected<void> check = checkNotifyIndex( notifyIndex ); check.has_value() == false )
            return check;
        recordUndo( string_view{} );
        _content._listNotify.erase( _content._listNotify.begin() + static_cast<ptrdiff_t>( notifyIndex ) );
        return {};
    }

    expected<void> PairedAnimationEditModel::setNotifyTime( uint32 notifyIndex, float32 time, bool bContinuous )
    {
        if ( expected<void> check = checkNotifyIndex( notifyIndex ); check.has_value() == false )
            return check;
        const bool bTimeInRange = MathUtil::isFinite( time ) && 0.0f <= time && time <= _content._duration;
        if ( bTimeInRange == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "Notify time must be within the timeline" } );
        recordUndo( bContinuous ? PairedAnimationEditModelInternal::makeCoalesceKey( "notifyTime", notifyIndex ) : string{} );
        _content._listNotify[notifyIndex]._time = time;
        return {};
    }

    expected<void> PairedAnimationEditModel::setNotifyRole( uint32 notifyIndex, const hashed_string& role )
    {
        if ( expected<void> check = checkNotifyIndex( notifyIndex ); check.has_value() == false )
            return check;
        bool bRoleKnown = false;
        for ( const PairedAnimationRoleDef& roleDef : _content._listRole )
        {
            bRoleKnown = bRoleKnown || roleDef._name == role;
        }
        if ( bRoleKnown == false )
            return unexpected( Error{ ErrorKind::NotFound, "Role is not in this paired animation", role.view() } );
        recordUndo( string_view{} );
        _content._listNotify[notifyIndex]._role = role;
        return {};
    }

    bool PairedAnimationEditModel::undo()
    {
        return _state.undo( _content );
    }

    bool PairedAnimationEditModel::redo()
    {
        return _state.redo( _content );
    }

    void PairedAnimationEditModel::setScrubTime( float32 seconds )
    {
        const float32 upper = _content._duration > 0.0f ? _content._duration : 0.0f;
        _scrubTime          = MathUtil::isFinite( seconds ) ? MathUtil::clamp( seconds, 0.0f, upper ) : 0.0f;
    }

    PairedAnimationRolePreview PairedAnimationEditModel::computeRolePreview( uint32 roleIndex, float32 clipSeconds ) const
    {
        PairedAnimationRolePreview preview{};
        if ( roleIndex >= _content._listRole.size() )
            return preview;
        const PairedAnimationRoleDef& role = _content._listRole[roleIndex];
        if ( role._bAnchor )
        {
            preview._stage       = PairedAnimationPreviewStage::Playing;
            preview._clipSeconds = clipSeconds > 0.0f ? MathUtil::min( _scrubTime, clipSeconds ) : _scrubTime;
            return preview;
        }
        if ( _scrubTime < role._startSeconds )
            return preview; // Waiting
        const float32 clipTime  = _scrubTime - role._startSeconds;
        const bool    bFinished = clipSeconds > 0.0f && clipTime > clipSeconds;
        preview._stage          = bFinished ? PairedAnimationPreviewStage::Exited : PairedAnimationPreviewStage::Playing;
        preview._clipSeconds    = bFinished ? clipSeconds : clipTime;
        return preview;
    }

    void PairedAnimationEditModel::collectNotifiesBetween( float32 fromSeconds, float32 toSeconds, vector<uint32>& outListNotifyIndex ) const
    {
        for ( uint32 notifyIndex = 0; notifyIndex < static_cast<uint32>( _content._listNotify.size() ); ++notifyIndex )
        {
            const float32 time     = _content._listNotify[notifyIndex]._time;
            const bool    bCrossed = fromSeconds < time && time <= toSeconds;
            if ( bCrossed )
                outListNotifyIndex.push_back( notifyIndex );
        }
    }

    void PairedAnimationEditModel::computeRoleTarget( uint32 roleIndex, const float3& anchorPosition, float32 anchorYaw, float3& outPosition, float32& outYaw ) const
    {
        outPosition = anchorPosition;
        outYaw      = anchorYaw;
        if ( roleIndex >= _content._listRole.size() )
            return;
        const PairedAnimationRoleDef& role     = _content._listRole[roleIndex];
        const bool                    bAligned = role._bAnchor == false && role._align != PairedAnimationAlign::None;
        if ( bAligned )
            PairedAnimationDef::computeAlignTarget( anchorPosition, anchorYaw, role, outPosition, outYaw );
    }

    void PairedAnimationEditModel::recordUndo( string_view coalesceKey )
    {
        _state.record( _content, coalesceKey );
    }

    expected<void> PairedAnimationEditModel::checkRoleIndex( uint32 roleIndex ) const
    {
        if ( roleIndex < _content._listRole.size() )
            return {};
        return unexpected( Error{ ErrorKind::InvalidArgument, "Role index is out of range" } );
    }

    expected<void> PairedAnimationEditModel::checkNotifyIndex( uint32 notifyIndex ) const
    {
        if ( notifyIndex < _content._listNotify.size() )
            return {};
        return unexpected( Error{ ErrorKind::InvalidArgument, "Notify index is out of range" } );
    }
} // namespace sw::editor
