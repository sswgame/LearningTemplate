/**
 * @file CameraMode.h
 * @brief 카메라 모드의 계산을 정의합니다.
 * @details 입력을 상태로 바꾸고(`applyCameraInput`), 프리셋과 대상과 상태를 포즈로 바꾼다(`evaluateCameraMode`).
 *          컴포넌트와 씬을 모르는 순수 계산이라 씬 없이 테스트할 수 있다.
 *          모드는 `CameraPresetMode` 하나이고 거기에 프리셋의 섹션(입력, 제약, 프레이밍, 충돌, 훑기)이 얹힌다.
 *          프레임 사이에 남는 것(입력이 돌린 각, 줌, 팬, 스프링 암 길이, 조준, look-ahead)은 `CameraModeState` 에 있다.
 *          1인칭(`FirstPersonCameraComponent`), 디렉터(`CameraDirectorComponent`)가 같은 계산을 부른다. 직교 시점도 디렉터의 모드(`OrthoTopDown`)다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/Base/Actor/Camera/CameraPose.h"
#include "GameFramework/Base/Actor/Camera/CameraPreset.h"
#include "GameFramework/Base/Actor/Camera/CameraShake.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    struct GameRay;

    class ICameraCollisionProbe;

    /** @brief 이번 프레임의 카메라 입력입니다. `InputManager` 를 보는 컴포넌트가 채우고, 모드는 프리셋의 `<Input>` 이 받는 것만 씁니다. */
    struct CameraModeInput
    {
        float2  _lookDelta{};           ///< 시점 이동량(`Camera.Look` — 마우스면 픽셀, +y 는 화면 아래)
        float2  _pan{};                 ///< 앞 · 오른쪽 축(−1..1)
        float32 _zoomNotches{ 0.0f };   ///< 확대 칸(`Camera.Zoom` — 휠 칸 · 패드 버튼 한 번 · 스틱 한 축 × 초당 칸 × 프레임 시간(`<stick axis="Y" scaleByDelta>`), + 는 확대)
        int32   _rotateSteps{ 0 };      ///< 회전 키(E +1, Q −1)
        uint8   _bLookHeld{ SW_FALSE }; ///< 시점 돌리기 버튼(오른쪽)을 누르고 있다
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 모드가 프레임 사이에 들고 있는 상태입니다. 새 프리셋을 켜면 새로 시작합니다(`reset`).
     * @details 요와 피치 오프셋은 입력이 돌린 것이며 프리셋 각에 더한다. 줌이 0 이면 프리셋 값(원근은 거리, 직교는 높이)을 쓴다.
     */
    struct CameraModeState
    {
        SplinePath _dollyPath{};            ///< 돌리 레일 곡선 — 처음 풀 때 `<Dolly>` 의 점으로 한 번 만든다
        float3     _panOffset{};            ///< 이동 키가 옮긴 초점(월드)
        float3     _previousFocus{};        ///< 지난 프레임의 대상 초점 — look-ahead 의 속도
        float3     _lookAhead{};            ///< 부드럽게 따라가는 look-ahead 오프셋(월드)
        float3     _bobPreviousFocus{};     ///< 머리 흔들림이 지난 평가에 본 초점 — 수평으로 간 거리
        float32    _bobPhase{ 0.0f };       ///< 머리 흔들림 위상(rad). 간 거리로 간다
        float32    _bobWeight{ 0.0f };      ///< 머리 흔들림 크기 비율(0..1). 빠르기를 따라간다
        float32    _bobDistance{ 0.0f };    ///< 시간이 흐르지 않은 평가(틱 밖에서 눈 자리를 넣은 것)까지 모은, 아직 빠르기로 바꾸지 않은 거리
        float32    _yawOffset{ 0.0f };      ///< 마우스가 돌린 요(rad)
        float32    _pitchOffset{ 0.0f };    ///< 마우스가 돌린 피치(rad)
        float32    _rotateYaw{ 0.0f };      ///< 회전 키가 정한 요(rad)
        float32    _rotateYawShown{ 0.0f }; ///< 화면에 보이는 회전 요 — `_rotateYaw` 를 `rotateTime` 으로 따라간다
        float32    _zoom{ 0.0f };           ///< 거리(원근) · 직교 높이. 0 이면 프리셋 값
        float32    _time{ 0.0f };           ///< 이 모드로 흐른 시간(훑기 · 잡음)
        float32    _armLength{ -1.0f };     ///< 스프링 암의 지금 길이. 음수면 아직 없음
        float32    _aimYaw{ 0.0f };         ///< 프레이밍이 정한 조준 요
        float32    _aimPitch{ 0.0f };       ///< 프레이밍이 정한 조준 피치
        uint8      _bAimValid{ SW_FALSE };
        uint8      _bHasPreviousFocus{ SW_FALSE };
        uint8      _bArmBlocked{ SW_FALSE }; ///< 지난 평가에서 암이 막혀 당겨졌다(진단 · 테스트)
        uint8      _bDollyPathBuilt{ SW_FALSE };
        uint8      _bHasBobFocus{ SW_FALSE };

        /** @brief 새로 시작합니다. 프리셋을 켤 때 부릅니다. */
        void reset() { *this = CameraModeState{}; }
    };

    /**
     * @brief 입력을 상태에 더합니다. 프리셋의 `<Input>` 이 받는 것만 쓰고 `<Confiner>` 의 피치와 줌 범위로 자릅니다.
     * @details 마우스 시점의 피치는 화면 아래로 끌면 아래(+)이다.
     *          휠 한 칸은 줌에 `zoomStep`(확대) 또는 1/`zoomStep`(축소)을 곱한다.
     *          이동은 지금 보이는 요 기준이고, 직교 시점은 화면 높이에 비례한다. 확대할수록 느려져서 화면에서 보이는 빠르기가 같다.
     */
    SW_GF_API void applyCameraInput( const CameraPresetDef& def, const CameraModeInput& input, float32 deltaTime, CameraModeState& inoutState );

    /**
     * @brief 프리셋을 대상과 상태로 풀어 포즈를 냅니다. 감쇠, 블렌드, 흔들림을 적용하기 전의 포즈입니다.
     * @details @p pProbe 가 있고 `<Collision>` 이 켜져 있으면 암을 쓸어 본다.
     *          시간(@p deltaTime)에 따라 상태의 시간, look-ahead, 회전 요 따라가기, 암 회복, 조준 감쇠가 흐른다.
     *          계산 순서는 다음과 같다.
     *          줌과 그룹 맞추기, 각(프리셋과 입력과 훑기를 더하고 피치는 제약으로 자름), 피벗(모드별로 look-ahead, 팬, 직교 초점 상자),
     *          자리(피벗에서 거리만큼 뒤), 스프링 암, 프레이밍(조준), 카메라 상자 순이다.
     */
    SW_GF_API CameraPose evaluateCameraMode( const CameraPresetDef& def, const CameraTarget& target, float32 deltaTime, CameraModeState& inoutState,
                                             const ICameraCollisionProbe* pProbe );

    /**
     * @brief 머리 흔들림(`<HeadBob>`)을 대상 초점 @p focus 의 수평 이동으로 풀어 오프셋을 냅니다. 진폭이 0 이면 0 이고 상태도 건드리지 않습니다.
     * @details 위상은 간 거리로, 크기는 빠르기(모은 거리 ÷ @p deltaTime)로 간다. @p deltaTime 이 0 인 평가(틱 밖에서 눈 자리를 넣은 것)는 거리만 모으고,
     *          다음에 시간이 흐른 평가가 그 거리로 빠르기를 구한다. 그래서 같은 프레임에 여러 번 불러도 위상이 겹쳐 가지 않는다.
     */
    SW_GF_API CameraShakeOffset computeCameraHeadBob( const CameraHeadBobDef& headBob, const float3& focus, float32 deltaTime, CameraModeState& inoutState );

    /** @brief 프리셋을 대상에 맞춰 상태 없이 풉니다. 입력, 암, 프레이밍 상태가 없고 시간은 0 입니다. 순수 함수입니다. */
    SW_GF_API CameraPose evaluatePreset( const CameraPresetDef& def, const CameraTarget& target );

    /** @brief 점 여럿을 담는 대상을 만듭니다. 초점은 점들을 감싸는 상자의 가운데이고, 반지름은 그 가운데에서 가장 먼 점까지입니다. 점이 없으면 기본값입니다. */
    SW_GF_API CameraTarget makeGroupCameraTarget( const float3* pPoint, uint32 pointCount );

    /** @brief @p from 에서 @p to 를 보는 요와 피치(rad, 피치는 +가 아래)입니다. 같은 점이면 0 입니다. */
    SW_GF_API float2 computeLookAngles( const float3& from, const float3& to );

    /**
     * @brief 포즈의 화면 점을 지나는 광선입니다. 화면 점 @p mouseNormalized 는 왼쪽 위가 (0, 0), 오른쪽 아래가 (1, 1) 입니다.
     * @details 직교 포즈는 광선이 모두 카메라 앞 방향과 나란하고 시작점이 화면 평면 위에서 옮겨진다. 원근 포즈는 카메라 자리에서 시야각만큼 벌어진다.
     *          마우스 고르기(땅 점 찾기)에 쓴다. 디렉터가 지금 낸 포즈를 넘기면 블렌드 중에도 화면에 보이는 그대로 고른다.
     * @param aspectRatio 화면의 가로/세로이다(`CameraPoseUtil::computeAspectRatio`).
     */
    SW_GF_API GameRay computeCameraScreenRay( const CameraPose& pose, float32 aspectRatio, const float2& mouseNormalized );
} // namespace sw
