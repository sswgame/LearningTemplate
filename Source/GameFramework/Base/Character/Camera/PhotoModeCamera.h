/**
 * @file PhotoModeCamera.h
 * @brief 포토 모드의 자유 카메라(입력으로 움직이는 시점과 렌즈 값)를 계산하는 순수 로직을 정의합니다.
 * @details 2D(직교)와 3D(원근)가 같은 코어를 쓴다. 직교 시점은 이동과 줌(화면 높이)만 받고, 원근 시점은 둘러보기와 시야각 줌도 받는다.
 *          컴포넌트, 입력 맵, 시간 배율을 모른다. `CameraDirectorComponent` 가 입력을 모아 넣고 결과 포즈를 카메라에 쓴다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Math/Math.h"

#include "GameFramework/Base/Character/Camera/CameraPose.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 포토 모드 한 프레임의 입력입니다. 단위는 입력 맵 값 그대로입니다. */
    struct PhotoModeInput
    {
        float2  _move{};                ///< 화면 기준 수평 이동(x 는 오른쪽, y 는 앞)
        float32 _rise{ 0.0f };          ///< 위(+)와 아래(-) 이동. 직교 시점은 무시한다
        float2  _look{};                ///< 둘러보기 이동량(마우스 델타). 직교 시점은 무시한다
        float32 _roll{ 0.0f };          ///< 기울이기(+ 시계 반대 방향, 누른 동안). 직교 시점은 무시한다
        float32 _zoomNotches{ 0.0f };   ///< 휠 칸 수(위로 굴리면 +, 가까이)
        float32 _exposureSteps{ 0.0f }; ///< 노출 보정 단계 수(+ 는 밝게)
    };
} // namespace sw

namespace sw
{
    /** @brief 포토 모드의 조절 값입니다. 기본값은 일반적인 자유 카메라 감각입니다. */
    struct PhotoModeSettings
    {
        float32 _moveSpeed{ 5.0f };                ///< 원근 시점의 이동 속도(m/s)
        float32 _orthoPanScreensPerSecond{ 0.5f }; ///< 직교 시점의 이동 속도(초당 화면 높이의 몇 배)
        float32 _lookSensitivity{ 0.0025f };       ///< 둘러보기 감도(rad / 입력 단위)
        float32 _rollSpeed{ 1.0f };                ///< 기울이기 속도(rad/s)
        float32 _zoomStep{ 0.9f };                 ///< 휠 한 칸의 배율. 시야각이나 화면 높이에 곱하며, 1 보다 작으면 확대다
        float32 _minFieldOfViewY{ 0.17f };         ///< 최소 수직 시야각(rad, 약 10°)
        float32 _maxFieldOfViewY{ 1.75f };         ///< 최대 수직 시야각(rad, 약 100°)
        float32 _minOrthoHeight{ 1.0f };           ///< 최소 화면 높이(m)
        float32 _maxOrthoHeight{ 500.0f };         ///< 최대 화면 높이(m)
        float32 _exposureStep{ 0.25f };            ///< 노출 한 단계(EV)
        float32 _maxExposureOffset{ 4.0f };        ///< 노출 보정 한계(±EV)
        float32 _pitchLimit{ 1.55f };              ///< 피치 한계(±rad). 정수직을 넘어 뒤집히지 않게 한다
    };
} // namespace sw

namespace sw
{
    /**
     * @class PhotoModeCamera
     * @brief 켤 때의 포즈에서 시작해 입력으로 위치, 방향, 시야각(직교면 화면 높이), 노출 보정을 바꾸고, 바탕 포즈 위에 덮은 포즈를 냅니다.
     * @details 언리얼 포토 모드 플러그인 · 유니티 Cinemachine FreeLook 디버그 카메라와 같은 자유 시점이다.
     *          상태는 켬/끔 하나뿐이다. 끄면 값은 버리고, 바탕 포즈(디렉터가 낸 원래 포즈)는 건드리지 않았으므로 되돌릴 것이 없다.
     *          방향은 요, 피치, 롤이며, 앞은 요 0, 피치 0 에서 +Z 다(`CameraMode` 의 `makeFromYawPitchRoll` 과 같은 규약). 켤 때의 롤은 이어받아
     *          둘러보기가 지우지 않고, 기울이기 입력이 바꾼다(포토 모드의 더치 앵글).
     */
    class SW_GF_API PhotoModeCamera
    {
    public:
        PhotoModeCamera();

        /** @brief @p from 에서 시작합니다. 이미 켜 있으면 지금 포즈에서 다시 시작합니다. */
        void begin( const CameraPose& from );
        /** @brief 끕니다. 값은 버립니다. */
        void end();
        bool isActive() const { return _bActive; }

        /** @brief 입력을 @p deltaTime(게임이 멈춰도 흐르는 실제 경과 시간) 만큼 적용합니다. 꺼 있으면 아무 일도 하지 않습니다. */
        void applyInput( const PhotoModeInput& input, float32 deltaTime );
        /** @brief @p base 를 바탕으로 위치, 방향, 시야각이나 화면 높이를 덮고 노출 보정을 더한 포즈입니다. 꺼 있으면 @p base 그대로입니다. */
        CameraPose makePose( const CameraPose& base ) const;

        void                     setSettings( const PhotoModeSettings& settings ) { _settings = settings; }
        const PhotoModeSettings& getSettings() const { return _settings; }
        const float3&            getPosition() const { return _position; }
        float32                  getYaw() const { return _yaw; }
        float32                  getPitch() const { return _pitch; }
        float32                  getRoll() const { return _roll; }
        const quaternion&        getRotation() const { return _rotation; }
        float32                  getFieldOfViewY() const { return _fieldOfViewY; }
        float32                  getOrthoHeight() const { return _orthoHeight; }
        float32                  getExposureOffset() const { return _exposureOffset; }

    private:
        PhotoModeSettings _settings;
        quaternion        _rotation; ///< 지금 방향. 직교 시점은 켤 때 그대로 쓰고, 원근 시점은 둘러보거나 기울이면 요, 피치, 롤로 다시 만든다
        float3            _position;
        float32           _yaw;
        float32           _pitch;
        float32           _roll;
        float32           _fieldOfViewY;
        float32           _orthoHeight;
        float32           _exposureOffset;
        bool              _bOrthographic;
        bool              _bActive;
    };
} // namespace sw
