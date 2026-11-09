/**
 * @file LoopbackCluster.h
 * @brief 네트워크 테스트의 엔드포인트 묶음 — 루프백 망 하나 위에서 엔드포인트마다 흉내(`NetEmulationTransport`)를 씌운 `NetEndpoint` 여럿을 손 시각이나 네트워크 스레드로 돌립니다.
 * @details Core 의 연결 · 라우터 · 스레드 단위 테스트가 함께 쓴다. 루프백 망은 프로세스 안 메모리라 포트가 실제 소켓과 부딪치지 않는다(고정 포트를 써도 된다).
 *          씬 · 오브젝트 · 라우터까지 세우는 판은 GameFramework 의 `NetSimHarness` 를 쓴다.
 */
#pragma once
#include "Core/Container/deque.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Connection/NetEndpoint.h"
#include "Core/Network/Connection/NetEndpointThread.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/Network/Transport/NetTransport.h"

namespace sw::test
{
    /**
     * @class LoopbackCluster
     * @brief 엔드포인트 0 이 서버, 1 부터가 클라이언트입니다(`addEndpoint` 순서). 회선 조건은 모든 엔드포인트의 보내는 쪽 흉내에 한 번에 겁니다(`setConditions`).
     * @details - 손 시각(`step` · `run`): 시각을 넘기고 흉내를 **모두 먼저** `update` 한 뒤(이 시각까지 닿을 패킷이 망에 실려 배달된다) 엔드포인트를 더한 순서로
     *            `update` 한다. 흉내는 보내는 쪽 줄이라 엔드포인트만 차례로 돌리면 뒤에 도는 쪽이 보낸 것이 한 프레임 늦는다 — 여기서는 지연 L 이 방향 ·
     *            순서와 상관없이 "보낸 시각 + L 이 지난 첫 `step`" 이다. 한 스레드라 같은 씨앗이면 같은 손실이다.
     *          - 스레드(`startThreads`): 엔드포인트마다 `NetEndpointThread` 가 돈다. 도는 동안 `step` · `addEndpoint` 는 부르지 않는다(단언). 소멸자가 멈춘다.
     *          - 흉내가 꺼져 있으면(기본) 패킷은 그대로 루프백에 넘어간다 — 맨 루프백과 같다.
     *          - 키트 테스트(락스텝 · 복제 병렬)도 쓰므로 Engine.dll 에서 내보낸다(`SW_API`).
     */
    class SW_API LoopbackCluster
    {
    public:
        /** @param seed 흉내 씨앗 — 엔드포인트 n 의 흉내는 @p seed + n 입니다. */
        explicit LoopbackCluster( uint32 seed = 1u );
        ~LoopbackCluster();

        LoopbackCluster( const LoopbackCluster& )            = delete;
        LoopbackCluster& operator=( const LoopbackCluster& ) = delete;

        /** @brief 루프백 엔드포인트(`127.0.0.1:port`) · 흉내 · `NetEndpoint` 를 하나 더합니다. 번호(더한 순서)이고, 포트가 겹치면 비어 있습니다. */
        [[nodiscard]] optional<int32> addEndpoint( uint16 port, const NetEndpointConfig& settings );
        /**
         * @brief 엔드포인트 0 이 listen 하고 나머지가 0 에 connect 한 뒤 @p stepCount 번 `step` 합니다.
         * @return listen · connect 를 모두 시작했고 클라이언트가 모두 연결됐으면 true 입니다.
         */
        [[nodiscard]] bool connectClients( int32 stepCount, float64 deltaTime = 1.0 / 60.0 );
        /** @brief 엔드포인트마다 네트워크 스레드를 띄웁니다. 하나라도 못 띄우면 false 입니다. */
        [[nodiscard]] bool startThreads( const NetEndpointThreadConfig& settings = NetEndpointThreadConfig{} );
        /** @brief 스레드를 모두 멈춥니다(여러 번 불러도 된다). 그 뒤 `step` 을 다시 쓸 수 있다. */
        void stopThreads();

        /** @brief 시각을 @p deltaTime 넘깁니다 — 흉내 모두 → 엔드포인트 모두(더한 순서). */
        void step( float64 deltaTime = 1.0 / 60.0 );
        /** @brief @p seconds 동안 @p deltaTime 마다 `step` 합니다. */
        void run( float64 seconds, float64 deltaTime = 1.0 / 60.0 );

        /** @brief 모든 엔드포인트의 보내는 쪽 기본 조건입니다 — 왕복이 같은 조건이다. 다음 보내기부터. */
        void setConditions( const NetEmulationConditions& conditions );

        NetEndpoint& getEndpoint( int32 endpointIndex );
        NetEndpoint& getServer() { return getEndpoint( 0 ); }
        /** @brief 클라이언트 @p clientIndex(0 부터)는 엔드포인트 @p clientIndex + 1 입니다. */
        NetEndpoint&       getClient( int32 clientIndex ) { return getEndpoint( clientIndex + 1 ); }
        NetEndpointThread& getThread( int32 endpointIndex );
        int32              getEndpointCount() const { return static_cast<int32>( _listEndpoint.size() ); }
        int32              getClientCount() const { return _listEndpoint.empty() ? 0 : getEndpointCount() - 1; }
        /** @brief 클라이언트가 모두 서버(연결 0)에 연결됐으면 true 입니다. */
        bool areClientsConnected() const;

    private:
        LoopbackNetwork              _network;
        deque<NetEmulationTransport> _listEmulation; ///< 엔드포인트마다 — deque 라 주소가 움직이지 않는다(엔드포인트가 들고 있다)
        deque<NetEndpoint>           _listEndpoint;  ///< deque — NetEndpoint 는 옮길 수 없다(잠금을 품는다)
        deque<NetEndpointThread>     _listThread;    ///< `startThreads` 뒤에만 — 엔드포인트와 같은 번호
        float64                      _time;
        uint32                       _seed;
    };
} // namespace sw::test
