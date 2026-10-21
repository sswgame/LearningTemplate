/**
 * @file NetSimHarness.h
 * @brief 한 프로세스 안에서 서버 월드 하나와 클라이언트 월드 여럿을 루프백 망 위에서 고정 스텝으로 함께 돌립니다.
 * @details 월드마다 씬, 오브젝트 매니저, 물리를 따로 가진다. 언리얼 PIE의 "Play As Client, Number of Players N"과 Network Emulation,
 *          유니티의 Multiplayer Play Mode와 Network Simulator에 해당한다.
 *          엔진 루프(`Engine`)는 활성 씬 하나만 틱하므로 하니스가 월드마다 오브젝트 매니저를 직접 틱한다.
 *          렌더러와 오디오는 쓰지 않으므로 화면 없이 nogpu 테스트와 게임 자동화에 쓸 수 있다.
 *
 *          - **결정적**: 시각은 틱 × 간격으로 계산하고 벽시계를 읽지 않는다. 망, 연결별 흉내, 챌린지 솔트는 모두 시드에서 나온다.
 *            한 스레드가 `step` 을 부르면 같은 시드와 같은 게임에서 같은 패킷이 같은 틱에 도착한다.
 *          - **연결별 조건**: 클라이언트마다 업스트림(클라이언트 → 서버)과 다운스트림(서버 → 클라이언트) 조건을 따로 준다.
 *            조건(`NetEmulationConditions`)은 지연, 지터, 손실, 중복, 손상, 순서 뒤바뀜, 대역폭이다. 도중에 `setLinkConditions` 로 바꿀 수 있다.
 *          - **늦은 참가와 이탈**: `addClient` 와 `removeClient` 는 어느 틱 사이에서나 부를 수 있다.
 *            떠나는 클라이언트는 연결 끊김 알림을 보내므로 서버는 타임아웃이 아니라 Remote 이벤트로 이탈을 안다. 월드는 종료되고 포트는 다시 쓰지 않는다.
 *
 *          `step` 한 번의 순서는 언리얼 NetDriver의 TickDispatch(받은 메시지 분배), 월드 틱, TickFlush(송신)와 같다.
 *          1. 시각을 틱 × 간격으로 정한다.
 *          2. 월드마다(서버가 먼저, 이어서 클라이언트를 번호 순으로) 다음을 처리한다.
 *             라우터가 호스트 이벤트(연결, 끊김)와 받은 메시지를 꺼내 핸들러에 나눠 준다. 이벤트를 먼저 처리한다.
 *             이어서 `INetSimSession::onHostEvent`, `INetSimSession::onTickBegin`, 오브젝트 매니저 틱(물리 포함), `INetSimSession::onTickEnd` 순서로 부른다.
 *             `onTickEnd` 에서 보낼 메시지를 쌓는다.
 *          3. 망을 처리한다. 호스트마다 `update` 로 쌓인 메시지를 보내고, 흉내 큐에서 전달 시각이 된 패킷을 망에 싣는다.
 *             망이 배달한 뒤 호스트마다 `update` 를 한 번 더 불러 받는다.
 *
 *          조건이 깨끗하면 틱 N 끝에 보낸 메시지를 틱 N + 1 이 받는다. 지연이 L 이면 보낸 시각 + L 이 지난 첫 틱에 받는다.
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

#include "Engine/Object/GameObject/Service/SceneEngineServices.h"

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
        NetEmulationConditions _upstream{};   ///< 클라이언트 → 서버 방향. 클라이언트의 송신 쪽에 적용한다
        NetEmulationConditions _downstream{}; ///< 서버 → 클라이언트 방향. 서버의 송신 쪽에서 그 클라이언트로 가는 패킷에만 적용한다

        /** @brief 두 방향에 같은 조건을 겁니다. */
        static NetSimLinkConditions makeSymmetric( const NetEmulationConditions& conditions ) { return NetSimLinkConditions{ conditions, conditions }; }
    };
} // namespace sw

namespace sw
{
    /** @brief 하니스 설정입니다. */
    struct NetSimConfig
    {
        NetHostConfig _hostSettings{};             ///< 월드마다 쓰는 호스트 설정. 챌린지 솔트 시드는 `_seed` 로 다시 정한다
        float64       _tickInterval{ 1.0 / 60.0 }; ///< 고정 스텝(초). 오브젝트 틱과 망 시각이 모두 이 간격으로 진행한다
        uint32        _seed{ 1u };                 ///< 망, 연결별 흉내, 챌린지 솔트의 시드
        uint16        _serverPort{ 7000 };         ///< 서버 엔드포인트 포트. 클라이언트는 그다음 번호부터 차례로 받고 다시 쓰지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 엔드포인트 하나가 보내고 받은 양입니다. 헤더와 체크섬을 포함한 데이터그램 바이트이고, 보낸 양은 흉내가 패킷을 버리기 전의 값입니다. */
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
     * @brief 다른 전송을 감싸 보내고 받은 바이트를 셉니다. 목적지별 송신량도 셉니다.
     * @details 하니스가 호스트와 흉내 사이에 둔다.
     */
    class SW_KIT_API NetSimTrafficCounter final : public INetTransport
    {
    public:
        explicit NetSimTrafficCounter( INetTransport* pInner );

        [[nodiscard]] bool send( const NetAddress& to, const uint8* pData, int32 size ) override;
        [[nodiscard]] bool receive( NetAddress& outFrom, vector<uint8>& outBuffer ) override;
        NetAddress         getLocalAddress() const override;
        void               update( float64 time ) override;

        const NetSimTraffic& getTraffic() const { return _traffic; }
        /** @brief @p to 로 보낸 바이트입니다. 서버 엔드포인트에서 클라이언트 하나로 내려간 양입니다. */
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
     * @brief 월드 하나 위에서 도는 게임의 네트워크 부분입니다. 복제 서버, 클라이언트, 게임 메시지를 다룹니다.
     * @details 월드가 소유하며 월드와 함께 종료된다.
     *          핸들러(`INetMessageHandler`)는 `INetSimGame::createSession` 에서 `world.getRouter()` 에 등록한다. 하니스가 틱마다 받은 메시지를 핸들러에 나눠 준다.
     */
    class SW_KIT_API INetSimSession
    {
    public:
        INetSimSession()          = default;
        virtual ~INetSimSession() = default;

        INetSimSession( const INetSimSession& )            = delete;
        INetSimSession& operator=( const INetSimSession& ) = delete;

        /** @brief 오브젝트 틱 직전에 호출합니다. 받은 메시지는 이미 분배된 상태이며, 서버는 입력을 꺼내고 클라이언트는 받은 상태를 씬에 반영한다. */
        virtual void onTickBegin( NetSimWorld& world, float32 deltaTime )
        {
            (void)world;
            (void)deltaTime;
        }
        /** @brief 오브젝트 틱 직후에 호출합니다. 보낼 스냅샷, 이벤트, 입력을 쌓아 두면 이 틱의 망 단계에서 전송된다. */
        virtual void onTickEnd( NetSimWorld& world, float32 deltaTime )
        {
            (void)world;
            (void)deltaTime;
        }
        /**
         * @brief 호스트 이벤트를 받아 처리합니다. 서버는 클라이언트의 연결과 끊김을, 클라이언트는 서버의 연결과 끊김을 받습니다.
         * @details 틱 직전에 라우터가 핸들러에 이벤트를 알린 바로 뒤에 불린다.
         *          키트의 연결별 상태는 라우터가 `INetMessageHandler::onConnectionOpened` 와 `onConnectionClosed` 로 이미 맞춰 두므로 여기서는 게임 쪽 처리만 한다.
         */
        virtual void onHostEvent( NetSimWorld& world, const NetHostEvent& event )
        {
            (void)world;
            (void)event;
        }
        /** @brief 월드가 종료되기 직전에 호출합니다. 플레이 종료와 씬 파괴보다 앞선다. */
        virtual void onWorldStopping( NetSimWorld& world ) { (void)world; }
    };
} // namespace sw

namespace sw
{
    /**
     * @class INetSimGame
     * @brief 하니스가 월드를 만들 때마다 호출하는 게임입니다. 월드의 내용(오브젝트와 씬 문서)을 만들고 그 월드의 세션을 반환합니다.
     */
    class INetSimGame
    {
    public:
        INetSimGame()          = default;
        virtual ~INetSimGame() = default;

        INetSimGame( const INetSimGame& )            = delete;
        INetSimGame& operator=( const INetSimGame& ) = delete;

        /**
         * @brief @p world 의 내용을 만들고 세션을 반환합니다. 하니스가 그 뒤에 플레이를 시작합니다(`GameObjectManager::beginPlay`).
         * @details 서버 월드를 먼저 만들고, 클라이언트 월드는 `addClient` 때 만든다. 늦은 참가도 같은 경로를 쓴다. nullptr 을 반환하면 세션 없이 돈다.
         */
        virtual unique_ptr<INetSimSession> createSession( NetSimWorld& world ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class NetSimWorld
     * @brief 하니스의 월드 하나입니다. 자기 씬(오브젝트 매니저와 물리), 호스트, 메시지 라우터, 세션을 가지며 하니스가 만들고 소유합니다.
     */
    class SW_KIT_API NetSimWorld
    {
    public:
        NetSimWorld( const SceneEngineServices& services, NetSimRole role, int32 worldIndex, INetTransport* pTransport, const NetHostConfig& hostSettings );
        ~NetSimWorld();

        NetSimWorld( const NetSimWorld& )            = delete;
        NetSimWorld& operator=( const NetSimWorld& ) = delete;

        NetSimRole getRole() const { return _role; }
        bool       isServer() const { return _role == NetSimRole::Server; }
        /** @brief 하니스 안의 월드 번호입니다. 서버는 0이고, 클라이언트는 들어온 순서대로 1부터 매기며 떠나도 번호를 다시 쓰지 않습니다. 호스트의 연결 id 와는 다릅니다. */
        int32 getWorldIndex() const { return _worldIndex; }
        /** @brief 이 월드가 진행한 틱 수입니다. 늦게 들어온 클라이언트는 하니스의 틱 수보다 작습니다. */
        uint32 getLocalTick() const { return _localTick; }
        /** @brief 클라이언트가 서버에 연결돼 있으면 true 입니다. 서버 월드는 항상 true 입니다. */
        bool isConnected() const;

        Scene&             getScene() { return *_pScene; }
        GameObjectManager& getObjectManager();
        /** @brief 키트가 보내고 연결을 묻는 창구입니다(게임 세션의 `GameNetSession::getPeers` 와 같은 모양). 호스트 자체(연결 · 진행)는 하니스만 다룬다. */
        INetPeers&        getPeers() { return _host; }
        NetMessageRouter& getRouter() { return _router; }
        INetSimSession*   getSession() const { return _pSession.get(); }
        /** @brief 라우터의 핸들러가 받지 않은 메시지(게임 영역 등)입니다. 틱마다 비우고 다시 채웁니다. */
        const vector<NetReceivedMessage>& getUnhandledMessages() const { return _listUnhandled; }
        /** @brief 이 월드의 엔드포인트가 보내고 받은 양입니다. */
        const NetSimTraffic& getTraffic() const { return _counter.getTraffic(); }
        /** @brief 서버가 @p to(클라이언트 엔드포인트)로 보낸 바이트입니다. */
        uint64     getSentBytesTo( const NetAddress& to ) const { return _counter.getSentBytesTo( to ); }
        NetAddress getAddress() const { return _counter.getLocalAddress(); }

    private:
        friend class NetSimHarness;

        void start( INetSimGame* pGame );
        void update( float32 deltaTime );
        void stop();

        NetSimTrafficCounter       _counter; ///< 호스트 → 카운터 → 흉내 → 루프백 엔드포인트 순서로 거친다
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
     * @brief 서버 월드 1개와 클라이언트 월드 N개를 한 스레드에서 고정 스텝으로 돌립니다. 자세한 동작은 파일 머리말을 참고하세요.
     * @code
     *     NetSimHarness harness;
     *     harness.initialize( services, settings, &game );                // 서버 월드를 만든다
     *     const int32 clientA = harness.addClient( NetSimLinkConditions::makeSymmetric( lossy ) );
     *     harness.stepTicks( 600 );
     *     const int32 lateClient = harness.addClient( {} );               // 늦은 참가
     *     harness.stepUntil( &isConverged, 300 );
     *     harness.removeClient( clientA );
     * @endcode
     */
    class SW_KIT_API NetSimHarness
    {
    public:
        NetSimHarness();
        ~NetSimHarness();

        NetSimHarness( const NetSimHarness& )            = delete;
        NetSimHarness& operator=( const NetSimHarness& ) = delete;

        /** @brief 망을 만들고 서버 월드를 만듭니다. `INetSimGame::createSession` 호출, 플레이 시작, listen 까지 처리합니다. @p pGame 은 하니스보다 오래 살아야 합니다.
         *        @p services 는 월드마다의 씬을 지을 엔진 서비스 묶음입니다(물리 · 오디오 · 에셋). 전역 표를 다시 찾지 않습니다. */
        [[nodiscard]] bool initialize( const SceneEngineServices& services, const NetSimConfig& settings, INetSimGame* pGame );
        /** @brief 클라이언트와 서버 월드를 모두 종료합니다. 소멸자도 이 함수를 부릅니다. */
        void shutdown();

        /** @brief 고정 스텝 하나를 진행합니다. 순서는 파일 머리말과 같습니다. */
        void step();
        void stepTicks( uint32 tickCount );
        /**
         * @brief @p pPredicate 가 true 를 반환할 때까지 최대 @p maxTicks 틱을 진행합니다. 진행한 틱 수를 반환하고, 끝내 조건이 맞지 않으면 -1 을 반환합니다.
         * @param pPredicate 틱마다 부른다. @p pContext 를 그대로 넘긴다. 처음부터 true 이면 0을 반환한다.
         */
        int32 stepUntil( bool ( *pPredicate )( const NetSimHarness&, void* ), void* pContext, uint32 maxTicks );

        /** @brief 클라이언트 월드를 만들고 서버에 연결을 시작합니다. 월드 번호(1부터)를 반환하고, 실패하면 -1 을 반환합니다. */
        int32 addClient( const NetSimLinkConditions& conditions );
        /** @brief 클라이언트를 내보냅니다. 끊김 알림을 보내고 월드를 종료하며, 없는 번호면 아무것도 하지 않습니다. */
        void removeClient( int32 worldIndex );
        /** @brief 클라이언트 회선의 조건을 바꿉니다. 다음 송신부터 적용됩니다. */
        void setLinkConditions( int32 worldIndex, const NetSimLinkConditions& conditions );

        NetSimWorld&       getServer() { return *_pServer; }
        const NetSimWorld& getServer() const { return *_pServer; }
        /** @brief 살아 있는 클라이언트 월드입니다. 없으면 nullptr 입니다. */
        NetSimWorld* findClient( int32 worldIndex ) const;
        /** @brief 살아 있는 클라이언트 월드를 들어온 순서대로 모읍니다. */
        void  collectClients( vector<NetSimWorld*>& outListWorld ) const;
        int32 getClientCount() const { return static_cast<int32>( _listClient.size() ); }
        /** @brief 살아 있는 클라이언트가 모두 서버에 연결됐으면 true 입니다. */
        bool areAllClientsConnected() const;

        uint32  getTick() const { return _tick; }
        float64 getTime() const { return _time; }
        float64 getTickInterval() const { return _settings._tickInterval; }
        /** @brief 버려진 패킷 수입니다. 엔드포인트 흉내가 버린 패킷(손실, 큐 넘침, 필터)과 받을 엔드포인트가 없어 망이 버린 패킷의 합입니다. */
        uint64 getDroppedPacketCount() const;

    private:
        /** @brief 엔드포인트 하나의 흉내 전송입니다. 월드가 떠나도 남아서 큐에 있는 패킷(끊김 알림)을 마저 내보냅니다. */
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

        NetSimConfig                    _settings;
        SceneEngineServices             _services; ///< `initialize` 가 받은 묶음(빌림). 월드의 씬을 이것으로 짓는다
        unique_ptr<LoopbackNetwork>     _pNetwork;
        vector<unique_ptr<Link>>        _listLink;
        unique_ptr<NetSimWorld>         _pServer;
        vector<unique_ptr<NetSimWorld>> _listClient; ///< 들어온 순서대로 보관한다
        INetSimGame*                    _pGame;
        float64                         _time;
        uint32                          _tick;
        int32                           _nextWorldIndex;
        bool                            _bStepping;
    };
} // namespace sw
