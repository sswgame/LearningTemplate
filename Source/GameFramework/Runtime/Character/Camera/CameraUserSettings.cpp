#include "pch.h"

#include "GameFramework/Runtime/Character/Camera/CameraUserSettings.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "Engine/UserSettings/UserSettingsManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        /** @brief 사용자 설정 변수 하나를 찾습니다. 엔진이 등록하는 변수라 없으면 등록이 빠진 결함입니다. */
        const GlobalVariableInfo& findCameraUserSettingVariable( GlobalVariableManager& variables, string_view name )
        {
            const GlobalVariableInfo* pVariable = variables.findVariable( name );
            if ( pVariable == nullptr )
                ::sw::internal::reportMissingRequiredService( name.data() ); // 등록이 빠진 결함 — Release 에서도 멈춘다
            return *pVariable;
        }
    } // namespace
} // namespace sw

namespace sw
{
    CameraUserSettings CameraUserSettings::makeFromGlobalVariables()
    {
        CameraUserSettings     settings;
        GlobalVariableManager* pVariables = game::findSubsystem<GlobalVariableManager>();
        if ( pVariables == nullptr )
            return settings;
        const GlobalVariableInfo& fieldOfView = findCameraUserSettingVariable( *pVariables, "gv_cameraFieldOfView" );
        const float32             reference   = std::get<float32>( fieldOfView._defaultValue ) * MathUtil::kDegreeToRadian;
        const float32             chosen      = fieldOfView.getValueAsFloat() * MathUtil::kDegreeToRadian;
        settings._fieldOfViewScale            = MathUtil::tan( chosen * 0.5f ) / MathUtil::tan( reference * 0.5f );
        settings._shakeScale                  = findCameraUserSettingVariable( *pVariables, "gv_cameraShakeScale" ).getValueAsFloat();
        SW_ASSERT( settings._shakeScale >= 0.0f ); // 범위(0 ~ 1)는 사용자 설정 스키마(`gameplay.cameraShake`)가 지킨다. 음수를 0 으로 맞춰 돌지 않는다
        settings._bHeadBob = findCameraUserSettingVariable( *pVariables, "gv_cameraHeadBob" ).getValueAsBool();
        return settings;
    }

    hashed_string CameraUserSettings::readChosenPreset()
    {
        GlobalVariableManager* pVariables = game::findSubsystem<GlobalVariableManager>();
        if ( pVariables == nullptr )
            return hashed_string{};
        return hashed_string( findCameraUserSettingVariable( *pVariables, "gv_cameraPreset" ).getValueAsString() );
    }

    void CameraUserSettings::saveChosenPreset( const hashed_string& id )
    {
        UserSettingsManager* pSettings = game::findSubsystem<UserSettingsManager>();
        if ( pSettings == nullptr )
            return;
        // 이 설정 하나만 확정한다 — 메뉴가 보류한 다른 값(확인을 기다리는 해상도)을 시점 키가 확정하지 않는다.
        const UserSettingSetResult result = pSettings->applyValue( hashed_string( "gameplay.cameraPreset" ), string_view( id.c_str() ) );
        const bool                 bTaken = result == UserSettingSetResult::Accepted || result == UserSettingSetResult::Unchanged;
        if ( bTaken == false ) // 엔진 스키마의 줄이다 — 받지 않으면 스키마와 이 코드가 어긋난 결함이다
            SW_LOG_ERROR_ID( "CameraUserSettings.ChosenPresetRejected", "User setting 'gameplay.cameraPreset' did not take '%#' (result %#)", id.c_str(), static_cast<uint32>( result ) );
    }

    CameraPose CameraUserSettings::applyFieldOfView( CameraPresetMode mode, const CameraPose& pose ) const
    {
        const bool bGameplayView = mode == CameraPresetMode::FirstPerson || mode == CameraPresetMode::ThirdPerson;
        const bool bUnchanged    = bGameplayView == false || pose._bOrthographic == SW_TRUE || _fieldOfViewScale == 1.0f;
        if ( bUnchanged )
            return pose;
        CameraPose scaled    = pose;
        scaled._fieldOfViewY = 2.0f * MathUtil::atan2( MathUtil::tan( pose._fieldOfViewY * 0.5f ) * _fieldOfViewScale, 1.0f );
        return scaled;
    }
} // namespace sw
