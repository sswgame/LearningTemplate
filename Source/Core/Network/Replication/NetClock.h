/**
 * @file NetClock.h
 * @brief 월드 하나의 서버 시계(`NetClock`)와, 기능마다 그 시계를 보간 지연만큼 뒤에서 따라가는 화면 시계(`NetRenderClock`)입니다.
 * @details 서버 틱은 월드에 수열 하나다 — `NetClock::kTickSeconds` 간격이고 월드를 열 때 0 에서 시작한다. 주기가 긴 기능(30 Hz 이동 복제)은 자기 번호를 만들지 않고
 *          이 수열의 배수(`computeStride`)에서 돈다. 예전에는 사격장 · 허브 · 파괴가 저마다 다른 간격으로 처음부터 세고 클라이언트도 저마다 추정해, 같은 순간이 기능마다 다른 시각이었다.
 *          - **서버**(`Authority`): 흐른 게임 시간이 곧 시각이다.
 *          - **클라이언트**(`Replica`): 받은 틱을 하한으로 둔다. 받은 틱은 서버가 적어도 거기까지 왔다는 뜻이므로, 추정이 그보다 작으면 끌어올리고 그 사이에는 흐른 시간만큼 흐른다.
 *            그래서 표본이 오지 않는 동안(멈춘 덩어리)에도 서버 시각을 따라간다. 받은 가장 새 틱 + `kMaxLeadSeconds` 를 넘지 않는다 — 로컬 시계가 빨라도 끝없이 앞서지 않는다.
 *          - 시계는 라우터가 월드 세대와 함께 든다(`NetMessageRouter::getWorldClock`) — 새 월드 · 새 연결이면 처음부터다. 서버가 주기적으로 알리고, 틱을 실은 메시지를 받은 키트도 알린다.
 *          - 흔들림 필터링(시간 척도를 조금씩 기울이기, 유니티 `NetworkTimeSystem` 의 AdjustmentRatio)은 하지 않는다. 받은 틱이 추정보다 앞서면 그만큼 한 번에 앞으로 간다.
 *          - 언리얼 `AGameStateBase::GetServerWorldTimeSeconds`(월드에 하나, 0.1 초마다 복제) · 유니티 NGO `NetworkTimeSystem`(틱 수열 하나)에 해당한다.
 *          - 스레드 안전하지 않다(게임 스레드 하나).
 */
#pragma once
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Time/TimeSeconds.h"

namespace sw
{
    /** @brief 시계가 서버의 것인지 클라이언트의 추정인지입니다. */
    enum class NetClockSide : uint8
    {
        Authority = 0, ///< 서버 — 흐른 시간이 시각이다
        Replica        ///< 클라이언트 — 받은 틱에서 추정한다
    };

    /**
     * @class NetClock
     * @brief 월드 하나의 서버 시각입니다. 단위는 서버 틱(소수)이고 규칙은 파일 머리말을 참고하세요.
     */
    class SW_API NetClock
    {
    public:
        static constexpr float32 kTickSeconds        = constant::time::kDefaultFixedStepSeconds;
        static constexpr float32 kMaxLeadSeconds     = 0.25f; ///< 클라이언트 추정은 받은 가장 새 틱보다 이만큼까지만 앞선다
        static constexpr uint32  kMaxTicksPerAdvance = 15;    ///< 서버 — 한 번에 흐르는 상한(0.25 초). 넘는 시간은 버린다 — 몰아 돌린 틱이 다음 프레임을 또 밀지 않는다

        NetClock();

        /** @brief 간격 @p intervalSeconds 가 서버 틱 몇 개인지입니다. 정수 배수가 아니면 없음입니다 — 그 간격의 기능은 이 수열 위에서 돌 수 없다. */
        static optional<uint32> computeStride( float32 intervalSeconds );

        /** @brief 시각을 처음으로 되돌립니다(새 월드 · 새 연결). */
        void reset( NetClockSide side );
        /** @brief 시간을 흘립니다. 프레임에 한 번입니다. */
        void advance( GameSeconds frameTime );
        /** @brief 클라이언트 — 서버 틱이 든 메시지를 받았습니다. 추정보다 앞이면 추정을 끌어올립니다. 서버의 시계에는 아무 일도 하지 않습니다. */
        void observe( uint32 serverTick );

        /** @brief 지금 서버 틱(소수)입니다. 클라이언트가 틱을 받기 전에는 0 입니다. */
        float64 getServerTick() const { return _serverTick; }
        /**
         * @brief 받은 가장 새 틱에 그 뒤 흐른 시간을 더한 값입니다 — 지금 회선의 한쪽 지연만큼 서버보다 뒤다. 서버의 시계에서는 지금 틱이다.
         * @details `getServerTick` 은 되돌아가지 않으려고 지연이 가장 짧았던 때의 추정을 이어 가, 지연이 늘어난 뒤에는 한쪽 지연보다 앞선다 — 거기에 왕복의 절반을 더하면 지연을 두 번 센다.
         *          서버의 지금을 어림하는 쪽(`SceneNetLink::computeSecondsSince`)은 이 값에 왕복의 절반을 더한다.
         */
        float64 getReceivedTick() const;
        /** @brief 이미 지난 서버 틱입니다. */
        uint32 getWholeTick() const;
        /** @brief 간격이 @p stride 틱인 기능의 지난 틱 번호입니다. */
        uint32 getWholeTick( uint32 stride ) const { return getWholeTick() / stride; }
        /** @brief 시각을 아는지입니다 — 서버는 늘, 클라이언트는 틱을 하나라도 받았으면 참입니다. */
        bool         hasServerTick() const { return _side == NetClockSide::Authority || _bObserved == SW_TRUE; }
        NetClockSide getSide() const { return _side; }

    private:
        float64      _serverTick; ///< float64 — float32 틱은 60 Hz 로 약 19 시간(2^22 틱)에서 한 프레임 걸음을 잃어 보간이 멈춘다
        float64      _newestAge;  ///< 클라이언트 — 받은 가장 새 틱 뒤 흐른 시간(틱)
        uint32       _newestTick; ///< 클라이언트 — 받은 가장 새 틱
        NetClockSide _side;
        uint8        _bObserved;
    };
} // namespace sw

namespace sw
{
    /** @brief 화면 시계 설정입니다. */
    struct NetRenderClockConfig
    {
        uint32  _stride{ 1 };                ///< 이 기능의 틱 하나가 서버 틱 몇 개인지(`NetClock::computeStride`)
        float32 _interpolationDelay{ 0.1f }; ///< 화면 지연의 최솟값(초). 실제 지연은 이 값과 (표본 간격 x `NetRenderClock::kSampleIntervalsBehind`) 중 큰 쪽이다
        float32 _sampleInterval{ 0.0f };     ///< 표본(스냅샷, 자세)이 오는 간격(초). 0이면 최솟값만 쓴다
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetRenderClock
     * @brief 기능 하나가 그리는 시각입니다 — 월드의 서버 시계보다 보간 지연만큼 뒤이고, 받은 표본을 지나치지 않으며, 되돌아가지 않습니다. 단위는 그 기능의 틱(소수)입니다.
     * @details 지연 = max( `_interpolationDelay`, 표본 간격 x `kSampleIntervalsBehind`(2) ). 표본이 두 간격 뒤에 있어야 하나를 잃거나 흔들림으로 늦어도 그다음 표본과의 사이를 보간할 수 있다.
     *          한 간격뿐이면 표본을 기다리며 멈춰 선다. 유니티 Netcode for Entities `InterpolationTick`(지연 기본 2틱)에 해당한다.
     * @code
     *     render.observeSample( tick );        // 표본을 받을 때마다
     *     render.follow( worldClock );         // 프레임에 한 번
     *     const float64 tick = render.getRenderTick();    // 음수면 아직 그릴 때가 아니다
     * @endcode
     */
    class SW_API NetRenderClock
    {
    public:
        static constexpr float32 kSampleIntervalsBehind = 2.0f; ///< 화면 지연은 표본 간격의 이 배수 이상이다
        static constexpr float64 kNoRenderTick          = -1.0; ///< 화면 틱이 아직 없다는 값이다(표본을 받기 전)

        NetRenderClock();

        /** @brief 설정을 저장하고 비웁니다. */
        void initialize( const NetRenderClockConfig& settings );
        /** @brief 받은 표본과 화면 틱을 비웁니다(재연결). 설정은 그대로 둡니다. */
        void reset();
        /** @brief 틱 @p tick(이 기능의 번호)의 표본을 받았습니다. */
        void observeSample( uint32 tick );
        /** @brief 월드의 시계를 지연만큼 뒤에서 따라갑니다. 프레임에 한 번입니다 — 한 프레임의 표본이 모두 한 시각을 본다. */
        void follow( const NetClock& clock );
        /** @brief 지금 화면 지연(초)입니다. */
        float32 computeInterpolationDelay() const;

        /** @brief 그릴 틱(소수)입니다. 표본을 받기 전에는 `kNoRenderTick` 이고, 처음 몇 틱은 지연 때문에 음수일 수 있습니다(음수면 그리지 않습니다). */
        float64 getRenderTick() const { return _renderTick; }
        uint32  getNewestTick() const { return _newestTick; }
        bool    hasSample() const { return _bHasSample == SW_TRUE; }
        /** @brief 표본 간격(초)입니다. 주기가 다른 표본을 여럿 받으면 가장 긴 것을 씁니다(파괴 덩어리에서는 등록한 오브젝트의 가장 긴 자세 간격). */
        void    setSampleInterval( float32 seconds ) { _settings._sampleInterval = seconds; }
        float32 getSampleInterval() const { return _settings._sampleInterval; }

    private:
        NetRenderClockConfig _settings;
        float64              _renderTick;
        uint32               _newestTick;
        uint8                _bHasSample;
        uint8                _bRendering; ///< 화면 틱이 정해졌다 — 그 뒤로는 되돌아가지 않는다
    };
} // namespace sw
