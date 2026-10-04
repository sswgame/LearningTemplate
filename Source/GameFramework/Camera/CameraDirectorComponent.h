/**
 * @file CameraDirectorComponent.h
 * @brief 같은 오브젝트의 `CameraComponent` 를 데이터 프리셋 · 블렌드로 움직이는 컴포넌트입니다(Cinemachine Brain · 언리얼 PlayerCameraManager 의 자리).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/GameObjectHandle.h"
#include "Core/Container/string.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Camera/CameraBlend.h"
#include "GameFramework/Camera/CameraDirector.h"
#include "GameFramework/Camera/CameraPose.h"
#include "GameFramework/Camera/CameraPreset.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class CameraDirectorComponent
     * @brief 프리셋 카탈로그(`<CameraPresets>` XML)를 읽고, 켠 프리셋을 대상에 맞춰 풀어 블렌드 · 감쇠를 거쳐 같은 오브젝트의 카메라에 씁니다.
     * @details `TickGroup::PostUpdate` 에서 자기 오브젝트의 카메라만 쓰고, 대상 오브젝트는 트랜스폼을 읽기만 합니다(대상이 앞 그룹에서 움직인 뒤다).
     *          카메라 자리는 월드 값으로 씁니다(`setWorldTransform`). 블렌드 · 감쇠 계산은 `CameraDirector` 에 있어 씬 없이 시험합니다.
     *          켠 프리셋 · 블렌드 진행은 저장하지 않습니다 — 다시 읽으면 시작 프리셋에서 다시 시작합니다.
     */
    REFLECT( Category = "Camera", DisplayName = "Camera Director", Tooltip = "Drives the camera on this object from data presets with blends and damping" )
    class SW_GF_API CameraDirectorComponent : public Component
    {
    public:
        REFLECT_BODY();

        CameraDirectorComponent();
        virtual ~CameraDirectorComponent() override = default;

        void onBeginPlay() override;
        void onTick( float32 deltaTime ) override;

        /** @brief 프리셋을 카탈로그의 블렌드로 켭니다. 없는 id 면 false 입니다. */
        [[nodiscard]] bool activatePreset( const hashed_string& id );
        /** @brief 프리셋을 @p blend 로 켭니다(블렌드 표를 무시한다). */
        [[nodiscard]] bool activatePreset( const hashed_string& id, const CameraBlendSpec& blend );

        /** @brief 카메라가 따라갈 오브젝트입니다. 비우면 초점은 원점 · 요 · 피치 0 입니다. */
        void                    setTarget( const GameObjectHandle& target ) { _target = target; }
        const GameObjectHandle& getTarget() const { return _target; }

        const hashed_string& getActivePresetId() const { return _director.getActivePresetId(); }
        bool                 isBlending() const { return _director.isBlending(); }
        const CameraPose&    getPose() const { return _director.getPose(); }
        /** @brief 카탈로그입니다. 코드로 프리셋을 더하거나 시험이 XML 글을 읽을 때 씁니다(경로 PROPERTY 가 비어 있으면 시작 때 다시 읽지 않는다). */
        CameraPresetCatalog&       getCatalog() { return _catalog; }
        const CameraPresetCatalog& getCatalog() const { return _catalog; }

        /** @brief 시간을 흘려 포즈를 구하고 같은 오브젝트의 카메라에 씁니다(틱 밖에서 부르면 바로 보인다). */
        void updateCamera( float32 deltaTime );

    private:
        CameraTarget computeTarget() const;
        void         applyToCamera( const CameraPose& pose ) const;

    private:
        PROPERTY( Category = "Camera", DisplayName = "Presets", AssetPath, Tooltip = "Camera preset XML (<CameraPresets>)" )
        string _catalogPath;
        PROPERTY( Category = "Camera", DisplayName = "Initial Preset", Tooltip = "Preset active at begin play; empty uses the first preset" )
        hashed_string _initialPreset;
        PROPERTY( Category = "Camera", DisplayName = "Target", Tooltip = "Object the presets follow (focus = its world position, yaw / pitch = its facing)" )
        GameObjectHandle _target;

        CameraPresetCatalog _catalog;
        CameraDirector      _director;
    };
} // namespace sw
