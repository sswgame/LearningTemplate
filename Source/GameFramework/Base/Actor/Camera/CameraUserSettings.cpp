#include "pch.h"

#include "GameFramework/Base/Actor/Camera/CameraUserSettings.h"

#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

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
        settings._shakeScale                  = MathUtil::max( 0.0f, findCameraUserSettingVariable( *pVariables, "gv_cameraShakeScale" ).getValueAsFloat() );
        settings._bHeadBob                    = findCameraUserSettingVariable( *pVariables, "gv_cameraHeadBob" ).getValueAsBool();
        return settings;
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
