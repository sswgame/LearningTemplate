/**
 * @file CameraPreset.h
 * @brief 데이터로 적는 카메라 프리셋 — 시점(`<View>`) · 렌즈(`<Lens>`) · 감쇠(`<Damping>`) · 들어오기 블렌드(`<BlendIn>`), 프리셋 사이 블렌드 표와
 *        그것을 읽는 `CameraPresetCatalog`, 프리셋 하나를 포즈로 푸는 `evaluatePreset` 입니다.
 * @details 참고: Cinemachine 가상 카메라(Body · Aim · Lens) + Custom Blends, 언리얼 카메라 모드. 섹션은 XML 자식 원소 하나씩이라 나중의 섹션
 *          (프레이밍 · 제약 · 충돌 · 흔들림 · 후처리)은 구조체 하나와 원소 하나를 더하면 됩니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/Math.h"
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
        Fixed = 0,    ///< 오프셋이 곧 월드 자리, 요 · 피치가 곧 회전(대상을 보지 않는다)
        OrthoTopDown, ///< 월드 고정 요 · 피치로 초점 둘레에서 내려다보는 직교 시점(렌즈가 무엇이든 직교)
        Orbit,        ///< 월드 고정 요 · 피치로 초점 둘레에서 초점을 본다
        Follow,       ///< 대상의 요에 더한 요로 대상 뒤에서 초점을 본다
        FirstPerson,  ///< 눈 = 대상 + 대상 요로 돌린 오프셋, 회전 = 대상 요 · 피치 + 프리셋 요 · 피치
    };
} // namespace sw

namespace sw
{
    /** @brief 시점 섹션(`<View>`)입니다. 각은 라디안이고 XML 에는 도로 적습니다. 피치는 + 가 아래를 보는 쪽입니다(`CameraComponent::lookAt` 과 같다). */
    REFLECT()
    struct SW_GF_API CameraViewDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Pivot offset from the focus (Fixed: world position, FirstPerson: eye offset in the target's yaw frame)", Meta = "Units=m" )
        float3 _offset{};
        PROPERTY( Tooltip = "Downward viewing angle", Meta = "Units=rad" )
        float32 _pitch{ 0.0f };
        PROPERTY( Tooltip = "Angle from +Z towards +X (Follow / FirstPerson: added to the target's yaw)", Meta = "Units=rad" )
        float32 _yaw{ 0.0f };
        PROPERTY( Min = 0.0, Tooltip = "Distance from the pivot to the camera", Meta = "Units=m" )
        float32 _distance{ 10.0f };
        PROPERTY()
        CameraPresetMode _mode{ CameraPresetMode::Orbit };
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
     * @brief 프리셋 하나입니다.
     * @code
     *     <Preset id="topdown">
     *         <View mode="OrthoTopDown" pitch="30" yaw="45" distance="250" offset="0 0 0"/>
     *         <Lens orthographic="true" orthoHeight="110" near="0.1" far="625"/>
     *         <Damping position="0.2" orientation="0.1"/>
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
    /** @brief 카메라가 따라가는 대상입니다 — 초점(월드) · 대상이 보는 요 · 피치(라디안, 피치는 + 가 아래)입니다. */
    struct CameraTarget
    {
        float3  _focus{};
        float32 _yaw{ 0.0f };
        float32 _pitch{ 0.0f };
    };

    /** @brief 프리셋을 대상에 맞춰 포즈로 풉니다(감쇠 없음). 순수 함수입니다. */
    SW_GF_API CameraPose evaluatePreset( const CameraPresetDef& def, const CameraTarget& target );

    /**
     * @brief @p current 를 @p target 쪽으로 @p deltaTime 만큼 지수 감쇠합니다 — 프레임 수와 상관없이 같은 곡선입니다(1 − e^(−dt/시간 상수)).
     * @details 자리 · 회전만 감쇠하고 렌즈는 @p target 의 것입니다.
     */
    SW_GF_API CameraPose dampPose( const CameraPose& current, const CameraPose& target, const CameraDampingDef& damping, float32 deltaTime );
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
