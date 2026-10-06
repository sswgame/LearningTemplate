/**
 * @file CameraMode.h
 * @brief 카메라 모드의 계산 — 입력을 상태로(`applyCameraInput`), 프리셋 · 대상 · 상태를 포즈로(`evaluateCameraMode`) 바꿉니다.
 * @details 컴포넌트 · 씬을 모르는 순수 계산이라 씬 없이 시험합니다. 모드는 `CameraPresetMode` 하나에 프리셋의 섹션(입력 · 제약 · 프레이밍 · 충돌 ·
 *          훑기)이 얹히고, 프레임 사이에 남는 것(입력이 돌린 각 · 줌 · 팬, 스프링 암 길이, 조준, look-ahead)은 `CameraModeState` 에 있습니다.
 *          직교 리그(`OrthoCameraRigComponent`) · 1인칭(`FirstPersonCameraComponent`) · 디렉터(`CameraDirectorComponent`)가 같은 계산을 부릅니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/Base/Camera/CameraPose.h"
#include "GameFramework/Base/Camera/CameraPreset.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ICameraCollisionProbe;

    /** @brief 이번 프레임의 카메라 입력입니다. 읽는 쪽(`InputManager` 를 보는 컴포넌트)이 채우고, 모드는 프리셋의 `<Input>` 이 받는 것만 씁니다. */
    struct CameraModeInput
    {
        float2  _lookDelta{};           ///< 마우스 이동(픽셀, +y 는 화면 아래)
        float2  _pan{};                 ///< 앞 · 오른쪽 축(−1..1)
        float32 _zoomNotches{ 0.0f };   ///< 휠 칸(+ 는 확대)
        int32   _rotateSteps{ 0 };      ///< 회전 키(E +1, Q −1)
        uint8   _bLookHeld{ SW_FALSE }; ///< 시점 돌리기 버튼(오른쪽)을 누르고 있다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 모드가 프레임 사이에 들고 있는 것입니다. 새 프리셋을 켜면 새로 시작합니다(`reset`).
     * @details 요 · 피치 오프셋은 입력이 돌린 것이고(프리셋 각에 더한다), 줌은 0 이면 프리셋 값(원근은 거리, 직교는 높이)입니다.
     */
    struct CameraModeState
    {
        float3  _panOffset{};            ///< 이동 키가 옮긴 초점(월드)
        float3  _previousFocus{};        ///< 지난 프레임의 대상 초점 — look-ahead 의 속도
        float3  _lookAhead{};            ///< 부드럽게 따라가는 look-ahead 오프셋(월드)
        float32 _yawOffset{ 0.0f };      ///< 마우스가 돌린 요(rad)
        float32 _pitchOffset{ 0.0f };    ///< 마우스가 돌린 피치(rad)
        float32 _rotateYaw{ 0.0f };      ///< 회전 키가 정한 요(rad)
        float32 _rotateYawShown{ 0.0f }; ///< 화면에 보이는 회전 요 — `_rotateYaw` 를 `rotateTime` 으로 따라간다
        float32 _zoom{ 0.0f };           ///< 거리(원근) · 직교 높이. 0 이면 프리셋 값
        float32 _time{ 0.0f };           ///< 이 모드로 흐른 시간(훑기 · 잡음)
        float32 _armLength{ -1.0f };     ///< 스프링 암의 지금 길이. 음수면 아직 없음
        float32 _aimYaw{ 0.0f };         ///< 프레이밍이 정한 조준 요
        float32 _aimPitch{ 0.0f };       ///< 프레이밍이 정한 조준 피치
        uint8   _bAimValid{ SW_FALSE };
        uint8   _bHasPreviousFocus{ SW_FALSE };
        uint8   _bArmBlocked{ SW_FALSE }; ///< 지난 평가에서 암이 막혀 당겨졌다(진단 · 시험)

        /** @brief 새로 시작합니다(프리셋을 켤 때). */
        void reset() { *this = CameraModeState{}; }
    };

    /**
     * @brief 입력을 상태에 더합니다. 프리셋의 `<Input>` 이 받는 것만 쓰고 `<Confiner>` 의 피치 · 줌 범위로 자릅니다.
     * @details 마우스 시점의 피치는 화면 아래로 끌면 아래(+)입니다. 휠 한 칸은 줌에 `zoomStep`(확대) 또는 1/`zoomStep`(축소)을 곱합니다. 이동은 지금
     *          보이는 요 기준이고 직교 시점은 화면 높이에 비례합니다(확대할수록 느리다 — 화면에서 보이는 빠르기가 같다).
     */
    SW_GF_API void applyCameraInput( const CameraPresetDef& def, const CameraModeInput& input, float32 deltaTime, CameraModeState& inoutState );

    /**
     * @brief 프리셋을 대상 · 상태로 풀어 포즈를 냅니다(감쇠 · 블렌드 · 흔들림 전). @p pProbe 가 있고 `<Collision>` 이 켜져 있으면 암을 쓸어 봅니다.
     * @details 시간(@p deltaTime)으로 상태의 시간 · look-ahead · 회전 요 따라가기 · 암 회복 · 조준 감쇠가 흐릅니다. 순서: 줌 · 그룹 맞추기 → 각
     *          (프리셋 + 입력 + 훑기, 피치는 제약으로 자름) → 피벗(모드별, look-ahead · 팬 · 직교 초점 상자) → 자리(피벗에서 거리만큼 뒤) → 스프링 암 →
     *          프레이밍(조준) → 카메라 상자.
     */
    SW_GF_API CameraPose evaluateCameraMode( const CameraPresetDef& def, const CameraTarget& target, float32 deltaTime, CameraModeState& inoutState,
                                             const ICameraCollisionProbe* pProbe );

    /** @brief 프리셋을 대상에 맞춰 상태 없이 풉니다(입력 · 암 · 프레이밍 상태 없음, 시간 0). 순수 함수입니다. */
    SW_GF_API CameraPose evaluatePreset( const CameraPresetDef& def, const CameraTarget& target );

    /** @brief 점 여럿을 담는 대상입니다 — 초점은 점들을 감싸는 상자의 가운데, 반지름은 그 가운데에서 가장 먼 점까지입니다. 점이 없으면 기본값입니다. */
    SW_GF_API CameraTarget makeGroupCameraTarget( const float3* pPoint, uint32 pointCount );

    /** @brief @p from 에서 @p to 를 보는 요 · 피치(rad, 피치는 + 가 아래)입니다. 같은 점이면 0 입니다. */
    SW_GF_API float2 computeLookAngles( const float3& from, const float3& to );
} // namespace sw
