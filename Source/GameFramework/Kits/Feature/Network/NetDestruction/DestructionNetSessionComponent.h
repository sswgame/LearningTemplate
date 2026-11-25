/**
 * @file DestructionNetSessionComponent.h
 * @brief 게임 인스턴스의 네트워크 세션(`GameNetSession`)이 열려 있으면 그 씬의 파괴 오브젝트를 복제합니다 — 전용 서버(`Server`)가 파괴를 UDP 서버로 돌리는 자리입니다.
 * @details 씬에 오브젝트 하나로 둔다. 역할은 실행 인자가 정한다(`GameNetSession` 머리말): 전용 서버 · `-gv_netListenPort` 면 서버,
 *          `-gv_netConnectAddress` 면 클라이언트, 세션이 없으면(혼자 하는 게임) 아무것도 하지 않는다. 복제와 해시 로그는 `DestructionNetSession` 이 맡는다.
 *          예: 서버 `Server -gv_firstScene=<씬> -gv_netListenPort=7777`, 클라이언트 `App -gv_firstScene=<같은 씬> -gv_netConnectAddress=127.0.0.1:7777`.
 *          두 로그를 `py -3 -m Scripts destruction-net compare` 로(두 프로세스를 띄우는 것까지는 `run`) 맞대면 컴파일러 · 플랫폼을 넘어 구조 해시가 같은지 본다.
 *
 *          틱은 마지막 그룹(`PostUpdate`)이다 — 서버는 이번 틱의 물리가 만든 사건을 같은 틱에 보낸다. 사건 · 자세의 시각은 월드의 서버 틱(`NetClock`)이라
 *          서버의 프레임 길이와 무관하다(예전에는 프레임마다 하나씩 세어 서버 틱 빈도가 설정과 다르면 복제를 멈췄다).
 *          틱은 걸기만 하고 복제는 틱 뒤 큐 한 번에 건다 — 복제가 남의 오브젝트(파괴 컴포넌트)의 상태를 쓰므로 틱 안(워커)에서 하지 않는다
 *          (`Gameplay.md` 규칙 6 — 씬에 하나라 프레임에 한 일이다. 하우징 `HousingNetSessionComponent` 와 같은 자리).
 *
 *          `DestructionNetSession`(같은 파일): 실제 네트워크 세션(전용 서버 · 리슨 호스트 · 클라이언트) 위에서 씬의 파괴 오브젝트를 복제하고, 구조 해시를 로그 줄로 남깁니다.
 *          - **번호**: 씬의 번호표(`NetObjectTable`)의 번호다. 씬 오브젝트는 이름 해시라 서버와 클라이언트가 같은 씬 문서에서 같은 번호를 얻는다(한쪽에만 있는
 *            오브젝트가 번호를 밀지 않는다). 플레이 중에 생긴 파괴 오브젝트는 서버가 `NetObjectTable::registerSpawned` 로, 클라이언트가 게임의 스폰 복제로
 *            `bindSpawned` 하면 다음 `update` 에서 등록된다 — 번호가 없는 오브젝트는 복제하지 않는다.
 *          - **권한**: 서버만 피해를 만든다. 클라이언트는 컴포넌트 권한을 끄고(`DestructionReplicationClient::registerObject`) 서버의 사건을 받는다.
 *            폭발 드럼통(`ExplosiveBarrelComponent`)처럼 피해를 내는 기믹은 서버의 게임플레이라 클라이언트에서 끈다.
 *          - **해시 로그**: 오브젝트의 사건 수가 바뀔 때마다 id 가 `DestructionNetSession.HashLine` 인 로그 줄 하나를 JSON 본문(`side` · `id` · `events` · `hash` · `name`)으로 남긴다.
 *            같은 그래프에 같은 사건을 같은 순서로 적용하면 같은 해시다(`DestructionState::computeStateHash`) — 다른 컴파일러 · 플랫폼의 서버와 클라이언트
 *            로그를 `py -3 -m Scripts destruction-net compare`(`Scripts/net/destruction_net_pair.py`)가 (번호, 사건 수) 로 맞대어 본다. 클라이언트가 스냅숏으로 건너뛴 사건 수는 그 줄이 없다.
 *            로그 매크로를 쓰므로 Shipping 에는 줄이 없다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Scene/SceneObjectHash.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetDestruction/DestructionReplication.h"

namespace sw
{
    class FractureComponent;
    class GameObjectManager;
    class INetPeers;
    class NetMessageRouter;

    /** @brief 세션의 이쪽 역할입니다. */
    enum class DestructionNetSide : uint8
    {
        Server = 0,
        Client
    };
} // namespace sw

namespace sw
{
    /** @brief 해시 로그 한 줄의 값입니다. */
    struct DestructionHashRecord
    {
        uint64      _hash{ 0 };
        NetObjectID _netID{ 0 };
        uint32      _eventCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class DestructionNetSession
     * @brief 한 씬의 파괴 복제(서버 또는 클라이언트 한쪽)와 해시 기록입니다. 자세한 계약은 파일 머리말을 보세요.
     * @code
     *     session.initialize( host, router, manager, DestructionNetSide::Server, settings );
     *     session.update(); // 서버는 월드 틱 뒤, 클라이언트는 받은 메시지를 나눈 뒤
     *     session.shutdown();
     * @endcode
     */
    class SW_KIT_API DestructionNetSession : public IStateHashProvider
    {
    public:
        DestructionNetSession();
        ~DestructionNetSession();

        DestructionNetSession( const DestructionNetSession& )            = delete;
        DestructionNetSession& operator=( const DestructionNetSession& ) = delete;

        /**
         * @brief @p manager 의 씬 오브젝트를 번호표에 잡고, 번호가 있는 파괴 오브젝트를 등록하고, 복제 처리기를 @p router 에 답니다. 클라이언트면 피해를 내는 기믹을 끕니다.
         * @details 이미 시작했으면 먼저 `shutdown` 한다. @p host 는 `shutdown` 까지 살아 있어야 한다. 처리기 등록은 구독이 소유한다 — @p router 가 먼저 사라지면
         *          등록이 무효가 되어 세션이 멈춘다(`isActive` 가 false). 라우터가 처리기를 받지 않아도(겹치는 영역, 오류가 남는다) 멈춘 채로 둔다.
         */
        void initialize( INetPeers& host, NetMessageRouter& router, GameObjectManager& manager, DestructionNetSide side, const DestructionReplicationConfig& settings );
        /** @brief 처리기를 떼고 클라이언트면 해시 대조 요약 한 줄을 남깁니다. 두 번 불러도 됩니다. */
        void shutdown();
        /**
         * @brief 서버는 새 사건 · 자세 · 해시를 월드의 서버 틱(`NetClock`)으로 보내고, 클라이언트는 받은 상태를 적용합니다. 그 뒤 사건 수가 바뀐 오브젝트의 해시를 남깁니다.
         * @details 그 전에 번호표의 생성 번호가 바뀌었으면 새로 번호를 받은 파괴 오브젝트를 등록한다.
         */
        void update();

        bool isActive() const { return _routerSubscription.isActive(); }
        /** @brief 서버가 마지막으로 보낸 서버 틱입니다. 클라이언트는 0 입니다. */
        uint32             getServerTick() const { return _serverTick; }
        DestructionNetSide getSide() const { return _side; }
        uint32             getObjectCount() const { return static_cast<uint32>( _listEntry.size() ); }
        /** @brief 등록한 오브젝트의 번호입니다(등록 순서). */
        void                                 collectObjectIDs( vector<NetObjectID>& outListNetID ) const;
        const vector<DestructionHashRecord>& getHashRecords() const { return _listRecord; }
        const DestructionReplicationStats&   getStats() const;
        /** @brief 등록한 모든 오브젝트의 구조 해시(`DestructionState::computeStateHash`)를 번호 순으로 접은 값입니다. 해시 이력의 `destruction` 줄이 틱마다 남긴다. */
        uint64 computeStateHash() const;
        /** @brief 해시 이력의 `destruction` 줄 제공자입니다 — 세션이 멈춰 있으면 남기지 않고, 아니면 호스트 틱에 `computeStateHash` 를 남깁니다. */
        [[nodiscard]] bool sampleStateHash( uint64 hostTick, StateHashSample& outSample ) const override;
        /** @brief 번호의 오브젝트 이름입니다. 등록하지 않은 번호면 빈 문자열입니다. */
        string_view findObjectName( NetObjectID netID ) const;
        /** @brief 클라이언트 복제기입니다(테스트의 결함 주입 `skipNextEvent`). 서버면 쓰지 않습니다. */
        DestructionReplicationClient& getClientReplication() { return _client; }

        /** @brief 해시 로그 한 줄의 JSON 본문을 만듭니다(`{"side":…,"id":…,"events":…,"hash":…,"name":…}`). 비교 스크립트가 읽습니다. */
        static string formatHashLine( DestructionNetSide side, const DestructionHashRecord& record, string_view objectName );

    private:
        /** @brief 사건 수가 바뀐 오브젝트를 기록하고 로그에 남깁니다. */
        void recordChangedHashes();
        /** @brief 번호표에서 번호가 있고 아직 등록하지 않은 파괴 오브젝트를 등록합니다. */
        void registerNumberedObjects();

    private:
        /** @brief 등록한 파괴 오브젝트 하나입니다. */
        struct Entry
        {
            FractureComponent* _pFracture{ nullptr }; ///< 오브젝트를 지우지 않는 씬이라 포인터를 든다 — 지우는 게임은 `shutdown` 먼저
            string             _name{};
            NetObjectID        _netID{ 0 };
            uint32             _loggedCount{ 0 }; ///< 마지막으로 남긴 사건 수
        };

        DestructionReplicationServer       _server;
        DestructionReplicationClient       _client;
        vector<Entry>                      _listEntry;
        unordered_map<NetObjectID, uint32> _mapEntryIndex; ///< 번호 → `_listEntry` 위치. 항목은 더하기만 하므로 위치가 바뀌지 않는다(`_listEntry` 와 함께 고친다)
        vector<DestructionHashRecord>      _listRecord;
        GameObjectManager*                 _pManager;           ///< 번호표가 있는 씬(빌린 것)
        Subscription                       _routerSubscription; ///< 라우터에 단 처리기(`_server` 또는 `_client`)의 등록. 처리기보다 뒤에 선언한다 — 먼저 풀린다
        const NetClock*                    _pWorldClock;        ///< 월드의 서버 시계(라우터가 든다)
        uint32                             _serverTick;
        uint32                             _seenSpawnedRevision; ///< 마지막으로 훑은 번호표의 생성 번호 판
        DestructionNetSide                 _side;
    };
} // namespace sw

namespace sw
{
    /**
     * @class DestructionNetSessionComponent
     * @brief 씬 하나의 파괴 복제를 게임 네트워크 세션에 잇습니다. 자세한 계약은 파일 머리말을 보세요.
     */
    REFLECT( Category = "Network", DisplayName = "Destruction Net Session", Tooltip = "Replicates this scene's fracture objects over the game's network session and logs structure hashes" )
    class SW_KIT_API DestructionNetSessionComponent : public Component
    {
    public:
        REFLECT_BODY();

        static constexpr TickGroup kTickGroup = TickGroup::PostUpdate; ///< 서버는 이번 틱의 물리가 만든 사건을 같은 틱에 보낸다 — 마지막 그룹에서 돈다.

        DestructionNetSessionComponent();
        ~DestructionNetSessionComponent() override = default;

        void onBeginPlay() override;
        void onEndPlay() override;
        void onTick( TickContext& context ) override;

        const DestructionNetSession& getSession() const { return _session; }

    private:
        /** @brief 틱 뒤(게임 스레드): 복제를 한 번 돌린 뒤 셈을 올립니다. */
        void updateSession();
        /** @brief 파괴 복제의 누적 셈을 `GameNetSession::setReplicaStats( "destruction" … )` 로 올립니다(Profiler 탭의 `replicas=` 표). */
        void reportReplicaStats();

        PROPERTY( Category = "Network", DisplayName = "Hash Interval", Min = 0.1, Units = s, Tooltip = "How often the server sends its structure hash for the clients to compare" )
        float32 _hashInterval;
        PROPERTY( Category = "Network", DisplayName = "Interpolation Delay", Min = 0.0, Units = s, Tooltip = "Least time clients draw moving chunks in the past" )
        float32 _interpolationDelay;

        DestructionNetSession _session;
        Subscription          _hashSubscription; ///< 해시 이력의 `destruction` 줄 등록. `_session` 보다 뒤에 선언한다 — 먼저 풀린다
    };
} // namespace sw
