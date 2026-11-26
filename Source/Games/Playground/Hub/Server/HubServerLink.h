/**
 * @file HubServerLink.h
 * @brief 허브의 서버 쪽 망 잇기입니다 — 허브 규칙 · 원격 호출 · MMO 복제 · 진행 저장을 묶습니다. 서버 전용 코드라 이 폴더(`Server/`)에 둔다.
 * @details 공유 쪽(`HubNetLink.h`)은 창구(`IHubServer` · `IHubServerFactory`)만 알고, 공장은 모듈 부분으로 공개한다(`HubServerFactory.cpp`).
 *          진행 저장소는 전용 서버의 온라인 조립 저장소다(`HubProfileStore` — 저장소 계약 `ServiceStore`).
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Time/Countdown.h"
#include "Core/Time/TimeSeconds.h"

#include "Engine/Scene/StreamingCellGrid.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/RemoteCall.h"
#include "GameFramework/Kits/Feature/Network/NetMMO/MMOReplicator.h"

#include "Games/Playground/Hub/HubNetLink.h"
#include "Games/Playground/Hub/HubWorld.h"
#include "Games/Playground/Hub/Server/HubProfileStore.h"

namespace sw
{
    /** @brief 서버 링크의 셈입니다(진단 · 테스트). */
    struct HubServerStats
    {
        uint64 _admittedCount{ 0 }; ///< 들인 접속
        uint64 _refusedCount{ 0 };  ///< 주체가 없거나 진행을 읽지 못해 받지 않은 접속
        uint64 _replacedCount{ 0 }; ///< 같은 계정이 다시 붙어 끊은 앞 접속
        uint64 _actionCount{ 0 };   ///< 판정한 행동
        uint64 _publishCount{ 0 };  ///< 복제기에 올린 엔티티 갱신(`setEntity`) — 바뀐 엔티티만 센다
    };

    /** @brief 복제기에 마지막으로 올린 엔티티 값입니다. 같으면 다시 올리지 않습니다(언리얼 Push Model · Iris 의 더티 표시 자리). */
    struct HubPublishedEntity
    {
        AbsolutePosition _position{};
        int32            _stateValue{ 0 };
        uint32           _typeID{ 0 };

        bool operator==( const HubPublishedEntity& other ) const { return _position._value == other._position._value && _stateValue == other._stateValue && _typeID == other._typeID; }
        bool operator!=( const HubPublishedEntity& other ) const { return ( *this == other ) == false; }
    };

    /**
     * @class HubServerLink
     * @brief 서버 쪽입니다. 허브 규칙 · 원격 호출 · MMO 복제 · 진행 저장을 묶습니다. 라우터에는 사건만 받는 처리기로 달린다(게임 영역, 마스크 0).
     */
    class HubServerLink final : public IHubServer
    {
    public:
        HubServerLink();
        ~HubServerLink() override;

        HubServerLink( const HubServerLink& )            = delete;
        HubServerLink& operator=( const HubServerLink& ) = delete;

        /**
         * @brief 허브를 세우고 라우터에 답니다. @p storeSource 는 진행 저장소를 찾는 곳이다(맡길 때마다 묻는다 — 온라인 조립은 리로드 · 종료마다 다시 선다).
         * @return 규칙 · 관심 격자 · 복제 설정이 틀렸거나 라우터 등록이 겹치면 오류를 남기고 false 입니다(링크는 서지 않는다)
         */
        [[nodiscard]] bool initialize( INetPeers& peers, NetMessageRouter& router, const HubRules& rules, const StreamingCellGridDef& gridDef, HubServiceStoreSource storeSource );
        /** @brief 접속한 모두의 진행 쓰기를 맡기고 라우터에서 뗍니다(맡긴 쓰기는 저장소가 끝낸다). 두 번 불러도 됩니다. */
        void shutdown() override;
        /**
         * @brief 접속한 모두의 진행을 맡기고 저장소에 모두 닿을 때까지 기다립니다 — 진행 저장소(온라인 조립)가 내려가기 직전에 부른다(`IServerKitLocator::subscribeBeforeWithdraw`).
         * @details 이 링크는 조립보다 늦게 내려간다 — 그때의 저장은 갈 곳이 없어 마지막 진행을 잃는다. 한도(`ServiceStore::kStopDrainMilliseconds`)를 넘으면 오류를 남긴다.
         */
        void flushProfiles();
        /** @brief 저장소가 내려가기 직전의 알림 구독을 맡깁니다(`shutdown` 이 푼다). */
        void holdBeforeWithdraw( Subscription beforeWithdraw ) { _beforeWithdraw = std::move( beforeWithdraw ); }
        /** @brief 규칙 · 저장 · 복제를 한 틱 돌립니다. */
        void            update( float32 deltaSeconds ) override;
        void            collectAvatars( vector<HubAvatarSession>& outListSession ) const override;
        HubActionResult moveAvatar( uint32 avatarID, const AbsolutePosition& position, StepSeconds stepSeconds, bool bRiding ) override { return _world.moveAvatar( avatarID, position, stepSeconds, bRiding ); }

        /**
         * @brief 연결을 계정 @p accountID 로 들입니다 — 진행 읽기를 맡기고, 읽히면 아바타를 세운다. 계정이 0 이면 받지 않고 끊습니다.
         * @details 연결 열림(`onConnectionOpened`)이 주체로 부른다. 테스트는 주체 없는 루프백 연결에 계정을 직접 넘긴다.
         */
        void acceptConnection( int32 connectionID, uint64 accountID );
        /** @brief 아바타 행동 하나의 판정을 알립니다(`HubAvatarAgent` 의 `Server` 함수 몸이 부른다). */
        void reportAction( HubAvatarAgent& agent, HubActionResult result );
        /** @brief 아바타의 공격 재사용 대기가 끝나는 월드 서버 틱입니다(대기가 없으면 지금 틱). */
        uint32 computeAttackReadyTick( uint32 avatarID ) const;

        /** @brief 연결이 열렸습니다 — 주체(계정)로 들입니다(라우터 사건 · 핫 리로드 뒤 붙어 있는 연결). */
        void onConnectionOpened( int32 connectionID );
        /** @brief 연결이 닫혔습니다 — 읽는 중이면 버리고, 들인 아바타는 진행 쓰기를 맡기고 내보냅니다. */
        void onConnectionClosed( int32 connectionID, NetDisconnectReason reason );

        bool                   isActive() const { return _pPeers != nullptr; }
        HubWorld&              getWorld() { return _world; }
        const HubWorld&        getWorld() const { return _world; }
        const HubServerStats&  getStats() const { return _stats; }
        const RemoteCallStats& getRemoteCallStats() const { return _remoteCalls.getStats(); }
        const HubProfileStore& getProfiles() const { return _profiles; }
        int32                  getOnlineCount() const { return static_cast<int32>( _mapAgent.size() ); }

    private:
        /** @brief 라우터에 다는 사건 처리기입니다 — 링크가 창구를 함께 상속하지 않고 멤버로 든다(기반은 하나). 게임 영역 마스크 0 으로 연결 사건만 받는다. */
        class NetEvents final : public INetMessageHandler
        {
        public:
            explicit NetEvents( HubServerLink& owner )
                : _owner{ owner }
            {
            }
            uint8                   getMessageRangeBase() const override { return NetMessageRange::kGame; }
            uint16                  getMessageKindMask() const override { return 0; }
            NetMessageDirectionMask getMessageDirectionMask() const override { return NetMessageDirectionMask{}; }
            uint32                  getWireVersion() const override { return kNoMessageTable; } // 연결 사건만 받는다 — 게임 메시지는 키트 표(RemoteCall · MMO)가 싣는다
            NetHandleResult         handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
            void                    onConnectionOpened( int32 connectionID ) override { _owner.onConnectionOpened( connectionID ); }
            void                    onConnectionClosed( int32 connectionID, NetDisconnectReason reason ) override { _owner.onConnectionClosed( connectionID, reason ); }

        private:
            HubServerLink& _owner;
        };

        /** @brief 읽힌 진행으로 아바타를 세우고 세션 대상으로 등록합니다. 그 사이 연결이 닫혔으면 버립니다. */
        void completeAdmission( const HubProfileLoad& load );
        /** @brief 진행을 쓰고 아바타를 내보냅니다. */
        void releaseAgent( int32 connectionID );
        void sendProgress( const HubAvatarAgent& agent );
        void saveAll();
        /** @brief 끝난 저장소 일을 거두고 읽힌 진행의 연결을 들입니다. */
        void pollProfiles();
        /** @brief 복제 엔티티(아바타 · 놓인 것) 가운데 지난번 올린 값과 다른 것만 이번 틱 상태로 올립니다. */
        void publishEntities();
        /** @brief 엔티티 하나를 올립니다. 지난번 올린 값과 같으면 아무것도 하지 않습니다. */
        void publishEntity( uint32 entityID, const HubPublishedEntity& value, float32 importance );
        /** @brief 엔티티를 복제기에서 내립니다. 올린 적이 없으면 아무것도 하지 않습니다. */
        void removePublishedEntity( uint32 entityID );

    private:
        HubWorld                                         _world;
        HubProfileStore                                  _profiles;
        NetEvents                                        _netEvents;
        RemoteCallServer                                 _remoteCalls;
        StreamingCellGrid                                _grid; ///< 복제기보다 먼저 선언한다 — 복제기가 빌려 쓴다
        MMOReplicator                                    _replicator;
        unordered_map<int32, unique_ptr<HubAvatarAgent>> _mapAgent;          ///< 연결 → 세션 객체(들인 접속)
        unordered_map<int32, Subscription>               _mapSessionTarget;  ///< 연결 → 세션 대상(`_mapAgent` 의 것)의 등록 — 뒤 멤버라 대상보다 먼저 내린다
        unordered_map<int32, uint64>                     _mapPendingAccount; ///< 진행을 읽는 중인 연결 → 계정
        vector<HubProfileLoad>                           _listLoadScratch;
        vector<uint8>                                    _listStateScratch;
        unordered_map<uint32, HubPublishedEntity>        _mapPublished;     ///< 엔티티 id → 복제기에 마지막으로 올린 값
        vector<Subscription>                             _listSubscription; ///< 라우터 등록. 처리기들보다 뒤에 선언한다 — 먼저 풀린다
        HubServerStats                                   _stats;
        Subscription                                     _beforeWithdraw; ///< 저장소가 내려가기 직전의 알림(`flushProfiles`)
        INetPeers*                                       _pPeers;
        const NetMessageRouter*                          _pRouter; ///< 월드의 서버 시계(`getWorldClock`)를 읽는다. 서 있는 동안만 있다
        Countdown                                        _autosaveTimer;
    };
} // namespace sw
