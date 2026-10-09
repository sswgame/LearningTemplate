#include "pch.h"

#include "Engine/Animation/Facial/FacialRig.h"

#include "Core/Log/Logger.h"

#include "Engine/Animation/AnimJsonUtil.h"
#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Serialization/Json/JsonDocument.h"

namespace sw
{
    SW_LOG_CALLER( "FacialRig" );

    bool FacialRig::parseJson( string_view json, string_view sourceLabel )
    {
        *this = FacialRig{};
        JsonDocument document;
        if ( document.parse( json, sourceLabel ) == false )
        {
            SW_LOG_ERROR( "Facial rig '%#': malformed JSON", sourceLabel );
            return false;
        }
        if ( parseRoot( document.getRoot(), sourceLabel ) )
            return true;
        *this = FacialRig{};
        return false;
    }

    bool FacialRig::loadFromResource( string_view path )
    {
        string text;
        if ( ResourceUtil::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Facial rig '%#' could not be read", path );
            *this = FacialRig{};
            return false;
        }
        return parseJson( text, path );
    }

    bool FacialRig::parsePoses( const JsonValue& value, vector<FacialPose>& outListPose, string_view sourceLabel )
    {
        if ( value.isValid() == false || value.isNull() )
            return true;
        if ( value.isObject() == false )
        {
            SW_LOG_ERROR( "Facial rig '%#': expressions and visemes must be objects of { target: weight }", sourceLabel );
            return false;
        }
        for ( const string& poseName : value.getMemberNames() )
        {
            const JsonValue targets = value.get( poseName, false );
            if ( targets.isObject() == false )
            {
                SW_LOG_ERROR( "Facial rig '%#': pose '%#' must be an object of { target: weight }", sourceLabel, poseName.c_str() );
                return false;
            }
            FacialPose pose{};
            pose._name = hashed_string( poseName );
            for ( const string& targetName : targets.getMemberNames() )
            {
                const JsonValue weight = targets.get( targetName, false );
                if ( weight.isNumber() == false )
                {
                    SW_LOG_ERROR( "Facial rig '%#': pose '%#' target '%#' needs a numeric weight", sourceLabel, poseName.c_str(), targetName.c_str() );
                    return false;
                }
                pose._listTarget.push_back( FacialTargetWeight{ hashed_string( targetName ), static_cast<float32>( weight.asFloat() ) } );
            }
            outListPose.push_back( std::move( pose ) );
        }
        return true;
    }

    bool FacialRig::parseRoot( const JsonValue& root, string_view sourceLabel )
    {
        if ( AnimJsonUtil::hasOnlyKnownKeys( root, { "expressions", "visemes", "blink", "gaze" }, sourceLabel ) == false )
            return false;
        if ( parsePoses( root.get( "expressions" ), _listExpression, sourceLabel ) == false || parsePoses( root.get( "visemes" ), _listViseme, sourceLabel ) == false )
            return false;

        const JsonValue blink = root.get( "blink" );
        if ( blink.isObject() )
        {
            if ( AnimJsonUtil::hasOnlyKnownKeys( blink, { "targets", "min_interval", "max_interval", "duration" }, sourceLabel ) == false )
                return false;
            const JsonValue targets = blink.get( "targets" );
            if ( targets.isArray() == false || blink.get( "min_interval" ).isNumber() == false || blink.get( "max_interval" ).isNumber() == false ||
                 blink.get( "duration" ).isNumber() == false )
            {
                SW_LOG_ERROR( "Facial rig '%#': blink needs targets, min_interval, max_interval and duration", sourceLabel );
                return false;
            }
            for ( size_t targetIndex = 0; targetIndex < targets.size(); ++targetIndex )
            {
                _blink._listTarget.push_back( hashed_string( targets.at( targetIndex ).asString() ) );
            }
            _blink._minInterval = static_cast<float32>( blink.get( "min_interval" ).asFloat() );
            _blink._maxInterval = static_cast<float32>( blink.get( "max_interval" ).asFloat() );
            _blink._duration    = static_cast<float32>( blink.get( "duration" ).asFloat() );
            if ( _blink._minInterval <= 0.0f || _blink._maxInterval < _blink._minInterval || _blink._duration <= 0.0f )
            {
                SW_LOG_ERROR( "Facial rig '%#': blink intervals must be positive and ordered, duration positive", sourceLabel );
                return false;
            }
        }

        const JsonValue gaze = root.get( "gaze" );
        if ( gaze.isObject() )
        {
            if ( AnimJsonUtil::hasOnlyKnownKeys( gaze, { "eye_bones", "forward_axis", "max_angle_degrees", "saccade_min_interval", "saccade_max_interval", "saccade_amplitude_degrees" },
                                                 sourceLabel ) == false )
                return false;
            const JsonValue bones = gaze.get( "eye_bones" );
            float32         arrForward[3]{};
            const bool      bComplete = bones.isArray() && AnimJsonUtil::readFloats( gaze.get( "forward_axis" ), arrForward, 3 ) &&
                                   gaze.get( "max_angle_degrees" ).isNumber() && gaze.get( "saccade_min_interval" ).isNumber() &&
                                   gaze.get( "saccade_max_interval" ).isNumber() && gaze.get( "saccade_amplitude_degrees" ).isNumber();
            if ( bComplete == false )
            {
                SW_LOG_ERROR( "Facial rig '%#': gaze needs eye_bones, forward_axis, max_angle_degrees and the saccade numbers", sourceLabel );
                return false;
            }
            for ( size_t boneIndex = 0; boneIndex < bones.size(); ++boneIndex )
            {
                _gaze._listEyeBone.push_back( hashed_string( bones.at( boneIndex ).asString() ) );
            }
            _gaze._forwardAxis             = float3{ arrForward }.normalize();
            _gaze._maxAngleDegrees         = static_cast<float32>( gaze.get( "max_angle_degrees" ).asFloat() );
            _gaze._saccadeMinInterval      = static_cast<float32>( gaze.get( "saccade_min_interval" ).asFloat() );
            _gaze._saccadeMaxInterval      = static_cast<float32>( gaze.get( "saccade_max_interval" ).asFloat() );
            _gaze._saccadeAmplitudeDegrees = static_cast<float32>( gaze.get( "saccade_amplitude_degrees" ).asFloat() );
            if ( _gaze._saccadeMinInterval <= 0.0f || _gaze._saccadeMaxInterval < _gaze._saccadeMinInterval )
            {
                SW_LOG_ERROR( "Facial rig '%#': saccade intervals must be positive and ordered", sourceLabel );
                return false;
            }
        }
        return true;
    }

    bool FacialRig::validate( const vector<hashed_string>& listMorphTargetName, const Skeleton& skeleton, string_view sourceLabel ) const
    {
        auto hasTarget = [&listMorphTargetName]( const hashed_string& name )
        { return std::find( listMorphTargetName.begin(), listMorphTargetName.end(), name ) != listMorphTargetName.end(); };
        bool bValid       = true;
        auto checkTargets = [&]( const vector<FacialPose>& listPose )
        {
            for ( const FacialPose& pose : listPose )
            {
                for ( const FacialTargetWeight& target : pose._listTarget )
                {
                    if ( hasTarget( target._target ) )
                        continue;
                    SW_LOG_ERROR( "Facial rig '%#': pose '%#' names morph target '%#' which the mesh does not have", sourceLabel, pose._name.c_str(),
                                  target._target.c_str() );
                    bValid = false;
                }
            }
        };
        checkTargets( _listExpression );
        // 표정 이름 = 표정 커브 이름이다. 타깃과 이름이 같으면(대소문자 무시) 그 커브가 타깃과 표정을 둘 다 움직인다.
        for ( const FacialPose& expression : _listExpression )
        {
            if ( hasTarget( expression._name ) == false )
                continue;
            SW_LOG_ERROR( "Facial rig '%#': expression '%#' has the name of a morph target - a curve of that name would drive both", sourceLabel,
                          expression._name.c_str() );
            bValid = false;
        }
        checkTargets( _listViseme );
        for ( const hashed_string& target : _blink._listTarget )
        {
            if ( hasTarget( target ) )
                continue;
            SW_LOG_ERROR( "Facial rig '%#': blink target '%#' is not a morph target of the mesh", sourceLabel, target.c_str() );
            bValid = false;
        }
        for ( const hashed_string& bone : _gaze._listEyeBone )
        {
            if ( skeleton.findBoneIndex( bone ) >= 0 )
                continue;
            SW_LOG_ERROR( "Facial rig '%#': eye bone '%#' is not in the skeleton", sourceLabel, bone.c_str() );
            bValid = false;
        }
        return bValid;
    }

    int32 FacialRig::findExpressionIndex( const hashed_string& name ) const
    {
        for ( size_t poseIndex = 0; poseIndex < _listExpression.size(); ++poseIndex )
        {
            if ( _listExpression[poseIndex]._name == name )
                return static_cast<int32>( poseIndex );
        }
        return -1;
    }

    int32 FacialRig::findVisemeIndex( const hashed_string& name ) const
    {
        for ( size_t poseIndex = 0; poseIndex < _listViseme.size(); ++poseIndex )
        {
            if ( _listViseme[poseIndex]._name == name )
                return static_cast<int32>( poseIndex );
        }
        return -1;
    }
} // namespace sw
