/**
 * @file NetSimHarness.h
 * @brief 한 프로세스 가상 서버 — 서버 월드 하나와 클라이언트 월드 여럿이 **각자 씬 · 오브젝트 매니저 · 물리**를 갖고 루프백 망 위에서 고정 스텝으로 함께 돕니다.
 * @details 언리얼 PIE 의 "Play As Client, Number of Players N"(한 프로세스에 서버 월드 + 클라이언트 월드) + Network Emulation, 유니티 Multiplayer Play Mode +
 *          Network Simulator 의 자리입니다. 엔진 루프(`EngineLoop`)는 활성 씬 하나만 틱하므로 하니스는 월드마다 오브젝트 매니저를 **직접** 틱합니다 — 렌더러 ·
 *          오디오는 쓰지 않습니다(화면 없이 · nogpu 시험 · 게임 자동화).
 *
 *          - **결정적**: 시각은 틱 × 간격(벽시계를 읽지 않는다), 망 · 연결마다의 흉내 · 도전 소금은 모두 씨앗에서 나온다. 한 스레드가 `step` 을 부르면
 *            같은 씨앗 · 같은 게임이면 같은 패킷이 같은 틱에 도착한다.
 *          - **연결마다 조건**: 클라이언트마다 올림(클라이언트 → 서버) · 내림(서버 → 클라이언트) 조건(`NetEmulationConditions` — 지연 · 흔들림 · 손실 ·
 *            중복 · 순서 바뀜 · 대역폭)을 따로 준다. 도중에 바꿀 수 있다(`setLinkConditions`).
 *          - **늦은 참가 · 떠남**: `addClient` · `removeClient` 는 어느 틱 사이에서나 부른다. 떠난 클라이언트는 끊김 알림을 보내고(서버는 타임아웃이 아니라
 *            Remote 로 안다) 월드가 내려간다. 포트는 다시 쓰지 않는다.
 *
 *          틱 하나(`step`)의 순서 — 언리얼 NetDriver 의 TickDispatch(받은 것 나눠 주기) → 월드 틱 → TickFlush(보내기):
 *          1. 시각 = 틱 × 간격.
 *          2. 월드마다(서버 → 클라이언트 번호 순): 라우터가 호스트 사건(연결 · 끊김)과 받은 메시지를 꺼내 처리기에 나눠 준다(사건 먼저) →
 *             사건을 `INetSimSession::onHostEvent` 로 → `INetSimSession::onTickBegin` → 오브젝트 매니저 틱(물리 포함) →
 *             `INetSimSession::onTickEnd`(보낼 것을 쌓는다).
 *          3. 망: 호스트마다 `update`(쌓인 것을 보낸다) → 흉내 줄에서 때가 된 패킷을 망에 싣고 망을 그 시각까지 배달 → 호스트마다 다시 `update`(받는다).
 *          그래서 조건이 깨끗하면 틱 N 끝에 보낸 메시지를 틱 N + 1 이 받는다. 지연 L 이면 보낸 시각 + L 이 지난 첫 틱이다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/NetTypes.h"
#include "Core/Network/Transport/NetEmulation.h"
#include "Core/Network/Transport/NetTransport.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;
    class NetSimWorld;
    class Scene;

    /** @brief 월드의 역할입니다. */
    enum class NetSimRole : uint8
    {
        Server = 0,
        Client
    };
} // namespace sw

namespace sw
{
    /** @brief 클라이언트 하나와 서버 사이 회선의 두 방향 조건입니다. */
    struct NetSimLinkConditions
    {
        NetEmulationConditions _upstream{};   ///< 클라이언트 → 서버(클라이언트의 보내는 쪽에 건다)
        NetEmulationConditions _downstream{}; ///< 서버 → 클라이언트(서버의 보내는 쪽에 그 목적지만)

        /** @brief 두 방향에 같은 조건을 겁니다. */
        static NetSimLinkConditions makeSymmetric( const NetEmulationConditions& conditions ) { return NetSimLinkConditions{ conditions, conditions }; }
    };
} // namespace sw

namespace sw
{
    /** @brief 하니스 설정입니다. */
    struct NetSimSettings
    {
        NetHostSettings _hostSettings{};             ///< 월드마다의 호스트 설정. 도전 소금 씨앗은 `_seed` 에서 다시 정한다
        float64         _tickInterval{ 1.0 / 60.0 }; ///< 고정 스텝(초) — 월드의 오브젝트 틱 · 망 시각이 모두 이 간격으로 간다
        uint32          _seed{ 1u };                 ///< 망 · 연결 흉내 · 도전 소금의 씨앗
        uint16          _serverPort{ 7000 };         ///< 서버 끝점 포트. 클라이언트는 그 뒤 번호를 차례로(다시 쓰지 않는다)
    };
} // namespace sw

namespace sw
{
    /** @brief 끝점 하나가 보내고 받은 양입니다(헤더 · 체크섬 포함 데이터그램 바이트). 보낸 양은 흉내가 버리기 전 — 회선에 내놓은 것이다. */
    struct NetSimTraffic
    {
        uint64 _sentBytes{ 0 };
        uint64 _sentPacketCount{ 0 };
        uint64 _receivedBytes{ 0 };
        uint64 _receivedPacketCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetSimTrafficCounter
     * @brief 다른 전송을 감싸 보내고 받은 바이트를 셉니다(목적지마다의 보낸 양 포함). 하니스가 호스트와 흉내 사이에 둡니다.
     */
    class SW_GF_API NetSimTrafficCounter final : public INetTransport
    {
    public:
        explicit NetSimTrafficCounter( INetTransport* pInner );

        [[nodiscard]] bool send( const NetAddress& to, const uint8* pData, int32 size ) override;
        [[nodiscard]] bool receive( NetAddress& outFrom, vector<uint8>& outBuffer ) override;
        NetAddress         getLocalAddress() const override;
        void               update( float64 time ) override;

        const NetSimTraffic& getTraffic() const { return _traffic; }
        /** @brief @p to 로 보낸 바이트입니다(서버 끝점에서 클라이언트 하나로 내려간 양). */
        uint64 getSentBytesTo( const NetAddress& to ) const;

    private:
        struct Destination
        {
            NetAddress _to{};
            uint64     _sentBytes{ 0 };
        };

        INetTransport*      _pInner;
        vector<Destination> _listDestination;
        NetSimTraffic       _traffic;
    };
} // namespace sw

namespace sw
{
    /**
     * @class INetSimSession
     * @brief 월드 하나 위에서 도는 게임의 네트워크 쪽입니다(복제 서버 · 클라이언트, 게임 메시지). 월드가 소유하고 월드와 함께 내려갑니다.
     * @details 처리기(`INetMessageHandler`)는 `INetSimGame::createSession` 에서 `world.getRouter()` 에 걸어 둔다 — 하니스가 틱마다 받은 메시지를 나눠 준다.
     */
    class SW_GF_API INetSimSession
    {
    public:
        INetSimSession()          = default;
        virtual ~INetSimSession() = default;

        INetSimSession( const INetSimSession& )            = delete;
        INetSimSession& operator=( const INetSimSession& ) = delete;

        /** @brief 오브젝트 틱 앞입니다(받은 메시지는 이미 나눠 줬다). 서버는 입력을 꺼내고, 클라이언트는 받은 상태를 씬에 적는다. */
        virtual void onTickBegin( NetSimWorld& world, float32 deltaTime )
        {
            (void)world;
            (void)deltaTime;
        }
        /** @brief 오브젝트 틱 뒤입니다. 보낼 것(스냅샷 · 사건 · 입력)을 쌓는다 — 이 틱의 망 단계에서 나간다. */
        virtual void onTickEnd( NetSimWorld& world, float32 deltaTime )
        {
            (void)world;
            (void)deltaTime;
        }
        /**
         * @brief 호스트 사건입니다(서버 — 클라이언트 연결 · 끊김, 클라이언트 — 서버 연결 · 끊김). 틱 앞, 라우터가 처리기에 사건을 알린 바로 뒤에 불린다.
         * @details 키트의 연결마다 상태는 라우터가 `INetMessageHandler::onConnectionOpened` · `onConnectionClosed` 로 이미 맞춘다 — 여기는 게임 몫만.
         */
        virtual void onHostEvent( NetSimWorld& world, const NetHostEvent& event )
        {
            (void)world;
            (void)event;
        }
        /** @brief 월드가 내려가기 직전입니다(플레이 끝 · 씬 파괴 앞). */
        virtual void onWorldStopping( NetSimWorld& world ) { (void)world; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class INetSimGame
     * @brief 하니스가 월드를 세울 때마다 부르는 게임입니다. 월드의 내용(오브젝트 · 씬 문서)을 짓고 그 월드의 세션을 돌려줍니다.
     */
    class SW_GF_API INetSimGame
    {
    public:
        INetSimGame()          = default;
        virtual ~INetSimGame() = default;

        INetSimGame( const INetSimGame& )            = delete;
        INetSimGame& operator=( const INetSimGame& ) = delete;

        /**
         * @brief @p world 의 내용을 짓고 세션을 돌려줍니다. 하니스가 그 뒤 플레이를 시작한다(`GameObjectManager::beginPlay`).
         * @details 서버 월드가 먼저, 클라이언트는 `addClient` 때 지어진다(늦은 참가도 같은 길). nullptr 이면 세션 없이 돈다.
         */
        virtual unique_ptr<INetSimSession> createSession( NetSimWorld& world ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetSimWorld
     * @brief 하니스의 월드 하나 — 씬(자기 오브젝트 매니저 · 물리) · 호스트 · 메시지 라우터 · 세션입니다. 하니스가 만들고 소유합니다.
     */
    class SW_GF_API NetSimWorld
    {
    public:
        NetSimWorld( NetSimRole role, int32 worldIndex, INetTransport* pTransport, const NetHostSettings& hostSettings );
        ~NetSimWorld();

        NetSimWorld( const NetSimWorld& )            = delete;
        NetSimWorld& operator=( const NetSimWorld& ) = delete;

        NetSimRole getRole() const { return _role; }
        bool       isServer() const { return _role == NetSimRole::Server; }
        /** @brief 하니스 안의 번호입니다 — 서버 0, 클라이언트는 1 부터 들어온 순서(떠나도 다시 쓰지 않는다). 호스트의 연결 id 와 다르다. */
        int32 getWorldIndex() const { return _worldIndex; }
        /** @brief 이 월드가 돈 틱 수입니다(늦게 들어온 클라이언트는 하니스 틱보다 작다). */
        uint32 getLocalTick() const { return _localTick; }
        /** @brief 클라이언트 — 서버에 연결돼 있으면 true. 서버는 늘 true 입니다. */
        bool isConnected() const;

        Scene&             getScene() { return *_pScene; }
        GameObjectManager& getObjectManager();
        NetHost&           getHost() { return _host; }
        NetMessageRouter&  getRouter() { return _router; }
        INetSimSession*    getSession() const { return _pSession.get(); }
        /** @brief 라우터의 처리기가 받지 않은 메시지(게임 영역 등)입니다. 틱마다 비우고 다시 채운다. */
        const vector<NetReceivedMessage>& getUnhandledMessages() const { return _listUnhandled; }
        /** @brief 이 월드의 끝점이 보내고 받은 양입니다. */
        const NetSimTraffic& getTraffic() const { return _counter.getTraffic(); }
        /** @brief 서버 — @p to(클라이언트 끝점)로 내려보낸 바이트입니다. */
        uint64     getSentBytesTo( const NetAddress& to ) const { return _counter.getSentBytesTo( to ); }
        NetAddress getAddress() const { return _counter.getLocalAddress(); }

    private:
        friend class NetSimHarness;

        void start( INetSimGame* pGame );
        void tick( float32 deltaTime );
        void stop();

        NetSimTrafficCounter       _counter; ///< 호스트 → 셈 → 흉내 → 루프백 끝점
        NetHost                    _host;
        NetMessageRouter           _router;
        unique_ptr<Scene>          _pScene;
        unique_ptr<INetSimSession> _pSession;
        vector<NetReceivedMessage> _listUnhandled;
        vector<NetHostEvent>       _listEvent;
        int32                      _worldIndex;
        uint32                     _localTick;
        NetSimRole                 _role;
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetSimHarness
     * @brief 서버 월드 1 + 클라이언트 월드 N 을 한 스레드에서 고정 스텝으로 돌립니다. 파일 머리말 참고.
     * @code
     *     NetSimHarness harness;
     *     harness.initialize( settings, &game );                          // 서버 월드를 짓는다
     *     const int32 clientA = harness.addClient( NetSimLinkConditions::makeSymmetric( lossy ) );
     *     harness.stepTicks( 600 );
     *     const int32 lateClient = harness.addClient( {} );               // 늦은 참가
     *     harness.stepUntil( &isConverged, 300 );
     *     harness.removeClient( clientA );
     * @endcode
     */
    class SW_GF_API NetSimHarness
    {
    public:
        NetSimHarness();
        ~NetSimHarness();

        NetSimHarness( const NetSimHarness& )            = delete;
        NetSimHarness& operator=( const NetSimHarness& ) = delete;

        /** @brief 망을 세우고 서버 월드를 짓습니다(`INetSimGame::createSession` · 플레이 시작 · listen). @p pGame 은 하니스보다 오래 산다. */
        [[nodiscard]] bool initialize( const NetSimSettings& settings, INetSimGame* pGame );
        /** @brief 클라이언트 · 서버 월드를 모두 내립니다. 소멸자도 부른다. */
        void shutdown();

        /** @brief 고정 스텝 하나를 돕니다(파일 머리말의 순서). */
        void step();
        void stepTicks( uint32 tickCount );
        /**
         * @brief @p pPredicate 가 true 를 돌려줄 때까지(또는 @p maxTicks 만큼) 돕니다. 돈 틱 수를 돌려주고, 끝내 맞지 않으면 -1 입니다.
         * @param pPredicate 틱마다 부른다(@p pContext 를 그대로 넘긴다). 처음에 이미 맞으면 0 이다.
         */
        int32 stepUntil( bool ( *pPredicate )( const NetSimHarness&, void* ), void* pContext, uint32 maxTicks );

        /** @brief 클라이언트 월드를 짓고 서버에 연결을 시작합니다. 월드 번호(1 부터)입니다. 실패하면 -1 입니다. */
        int32 addClient( const NetSimLinkConditions& conditions );
        /** @brief 클라이언트가 떠납니다 — 끊김 알림을 보내고 월드를 내린다. 없는 번호면 아무것도 하지 않는다. */
        void removeClient( int32 worldIndex );
        /** @brief 클라이언트 회선의 조건을 바꿉니다(다음 보내기부터). */
        void setLinkConditions( int32 worldIndex, const NetSimLinkConditions& conditions );

        NetSimWorld&       getServer() { return *_pServer; }
        const NetSimWorld& getServer() const { return *_pServer; }
        /** @brief 살아 있는 클라이언트 월드입니다. 없으면 nullptr 입니다. */
        NetSimWorld* findClient( int32 worldIndex ) const;
        /** @brief 살아 있는 클라이언트 월드들(들어온 순서)입니다. */
        void  collectClients( vector<NetSimWorld*>& outListWorld ) const;
        int32 getClientCount() const { return static_cast<int32>( _listClient.size() ); }
        /** @brief 살아 있는 클라이언트가 모두 서버에 연결됐으면 true 입니다. */
        bool areAllClientsConnected() const;

        uint32  getTick() const { return _tick; }
        float64 getTime() const { return _time; }
        float64 getTickInterval() const { return _settings._tickInterval; }
        /** @brief 망이 배달한 · 버린 패킷 수입니다. */
        const LoopbackNetwork* getNetwork() const { return _pNetwork.get(); }

    private:
        /** @brief 끝점 하나의 흉내 전송입니다. 월드가 떠나도 남아 줄에 든 패킷(끊김 알림)을 마저 내보낸다. */
        struct Link
        {
            unique_ptr<NetEmulationTransport> _pEmulation{};
            NetAddress                        _address{};
            int32                             _worldIndex{ -1 };
        };

        Link*        createLink( int32 worldIndex );
        Link*        findLink( int32 worldIndex );
        NetSimWorld* createWorld( NetSimRole role, int32 worldIndex, Link& link );
        void         applyLinkConditions( int32 worldIndex, const NetSimLinkConditions& conditions );
        void         updateNetwork();

        NetSimSettings                  _settings;
        unique_ptr<LoopbackNetwork>     _pNetwork;
        vector<unique_ptr<Link>>        _listLink;
        unique_ptr<NetSimWorld>         _pServer;
        vector<unique_ptr<NetSimWorld>> _listClient; ///< 들어온 순서
        INetSimGame*                    _pGame;
        float64                         _time;
        uint32                          _tick;
        int32                           _nextWorldIndex;
        bool                            _bStepping;
    };
} // namespace sw
