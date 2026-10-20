#include "pch.h"

#include "GameFramework/PlugIn/Editor/CharacterEditor/Model/PairedAnimationEditModel.h"

#include "Core/File/FileUtil.h"
#include "Core/Math/MathUtil.h"

namespace sw::editor
{
    namespace
    {
        struct PairedAnimationEditModelInternal
        {
            /** @brief 값이 유한하고 0 이상이면 true, 아니면 오류를 쓰고 false 입니다. */
            static bool isNonNegative( float32 value, const utf8* pField, string& outError )
            {
                if ( MathUtil::isFinite( value ) && value >= 0.0f )
                    return true;
                outError = string( pField ) + " must be a finite value of 0 or more";
                return false;
            }

            /** @brief 연속 편집을 합칠 키입니다(필드 + 인덱스). */
            static string makeCoalesceKey( const utf8* pField, uint32 index )
            {
                utf8 arrKey[constant::kMaxBuffer64];
                formatstring( arrKey, constant::kMaxBuffer64, "%#:%#", pField, index );
                return string( arrKey );
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    PairedAnimationEditModel::PairedAnimationEditModel()
        : _content{}
        , _savedText{ PairedAnimationDef::makeXMLText( PairedAnimationContentDef{} ) }
        , _history{}
        , _scrubTime{ 0.0f }
    {
    }

    bool PairedAnimationEditModel::loadFromXMLText( string_view xmlText, string_view sourceName )
    {
        PairedAnimationDef loaded;
        if ( loaded.loadFromXMLText( xmlText, sourceName ) == false )
            return false;
        _content   = loaded.getContent();
        _savedText = saveToXMLText();
        _history.clear();
        _scrubTime = 0.0f;
        return true;
    }

    bool PairedAnimationEditModel::loadFromFile( string_view absolutePath )
    {
        string text;
        if ( FileUtil::readTextFile( absolutePath, text ) == false )
        {
            SW_LOG_WARNING( "Paired animation file could not be read: %#", string( absolutePath ).c_str() );
            return false;
        }
        return loadFromXMLText( text, absolutePath );
    }

    bool PairedAnimationEditModel::saveToFile( string_view absolutePath, string& outError )
    {
        if ( isValid() == false )
        {
            outError = "The paired animation breaks a rule (see the log) - fix it before saving";
            return false;
        }
        const string text = saveToXMLText();
        if ( FileUtil::writeTextFile( absolutePath, text ) == false )
        {
            outError = "The file could not be written";
            return false;
        }
        _savedText = text;
        return true;
    }

    bool PairedAnimationEditModel::isValid() const
    {
        PairedAnimationDef check;
        return check.loadFromXMLText( saveToXMLText(), "paired animation editor" );
    }

    bool PairedAnimationEditModel::setDuration( float32 seconds, bool bContinuous, string& outError )
    {
        if ( PairedAnimationEditModelInternal::isNonNegative( seconds, "Duration", outError ) == false )
            return false;
        if ( seconds <= 0.0f )
        {
            outError = "Duration must be positive";
            return false;
        }
        recordUndo( bContinuous ? string_view( "duration" ) : string_view{} );
        _content._duration = seconds;
        setScrubTime( _scrubTime ); // 줄었으면 스크럽 시각도 자른다
        return true;
    }

    bool PairedAnimationEditModel::setRoleStart( uint32 roleIndex, float32 seconds, bool bContinuous, string& outError )
    {
        if ( isRoleIndexValid( roleIndex, outError ) == false || PairedAnimationEditModelInternal::isNonNegative( seconds, "Start", outError ) == false )
            return false;
        if ( _content._listRole[roleIndex]._bAnchor )
        {
            outError = "The anchor role starts at 0";
            return false;
        }
        recordUndo( bContinuous ? PairedAnimationEditModelInternal::makeCoalesceKey( "start", roleIndex ) : string{} );
        _content._listRole[roleIndex]._startSeconds = seconds;
        return true;
    }

    bool PairedAnimationEditModel::setRoleBlend( uint32 roleIndex, float32 enterSeconds, float32 exitSeconds, bool bContinuous, string& outError )
    {
        const bool bValid = isRoleIndexValid( roleIndex, outError ) && PairedAnimationEditModelInternal::isNonNegative( enterSeconds, "Enter blend", outError ) &&
                            PairedAnimationEditModelInternal::isNonNegative( exitSeconds, "Exit blend", outError );
        if ( bValid == false )
            return false;
        recordUndo( bContinuous ? PairedAnimationEditModelInternal::makeCoalesceKey( "blend", roleIndex ) : string{} );
        _content._listRole[roleIndex]._enterBlendSeconds = enterSeconds;
        _content._listRole[roleIndex]._exitBlendSeconds  = exitSeconds;
        return true;
    }

    bool PairedAnimationEditModel::setRoleOffset( uint32 roleIndex, const float3& offset, float32 yawDegrees, bool bContinuous, string& outError )
    {
        if ( isRoleIndexValid( roleIndex, outError ) == false )
            return false;
        PairedAnimationRoleDef& role     = _content._listRole[roleIndex];
        const bool              bAligned = role._bAnchor == false && role._align != PairedAnimationAlign::None;
        if ( bAligned == false )
        {
            outError = string( "Role '" ) + role._name.c_str() + "' does not align (align is none or it is the anchor)";
            return false;
        }
        const bool bFinite = MathUtil::isFinite( offset._x ) && MathUtil::isFinite( offset._y ) && MathUtil::isFinite( offset._z ) && MathUtil::isFinite( yawDegrees );
        if ( bFinite == false )
        {
            outError = "Offset and yaw must be finite";
            return false;
        }
        recordUndo( bContinuous ? PairedAnimationEditModelInternal::makeCoalesceKey( "offset", roleIndex ) : string{} );
        role._offset     = offset;
        role._yawRadians = MathUtil::wrapAngle( yawDegrees * MathUtil::kDegreeToRadian );
        return true;
    }

    bool PairedAnimationEditModel::setRoleOffsetFromWorld( uint32 roleIndex, const float3& anchorPosition, float32 anchorYaw, const float3& rolePosition, float32 roleYaw,
                                                           bool bContinuous, string& outError )
    {
        // `PairedAnimationDef::computeAlignTarget` 의 역 — 앵커 요를 되돌려 앵커 기준 자리로 만든다.
        const quaternion unturn = quaternion::makeFromAxisAngle( float3{ 0.0f, 1.0f, 0.0f }, -anchorYaw );
        const float3     offset = float3::transform( rolePosition - anchorPosition, unturn );
        const float32    yaw    = MathUtil::wrapAngle( roleYaw - anchorYaw );
        return setRoleOffset( roleIndex, offset, yaw * MathUtil::kRadianToDegree, bContinuous, outError );
    }

    bool PairedAnimationEditModel::setRoleWarpEnd( uint32 roleIndex, float32 seconds, bool bContinuous, string& outError )
    {
        if ( isRoleIndexValid( roleIndex, outError ) == false || PairedAnimationEditModelInternal::isNonNegative( seconds, "Warp end", outError ) == false )
            return false;
        PairedAnimationRoleDef& role = _content._listRole[roleIndex];
        if ( role._bAnchor || role._align != PairedAnimationAlign::Warp )
        {
            outError = string( "Role '" ) + role._name.c_str() + "' does not warp";
            return false;
        }
        recordUndo( bContinuous ? PairedAnimationEditModelInternal::makeCoalesceKey( "warpEnd", roleIndex ) : string{} );
        role._warpEndSeconds = seconds;
        return true;
    }

    bool PairedAnimationEditModel::addNotify( const hashed_string& name, const hashed_string& role, float32 time, string& outError )
    {
        if ( name.empty() )
        {
            outError = "Notify name is empty";
            return false;
        }
        const bool bTimeInRange = MathUtil::isFinite( time ) && 0.0f <= time && time <= _content._duration;
        if ( bTimeInRange == false )
        {
            outError = "Notify time must be within the timeline";
            return false;
        }
        bool bRoleKnown = false;
        for ( const PairedAnimationRoleDef& roleDef : _content._listRole )
        {
            bRoleKnown = bRoleKnown || roleDef._name == role;
        }
        if ( bRoleKnown == false )
        {
            outError = string( "Role '" ) + role.c_str() + "' is not in this paired animation";
            return false;
        }
        recordUndo( string_view{} );
        _content._listNotify.push_back( PairedAnimationNotifyDef{ name, role, time } );
        return true;
    }

    bool PairedAnimationEditModel::removeNotify( uint32 notifyIndex, string& outError )
    {
        if ( isNotifyIndexValid( notifyIndex, outError ) == false )
            return false;
        recordUndo( string_view{} );
        _content._listNotify.erase( _content._listNotify.begin() + static_cast<ptrdiff_t>( notifyIndex ) );
        return true;
    }

    bool PairedAnimationEditModel::setNotifyTime( uint32 notifyIndex, float32 time, bool bContinuous, string& outError )
    {
        if ( isNotifyIndexValid( notifyIndex, outError ) == false )
            return false;
        const bool bTimeInRange = MathUtil::isFinite( time ) && 0.0f <= time && time <= _content._duration;
        if ( bTimeInRange == false )
        {
            outError = "Notify time must be within the timeline";
            return false;
        }
        recordUndo( bContinuous ? PairedAnimationEditModelInternal::makeCoalesceKey( "notifyTime", notifyIndex ) : string{} );
        _content._listNotify[notifyIndex]._time = time;
        return true;
    }

    bool PairedAnimationEditModel::setNotifyRole( uint32 notifyIndex, const hashed_string& role, string& outError )
    {
        if ( isNotifyIndexValid( notifyIndex, outError ) == false )
            return false;
        bool bRoleKnown = false;
        for ( const PairedAnimationRoleDef& roleDef : _content._listRole )
        {
            bRoleKnown = bRoleKnown || roleDef._name == role;
        }
        if ( bRoleKnown == false )
        {
            outError = string( "Role '" ) + role.c_str() + "' is not in this paired animation";
            return false;
        }
        recordUndo( string_view{} );
        _content._listNotify[notifyIndex]._role = role;
        return true;
    }

    bool PairedAnimationEditModel::undo()
    {
        return _history.undo( _content );
    }

    bool PairedAnimationEditModel::redo()
    {
        return _history.redo( _content );
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
        _history.record( _content, coalesceKey );
    }

    bool PairedAnimationEditModel::isRoleIndexValid( uint32 roleIndex, string& outError ) const
    {
        if ( roleIndex < _content._listRole.size() )
            return true;
        outError = "Role index is out of range";
        return false;
    }

    bool PairedAnimationEditModel::isNotifyIndexValid( uint32 notifyIndex, string& outError ) const
    {
        if ( notifyIndex < _content._listNotify.size() )
            return true;
        outError = "Notify index is out of range";
        return false;
    }
} // namespace sw::editor
