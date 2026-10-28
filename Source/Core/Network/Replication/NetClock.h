/**
 * @file NetClock.h
 * @brief 클라이언트의 서버 시계를 구현합니다. 받은 서버 틱으로 서버 틱을 추정하고, 보간 지연만큼 과거인 렌더 틱을 계산합니다. 단위는 서버 틱(소수)입니다.
 * @details 지연 규칙은 하나다. 지연 = max( `_interpolationDelay`, 표본 간격 x `kSampleIntervalsBehind`(2) ).
 *          표본(스냅샷, 자세)이 두 간격 뒤에 있어야 하나를 잃거나 흔들림으로 늦어도 그다음 표본과의 사이를 보간할 수 있다. 한 간격뿐이면 표본을 기다리며 멈춰 선다.
 *          - **추정**은 받은 틱을 하한으로 둔다. 받은 틱은 서버가 적어도 거기까지 왔다는 뜻이므로, 추정이 그보다 작으면 끌어올리고 그 사이에는 흐른 시간만큼 흐른다.
 *            그래서 표본이 오지 않는 동안(멈춘 덩어리)에도 서버 시각을 따라간다.
 *          - 추정은 "받은 가장 새 틱 + 지연"을 넘지 않는다. 렌더 틱이 받은 가장 새 틱을 넘지 않게(내다보지 않게) 하고, 로컬 시계가 빨라도 끝없이 앞서지 않게 하기 위해서다.
 *          - **렌더 틱** = 추정 - 지연. 되돌아가지 않는다. 지연이 커지거나 받은 틱이 끊겨도 멈췄다가 따라가므로 보간 위치가 뒤로 튀지 않는다.
 *          - 흔들림 필터링(시간 척도를 조금씩 기울이기, 유니티 `NetworkTimeSystem` 의 AdjustmentRatio)은 하지 않는다. 받은 틱이 추정보다 앞서면 그만큼 렌더 틱이 한 번에 앞으로 간다.
 *          - 유니티 Netcode for Entities `NetworkTime`(InterpolationTick, 지연 기본 2틱)과 언리얼 `AGameStateBase::GetServerWorldTimeSeconds` 에 해당한다.
 *          - 스레드 안전하지 않다(클라이언트 게임 스레드 하나).
 * @code
 *     clock.initialize( settings );
 *     clock.observeServerTick( tick );                     // 서버 틱이 든 메시지를 받을 때마다
 *     clock.advance( deltaTime );                          // 프레임마다
 *     const float32 renderTick = clock.getRenderTick();    // 음수면 아직 그릴 때가 아니다
 * @endcode
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @brief 시계 설정입니다. */
    struct NetClockConfig
    {
        float32 _tickInterval{ 1.0f / 30.0f }; ///< 서버 틱 간격(초). 1e-4보다 작으면 1e-4로 올려 쓴다.
        float32 _interpolationDelay{ 0.1f };   ///< 렌더 지연의 최솟값(초). 실제 지연은 이 값과 (표본 간격 x `NetClock::kSampleIntervalsBehind`) 중 큰 쪽이다.
        float32 _sampleInterval{ 0.0f };       ///< 표본(스냅샷, 자세)이 오는 간격(초). 0이면 최솟값만 쓴다.
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetClock
     * @brief 서버 틱 추정과 렌더 틱을 계산합니다. 자세한 규칙은 파일 머리말을 참고하세요.
     */
    class SW_API NetClock
    {
    public:
        static constexpr float32 kSampleIntervalsBehind = 2.0f;  ///< 렌더 지연은 표본 간격의 이 배수 이상이다.
        static constexpr float32 kNoRenderTick          = -1.0f; ///< 렌더 틱이 아직 없다는 값이다(서버 틱을 받기 전).

        NetClock();

        /** @brief 설정을 저장하고 상태를 비웁니다. */
        void initialize( const NetClockConfig& settings );
        /** @brief 받은 서버 틱과 렌더 틱을 비웁니다. 재연결하면 새 서버의 틱이 옛 틱보다 작을 수 있기 때문입니다. 설정은 그대로 둡니다. */
        void reset();
        /** @brief 서버 틱이 든 메시지를 받았음을 알립니다. 처음이면 추정을 그 틱에, 렌더 틱을 그 틱 - 지연에 둡니다. 추정보다 앞이면 추정을 끌어올립니다. */
        void observeServerTick( uint32 serverTick );
        /** @brief 시간을 흘립니다. 추정을 흐른 틱만큼(받은 가장 새 틱 + 지연까지) 올리고, 렌더 틱을 추정 - 지연으로 올립니다(되돌아가지 않습니다). */
        void advance( float32 deltaTime );
        /** @brief 현재 렌더 지연(초)을 반환합니다. max( 최솟값, 표본 간격 x `kSampleIntervalsBehind` ) 입니다. */
        float32 computeInterpolationDelay() const;

        /** @brief 그릴 서버 틱(소수)을 반환합니다. 서버 틱을 받기 전에는 `kNoRenderTick` 이고, 처음 몇 틱은 지연 때문에 음수일 수 있습니다(음수면 그리지 않습니다). */
        float32 getRenderTick() const { return _renderTick; }
        /** @brief 서버 틱 추정값을 반환합니다. 받은 틱을 하한으로 하고 그 뒤 흐른 틱을 더하며, 받은 가장 새 틱 + 지연까지만 올라갑니다. */
        float32 getServerTick() const { return _serverTick; }
        /** @brief 받은 가장 새 서버 틱을 반환합니다. */
        uint32 getNewestServerTick() const { return _newestTick; }
        bool   hasServerTick() const { return _bHasServerTick != SW_FALSE; }
        /** @brief 표본 간격(초)을 반환합니다. 주기가 다른 표본을 여럿 받으면 가장 긴 것을 씁니다(파괴 덩어리에서는 등록한 오브젝트의 가장 긴 자세 간격). */
        void    setSampleInterval( float32 seconds ) { _settings._sampleInterval = seconds; }
        float32 getSampleInterval() const { return _settings._sampleInterval; }
        /** @brief 서버 틱 간격(초)을 반환합니다. 1e-4 아래로 내려가지 않습니다. */
        float32 getTickInterval() const;

    private:
        NetClockConfig _settings;
        float32        _serverTick;
        float32        _renderTick;
        uint32         _newestTick;
        uint8          _bHasServerTick;
    };
} // namespace sw
