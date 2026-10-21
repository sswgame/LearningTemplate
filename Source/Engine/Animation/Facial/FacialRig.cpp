#include "pch.h"

#include "Engine/Animation/Facial/FacialRig.h"

#include "Core/Log/Logger.h"

#include "Engine/Animation/Skeletal/Skeleton.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Resource/ResourceIO.h"

namespace sw
{
    SW_LOG_CALLER( "FacialRig" );

    namespace
    {
        /** @brief 포즈 이름이 모두 비지 않았고 겹치지 않는지 봅니다. 어긋난 줄마다 오류를 남깁니다. */
        bool hasUniquePoseNames( const vector<FacialPose>& listPose, const utf8* pListName, string_view sourceLabel )
        {
            bool bValid = true;
            for ( size_t poseIndex = 0; poseIndex < listPose.size(); ++poseIndex )
            {
                const FacialPose& pose       = listPose[poseIndex];
                bool              bDuplicate = false;
                for ( size_t previousIndex = 0; previousIndex < poseIndex; ++previousIndex )
                {
                    if ( listPose[previousIndex]._name == pose._name )
                        bDuplicate = true;
                }
                if ( pose._name.empty() == false && bDuplicate == false )
                    continue;
                SW_LOG_ERROR( "Facial rig '%#': %# entry %# needs a unique, non-empty _name", sourceLabel, pListName, poseIndex );
                bValid = false;
            }
            return bValid;
        }
    } // namespace
} // namespace sw

namespace sw
{
    bool FacialRig::parseJSON( string_view json, string_view sourceLabel )
    {
        *this = FacialRig{};
        const string label{ sourceLabel };
        // 키는 대소문자까지 맞아야 하고, 모르는 키 · 읽지 못한 값 · 범위 밖 숫자는 오류다(`readConfigJSON` 이 로그로 알린다).
        if ( ConfigManager::readConfigJSON( *this, json, label.c_str() ) && validateFile( sourceLabel ) )
        {
            _gaze._forwardAxis.normalize();
            return true;
        }
        *this = FacialRig{};
        return false;
    }

    bool FacialRig::loadFromResource( string_view path )
    {
        string text;
        if ( ResourceIO::readTextResource( path, text ) == false )
        {
            SW_LOG_ERROR( "Facial rig '%#' could not be read", path );
            *this = FacialRig{};
            return false;
        }
        return parseJSON( text, path );
    }

    bool FacialRig::validateFile( string_view sourceLabel ) const
    {
        bool bValid = hasUniquePoseNames( _listExpression, "_listExpression", sourceLabel );
        if ( hasUniquePoseNames( _listViseme, "_listViseme", sourceLabel ) == false )
            bValid = false;
        const bool bInvalidBlinkSettings = _blink._minInterval <= 0.0f || _blink._maxInterval < _blink._minInterval || _blink._duration <= 0.0f;
        if ( bInvalidBlinkSettings )
        {
            SW_LOG_ERROR( "Facial rig '%#': _blink intervals must be positive and ordered, _duration positive", sourceLabel );
            bValid = false;
        }
        if ( _gaze._saccadeMinInterval <= 0.0f || _gaze._saccadeMaxInterval < _gaze._saccadeMinInterval )
        {
            SW_LOG_ERROR( "Facial rig '%#': _gaze saccade intervals must be positive and ordered", sourceLabel );
            bValid = false;
        }
        if ( _gaze._forwardAxis.dot( _gaze._forwardAxis ) <= 0.0f )
        {
            SW_LOG_ERROR( "Facial rig '%#': _gaze._forwardAxis must not be zero", sourceLabel );
            bValid = false;
        }
        return bValid;
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

namespace sw
{
    SW_ASSET_TYPE( FacialRig, FacialRig::kAssetType );
} // namespace sw
