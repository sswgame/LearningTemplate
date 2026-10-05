/**
 * @file NetClock.h
 * @brief 클라이언트의 서버 시계입니다 — 받은 서버 틱으로 서버 틱을 추정하고, 보간 지연만큼 과거의 렌더 틱을 흘립니다. 단위는 서버 틱(소수)입니다.
 * @details 지연 규칙은 하나입니다: 지연 = max( `_interpolationDelay`, 표본 간격 × `kSampleIntervalsBehind`(2) ). 표본(스냅숏 · 자세)이 두 간격 뒤에 있어야
 *          하나를 잃거나 흔들림으로 늦어도 그다음 것과 사이를 잇는다 — 한 간격뿐이면 표본을 기다리며 멈춰 선다.
 *          렌더 틱이 목표(서버 틱 − 지연)를 따르는 방식은 둘입니다(`NetClockMode`).
 *          - Smooth: 목표 = 가장 새로 받은 틱 − 지연(받은 틱 사이에 앞으로 밀지 않는다). 렌더 틱은 흐르는 시간만큼 가되 벗어난 만큼 초당 `_clockCorrection` 몫까지
 *            빠르게 · 느리게 가고, 지연의 `kSnapDelayMultiple` 배를 넘게 벗어나면 바로 맞춘다. 첫 틱을 받으면 바로 목표에 선다 — 표본이 틱마다 오는 복제 클라이언트.
 *          - Monotonic: 서버 틱 추정을 받은 틱 사이에도 흐르는 시간으로 앞으로 밀고(받은 틱보다 뒤처지지 않는다), 렌더 틱은 (추정 − 지연)이되 뒤로 가지 않는다 —
 *            표본이 성기고 한동안 끊기는(멈춘 덩어리) 파괴 클라이언트. 표본이 오지 않는 동안에도 서버 시각을 따른다.
 *          유니티 Netcode for GameObjects `NetworkTimeSystem`(ServerBufferSec · AdjustmentRatio · HardResetThresholdSec) · Netcode for Entities `NetworkTime`
 *          (InterpolationTick, 지연 기본 2 틱) · 언리얼 `AGameStateBase::GetServerWorldTimeSeconds` 의 자리입니다.
 *          스레드 안전하지 않다(클라이언트 게임 스레드 하나).
 * @code
 *     clock.initialize( settings );
 *     clock.observeServerTick( tick );                     // 서버 틱이 든 메시지를 받을 때마다
 *     clock.advance( deltaTime );                          // 프레임마다
 *     const float32 renderTick = clock.getRenderTick();    // 음수면 아직 그릴 때가 아니다
 * @endcode
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

namespace sw
{
    /** @brief 렌더 틱이 목표(서버 틱 − 지연)를 따르는 방식입니다 — 파일 머리말 참고. */
    enum class NetClockMode : uint8
    {
        Smooth,    ///< 가장 새로 받은 틱 − 지연을 흐름 빠르기로 맞춘다(크게 벗어나면 바로) — 복제 클라이언트
        Monotonic, ///< 흐르는 시간으로 민 서버 틱 추정 − 지연, 뒤로 가지 않는다 — 파괴 클라이언트
    };
} // namespace sw

namespace sw
{
    /** @brief 시계 설정입니다. */
    struct NetClockSettings
    {
        float32      _tickInterval{ 1.0f / 30.0f }; ///< 서버 틱 간격(초, 1e-4 보다 작으면 1e-4)
        float32      _interpolationDelay{ 0.1f };   ///< 렌더 지연의 최소값(초) — 실제는 이것과 (표본 간격 × `NetClock::kSampleIntervalsBehind`) 중 큰 것
        float32      _sampleInterval{ 0.0f };       ///< 표본(스냅숏 · 자세)이 오는 간격(초). 0 이면 최소값만
        float32      _clockCorrection{ 0.1f };      ///< Smooth — 렌더 틱이 목표에서 벗어나면 초당 이 몫까지 빠르게 · 느리게
        NetClockMode _mode{ NetClockMode::Smooth };
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetClock
     * @brief 서버 틱 추정과 렌더 틱 — 파일 머리말 참고.
     */
    class SW_API NetClock
    {
    public:
        static constexpr float32 kSampleIntervalsBehind = 2.0f;  ///< 렌더 지연은 표본 간격의 이 배 이상
        static constexpr float32 kSnapDelayMultiple     = 4.0f;  ///< Smooth — 목표에서 지연의 이 배를 넘게 벗어나면 흐르며 맞추지 않고 바로 맞춘다
        static constexpr float32 kNoRenderTick          = -1.0f; ///< 렌더 틱이 아직 없다(서버 틱을 받기 전, Monotonic 은 첫 `advance` 전)

        NetClock();

        /** @brief 설정을 두고 비웁니다. */
        void initialize( const NetClockSettings& settings );
        /** @brief 받은 서버 틱 · 렌더 틱을 비웁니다(다시 연결 — 새 서버의 틱은 옛 것보다 작을 수 있다). 설정은 그대로입니다. */
        void reset();
        /** @brief 서버 틱이 든 메시지를 받았습니다. 추정은 받은 틱 중 가장 큰 것 아래로 내려가지 않는다. Smooth 는 첫 틱에서 렌더 틱을 바로 목표에 둔다. */
        void observeServerTick( uint32 serverTick );
        /** @brief 시간을 흘립니다. 서버 틱을 받기 전에는 아무것도 하지 않는다(Smooth 는 0 이하의 시간도). */
        void advance( float32 deltaTime );
        /** @brief 지금 렌더 지연(초)입니다 — max( 최소값, 표본 간격 × `kSampleIntervalsBehind` ). */
        float32 computeInterpolationDelay() const;

        /** @brief 그리는 서버 틱(소수)입니다. 아직 없으면 음수입니다(`kNoRenderTick`, 또는 Monotonic 의 첫 계산이 0 아래). */
        float32 getRenderTick() const { return _renderTick; }
        /** @brief 서버 틱 추정입니다 — Smooth 는 가장 새로 받은 틱, Monotonic 은 거기에 그 뒤 흐른 틱을 더한 것. */
        float32 getServerTick() const { return _serverTick; }
        bool    hasServerTick() const { return _bHasServerTick != SW_FALSE; }
        /** @brief 표본 간격(초)입니다. 주기가 다른 표본을 여럿 받으면 가장 긴 것을 둔다(파괴 — 등록한 오브젝트의 가장 긴 자세 간격). */
        void    setSampleInterval( float32 seconds ) { _settings._sampleInterval = seconds; }
        float32 getSampleInterval() const { return _settings._sampleInterval; }
        /** @brief 서버 틱 간격(초, 1e-4 아래로 내려가지 않는다)입니다. */
        float32 getTickInterval() const;

    private:
        void advanceSmooth( float32 deltaTime );
        void advanceMonotonic( float32 deltaTime );

        NetClockSettings _settings;
        float32          _serverTick;
        float32          _renderTick;
        uint8            _bHasServerTick;
    };
} // namespace sw
