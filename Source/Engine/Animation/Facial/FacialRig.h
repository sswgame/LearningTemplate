/**
 * @file FacialRig.h
 * @brief 얼굴 리그(`<얼굴>.facial.json`) — 표정 · 비즘을 모프 타깃 가중치로, 눈 깜빡임 · 시선(눈 본 · 사카드)의 수치를 데이터로 둡니다.
 * @details 코드는 이름으로 고를 뿐이고(표정 "Happy", 비즘 "AA"), 어느 타깃을 얼마나 움직일지는 이 표가 정합니다(언리얼 MetaHuman 의 포즈 에셋 ·
 *          Live Link Face 커브 → 컨트롤 매핑의 자리). 모르는 키는 오류이고, 메시에 없는 타깃 · 스켈레톤에 없는 본은 묶을 때 오류입니다(`validate`).
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

namespace sw
{
    class JSONValue;
    class Skeleton;

    /** @brief 모프 타깃 하나의 가중치입니다. */
    struct FacialTargetWeight
    {
        hashed_string _target;
        float32       _weight{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /** @brief 이름 붙은 얼굴 포즈(표정 · 비즘) — 타깃 가중치 묶음입니다. 포즈 가중치를 곱해 더합니다. */
    struct FacialPose
    {
        hashed_string              _name;
        vector<FacialTargetWeight> _listTarget;
    };
} // namespace sw

namespace sw
{
    /** @brief 눈 깜빡임 — 타깃(눈꺼풀)과 간격 · 길이(초)입니다. 타깃이 없으면 깜빡이지 않습니다. */
    struct FacialBlinkSettings
    {
        vector<hashed_string> _listTarget;
        float32               _minInterval{ 2.0f };
        float32               _maxInterval{ 5.0f };
        float32               _duration{ 0.15f };
    };
} // namespace sw

namespace sw
{
    /** @brief 시선 — 눈 본들이 목표를 봅니다(앞 축 · 최대 각), 사카드(작은 튐)는 간격 · 크기로 정합니다. 눈 본이 없으면 보지 않습니다. */
    struct FacialGazeSettings
    {
        vector<hashed_string> _listEyeBone;
        float3                _forwardAxis{ 0.0f, 0.0f, 1.0f };
        float32               _maxAngleDegrees{ 30.0f };
        float32               _saccadeMinInterval{ 0.4f };
        float32               _saccadeMaxInterval{ 1.6f };
        float32               _saccadeAmplitudeDegrees{ 3.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @class FacialRig
     * @brief 형식: `{ "expressions": { 이름: { 타깃: 가중치 } }, "visemes": { 이름: { 타깃: 가중치 } }, "blink": { "targets", "min_interval",
     *        "max_interval", "duration" }, "gaze": { "eye_bones", "forward_axis", "max_angle_degrees", "saccade_min_interval", "saccade_max_interval",
     *        "saccade_amplitude_degrees" } }`. 비즘 이름은 립싱크 표(`engine/animation/lipsync.json`)의 것과 맞춥니다.
     */
    class SW_API FacialRig
    {
    public:
        /** @brief 얼굴 리그 확장자입니다. */
        static constexpr string_view kExtension = ".facial.json";

        [[nodiscard]] bool parseJSON( string_view json, string_view sourceLabel );
        [[nodiscard]] bool loadFromResource( string_view path );
        /** @brief 메시의 모프 타깃 이름들에 없는 타깃 · 스켈레톤에 없는 눈 본 · 타깃과 같은 이름의 표정(커브가 둘 다 움직인다)이 있으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool validate( const vector<hashed_string>& listMorphTargetName, const Skeleton& skeleton, string_view sourceLabel ) const;

        const vector<FacialPose>&  getExpressions() const { return _listExpression; }
        const vector<FacialPose>&  getVisemes() const { return _listViseme; }
        const FacialBlinkSettings& getBlink() const { return _blink; }
        const FacialGazeSettings&  getGaze() const { return _gaze; }
        /** @brief 이름의 표정 번호입니다. 없으면 -1 입니다. */
        int32 findExpressionIndex( const hashed_string& name ) const;
        /** @brief 이름의 비즘 번호입니다. 없으면 -1 입니다. */
        int32 findVisemeIndex( const hashed_string& name ) const;

    private:
        [[nodiscard]] bool        parseRoot( const JSONValue& root, string_view sourceLabel );
        [[nodiscard]] static bool parsePoses( const JSONValue& value, vector<FacialPose>& outListPose, string_view sourceLabel );

        vector<FacialPose>  _listExpression;
        vector<FacialPose>  _listViseme;
        FacialBlinkSettings _blink;
        FacialGazeSettings  _gaze;
    };
} // namespace sw
