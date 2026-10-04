/**
 * @file CameraBlend.h
 * @brief 카메라 전환의 곡선 · 길이(`CameraBlendSpec`)와 두 순수 함수 — 시간 → 가중치(`evaluateBlendWeight`), 두 포즈 섞기(`blendPoses`)입니다.
 * @details 컴포넌트를 모르는 함수라 디렉터 · 시퀀서(카메라 컷 · 값 커브)가 같은 곡선을 씁니다. 참고: Cinemachine 의 Blend List · Custom Blends,
 *          언리얼 `EViewTargetBlendFunction`.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Camera/CameraPose.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /** @brief 블렌드 가중치 곡선입니다. 모두 0 에서 0, 1 에서 1 이고 단조 증가합니다(`Cut` 은 0 보다 크면 바로 1). */
    ENUM()
    enum class CameraBlendCurve : uint8
    {
        Cut = 0,     ///< 바로 바꾼다
        Linear,      ///< t
        EaseIn,      ///< t^지수 — 천천히 출발
        EaseOut,     ///< 1 − (1 − t)^지수 — 천천히 도착
        EaseInOut,   ///< 앞 절반은 EaseIn, 뒤 절반은 EaseOut(지수)
        SmoothStep,  ///< 3t² − 2t³(큐빅 에르미트, 끝 속도 0)
        Cubic,       ///< 세제곱 ease-in-out(4t³ · 1 − (2 − 2t)³ / 2)
        Exponential, ///< (1 − e^(−지수·t)) / (1 − e^(−지수)) — 빨리 다가가 천천히 붙는다
        Spring,      ///< 임계 감쇠(넘치지 않는) 스프링이 1 로 다가가는 모양 — 길이(초)와 진동수로 정한다
        Custom,      ///< 키(시간 · 값) 사이 직선 보간
    };
} // namespace sw

namespace sw
{
    /** @brief `Custom` 곡선의 키 하나입니다. 시간 · 값 모두 [0, 1] 입니다. */
    REFLECT()
    struct SW_GF_API CameraBlendKey
    {
        REFLECT_BODY();

        PROPERTY( Min = 0.0, Max = 1.0 )
        float32 _time{ 0.0f };
        PROPERTY()
        float32 _value{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 전환 하나의 곡선과 길이입니다(프리셋의 들어오기 블렌드 · 프리셋 사이 덮어쓰기 · 시퀀서 컷).
     * @details XML 은 `<BlendIn curve="EaseInOut" duration="0.8" exponent="2"/>`, `Custom` 은 안에 `<Key time="0.5" value="0.8"/>` 를 적습니다.
     */
    REFLECT()
    struct SW_GF_API CameraBlendSpec
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Keys of the Custom curve, time ascending in [0, 1]" )
        vector<CameraBlendKey> _listCustomKey{};
        PROPERTY( Min = 0.0, Tooltip = "Blend length; 0 cuts", Meta = "Units=s" )
        float32 _duration{ 0.5f };
        PROPERTY( Min = 0.01, Tooltip = "Strength of EaseIn / EaseOut / EaseInOut / Exponential" )
        float32 _exponent{ 2.0f };
        PROPERTY( Min = 0.01, Tooltip = "Spring frequency", Meta = "Units=Hz" )
        float32 _springFrequency{ 1.0f };
        PROPERTY( Min = 1.0, Tooltip = "Spring damping ratio; 1 is critical, more is slower (below 1 is raised to 1)" )
        float32 _springDamping{ 1.0f };
        PROPERTY()
        CameraBlendCurve _curve{ CameraBlendCurve::SmoothStep };
    };

    /**
     * @brief 블렌드 진행(@p normalizedTime, 0..1 로 묶는다)의 가중치입니다. 시간 외의 상태가 없습니다.
     * @details `Spring` 은 초 단위 시간(진행 × 길이)으로 스프링을 풀고 끝(길이)의 값으로 나눠 1 에서 1 이 되게 합니다. 감쇠비는 1 이상으로 묶어
     *          넘치지 않습니다. `Custom` 의 키가 없으면 `Linear` 입니다.
     */
    SW_GF_API float32 evaluateBlendWeight( const CameraBlendSpec& spec, float32 normalizedTime );

    /**
     * @brief 두 포즈를 @p weight(0 = @p from, 1 = @p to)로 섞습니다.
     * @details 자리 · 시야각 · 직교 높이 · 근/원평면은 직선, 회전은 짧은 쪽 구면 보간입니다. 직교 ↔ 원근은 섞지 않고 가중치 0.5 에서 바꿉니다
     *          (Cinemachine 도 투영을 섞지 않는다 — 두 투영 사이의 행렬은 어느 쪽 화면과도 닮지 않는다).
     */
    SW_GF_API CameraPose blendPoses( const CameraPose& from, const CameraPose& to, float32 weight );
} // namespace sw
