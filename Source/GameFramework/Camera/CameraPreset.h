/**
 * @file CameraPreset.h
 * @brief 데이터로 적는 카메라 프리셋 — 시점(`<View>`) · 렌즈(`<Lens>`) · 감쇠(`<Damping>`) · 들어오기 블렌드(`<BlendIn>`) · 입력(`<Input>`) ·
 *        제약(`<Confiner>`) · 프레이밍(`<Framing>`) · 충돌(`<Collision>`) · 손떨림(`<Noise>`) · 훑기(`<Sweep>`), 프리셋 사이 블렌드 표와 그것을 읽는
 *        `CameraPresetCatalog` 입니다. 프리셋을 포즈로 푸는 계산은 `CameraMode.h` 에 있습니다.
 * @details 참고: Cinemachine 가상 카메라(Body · Aim · Lens · Noise · Confiner · Deoccluder · Composer) + Custom Blends, 언리얼 카메라 모드 · SpringArm.
 *          섹션은 XML 자식 원소 하나씩이라 다음 섹션은 구조체 하나와 원소 하나를 더하면 됩니다.
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

#include "GameFramework/Camera/CameraBlend.h"
#include "GameFramework/Camera/CameraPose.h"
#include "GameFramework/Data/GameCatalog.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class XmlNode;

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
    /** @brief 시점 섹션(`<View>`)입니다. 각은 라디안이고 XML 에는 도로 적습니다. 피치는 + 가 아래를 보는 쪽입니다(`CameraComponent::lookAt` 과 같다). */
    REFLECT()
    struct SW_GF_API CameraViewDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Pivot offset from the focus (Fixed: world position, FirstPerson / ThirdPerson / Follow: offset in the target's yaw frame)", Meta = "Units=m" )
        float3 _offset{};
        PROPERTY( Tooltip = "World point a Fixed camera looks at when Aim is Point", Meta = "Units=m" )
        float3 _lookAt{};
        PROPERTY( Tooltip = "Downward viewing angle", Meta = "Units=rad" )
        float32 _pitch{ 0.0f };
        PROPERTY( Tooltip = "Angle from +Z towards +X (Follow / FirstPerson / ThirdPerson: added to the target's yaw)", Meta = "Units=rad" )
        float32 _yaw{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Distance from the pivot to the camera", Meta = "Units=m" )
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

        PROPERTY( Min = 0.1, Max = 3.14, Tooltip = "Vertical field of view", Meta = "Units=rad" )
        float32 _fieldOfViewY{ 0.70f };
        PROPERTY( Min = 0.1, Tooltip = "Visible height of the orthographic view", Meta = "Units=m" )
        float32 _orthoHeight{ 10.0f };
        PROPERTY( Min = 0.01, Meta = "Units=m" )
        float32 _nearPlane{ 0.1f };
        PROPERTY( Min = 1.0, Meta = "Units=m" )
        float32 _farPlane{ 100.0f };
        PROPERTY()
        bool _bOrthographic{ false };
    };
} // namespace sw

namespace sw
{
    /** @brief 감쇠 섹션(`<Damping>`)입니다. 시간 상수(초) — 남은 차이가 이 시간마다 1/e 로 줄고, 0 이면 바로 붙습니다. 렌즈는 감쇠하지 않습니다. */
    REFLECT()
    struct SW_GF_API CameraDampingDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Tooltip = "Time constant of the position lag; 0 snaps", Meta = "Units=s" )
        float32 _positionTime{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Time constant of the orientation lag; 0 snaps", Meta = "Units=s" )
        float32 _orientationTime{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 입력 섹션(`<Input>`)입니다 — 모드가 입력을 어떻게 받는지입니다. 값이 0 인 칸은 그 입력을 받지 않습니다.
     * @details 마우스 시점은 요 · 피치 오프셋에 더하고(1인칭 · 3인칭 · 궤도), 휠은 거리(원근) · 직교 높이를 배로 바꾸고, 이동 키는 초점을 요 기준으로 옮기고,
     *          회전 키(Q/E)는 요를 한 칸씩 돌리되 `rotateTime` 으로 부드럽게 따라간다. 범위는 `<Confiner>` 가 자른다.
     */
    REFLECT()
    struct SW_GF_API CameraInputDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Tooltip = "Radians per pixel of mouse movement; 0 ignores the mouse" )
        float32 _lookSensitivity{ 0.0f };
        PROPERTY( Min = 0.0, Max = 0.99, Tooltip = "Distance / ortho height factor per wheel notch (below 1); 0 ignores the wheel" )
        float32 _zoomStep{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Focus pan speed with WASD / arrows; 0 ignores them", Meta = "Units=m/s" )
        float32 _panSpeed{ 0.0f };
        PROPERTY( Tooltip = "Yaw change per Q/E press; 0 ignores them", Meta = "Units=rad" )
        float32 _rotateStep{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Time constant the shown yaw follows a Q/E step with", Meta = "Units=s" )
        float32 _rotateTime{ 0.15f };
        PROPERTY( Tooltip = "Mouse turns the view only while the right button is held (orbit drag)" )
        bool _bLookWhileHeld{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 제약 섹션(`<Confiner>`)입니다(Cinemachine Confiner · 언리얼 피치 한계). 피치 · 줌은 입력이 넘지 못하는 범위이고, 상자는 카메라 자리
     *        (직교 시점은 초점의 X · Z)를 그 안에 둡니다.
     */
    REFLECT()
    struct SW_GF_API CameraConfinerDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Lowest pitch (up is negative)", Meta = "Units=rad" )
        float32 _pitchMin{ -MathUtil::HalfPi };
        PROPERTY( Tooltip = "Highest pitch (down is positive)", Meta = "Units=rad" )
        float32 _pitchMax{ MathUtil::HalfPi };
        PROPERTY( Min = 0.0, Tooltip = "Closest distance / smallest ortho height; 0 has no limit", Meta = "Units=m" )
        float32 _zoomMin{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Farthest distance / largest ortho height; 0 has no limit", Meta = "Units=m" )
        float32 _zoomMax{ 0.0f };
        PROPERTY( Tooltip = "Lowest corner of the box the camera stays in", Meta = "Units=m" )
        float3 _boundsMin{};
        PROPERTY( Tooltip = "Highest corner of the box the camera stays in", Meta = "Units=m" )
        float3 _boundsMax{};
        PROPERTY( Tooltip = "Keep the camera (ortho: the focus X and Z) inside the box" )
        bool _bBounds{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 프레이밍 섹션(`<Framing>`)입니다(Cinemachine Composer · Group Framing). 대상이 화면의 어디에 오는지, 얼마나 움직여야 카메라가
     *        돌아가는지(데드존 · 소프트존), 움직이는 쪽을 미리 보기(look-ahead), 여러 대상을 다 담기(그룹 반지름에 맞춰 물러남)입니다.
     * @details 화면 위치 · 존은 화면 비율(0..1, 왼쪽 위가 0)입니다. 조준을 가르는 것은 대상을 보는 모드(궤도 · 따라가기 · 대상을 보는 고정)뿐이고,
     *          그룹 맞추기는 거리를 가진 모드와 직교 시점이 씁니다.
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
        PROPERTY( Min = 0.0, Tooltip = "Time constant of turning towards the target outside the dead zone", Meta = "Units=s" )
        float32 _damping{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Aim at where the target will be after this time", Meta = "Units=s" )
        float32 _lookAheadTime{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Time constant smoothing the look-ahead", Meta = "Units=s" )
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
     * @brief 충돌 섹션(`<Collision>`)입니다(언리얼 SpringArm · Cinemachine Deoccluder). 피벗에서 카메라까지 구를 쓸어 막히면 그 앞으로 당기고,
     *        막힘이 풀리면 `recoverTime` 으로 천천히 돌아갑니다. 당길 때는 바로 당긴다 — 벽 안이 한 프레임도 보이지 않게.
     */
    REFLECT()
    struct SW_GF_API CameraCollisionDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Tooltip = "Radius of the probe sphere", Meta = "Units=m" )
        float32 _radius{ 0.2f };
        PROPERTY( Min = 0.0, Tooltip = "Closest the arm pulls in", Meta = "Units=m" )
        float32 _minDistance{ 0.3f };
        PROPERTY( Min = 0.0, Tooltip = "Time constant of easing back out once the way is clear", Meta = "Units=s" )
        float32 _recoverTime{ 0.3f };
        PROPERTY( Tooltip = "Sweep the arm against the scene" )
        bool _bEnabled{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 손떨림 섹션(`<Noise>`)입니다(Cinemachine Basic Multi Channel Perlin). 채널마다 다른 위상의 1D 펄린 잡음이 자리 · 회전을 흔듭니다.
     * @details 같은 시드 · 같은 시간이면 같은 값이다(시험 · 리플레이). 회전은 피치 · 요 · 롤(라디안, XML 에는 도)입니다.
     */
    REFLECT()
    struct SW_GF_API CameraNoiseDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Position amplitude along the camera's right, up and forward", Meta = "Units=m" )
        float3 _positionAmplitude{};
        PROPERTY( Tooltip = "Rotation amplitude (pitch, yaw, roll)", Meta = "Units=rad" )
        float3 _rotationAmplitude{};
        PROPERTY( Min = 0.0, Tooltip = "Noise frequency", Meta = "Units=Hz" )
        float32 _frequency{ 0.5f };
        PROPERTY( Tooltip = "Noise seed; the same seed gives the same shake" )
        uint32 _seed{ 0 };
    };
} // namespace sw

namespace sw
{
    /** @brief 훑기 섹션(`<Sweep>`)입니다 — 요를 사인으로 오가는 CCTV 팬입니다(고정 · 궤도). */
    REFLECT()
    struct SW_GF_API CameraSweepDef
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Tooltip = "Half the sweep angle", Meta = "Units=rad" )
        float32 _yawAmplitude{ 0.0f };
        PROPERTY( Min = 0.01, Tooltip = "Time of one full sweep there and back", Meta = "Units=s" )
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
     * @details `<BlendIn>` 이 없으면 카탈로그의 `<DefaultBlend>` 입니다(읽을 때 채운다).
     */
    REFLECT()
    struct SW_GF_API CameraPresetDef
    {
        REFLECT_BODY();

        PROPERTY()
        hashed_string _id{};
        PROPERTY()
        BlendCurveSpec _blendIn{};
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
        hashed_string  _from{};
        hashed_string  _to{};
        BlendCurveSpec _blend{};
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 카메라가 따라가는 대상입니다 — 초점(월드) · 대상이 보는 요 · 피치(라디안, 피치는 + 가 아래)와 대상 묶음의 반지름입니다.
     * @details 여러 대상을 담을 때는 `makeGroupCameraTarget` 이 초점을 묶음의 가운데로, 반지름을 묶음을 감싸는 구의 반지름으로 채웁니다.
     */
    struct CameraTarget
    {
        float3  _focus{};
        float32 _yaw{ 0.0f };
        float32 _pitch{ 0.0f };
        float32 _groupRadius{ 0.0f }; ///< 0 이면 대상 하나
    };

    /**
     * @brief @p current 를 @p target 쪽으로 @p deltaTime 만큼 지수 감쇠합니다 — 프레임 수와 상관없이 같은 곡선입니다(1 − e^(−dt/시간 상수)).
     * @details 자리 · 회전만 감쇠하고 렌즈는 @p target 의 것입니다.
     */
    SW_GF_API CameraPose dampPose( const CameraPose& current, const CameraPose& target, const CameraDampingDef& damping, float32 deltaTime );
    /** @brief 지수 감쇠의 이번 걸음 비율(1 − e^(−dt/τ))입니다. 시간 상수가 0 이하면 1(바로 붙음), 시간이 0 이하면 0 입니다. */
    SW_GF_API float32 computeDampingAlpha( float32 timeConstant, float32 deltaTime );
} // namespace sw

namespace sw
{
    /**
     * @class CameraPresetCatalog
     * @brief `<CameraPresets>` XML 의 프리셋 · 블렌드 표입니다.
     * @code
     *     <CameraPresets>
     *         <DefaultBlend curve="SmoothStep" duration="0.5"/>
     *         <Preset id="orbit"> ... </Preset>
     *         <Blend from="topdown" to="orbit" curve="Cut"/>
     *     </CameraPresets>
     * @endcode
     * @details 모르는 속성 · 원소 · 열거자 이름은 경고하고 넘깁니다(`ResourceDataSchemaTest` 가 그 경고를 잡는다).
     */
    class SW_GF_API CameraPresetCatalog
    {
    public:
        CameraPresetCatalog();

        [[nodiscard]] bool loadFromResource( string_view path );
        [[nodiscard]] bool loadFromXmlText( string_view xmlText, string_view sourceName = {} );
        void               addPreset( const CameraPresetDef& def );
        void               addBlendRule( const CameraBlendRule& rule );
        void               clear();

        const CameraPresetDef*         findPreset( const hashed_string& id ) const { return _catalog.find( id ); }
        const vector<CameraPresetDef>& getPresets() const { return _catalog.getAll(); }
        /**
         * @brief @p from 에서 @p to 로 갈 때의 블렌드입니다. 덮어쓰기 표(정확히 맞는 줄 → `from="*"` → `to="*"`) → @p to 프리셋의 들어오기 블렌드 →
         *        카탈로그 기본 순으로 찾습니다(Cinemachine Custom Blends 와 같은 순서).
         */
        const BlendCurveSpec& getBlend( const hashed_string& from, const hashed_string& to ) const;
        const BlendCurveSpec& getDefaultBlend() const { return _defaultBlend; }
        void                  setDefaultBlend( const BlendCurveSpec& blend ) { _defaultBlend = blend; }

    private:
        uint32 loadRoot( const XmlNode& root, string_view sourceName );

    private:
        GameCatalog<CameraPresetDef> _catalog;
        vector<CameraBlendRule>      _listBlendRule;
        BlendCurveSpec               _defaultBlend;
    };
} // namespace sw
