/**
 * @file NetHostThread.h
 * @brief `NetHost` 를 게임 프레임과 따로 도는 전용 네트워크 스레드입니다 — 소켓을 기다렸다가(`poll`) 받기 · 확인 · 재전송 · 유지 · 타임아웃을 돌립니다.
 * @details 게임 스레드가 멈춰도(로딩 · 긴 프레임 · 중단점) 연결이 끊기지 않고, 받은 패킷의 확인이 프레임 간격만큼 늦지 않습니다(RTT 가 프레임
 *          길이를 포함하지 않는다). 게임은 아무 스레드에서나 `NetHost::sendMessage` · `receiveMessage` · `drainEvents` 를 부릅니다 — `NetHost`
 *          가 잠금으로 지킨다. 스레드가 도는 동안에는 `NetHost::update` 를 직접 부르지 않습니다.
 *
 *          작업 스레드 풀(`TaskManager`)에 맡기지 않는 까닭: 소켓을 기다리며 잠드는 일이라 풀의 워커 하나를 붙잡게 된다. 로그 · 파일 감시와 같은
 *          "기다리는 일은 전용 스레드" 규칙입니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Concurrency/atomic.h"

#include <thread>

namespace sw
{
    class NetHost;

    /** @brief 네트워크 스레드 설정입니다. 시간은 초입니다. */
    struct NetHostThreadSettings
    {
        /**
         * @brief 받을 것이 없을 때 한 번에 기다리는 가장 긴 시간입니다. 보낼 패킷(보내기 간격 · 재전송 · 유지)은 이 간격 안에 나간다 —
         *        `NetHostSettings::_sendInterval` 보다 충분히 짧게. 받을 것이 오면 바로 깬다(UDP).
         */
        float32 _maxWaitSeconds{ 0.002f };
    };

    /**
     * @class NetHostThread
     * @brief `start` 로 스레드를 띄우고 `stop`(또는 소멸)으로 합류합니다. 호스트 · 전송은 스레드보다 오래 살아야 합니다.
     * @code
     *     host.initialize( &transport, settings );
     *     (void)host.listen();
     *     (void)netThread.start( &host );
     *     // 게임 스레드: host.sendMessage( … ) · router.pump( host ) — update 는 부르지 않는다
     *     netThread.stop();
     *     host.disconnectAll();
     * @endcode
     */
    class SW_API NetHostThread
    {
    public:
        NetHostThread();
        ~NetHostThread();

        NetHostThread( const NetHostThread& )            = delete;
        NetHostThread& operator=( const NetHostThread& ) = delete;

        /** @brief 스레드를 띄웁니다. 이미 돌고 있거나 @p pHost 가 없으면 false 입니다. */
        [[nodiscard]] bool start( NetHost* pHost, const NetHostThreadSettings& settings = NetHostThreadSettings{} );
        /** @brief 멈추고 합류합니다(최대 `_maxWaitSeconds` 남짓). 돌고 있지 않으면 아무것도 하지 않는다. */
        void stop();

        bool isRunning() const { return _bRunning.load( std::memory_order_acquire ); }
        /** @brief 스레드가 `update` 를 돈 수입니다(진단 · 시험). */
        uint64 getUpdateCount() const { return _updateCount.load( std::memory_order_relaxed ); }
        /** @brief 스레드가 `update` 에 넘기는 시각(초, 단조 증가 — 프로세스 안에서 공통)입니다. */
        static float64 getTime();

    private:
        void run();

        std::thread           _thread;
        NetHost*              _pHost;
        NetHostThreadSettings _settings;
        atomic<uint64>        _updateCount;
        atomic<bool>          _bStopRequested;
        atomic<bool>          _bRunning;
    };
} // namespace sw
