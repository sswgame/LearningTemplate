/**
 * @file CameraDirectorComponentWatches.cpp
 * @brief `CameraDirectorComponent` 의 자동화 감시 값입니다. 개발 전용 모듈(GameFrameworkDev)이라 배포에는 없다.
 */
#include "pch.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Math/MathUtil.h"
#include "Core/Math/Ray.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Automation/Watch/AutomationWatchComponent.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Config/GameTimeScale.h"
#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Input/IInputCaptureQuery.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/CommonInputActions.h"
#include "Engine/Input/Map/GameInputView.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/ComponentRegistry.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/FormatDesc.h"
#include "Engine/Serialization/Serializer/Archive.h"

#include "GameFramework/Runtime/Character/Camera/CameraCollisionProbe.h"
#include "GameFramework/Runtime/Character/Camera/CameraDirectorComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraManagerComponent.h"
#include "GameFramework/Runtime/Character/Camera/CameraPose.h"
#include "GameFramework/Runtime/Character/Camera/CameraSystem.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Foundation/Subsystem/GameSubsystems.h"

namespace sw
{
    namespace
    {
        struct CameraDirectorWatchDevInternal
        {
            static const CameraDirectorComponent* findDirector( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                return query.findTargetComponent<CameraDirectorComponent>( "camera director", outValue );
            }

            [[nodiscard]] static bool readActivePreset( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CameraDirectorComponent* pDirector = findDirector( query, outValue );
                if ( pDirector == nullptr )
                    return false;
                outValue.setText( pDirector->getActivePresetID().empty() ? "none" : pDirector->getActivePresetID().c_str() );
                return true;
            }

            [[nodiscard]] static bool readSelectedPreset( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CameraDirectorComponent* pDirector = findDirector( query, outValue );
                if ( pDirector == nullptr )
                    return false;
                outValue.setText( pDirector->getSelectedPresetID().empty() ? "none" : pDirector->getSelectedPresetID().c_str() );
                return true;
            }

            [[nodiscard]] static bool readBlendWeight( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CameraDirectorComponent* pDirector = findDirector( query, outValue );
                if ( pDirector == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( pDirector->getBlendWeight() ) );
                return true;
            }

            [[nodiscard]] static bool readFieldOfView( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CameraDirectorComponent* pDirector = findDirector( query, outValue );
                if ( pDirector == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( MathUtil::toDegree( pDirector->getPose()._fieldOfViewY ) ) );
                return true;
            }

            /** @brief 포즈 자리에서 대상 오브젝트 자리까지의 거리(m)입니다. 1인칭은 0 에 가깝고 3인칭은 암 길이다. 대상이 없으면 까닭을 적고 false 입니다. */
            [[nodiscard]] static bool readTargetDistance( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const CameraDirectorComponent* pDirector = findDirector( query, outValue );
                if ( pDirector == nullptr )
                    return false;
                const GameObject*     pTarget = query._pManager->resolveGameObject( pDirector->getTarget() );
                const SceneComponent* pScene  = pTarget != nullptr ? pTarget->getComponent<SceneComponent>() : nullptr;
                if ( pScene == nullptr )
                {
                    outValue.setText( "the camera director has no target with a scene component" );
                    return false;
                }
                outValue.setNumber( static_cast<float64>( ( pDirector->getPose()._position - pScene->getWorldMatrix().getTranslation() ).getLength() ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( cameraDirectorActivePreset, "CameraDirector.ActivePreset", "Preset the target's camera director shows (selected preset with contexts applied); none without one",
                               &CameraDirectorWatchDevInternal::readActivePreset, true, false );
    SW_AUTOMATION_QUERY_WATCH( cameraDirectorSelectedPreset, "CameraDirector.SelectedPreset", "Preset the player or code chose on the target's camera director (before contexts); none without one",
                               &CameraDirectorWatchDevInternal::readSelectedPreset, true, false );
    SW_AUTOMATION_QUERY_WATCH( cameraDirectorBlendWeight, "CameraDirector.BlendWeight", "Blend weight of the target's camera director toward its active preset (1 when not blending)",
                               &CameraDirectorWatchDevInternal::readBlendWeight, true, false );
    SW_AUTOMATION_QUERY_WATCH( cameraDirectorFieldOfView, "CameraDirector.FieldOfView", "Vertical field of view of the target's camera director pose in degrees (blended)",
                               &CameraDirectorWatchDevInternal::readFieldOfView, true, false );
    SW_AUTOMATION_QUERY_WATCH( cameraDirectorTargetDistance, "CameraDirector.TargetDistance", "Distance in m from the target's camera director pose to the object it follows (0 in first person, the arm in third person)",
                               &CameraDirectorWatchDevInternal::readTargetDistance, true, false );
} // namespace sw
