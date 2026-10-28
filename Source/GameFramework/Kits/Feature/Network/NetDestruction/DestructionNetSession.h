/**
 * @file DestructionNetSession.h
 * @brief 실제 네트워크 세션(전용 서버 · 리슨 호스트 · 클라이언트) 위에서 씬의 파괴 오브젝트를 복제하고, 구조 해시를 로그 줄로 남깁니다.
 * @details 가상 서버 하니스의 파괴 시나리오(`NetSimDestructionScenario.h`)가 월드마다 하던 일을 한 곳으로 모았다. 하니스는 루프백 망 위의
 *          `NetSimWorld` 를, 실제 실행은 `GameNetSession`(`DestructionNetSessionComponent`)을 넘긴다 — 같은 코드가 두 길을 탄다.
 *
 *          - **번호**: 씬의 번호표(`NetObjectTable`)의 번호다. 씬 오브젝트는 이름 해시라 서버와 클라이언트가 같은 씬 문서에서 같은 번호를 얻는다(한쪽에만 있는
 *            오브젝트가 번호를 밀지 않는다). 플레이 중에 생긴 파괴 오브젝트는 서버가 `NetObjectTable::registerSpawned` 로, 클라이언트가 게임의 스폰 복제로
 *            `bindSpawned` 하면 다음 `update` 에서 등록된다 — 번호가 없는 오브젝트는 복제하지 않는다.
 *          - **권한**: 서버만 피해를 만든다. 클라이언트는 컴포넌트 권한을 끄고(`DestructionReplicationClient::registerObject`) 서버의 사건을 받는다.
 *            폭발 드럼통(`ExplosiveBarrelComponent`)처럼 피해를 내는 기믹은 서버의 게임플레이라 클라이언트에서 끈다.
 *          - **해시 로그**: 오브젝트의 사건 수가 바뀔 때마다 `[DestructionHash] side=<server|client> id=<번호> events=<사건 수> hash=<16 자리> name=<이름>` 한 줄을 남긴다.
 *            같은 그래프에 같은 사건을 같은 순서로 적용하면 같은 해시다(`DestructionState::computeStateHash`) — 다른 컴파일러 · 플랫폼의 서버와 클라이언트
 *            로그를 `py -3 -m Scripts destruction-net compare`(`Scripts/dev/DestructionNetPair.py`)가 (번호, 사건 수) 로 맞대어 본다. 클라이언트가 스냅숏으로 건너뛴 사건 수는 그 줄이 없다.
 *            로그 매크로를 쓰므로 Shipping 에는 줄이 없다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"

#include "Engine/Scene/SceneStateHash.h"

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
        uint64 _hash{ 0 };
        uint32 _netID{ 0 };
        uint32 _eventCount{ 0 };
    };
} // namespace sw

namespace sw
{
    /**
     * @class DestructionNetSession
     * @brief 한 씬의 파괴 복제(서버 또는 클라이언트 한쪽)와 해시 기록입니다. 자세한 계약은 파일 머리말을 보세요.
     * @code
     *     session.initialize( host, router, manager, DestructionNetSide::Server, settings );
     *     session.update( deltaTime ); // 서버는 월드 틱 뒤, 클라이언트는 받은 메시지를 나눈 뒤
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
         * @brief 서버는 새 사건 · 자세 · 해시를 보내고(틱 번호를 하나 올린다), 클라이언트는 받은 상태를 적용합니다. 그 뒤 사건 수가 바뀐 오브젝트의 해시를 남깁니다.
         * @details 그 전에 번호표의 생성 번호가 바뀌었으면 새로 번호를 받은 파괴 오브젝트를 등록한다.
         */
        void update( float32 deltaTime );

        bool               isActive() const { return _routerSubscription.isActive(); }
        DestructionNetSide getSide() const { return _side; }
        uint32             getObjectCount() const { return static_cast<uint32>( _listEntry.size() ); }
        /** @brief 등록한 오브젝트의 번호입니다(등록 순서). */
        void                                 collectObjectIDs( vector<uint32>& outListNetID ) const;
        const vector<DestructionHashRecord>& getHashRecords() const { return _listRecord; }
        const DestructionReplicationStats&   getStats() const;
        /** @brief 등록한 모든 오브젝트의 구조 해시(`DestructionState::computeStateHash`)를 번호 순으로 접은 값입니다. 해시 이력의 `destruction` 줄이 틱마다 남긴다. */
        uint64 computeStateHash() const;
        /** @brief 해시 이력의 `destruction` 줄 제공자입니다 — 세션이 멈춰 있으면 남기지 않고, 아니면 호스트 틱에 `computeStateHash` 를 남깁니다. */
        [[nodiscard]] bool sampleStateHash( uint64 hostTick, StateHashSample& outSample ) const override;
        /** @brief 번호의 오브젝트 이름입니다. 등록하지 않은 번호면 빈 문자열입니다. */
        string_view findObjectName( uint32 netID ) const;
        /** @brief 클라이언트 복제기입니다(테스트의 결함 주입 `skipNextEvent`). 서버면 쓰지 않습니다. */
        DestructionReplicationClient& getClientReplication() { return _client; }

        /** @brief 해시 로그 한 줄을 만듭니다(`[DestructionHash] side=… id=… events=… hash=… name=…`). 비교 스크립트가 이 형식을 읽습니다. */
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
            uint32             _netID{ 0 };
            uint32             _loggedCount{ 0 }; ///< 마지막으로 남긴 사건 수
        };

        DestructionReplicationServer  _server;
        DestructionReplicationClient  _client;
        vector<Entry>                 _listEntry;
        unordered_map<uint32, uint32> _mapEntryIndex; ///< 번호 → `_listEntry` 위치. 항목은 더하기만 하므로 위치가 바뀌지 않는다(`_listEntry` 와 함께 고친다)
        vector<DestructionHashRecord> _listRecord;
        GameObjectManager*            _pManager;           ///< 번호표가 있는 씬(빌린 것)
        Subscription                  _routerSubscription; ///< 라우터에 단 처리기(`_server` 또는 `_client`)의 등록. 처리기보다 뒤에 선언한다 — 먼저 풀린다
        uint32                        _serverTick;
        uint32                        _seenSpawnedRevision; ///< 마지막으로 훑은 번호표의 생성 번호 판
        DestructionNetSide            _side;
    };
} // namespace sw
