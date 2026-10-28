/**
 * @file BlendCurve.h
 * @brief 전환 하나의 곡선과 길이(`BlendCurveDef`), 그리고 시간에서 가중치를 구하는 순수 함수(`evaluateBlendWeight`)입니다.
 * @details 컴포넌트를 모르는 함수라서 카메라 디렉터, 시퀀서, 소켓 부착(`SocketBindingComponent` 의 되돌아가기)이 같은 곡선을 쓴다.
 *          참고: Cinemachine 의 Blend List 와 Custom Blends, 언리얼의 `EViewTargetBlendFunction` 과 `EAlphaBlendOption`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /** @brief 블렌드 가중치 곡선입니다. 모두 0 에서 0, 1 에서 1 이고 단조 증가합니다(`Cut` 은 0 보다 크면 바로 1). */
    ENUM()
    enum class BlendCurve : uint8
    {
        Cut = 0,     ///< 바로 바꾼다.
        Linear,      ///< t
        EaseIn,      ///< t^지수이며 천천히 출발한다.
        EaseOut,     ///< 1 - (1 - t)^지수이며 천천히 도착한다.
        EaseInOut,   ///< 앞 절반은 EaseIn, 뒤 절반은 EaseOut(지수)이다.
        SmoothStep,  ///< 3t² - 2t³(큐빅 에르미트, 끝 속도 0)이다.
        Cubic,       ///< 세제곱 ease-in-out(4t³, 1 - (2 - 2t)³ / 2)이다.
        Exponential, ///< (1 - e^(-지수·t)) / (1 - e^(-지수))이며 빨리 다가가 천천히 붙는다.
        Spring,      ///< 임계 감쇠(넘치지 않는) 스프링이 1 로 다가가는 형태다. 길이(초)와 진동수로 정한다.
        Custom,      ///< 키(시간, 값) 사이를 직선으로 보간한다.
    };
} // namespace sw

namespace sw
{
    /** @brief `Custom` 곡선의 키 하나입니다. 시간과 값 모두 [0, 1] 입니다. */
    REFLECT()
    struct SW_API BlendCurveKey
    {
        REFLECT_BODY();

        PROPERTY( Required, Min = 0.0, Max = 1.0 )
        float32 _time{ 0.0f };
        PROPERTY( Required )
        float32 _value{ 0.0f };
    };
} // namespace sw

namespace sw
{
    /**
     * @brief 전환 하나의 곡선과 길이입니다(카메라 프리셋의 들어오기 블렌드, 프리셋 사이 덮어쓰기, 시퀀서 컷, 소켓으로 되돌아가기).
     * @details 카메라 XML 은 `<BlendIn curve="EaseInOut" duration="0.8" exponent="2"/>` 처럼 쓰고, `Custom` 은 안에 `<Key time="0.5" value="0.8"/>` 를 적는다.
     */
    REFLECT()
    struct SW_API BlendCurveDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Keys of the Custom curve, time ascending in [0, 1]" )
        vector<BlendCurveKey> _listCustomKey{};
        PROPERTY( Min = 0.0, Tooltip = "Blend length; 0 cuts", Units = s )
        float32 _duration{ 0.5f };
        PROPERTY( Min = 0.01, Tooltip = "Strength of EaseIn / EaseOut / EaseInOut / Exponential" )
        float32 _exponent{ 2.0f };
        PROPERTY( Min = 0.01, Tooltip = "Spring frequency", Units = Hz )
        float32 _springFrequency{ 1.0f };
        PROPERTY( Min = 1.0, Tooltip = "Spring damping ratio; 1 is critical, more is slower (below 1 is raised to 1)" )
        float32 _springDamping{ 1.0f };
        PROPERTY()
        BlendCurve _curve{ BlendCurve::SmoothStep };
    };

    /**
     * @brief 블렌드 진행(@p normalizedTime, 0..1 로 제한)의 가중치를 반환합니다. 시간 외의 상태가 없습니다.
     * @details `Spring` 은 초 단위 시간(진행 × 길이)으로 스프링을 풀고, 끝(길이)의 값으로 나눠 1 에서 1 이 되게 한다. 감쇠비는 1 이상으로 제한해
     *          넘치지 않게 한다. `Custom` 의 키가 없으면 `Linear` 와 같다.
     */
    SW_API float32 evaluateBlendWeight( const BlendCurveDef& spec, float32 normalizedTime );
} // namespace sw
