/**
 * @file Test/TestFramework/TestLoopbackCluster.h
 * @brief 네트워크 시험의 호스트 묶음 — 루프백 망 하나 위에서 끝점마다 흉내(`NetEmulationTransport`)를 씌운 `NetHost` 여럿을 손 시각이나 네트워크 스레드로 돌립니다.
 * @details CoreTest · EngineTest 가 같이 씁니다(Core 헤더만 include 한다 — CoreTest 의 엔진 금지 규칙). 씬 · 오브젝트 · 라우터까지 세우는 판은
 *          GameFramework 의 `NetSimHarness` 를 쓴다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/deque.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Connection/NetHostThread.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/Network/Transport/NetTransport.h"

namespace test
{
    /**
     * @class LoopbackCluster
     * @brief 호스트 0 이 서버, 1 부터가 클라이언트입니다(`addHost` 순서). 회선 조건은 모든 끝점의 보내는 쪽 흉내에 한 번에 겁니다(`setConditions`).
     * @details - 손 시각(`step` · `run`): 시각을 넘기고 흉내를 **모두 먼저** `update` 한 뒤(이 시각까지 닿을 패킷이 망에 실려 배달된다) 호스트를 더한 순서로
     *            `update` 한다. 흉내는 보내는 쪽 줄이라 호스트만 차례로 돌리면 뒤에 도는 호스트가 보낸 것이 한 프레임 늦는다 — 여기서는 지연 L 이 방향 ·
     *            호스트 순서와 상관없이 "보낸 시각 + L 이 지난 첫 `step`" 이다. 한 스레드라 같은 씨앗이면 같은 손실이다.
     *          - 스레드(`startThreads`): 호스트마다 `NetHostThread` 가 돈다. 도는 동안 `step` · `addHost` 는 부르지 않는다(단언). 소멸자가 멈춘다.
     *          - 흉내가 꺼져 있으면(기본) 패킷은 그대로 루프백에 넘어간다 — 맨 루프백과 같다.
     */
    class LoopbackCluster
    {
    public:
        /** @param seed 흉내 씨앗 — 호스트 n 의 흉내는 @p seed + n 입니다. */
        explicit LoopbackCluster( uint32 seed = 1u );
        ~LoopbackCluster();

        LoopbackCluster( const LoopbackCluster& )            = delete;
        LoopbackCluster& operator=( const LoopbackCluster& ) = delete;

        /** @brief 끝점(`127.0.0.1:port`) · 흉내 · 호스트를 하나 더합니다. 호스트 번호(더한 순서)이고, 포트가 겹치면 -1 입니다. */
        int32 addHost( uint16 port, const sw::NetHostSettings& settings );
        /**
         * @brief 호스트 0 이 listen 하고 나머지가 0 에 connect 한 뒤 @p stepCount 번 `step` 합니다.
         * @return listen · connect 를 모두 시작했고 클라이언트가 모두 연결됐으면 true 입니다.
         */
        [[nodiscard]] bool connectClients( int32 stepCount, float64 deltaTime = 1.0 / 60.0 );
        /** @brief 호스트마다 네트워크 스레드를 띄웁니다. 하나라도 못 띄우면 false 입니다. */
        [[nodiscard]] bool startThreads( const sw::NetHostThreadSettings& settings = sw::NetHostThreadSettings{} );
        /** @brief 스레드를 모두 멈춥니다(여러 번 불러도 된다). 그 뒤 `step` 을 다시 쓸 수 있다. */
        void stopThreads();

        /** @brief 시각을 @p deltaTime 넘깁니다 — 흉내 모두 → 호스트 모두(더한 순서). */
        void step( float64 deltaTime = 1.0 / 60.0 );
        /** @brief @p seconds 동안 @p deltaTime 마다 `step` 합니다. */
        void run( float64 seconds, float64 deltaTime = 1.0 / 60.0 );

        /** @brief 모든 끝점의 보내는 쪽 기본 조건입니다 — 왕복이 같은 조건이다. 다음 보내기부터. */
        void setConditions( const sw::NetEmulationConditions& conditions );

        sw::NetHost& getHost( int32 hostIndex );
        sw::NetHost& getServer() { return getHost( 0 ); }
        /** @brief 클라이언트 @p clientIndex(0 부터)는 호스트 @p clientIndex + 1 입니다. */
        sw::NetHost&       getClient( int32 clientIndex ) { return getHost( clientIndex + 1 ); }
        sw::NetHostThread& getThread( int32 hostIndex );
        int32              getHostCount() const { return static_cast<int32>( _listHost.size() ); }
        int32              getClientCount() const { return _listHost.empty() ? 0 : getHostCount() - 1; }
        /** @brief 클라이언트가 모두 서버(연결 0)에 연결됐으면 true 입니다. */
        bool areClientsConnected() const;

    private:
        sw::LoopbackNetwork                  _network;
        sw::deque<sw::NetEmulationTransport> _listEmulation; ///< 호스트마다 — deque 라 주소가 움직이지 않는다(호스트가 들고 있다)
        sw::deque<sw::NetHost>               _listHost;      ///< deque — NetHost 는 옮길 수 없다(잠금을 품는다)
        sw::deque<sw::NetHostThread>         _listThread;    ///< `startThreads` 뒤에만 — 호스트와 같은 번호
        float64                              _time;
        uint32                               _seed;
    };
} // namespace test
