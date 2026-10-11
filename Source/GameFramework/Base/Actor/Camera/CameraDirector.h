/**
 * @file CameraDirector.h
 * @brief 매 프레임의 카메라 포즈를 내는 순수 상태 기계를 정의합니다.
 * @details 활성 프리셋, 모드 상태, 블렌드, 감쇠, 흔들림을 시간으로 굴린다. 컴포넌트와 씬은 모른다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/String/hashed_string.h"

#include "GameFramework/Base/Actor/Camera/CameraBlend.h"
#include "GameFramework/Base/Actor/Camera/CameraMode.h"
#include "GameFramework/Base/Actor/Camera/CameraPose.h"
#include "GameFramework/Base/Actor/Camera/CameraPreset.h"
#include "GameFramework/Base/Actor/Camera/CameraShake.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ICameraCollisionProbe;

    /**
     * @class CameraDirector
     * @brief 프리셋을 켜고(`activatePreset`) 입력을 넣고(`applyInput`) 시간을 흘리면(`step`), 모드, 감쇠, 블렌드, 흔들림을 거친 포즈를 줍니다.
     * @details 블렌드는 **지금 내보내는 포즈**에서 출발한다(`CameraPoseBlender`). Cinemachine 과 언리얼의 PlayerCameraManager 와 같은 방식이다.
     *          블렌드가 없던 때 켜면 나가는 프리셋도 대상을 계속 따라가며(살아 있는 쪽, 자기 모드 상태로) 섞인다.
     *          블렌드 도중에 다시 켜면 그 순간의 섞인 포즈를 고정해서 거기서 출발한다.
     *          어느 쪽이든 켠 순간 화면이 튀지 않는다.
     *          감쇠와 손떨림은 프리셋마다의 포즈에 걸린다. 나가는 쪽과 들어오는 쪽이 각자 건다.
     *          새로 켠 프리셋은 감쇠 없이 그 자리에서 시작한다.
     *          충격(`addImpulse`)은 섞인 포즈 위에 마지막으로 얹는다.
     *          처음 켠 프리셋(아직 낸 포즈가 없다)은 블렌드 없이 바로 붙는다.
     *          포즈를 내던 중의 컷은 `consumeCut` 이 한 번 알리며, 렌더러의 컷 신호로 쓴다.
     */
    class SW_GF_API CameraDirector
    {
    public:
        CameraDirector();

        /** @brief 카탈로그의 프리셋을 켭니다. 블렌드는 `catalog.getBlend( 지금 프리셋, id )` 로 구합니다. 없는 id 면 아무것도 바꾸지 않고 false 입니다. */
        [[nodiscard]] bool activatePreset( const CameraPresetCatalog& catalog, const hashed_string& id );
        /** @brief 카탈로그의 프리셋을 @p blend 로 켭니다. 블렌드 표는 무시합니다. */
        [[nodiscard]] bool activatePreset( const CameraPresetCatalog& catalog, const hashed_string& id, const BlendCurveDef& blend );
        /** @brief 정의를 그대로 켭니다. 카탈로그 밖의 프리셋(시퀀서나 코드로 만든 것)에 쓰며, 모드 상태는 새로 시작합니다. */
        void activatePreset( const CameraPresetDef& def, const BlendCurveDef& blend );
        /** @brief 켠 프리셋의 값만 바꿉니다. 블렌드와 모드 상태는 그대로 두며, 리그가 매 프레임 바뀌는 값(덮어쓴 탑승 시점)을 넣을 때 씁니다. */
        void refreshActivePreset( const CameraPresetDef& def ) { _activeDef = def; }

        /** @brief 이번 프레임의 입력을 켠 프리셋의 모드 상태에 넣습니다(`applyCameraInput`). */
        void applyInput( const CameraModeInput& input, float32 deltaTime );
        /** @brief 암 충돌 질의를 정합니다. 수명은 부르는 쪽이 쥐며, nullptr 이면 쓸어 보지 않습니다. */
        void setCollisionProbe( const ICameraCollisionProbe* pProbe ) { _pProbe = pProbe; }
        /** @brief 충격을 더합니다. 원점과 모양을 받으며, 듣는 자리는 카메라입니다. */
        void addImpulse( const CameraImpulseDef& def, const float3& origin ) { _impulseListener.addImpulse( def, origin ); }

        /** @brief 시간을 흘리고 이번 프레임의 포즈를 반환합니다. 켠 프리셋이 없으면 지난 포즈를 그대로 반환하며, 처음에는 기본값입니다. */
        const CameraPose& step( float32 deltaTime, const CameraTarget& target );

        const hashed_string&   getActivePresetID() const { return _activeDef._id; }
        const CameraPresetDef& getActivePreset() const { return _activeDef; }
        bool                   hasActivePreset() const { return _bHasActive == SW_TRUE; }
        bool                   isBlending() const { return _blender.isBlending(); }
        /** @brief 지금 블렌드의 가중치입니다. 0 이 나가는 쪽이고 1 이 들어오는 쪽이며, 블렌드 중이 아니면 1 입니다. */
        float32           getBlendWeight() const { return _blender.getWeight(); }
        const CameraPose& getPose() const { return _outputPose; }
        bool              hasPose() const { return _blender.hasPose(); }
        /** @brief 켠 프리셋의 모드 상태입니다. 입력이 돌린 각, 줌, 팬, 암 길이를 담습니다. */
        CameraModeState&       getModeState() { return _activeState; }
        const CameraModeState& getModeState() const { return _activeState; }
        /** @brief 포즈를 내던 중 컷으로 바꿨으면 true 입니다. 읽으면 지우므로, 카메라에 컷 표시를 넣는 쪽이 한 번만 읽습니다. */
        bool consumeCut() { return _blender.consumeCut(); }

    private:
        /** @brief 프리셋 하나를 이번 프레임 포즈로 풉니다. 모드, 감쇠(새로 켰으면 그 자리), 손떨림 순으로 처리합니다. */
        CameraPose evaluateLayer( const CameraPresetDef& def, CameraModeState& inoutState, CameraPose& inoutDampedPose, bool bFresh, float32 deltaTime,
                                  const CameraTarget& target ) const;

    private:
        CameraPresetDef              _activeDef;
        CameraPresetDef              _fromDef; ///< 살아 있는 나가는 쪽일 때만 쓴다
        CameraModeState              _activeState;
        CameraModeState              _fromState;
        CameraPoseBlender            _blender;
        CameraImpulseListener        _impulseListener;
        CameraPose                   _activePose; ///< 들어오는 쪽 — 감쇠를 거친 포즈(흔들림 전)
        CameraPose                   _fromPose;   ///< 나가는 쪽 — 감쇠를 거친 포즈(흔들림 전)
        CameraPose                   _outputPose;
        const ICameraCollisionProbe* _pProbe;
        uint8                        _bHasActive       : 1;
        uint8                        _bActivePoseFresh : 1; ///< 다음 `step` 이 감쇠 없이 그 자리에서 시작한다
        [[maybe_unused]] uint8       _reserved         : 6;
    };
} // namespace sw
