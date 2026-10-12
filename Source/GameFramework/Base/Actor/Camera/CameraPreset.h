/**
 * @file CameraPreset.h
 * @brief 데이터로 적는 카메라 프리셋과 그 프리셋을 읽는 `CameraPresetCatalog` 를 정의합니다.
 * @details 프리셋은 XML 자식 원소 하나가 섹션 하나이다.
 *          섹션은 시점(`<View>`), 렌즈(`<Lens>`), 감쇠(`<Damping>`), 들어오기 블렌드(`<BlendIn>`), 입력(`<Input>`), 제약(`<Confiner>`),
 *          프레이밍(`<Framing>`), 충돌(`<Collision>`), 손떨림(`<Noise>`), 훑기(`<Sweep>`)가 있다.
 *          섹션을 더하려면 구조체 하나와 원소 하나를 더하면 된다.
 *          프리셋 사이의 블렌드 표도 여기서 읽는다. 프리셋을 포즈로 푸는 계산은 `CameraMode.h` 에 있다.
 *          Cinemachine 의 가상 카메라(Body, Aim, Lens, Noise, Confiner, Deoccluder, Composer)와 Custom Blends, 언리얼의 카메라 모드와 SpringArm 을 참고했다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Actor/Camera/CameraBlend.h"
#include "GameFramework/Base/Actor/Camera/CameraPose.h"
#include "GameFramework/Base/Foundation/Data/GameCatalog.h"
#include "GameFramework/Base/Foundation/Data/XMLCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XMLNode;

    /** @brief 프리셋이 카메라를 놓는 방식입니다. */
    ENUM()
    enum class CameraPresetMode : uint8
    {
        Fixed = 0,    ///< 오프셋이 곧 월드 자리(CCTV). 회전은 요 · 피치, 또는 `aim` 이 고른 점 · 대상을 본다. `<Sweep>` 으로 좌우로 훑는다
        OrthoTopDown, ///< 월드 고정 요 · 피치로 초점 둘레에서 내려다보는 직교 시점(렌즈가 무엇이든 직교)
        Orbit,        ///< 월드 고정 요 · 피치로 초점 둘레에서 초점을 본다(입력으로 돌리고 당긴다)
        Follow,       ///< 대상의 요에 더한 요로 대상 뒤에서 초점을 본다
        FirstPerson,  ///< 눈 = 대상 + 대상 요로 돌린 오프셋, 회전 = 대상 요 · 피치 + 프리셋 요 · 피치
        ThirdPerson,  ///< 어깨 너머 — 대상의 시점(요 · 피치)을 따라 어깨 피벗(대상 요로 돌린 오프셋)에서 거리만큼 물러난다. 스프링 암(`<Collision>`)이 벽 앞으로 당긴다
        FollowXY,     ///< 2D(XY 평면, +Y 가 위) — 초점의 X · Y 에 오프셋 X · Y 를 더한 자리, Z 는 오프셋 Z(카메라 평면) 그대로. 회전은 요 · 피치(0 이면 +Z 를 본다). 따라가는 빠르기는 `<Damping>`
    };
} // namespace sw

namespace sw
{
    /** @brief `Fixed` 프리셋이 무엇을 보는지입니다. */
    ENUM()
    enum class CameraAimMode : uint8
    {
        Angles = 0, ///< 요 · 피치 그대로
        Point,      ///< `lookAt` 의 월드 점
        Target,     ///< 대상(초점)
    };
} // namespace sw

namespace sw
{
    /** @brief 시점 섹션(`<View>`)입니다. 각은 라디안이고 XML 에는 도로 적습니다. 피치는 +가 아래를 보는 쪽이며 `CameraComponent::lookAt` 과 같습니다. */
    REFLECT()
    struct SW_GF_API CameraViewDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Pivot offset from the focus (Fixed: world position, FirstPerson / ThirdPerson / Follow: offset in the target's yaw frame)", Units = m )
        float3 _offset{};
        PROPERTY( Tooltip = "World point a Fixed camera looks at when Aim is Point", Units = m )
        float3 _lookAt{};
        PROPERTY( Tooltip = "Downward viewing angle", Units = rad )
        float32 _pitch{ 0.0f };
        PROPERTY( Tooltip = "Angle from +Z towards +X (Follow / FirstPerson / ThirdPerson: added to the target's yaw)", Units = rad )
        float32 _yaw{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Distance from the pivot to the camera", Units = m )
        float32 _distance{ 10.0f };
        PROPERTY()
        CameraPresetMode _mode{ CameraPresetMode::Orbit };
        PROPERTY( Tooltip = "What a Fixed camera looks at" )
        CameraAimMode _aim{ CameraAimMode::Angles };
    };
} // namespace sw

namespace sw
{
    /** @brief 렌즈 섹션(`<Lens>`)입니다. */
    REFLECT()
    struct SW_GF_API CameraLensDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.1, Max = 3.14, Tooltip = "Vertical field of view", Units = rad )
        float32 _fieldOfViewY{ 0.70f };
        PROPERTY( Min = 0.1, Tooltip = "Visible height of the orthographic view", Units = m )
        float32 _orthoHeight{ 10.0f };
        PROPERTY( Min = 0.01, Units = m )
        float32 _nearPlane{ 0.1f };
        PROPERTY( Min = 1.0, Units = m )
        float32 _farPlane{ 100.0f };
        PROPERTY()
        bool _bOrthographic{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 감쇠 섹션(`<Damping>`)입니다. 값은 시간 상수(초)이며, 남은 차이가 이 시간마다 1/e 로 줄고 0 이면 바로 붙습니다. 렌즈는 감쇠하지 않습니다. */
    REFLECT()
    struct SW_GF_API CameraDampingDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Tooltip = "Time constant of the position lag; 0 snaps", Units = s )
        float32 _positionTime{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Time constant of the orientation lag; 0 snaps", Units = s )
        float32 _orientationTime{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 입력 섹션(`<Input>`)입니다. 모드가 입력을 어떻게 받는지 정하며, 값이 0 인 칸은 그 입력을 받지 않습니다.
     * @details 마우스 시점은 요와 피치 오프셋에 더한다(1인칭, 3인칭, 궤도).
     *          휠은 원근 시점에서는 거리를, 직교 시점에서는 직교 높이를 배수로 바꾼다.
     *          이동 키는 초점을 요 기준으로 옮긴다.
     *          회전 키(Q/E)는 요를 한 칸씩 돌리고, `rotateTime` 으로 부드럽게 따라간다.
     *          값의 범위는 `<Confiner>` 가 자른다.
     */
    REFLECT()
    struct SW_GF_API CameraInputDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Tooltip = "Radians per pixel of mouse movement; 0 ignores the mouse" )
        float32 _lookSensitivity{ 0.0f };
        PROPERTY( Min = 0.0, Max = 0.99, Tooltip = "Distance / ortho height factor per wheel notch (below 1); 0 ignores the wheel" )
        float32 _zoomStep{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Focus pan speed with WASD / arrows; 0 ignores them", Units = "m/s" )
        float32 _panSpeed{ 0.0f };
        PROPERTY( Tooltip = "Yaw change per Q/E press; 0 ignores them", Units = rad )
        float32 _rotateStep{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Time constant the shown yaw follows a Q/E step with", Units = s )
        float32 _rotateTime{ 0.15f };
        PROPERTY( Tooltip = "Mouse turns the view only while the right button is held (orbit drag)" )
        bool _bLookWhileHeld{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 제약 섹션(`<Confiner>`)입니다. Cinemachine 의 Confiner 와 언리얼의 피치 한계에 해당합니다.
     * @details 피치와 줌은 입력이 넘지 못하는 범위이다. 상자는 카메라 자리를 그 안에 둔다. 직교 시점에서는 초점의 X, Z 를 둔다.
     */
    REFLECT()
    struct SW_GF_API CameraConfinerDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Lowest pitch (up is negative)", Units = rad )
        float32 _pitchMin{ -MathUtil::kHalfPi };
        PROPERTY( Tooltip = "Highest pitch (down is positive)", Units = rad )
        float32 _pitchMax{ MathUtil::kHalfPi };
        PROPERTY( Min = 0.0, Tooltip = "Closest distance / smallest ortho height; 0 has no limit", Units = m )
        float32 _zoomMin{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Farthest distance / largest ortho height; 0 has no limit", Units = m )
        float32 _zoomMax{ 0.0f };
        PROPERTY( Tooltip = "Lowest corner of the box the camera stays in", Units = m )
        float3 _boundsMin{};
        PROPERTY( Tooltip = "Highest corner of the box the camera stays in", Units = m )
        float3 _boundsMax{};
        PROPERTY( Tooltip = "Keep the camera (ortho: the focus X and Z) inside the box" )
        bool _bBounds{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 프레이밍 섹션(`<Framing>`)입니다. Cinemachine 의 Composer 와 Group Framing 에 해당합니다.
     * @details 대상이 화면의 어디에 오는지, 얼마나 움직여야 카메라가 돌아가는지(데드존과 소프트존)를 정한다.
     *          움직이는 쪽을 미리 보는 look-ahead 와, 여러 대상을 다 담도록 그룹 반지름에 맞춰 물러나는 동작도 여기서 정한다.
     *          화면 위치와 존은 화면 비율(0..1, 왼쪽 위가 0)이다.
     *          조준은 대상을 보는 모드(궤도, 따라가기, 대상을 보는 고정)만 쓰고, 그룹 맞추기는 거리를 가진 모드와 직교 시점이 쓴다.
     */
    REFLECT()
    struct SW_GF_API CameraFramingDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Where the target sits on screen (0..1, top-left origin)" )
        float2 _screenPosition{ 0.5f, 0.5f };
        PROPERTY( Tooltip = "Screen fraction around the screen position where the target moves without turning the camera" )
        float2 _deadZone{ 0.0f, 0.0f };
        PROPERTY( Tooltip = "Screen fraction the target never leaves; outside the dead zone the camera turns with the damping" )
        float2 _softZone{ 0.8f, 0.8f };
        PROPERTY( Min = 0.0, Tooltip = "Time constant of turning towards the target outside the dead zone", Units = s )
        float32 _damping{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Aim at where the target will be after this time", Units = s )
        float32 _lookAheadTime{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Time constant smoothing the look-ahead", Units = s )
        float32 _lookAheadSmoothing{ 0.2f };
        PROPERTY( Min = 0.0, Tooltip = "Back off so the target group radius times this fits on screen; 0 keeps the distance" )
        float32 _groupPadding{ 0.0f };
        PROPERTY( Tooltip = "Turn the camera to keep the target at the screen position" )
        bool _bCompose{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 충돌 섹션(`<Collision>`)입니다. 언리얼의 SpringArm 과 Cinemachine 의 Deoccluder 에 해당합니다.
     * @details 피벗에서 카메라까지 구를 쓸어 막히면 그 앞으로 당기고, 막힘이 풀리면 `recoverTime` 에 걸쳐 천천히 돌아간다.
     *          당길 때는 바로 당긴다. 벽 안이 한 프레임도 보이지 않게 하려는 것이다.
     */
    REFLECT()
    struct SW_GF_API CameraCollisionDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Tooltip = "Radius of the probe sphere", Units = m )
        float32 _radius{ 0.2f };
        PROPERTY( Min = 0.0, Tooltip = "Closest the arm pulls in", Units = m )
        float32 _minDistance{ 0.3f };
        PROPERTY( Min = 0.0, Tooltip = "Time constant of easing back out once the way is clear", Units = s )
        float32 _recoverTime{ 0.3f };
        PROPERTY( Tooltip = "Sweep the arm against the scene" )
        bool _bEnabled{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 손떨림 섹션(`<Noise>`)입니다. Cinemachine 의 Basic Multi Channel Perlin 에 해당합니다.
     * @details 채널마다 위상이 다른 1D 펄린 잡음이 자리와 회전을 흔든다.
     *          같은 시드와 같은 시간이면 같은 값이 나오므로 테스트와 리플레이에서 쓸 수 있다.
     *          회전은 피치, 요, 롤이며 단위는 라디안이고 XML 에는 도로 적는다.
     */
    REFLECT()
    struct SW_GF_API CameraNoiseDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Position amplitude along the camera's right, up and forward", Units = m )
        float3 _positionAmplitude{};
        PROPERTY( Tooltip = "Rotation amplitude (pitch, yaw, roll)", Units = rad )
        float3 _rotationAmplitude{};
        PROPERTY( Min = 0.0, Tooltip = "Noise frequency", Units = Hz )
        float32 _frequency{ 0.5f };
        PROPERTY( Tooltip = "Noise seed; the same seed gives the same shake" )
        uint32 _seed{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 훑기 섹션(`<Sweep>`)입니다. 요를 사인 곡선으로 오가는 CCTV 팬이며 고정 모드와 궤도 모드에서 씁니다. */
    REFLECT()
    struct SW_GF_API CameraSweepDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Tooltip = "Half the sweep angle", Units = rad )
        float32 _yawAmplitude{ 0.0f };
        PROPERTY( Min = 0.01, Tooltip = "Time of one full sweep there and back", Units = s )
        float32 _period{ 8.0f };
        PROPERTY( Min = 0.0, Max = 1.0, Tooltip = "Start phase as a fraction of the period" )
        float32 _phase{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 프리셋 하나입니다.
     * @code
     *     <Preset id="topdown">
     *         <View mode="OrthoTopDown" pitch="30" yaw="45" distance="250" offset="0 0 0"/>
     *         <Lens orthographic="true" orthoHeight="110" near="0.1" far="625"/>
     *         <Damping position="0.2" orientation="0.1"/>
     *         <Input zoomStep="0.85" panSpeed="40" rotateStep="90"/>
     *         <Confiner zoomMin="25" zoomMax="220"/>
     *         <BlendIn curve="EaseInOut" duration="0.6"/>
     *     </Preset>
     * @endcode
     * @details `<BlendIn>` 이 없으면 카탈로그의 `<DefaultBlend>` 를 쓰며, 읽을 때 채운다.
     */
    REFLECT()
    struct SW_GF_API CameraPresetDef
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _id{};
        PROPERTY()
        BlendCurveDef _blendIn{};
        PROPERTY()
        CameraViewDef _view{};
        PROPERTY()
        CameraLensDef _lens{};
        PROPERTY()
        CameraDampingDef _damping{};
        PROPERTY()
        CameraInputDef _input{};
        PROPERTY()
        CameraConfinerDef _confiner{};
        PROPERTY()
        CameraFramingDef _framing{};
        PROPERTY()
        CameraCollisionDef _collision{};
        PROPERTY()
        CameraNoiseDef _noise{};
        PROPERTY()
        CameraSweepDef _sweep{};
    };
} // namespace sw

namespace sw
{
    /** @brief 프리셋 사이 블렌드 덮어쓰기 한 줄(`<Blend from="a" to="b" curve="Cut"/>`)입니다. `*` 는 아무 프리셋입니다. */
    struct CameraBlendRule
    {
        hashed_string _from{};
        hashed_string _to{};
        BlendCurveDef _blend{};
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 카메라가 따라가는 대상과 그 카메라가 그리는 화면의 비율입니다.
     * @details 월드 초점, 대상이 보는 요와 피치(라디안, 피치는 +가 아래), 대상 묶음의 반지름을 담는다.
     *          여러 대상을 담을 때는 `makeGroupCameraTarget` 이 초점을 묶음의 가운데로, 반지름을 묶음을 감싸는 구의 반지름으로 채운다.
     *          화면 비율은 프레이밍의 가로 위치와 존을 각으로 바꿀 때 쓴다. 디렉터가 카메라의 출력(렌더 텍스처, 창, 화면 사각형)에서 채운다.
     */
    struct CameraTarget
    {
        /** @brief 화면 비율을 모를 때(창이 없는 테스트, 서버) 쓰는 가로/세로 값입니다. */
        static constexpr float32 kDefaultAspectRatio = 16.0f / 9.0f;

        float3  _focus{};
        float32 _yaw{ 0.0f };
        float32 _pitch{ 0.0f };
        float32 _groupRadius{ 0.0f };                ///< 0 이면 대상 하나
        float32 _aspectRatio{ kDefaultAspectRatio }; ///< 카메라 화면의 가로/세로
    };

    /**
     * @brief @p current 를 @p target 쪽으로 @p deltaTime 만큼 지수 감쇠합니다.
     * @details 곡선은 1 − e^(−dt/시간 상수)이라서 프레임 수와 상관없이 같다. 자리와 회전만 감쇠하고 렌즈는 @p target 의 것을 쓴다.
     */
    SW_GF_API CameraPose dampPose( const CameraPose& current, const CameraPose& target, const CameraDampingDef& damping, float32 deltaTime );
    /** @brief 지수 감쇠의 이번 걸음 비율(1 − e^(−dt/τ))입니다. 시간 상수가 0 이하면 1(바로 붙음)이고, 시간이 0 이하면 0 입니다. */
    SW_GF_API float32 computeDampingAlpha( float32 timeConstant, float32 deltaTime );
} // namespace sw

namespace sw
{
    /**
     * @class CameraPresetCatalog
     * @brief `<CameraPresets>` XML 의 프리셋과 블렌드 표입니다.
     * @code
     *     <CameraPresets>
     *         <DefaultBlend curve="SmoothStep" duration="0.5"/>
     *         <Preset id="orbit"> ... </Preset>
     *         <Blend from="topdown" to="orbit" curve="Cut"/>
     *     </CameraPresets>
     * @endcode
     * @details 모르는 속성, 원소, 열거자 이름은 경고하고 넘어간다. `ResourceDataSchemaTest` 가 그 경고를 잡는다.
     */
    class SW_GF_API CameraPresetCatalog : public XMLCatalog<CameraPresetCatalog>
    {
        friend class XMLCatalog<CameraPresetCatalog>;

    public:
        CameraPresetCatalog();

        void addPreset( const CameraPresetDef& def );
        void addBlendRule( const CameraBlendRule& rule );
        void clear();

        const CameraPresetDef*         findPreset( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<CameraPresetDef>& getPresets() const { return _catalog.getAll(); }
        /**
         * @brief @p from 에서 @p to 로 갈 때의 블렌드입니다.
         * @details 덮어쓰기 표(정확히 맞는 줄, `from="*"`, `to="*"` 순), @p to 프리셋의 들어오기 블렌드, 카탈로그 기본 순으로 찾는다.
         *          Cinemachine 의 Custom Blends 와 같은 순서이다.
         */
        const BlendCurveDef& getBlend( const hashed_string& from, const hashed_string& to ) const;
        const BlendCurveDef& getDefaultBlend() const { return _defaultBlend; }
        void                 setDefaultBlend( const BlendCurveDef& blend ) { _defaultBlend = blend; }

    private:
        static constexpr const utf8* kXMLRootName = "CameraPresets"; ///< 루트 원소(`XMLCatalog`)
        uint32                       loadRoot( const XMLNode& root, string_view sourceName );

    private:
        GameCatalog<CameraPresetDef> _catalog;
        vector<CameraBlendRule>      _listBlendRule;
        BlendCurveDef                _defaultBlend;
    };
} // namespace sw
