/**
 * @file ValeNetLink.h
 * @brief LanternVale 의 서버 · 클라이언트 망 잇기입니다 — 반사 함수 호출(`RemoteCall`)로 행동을 서버에 청하고, MMO 복제(`MMOReplicator`)로 근처의 짐승 · 버섯 · 아바타를 받습니다.
 * @details - 세션 대상: 연결마다 `ValeAvatarAgent` 하나가 양쪽의 id 0 이다(언리얼 PlayerController 자리). 클라이언트는 `Server` 함수(공격 · 채집 · 이동 · 파티)를 부르고,
 *            서버는 그 결과와 진행을 `Client` 함수로 알린다. 행동의 판정은 서버의 `ValeWorld` 가 한다 — 클라이언트는 결과를 보여 줄 뿐이다.
 *          - 검증 짝(`validate*`)은 지어낸 번호 · 경계 밖 좌표 · 자기 초대 같은 조작만 거른다(실패하면 연결을 끊는다). 사거리 · 재사용 대기 같은 게임 규칙은 결과로 알린다.
 *          - 계정: 접속 인증기의 주체(`INetPeers::getConnectionPrincipal` — 게임 접속 티켓의 계정)만 쓴다. 주체가 없는 연결(자격 없이 붙은)은 받지 않는다 —
 *            클라이언트가 보낸 이름으로 계정을 고르면 남의 진행을 가져갈 수 있다. 같은 계정이 다시 붙으면 앞 연결을 저장한 뒤 끊는다(재접속이 옛 연결을 기다리지 않는다).
 *          - 저장: 접속 끝 · 주기(`ValeRules::_autosaveSeconds`) · 서버 내림에 쓴다. 읽기는 접속 때 한 번이다(`ValeProfileStore`).
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Network/Message/NetMessage.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Scene/StreamingCellGrid.h"

#include "GameFramework/Kits/Feature/Network/NetClientServer/RemoteCall.h"
#include "GameFramework/Kits/Feature/Network/NetMMO/MMOReplicator.h"

#include "Games/LanternVale/ValeProfileStore.h"
#include "Games/LanternVale/ValeWorld.h"

namespace sw
{
    class ValeServerLink;

    /**
     * @brief 연결 하나의 세션 객체입니다. 서버에서는 그 연결의 아바타를 움직이고, 클라이언트에서는 서버가 알린 진행을 들고 있습니다.
     * @details 같은 형식이 양쪽에 있다(언리얼 PlayerController 처럼). `Server` 함수 몸은 서버에서만, `Client` 함수 몸은 클라이언트에서만 불린다(`RemoteCall` 이 역할로 막는다).
     */
    REFLECT()
    struct ValeAvatarAgent
    {
        REFLECT_BODY();

        // 서버가 알린 값(클라이언트). 감시 값과 개발 명령이 읽는다
        PROPERTY()
        uint32 _avatarID{ 0 };
        PROPERTY()
        int32 _shownLevel{ 0 };
        PROPERTY()
        int32 _shownExperience{ 0 };
        PROPERTY()
        int32 _shownCoins{ 0 };
        PROPERTY()
        int32 _shownHerbCount{ 0 };
        PROPERTY()
        int32 _shownDefeatCount{ 0 };
        PROPERTY()
        int32 _shownPartySize{ 0 };
        PROPERTY()
        uint32 _pendingInviteFromID{ 0 };
        PROPERTY()
        ValeActionResult _lastResult{ ValeActionResult::Ok };
        PROPERTY()
        int32 _resultCount{ 0 };

        ValeServerLink* _pServer{ nullptr }; ///< 서버 — 이 연결의 링크. 클라이언트는 비어 있다
        int32           _connectionID{ -1 }; ///< 서버 — 이 객체의 연결

        /** @brief 짐승 하나를 칩니다. */
        FUNCTION( Server, Reliable, Validate )
        void attackCreature( uint32 creatureID );
        /** @brief 짐승 번호가 골짜기의 짐승 번호인지 봅니다(지어낸 번호는 조작이다). */
        FUNCTION()
        bool validateAttackCreature( uint32 creatureID ) const;

        /** @brief 버섯 하나를 캡니다. */
        FUNCTION( Server, Reliable, Validate )
        void gatherHerb( uint32 herbID );
        FUNCTION()
        bool validateGatherHerb( uint32 herbID ) const;

        /** @brief 골짜기의 한 점으로 걷습니다. 잃어도 되는 요청이라 비신뢰다 — 다음 요청이 대신한다. */
        FUNCTION( Server, Validate )
        void moveTo( float32 x, float32 z );
        /** @brief 좌표가 유한하고 골짜기 안인지 봅니다. */
        FUNCTION()
        bool validateMoveTo( float32 x, float32 z ) const;

        /** @brief 다른 아바타를 파티에 부릅니다. */
        FUNCTION( Server, Reliable, Validate )
        void inviteToParty( uint32 avatarID );
        /** @brief 아바타 번호 꼴이고 자기가 아닌지 봅니다. */
        FUNCTION()
        bool validateInviteToParty( uint32 avatarID ) const;

        /** @brief 받은 초대를 받아들입니다. */
        FUNCTION( Server, Reliable )
        void acceptPartyInvite( uint32 leaderAvatarID );
        /** @brief 파티를 떠납니다. */
        FUNCTION( Server, Reliable )
        void leaveParty();

        /** @brief 서버가 알린 진행과 파티입니다. */
        FUNCTION( Client, Reliable )
        void receiveProgress( uint32 avatarID, int32 level, int32 experience, int32 coins, int32 herbCount, int32 defeatCount, int32 partySize, uint32 inviteFromID );
        /** @brief 서버가 판정한 행동 결과입니다. */
        FUNCTION( Client, Reliable )
        void receiveActionResult( ValeActionResult result );
    };
} // namespace sw

namespace sw
{
    /** @brief 서버 링크의 셈입니다(진단 · 테스트). */
    struct ValeServerStats
    {
        uint64 _admittedCount{ 0 }; ///< 들인 접속
        uint64 _refusedCount{ 0 };  ///< 주체가 없거나 진행을 읽지 못해 받지 않은 접속
        uint64 _replacedCount{ 0 }; ///< 같은 계정이 다시 붙어 끊은 앞 접속
        uint64 _actionCount{ 0 };   ///< 판정한 행동
    };

    /**
     * @class ValeServerLink
     * @brief 서버 쪽입니다. 골짜기 규칙 · 원격 호출 · MMO 복제 · 진행 저장을 묶습니다. 라우터에는 사건만 받는 처리기로 달린다(게임 영역, 마스크 0).
     */
    class ValeServerLink final : public INetMessageHandler
    {
    public:
        static constexpr uint32 kAvatarTypeID   = 1;
        static constexpr uint32 kCreatureTypeID = 2;
        static constexpr uint32 kHerbTypeID     = 3;

        ValeServerLink();
        ~ValeServerLink() override;

        ValeServerLink( const ValeServerLink& )            = delete;
        ValeServerLink& operator=( const ValeServerLink& ) = delete;

        /**
         * @brief 골짜기를 세우고 라우터에 답니다. @p pStore 는 진행 저장소이고 링크가 가집니다.
         * @return 규칙 · 관심 격자 · 복제 설정이 틀렸거나 라우터 등록이 겹치면 오류를 남기고 false 입니다(링크는 서지 않는다)
         */
        [[nodiscard]] bool initialize( INetPeers& peers, NetMessageRouter& router, const ValeRules& rules, const StreamingCellGridDef& gridDef, unique_ptr<ILocalStore> pStore );
        /** @brief 접속한 모두의 진행을 쓰고 저장소를 닫습니다(남은 쓰기를 마친다). 두 번 불러도 됩니다. */
        void shutdown();
        /** @brief 규칙 · 저장 · 복제를 한 틱 돌립니다. */
        void update( float32 deltaSeconds );

        /**
         * @brief 연결을 계정 @p accountID 로 들입니다 — 진행 읽기를 맡기고, 읽히면 아바타를 세운다. 계정이 0 이면 받지 않고 끊습니다.
         * @details 연결 열림(`onConnectionOpened`)이 주체로 부른다. 테스트는 주체 없는 루프백 연결에 계정을 직접 넘긴다.
         */
        void acceptConnection( int32 connectionID, uint64 accountID );
        /** @brief 아바타 행동 하나의 판정을 알립니다(`ValeAvatarAgent` 의 `Server` 함수 몸이 부른다). */
        void reportAction( ValeAvatarAgent& agent, ValeActionResult result );

        uint8           getMessageRangeBase() const override { return NetMessageRange::kGame; }
        uint16          getMessageKindMask() const override { return 0; }
        NetHandleResult handleNetMessage( const NetMessageContext& context, BitReader& body ) override;
        void            onConnectionOpened( int32 connectionID ) override;
        void            onConnectionClosed( int32 connectionID, NetDisconnectReason reason ) override;

        bool                    isActive() const { return _pPeers != nullptr; }
        ValeWorld&              getWorld() { return _world; }
        const ValeWorld&        getWorld() const { return _world; }
        const ValeServerStats&  getStats() const { return _stats; }
        const RemoteCallStats&  getRemoteCallStats() const { return _remoteCalls.getStats(); }
        const ValeProfileStore& getProfiles() const { return _profiles; }
        int32                   getOnlineCount() const { return static_cast<int32>( _mapAgent.size() ); }

    private:
        /** @brief 읽힌 진행으로 아바타를 세우고 세션 대상으로 등록합니다. 그 사이 연결이 닫혔으면 버립니다. */
        void completeAdmission( const ValeProfileLoad& load );
        /** @brief 진행을 쓰고 아바타를 내보냅니다. */
        void releaseAgent( int32 connectionID );
        void sendProgress( const ValeAvatarAgent& agent );
        void saveAll();
        /** @brief 복제 엔티티(아바타 · 놓인 것)를 이번 틱 상태로 올립니다. */
        void publishEntities();

    private:
        ValeWorld                                         _world;
        ValeProfileStore                                  _profiles;
        unique_ptr<ILocalStore>                           _pStore;
        RemoteCallServer                                  _remoteCalls;
        StreamingCellGrid                                 _grid; ///< 복제기보다 먼저 선언한다 — 복제기가 빌려 쓴다
        MMOReplicator                                     _replicator;
        unordered_map<int32, unique_ptr<ValeAvatarAgent>> _mapAgent;          ///< 연결 → 세션 객체(들인 접속)
        unordered_map<int32, uint64>                      _mapPendingAccount; ///< 진행을 읽는 중인 연결 → 계정
        vector<ValeProfileLoad>                           _listLoadScratch;
        vector<uint8>                                     _listStateScratch;
        vector<Subscription>                              _listSubscription; ///< 라우터 등록. 처리기들보다 뒤에 선언한다 — 먼저 풀린다
        ValeServerStats                                   _stats;
        INetPeers*                                        _pPeers;
        float32                                           _autosaveTimer;
    };
} // namespace sw

namespace sw
{
    /**
     * @class ValeClientLink
     * @brief 클라이언트 쪽입니다. 세션 대상(id 0)에 서버가 알린 진행이 쌓이고, 행동은 `Server` 함수 호출로 청합니다.
     */
    class ValeClientLink final
    {
    public:
        ValeClientLink();

        ValeClientLink( const ValeClientLink& )            = delete;
        ValeClientLink& operator=( const ValeClientLink& ) = delete;

        [[nodiscard]] bool initialize( INetPeers& peers, NetMessageRouter& router );
        void               shutdown();
        /** @brief 서버의 거절 알림을 로그로 남깁니다. */
        void update();

        /** @brief 세션 대상의 `Server` 함수를 부릅니다. 보내지 못하면 0 입니다(`RemoteCallClient::callServer`). */
        uint32 callServerFunction( string_view functionName, const vector<ReflectedValue>& listArg );

        bool                   isActive() const { return _bActive != SW_FALSE; }
        const ValeAvatarAgent& getAgent() const { return _agent; }
        const MMOClientView&   getView() const { return _view; }
        /** @brief 공격 결과를 받은 실제 시각(나노초)입니다. 재사용 대기가 지났는지 클라이언트가 어림할 때 씁니다. 없으면 0 입니다. */
        int64 getLastResultNanoseconds() const { return _lastResultNanoseconds; }

    private:
        ValeAvatarAgent                      _agent;
        RemoteCallClient                     _remoteCalls;
        MMOClientView                        _view;
        vector<RemoteCallRejectNotification> _listRejectScratch;
        vector<Subscription>                 _listSubscription;
        int64                                _lastResultNanoseconds;
        int32                                _seenResultCount;
        uint8                                _bActive;
    };
} // namespace sw
