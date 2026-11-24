/**
 * @file RemoteCall.h
 * @brief 반사 함수(`FUNCTION( Server · Client · Reliable · Validate )`)를 네트워크로 부르는 길입니다 — 메타데이터가 그대로 호출 정책입니다.
 * @details 언리얼 RPC(`UFUNCTION( Server, Reliable, WithValidation )`)와 Godot `@rpc` 의 자리이다(기억에 의존). 손으로 메시지를 짜지 않고 반사 함수 하나가 곧 원격 함수다.
 *          - **대상**: 호출은 대상 id(게임이 정한 32 비트 수) 하나를 가리킨다. 서버는 대상마다 "주인 연결" 을 적어 두고, 그 연결만 그 대상의 `Server` 함수를 부를 수 있다
 *            (언리얼의 소유 연결 규칙). 대상이 없거나 이미 사라졌으면 거절 알림(`Rejected`)을 돌려준다.
 *          - **세션 대상**: id 0(`RemoteCallTargets::kSessionTargetID`)은 "이 연결의 세션 객체" 다 — 언리얼 PlayerController 의 자리. 서버는 연결마다 하나를 두고
 *            (`registerSessionTarget`), 클라이언트는 자기 것 하나를 0 으로 등록한다. 그래서 서버가 정한 번호를 클라이언트가 먼저 알 필요 없이 양쪽이 서로 부른다.
 *          - **역할**: 서버는 `Server` 함수만 받고, 클라이언트는 `Client` · `Multicast` 함수만 받는다. 다른 역할의 함수를 부르는 연결은 결함이거나 조작이라 끊는다.
 *          - **신뢰**: `Reliable` 이면 신뢰 순서 채널, 아니면 비신뢰 채널로 보낸다(비신뢰는 조각나지 않아 1 KB 안에 들어야 한다). `Reliable` 함수가 비신뢰로 오면 끊는다(보내는 쪽 규칙을 어겼다).
 *          - **검증**: `Validate` 함수는 같은 인자의 `bool validate<함수 이름>( … )` 반사 함수를 짝으로 가진다(`applyDamage` → `validateApplyDamage`).
 *            짝이 없거나 꼴이 다르면 대상 등록이 오류로 멈춘다(실행 중에 찾지 못하는 일이 없다). 서버는 몸을 부르기 전에 짝을 부르고, 거짓이면 언리얼처럼 그 연결을 끊는다 —
 *            검증은 부정행위 탐지이고 게임 규칙의 거절(사거리 밖 · 재사용 대기)은 함수 몸이 한다.
 *          - **함수 번호**: 함수는 이름 글이 아니라 이름의 32 비트 해시(`hashed_string::getHash` — 대소문자 무시 FNV-1a, 이름 같음 규칙과 같다)로 가리킨다(감사 N-13).
 *            접속 때 표를 주고받지 않아도 양쪽이 같은 번호를 낸다. 한 타입 사슬 안에서 원격 함수의 번호가 다른 함수와 겹치면 등록이 오류로 멈춘다(언리얼 NetFieldExport 인덱스의 자리).
 *          - **호출률**: 서버는 연결마다 토큰 버킷으로 `Server` 호출 수를 제한한다(`RemoteCallServerConfig`, 언리얼 RPC DoS Detection). 넘친 호출은 몸을 부르지 않고
 *            `RateLimited` 로 거절한다 — 끊지 않는다(정상 클라이언트도 지연 뒤 몰아 보낼 수 있다). 함수에 `FUNCTION( Server, MaxCallsPerSecond = N )` 을 적으면
 *            연결 버킷 위에 그 연결 · 그 함수의 버킷(초당 N, 한꺼번에 N)을 하나 더 둔다. 적지 않은 함수는 연결 상한만 받는다 — 함수 상한의 기본값이 아니라 계약이다.
 *            `Server` 가 아닌 함수에 적으면 서버가 지킬 수 없어 등록이 거절한다.
 *          - **인자**: 함수의 인자 타입이 정하는 이진 꼴로 싣는다(언리얼 RPC 파라미터 배치 · 유니티 `FastBufferWriter` · Godot Variant 인코딩과 같은 자리) — bool 은 1 비트,
 *            정수 · 열거형은 가변 길이 정수, 실수는 그 비트(유한만), 글은 길이와 바이트다. 양쪽이 같은 함수 정의를 보므로 형식 표시를 싣지 않는다.
 *            원격 함수의 인자는 글 · 수 · bool · 열거형만 된다 — 등록이 확인한다. 인자 수 · 글 길이는 상한이 있고 넘는 메시지는 손상된 것으로 버린다.
 *            인자는 대상 · 주인 · 역할 · 채널 · 호출률을 모두 지난 뒤에만 푼다 — 거절할 호출에 푸는 비용을 쓰지 않는다.
 *          수명: 대상의 인스턴스는 빌려 쓴다 — 대상이 사라지기 전에 `unregisterTarget` 을 부른다. 서버는 연결이 닫히면 그 연결이 주인인 대상을 지운다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Event/EventBuffer.h"
#include "Core/Foundation/Constants.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Message/NetMessage.h"
#include "Core/Network/Message/NetMessageTable.h"

#include "Engine/Net/NetMessageBody.h"
#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Reflection/ReflectionMetadataBox.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"
#include "GameFramework/Runtime/Online/Guard/TokenBucket.h"

namespace sw
{
    class INetPeers;
    class NetMessageRouter;

    struct FunctionInfo;
    struct TypeInfo;

    /** @brief 받지 못한 호출의 까닭입니다. 선 형식(`RemoteCallRejectedBody`)이라 열거자를 더하면 본문 판(스키마 해시)이 저절로 바뀐다. */
    ENUM()
    enum class RemoteCallError : uint8
    {
        UnknownTarget = 0, ///< 그 id 의 대상이 없다(이미 사라졌다)
        UnknownFunction,   ///< 대상 타입에 그 이름의 함수가 없다
        ArgumentMismatch,  ///< 인자를 그 타입으로 읽지 못했거나 수가 맞지 않는다
        RateLimited        ///< 이 연결의 호출률 상한(`RemoteCallServerConfig`)을 넘었다 — 몸을 부르지 않았다
    };

    /** @brief 받지 못한 호출(서버 → 클라이언트) 본문입니다. */
    REFLECT()
    struct RemoteCallRejectedBody
    {
        REFLECT_BODY();
        PROPERTY()
        uint32 _sequence{ 0 }; ///< `callServer` 가 돌려준 순번
        PROPERTY()
        RemoteCallError _error{ RemoteCallError::UnknownTarget };
    };

    /**
     * @brief 반사 함수 호출의 메시지 표입니다. 종류 · 마스크 · 와이어 버전은 표에서 나온다(`NetMessageTable`).
     * @details 호출 본문은 함수마다 인자 형식이 달라 손 본문이다. 거절은 고정 배치형(`NetMessageBodyDef`)이다.
     */
    struct NetRemoteCallMessage
    {
        /**
         * @brief 호출 하나 — 순번(varuint) · 대상 id(varuint) · 함수 번호(32 비트, 이름 해시) · 인자 수(varuint) · 인자들(타입별 이진 꼴). 클라이언트 → 서버는 `Server` 함수, 서버 → 클라이언트는 `Client` · `Multicast` 함수다.
         *        판 2: 함수 이름 글 대신 함수 번호를 싣는다. 판 3: 인자를 글 대신 타입별 이진 꼴로 싣는다.
         */
        static constexpr uint32 kCallHoldMilliseconds = 10000;                                                                                                                                  ///< 대상이 아직 서지 않은 신뢰 호출을 기다리는 시간(씬 로드 · 스폰 복제)
        using Call                                    = NetHeldMessageDef<NetMessageSenderChannelDef<0, NetMessageDirection::Both, NetChannelKind::ReliableOrdered, 5>, kCallHoldMilliseconds>; ///< 판 5: 월드 세대가 8 비트다. 4: 월드 대상(세션 대상 0 이 아닌 번호) 뒤에 월드 세대가 온다. 채널은 함수의 `Reliable` 이 고른다(`beginMessage( channel )`)
        /** @brief 서버 → 클라이언트. 받지 못한 호출(`RemoteCallRejectedBody`). */
        using Rejected = NetMessageBodyDef<1, NetMessageDirection::ToClient, NetChannelKind::ReliableOrdered, RemoteCallRejectedBody>;
        using Table    = NetMessageTable<NetKitMessageRange::kRemoteCall, Call, Rejected>;

        static constexpr uint8 kCall     = Table::getKindOf<Call>();
        static constexpr uint8 kRejected = Table::getKindOf<Rejected>();

        static constexpr int32 kMaxArgumentBytes = 256; ///< 인자 글 하나의 상한
        static constexpr int32 kMaxArgumentCount = 8;   ///< 인자 수의 상한
        static_assert( 32 + kMaxArgumentCount * ( kMaxArgumentBytes + 4 ) <= NetConnection::kMaxReliableMessageSize, "a call must fit one reliable message" );
    };
} // namespace sw

namespace sw
{
    /** @brief 서버가 돌려준 거절 하나입니다(`RemoteCallClient::pollRejections`). */
    struct RemoteCallRejectNotification
    {
        uint32          _sequence{ 0 }; ///< `callServer` 가 돌려준 순번
        RemoteCallError _error{ RemoteCallError::UnknownTarget };
    };

    /** @brief 호출 하나를 서버가 처리한 결과의 셈입니다. 진단 · 테스트가 읽습니다. */
    struct RemoteCallStats
    {
        uint64 _invokedCount{ 0 };          ///< 몸까지 부른 호출
        uint64 _rejectedCount{ 0 };         ///< 거절 알림을 돌려준 호출(대상 · 함수 없음, 인자)
        uint64 _validationFailedCount{ 0 }; ///< 검증 짝이 거짓이라 연결을 끊은 호출
        uint64 _violationCount{ 0 };        ///< 주인 아님 · 역할 · 채널을 어겨 연결을 끊은 호출
        uint64 _deliveryFailedCount{ 0 };   ///< 클라이언트가 대상을 기다리다 부르지 못했다고 알려 온 신뢰 호출
        uint64 _rateLimitedCount{ 0 };      ///< 호출률 상한(연결 · 함수)을 넘어 거절한 호출(`_rejectedCount` 에도 든다)
    };

    /** @brief 서버의 호출률 상한입니다. 기본값이 없다 — 게임 데이터가 정하고, 0 이하면 `RemoteCallServer::initialize` 가 오류로 멈춘다. */
    struct RemoteCallServerConfig
    {
        int32 _callBurstCount{ 0 };       ///< 연결 하나가 몰아 부를 수 있는 호출 수(버킷 크기)
        int64 _callRefillIntervalMs{ 0 }; ///< 호출 하나가 다시 차는 간격(밀리초) — 지속 상한은 초당 1000 / 이 값이다
    };
} // namespace sw

namespace sw
{
    /**
     * @struct RemoteCallTargets
     * @brief 대상 id 에서 (반사 타입, 인스턴스, 주인 연결)로 가는 표입니다. 서버 · 클라이언트가 같이 씁니다.
     */
    struct SW_KIT_API RemoteCallTargets
    {
        struct Target
        {
            const TypeInfo* _pType{ nullptr };
            void*           _pInstance{ nullptr };
            int32           _ownerConnectionID{ constant::invalid_index::kInt32 }; ///< 서버 — 이 대상의 `Server` 함수를 부를 수 있는 연결. 클라이언트는 쓰지 않는다
        };

        /** @brief 연결의 세션 객체를 가리키는 id 입니다. `registerTarget` 으로는 등록하지 못합니다(서버는 `registerSessionTarget`). */
        static constexpr uint32 kSessionTargetID = 0;

        /** @brief 와이어 번호 하나가 가리키는 함수와 그 검증 짝입니다. */
        struct RemoteFunction
        {
            const FunctionInfo* _pFunction{ nullptr };  ///< 없는 번호면 nullptr
            const FunctionInfo* _pValidator{ nullptr }; ///< `Validate` 함수의 짝(`validate<이름>`). 아니거나 없으면 nullptr
        };

        /**
         * @brief 타입 하나의 와이어 번호 → 함수 색인입니다. 타입마다 한 번 짓고 호출은 해시 조회 하나입니다(언리얼 `FClassNetCache` 와 같은 자리).
         * @details 반사 타입의 함수 목록은 모듈이 다시 읽히면 바뀌므로 지은 때의 타입 테이블 세대를 적고, 세대가 다르면 다시 짓는다.
         */
        struct FunctionTable
        {
            unordered_map<uint32, RemoteFunction> _mapFunction{};
            uint32                                _typeTableGeneration{ 0 };
            uint32                                _contractGeneration{ 0 }; ///< 원격 호출 계약을 확인한 세대. 같은 타입의 대상이 많아도 세대마다 한 번이다(0 = 아직)
        };

        unordered_map<uint32, Target>                 _mapTarget{};
        unordered_map<const TypeInfo*, FunctionTable> _mapFunctionTable{}; ///< 등록했거나 호출을 받은 타입마다 하나

        const Target* findTarget( uint32 targetID ) const;
        /**
         * @brief @p type 의 함수 가운데 @p role 의 원격 함수가 원격 호출 계약을 지키는지 봅니다 — 인자는 글 · 수 · bool · 열거형, `Validate` 면 짝 `validate<이름>` 이 bool 을 돌려주고 인자 타입이 같다.
         * @details 어긴 함수마다 오류를 남기고 false 를 반환합니다. 등록이 부른다 — 실행 중에 부를 수 없는 함수를 발견하는 일이 없게.
         */
        bool validateRemoteCallContract( const TypeInfo& type, FunctionNetRole role );
        /** @brief 함수의 와이어 번호입니다(이름 해시). */
        static uint32 getFunctionID( const FunctionInfo& function );
        /** @brief @p type 의 계층에서 와이어 번호가 @p functionID 인 함수를 찾습니다(파생이 이긴다). 없으면 빈 값입니다. 값으로 받아 함수 몸이 대상을 더 등록해도 그대로다. */
        RemoteFunction findFunctionByID( const TypeInfo& type, uint32 functionID );
        /** @brief @p type 의 지금 세대 색인입니다. 없거나 낡았으면 짓습니다. */
        FunctionTable& getOrBuildFunctionTable( const TypeInfo& type );
    };
} // namespace sw

namespace sw
{
    /**
     * @class RemoteCallServer
     * @brief 서버 쪽입니다. 클라이언트가 부른 `Server` 함수를 주인 · 역할 · 채널 · 검증을 거쳐 부르고, 주인 클라이언트의 `Client` 함수를 부릅니다.
     * @code
     *     if ( server.initialize( &peers, &router, config ) == false ) ...; // 호출률 상한이 없으면 오류
     *     _subscription = router.registerHandler( &server );
     *     if ( server.registerTarget( creatureID, *Creature::StaticType(), &creature, connectionID ) == false ) ...; // 계약을 어긴 타입은 등록되지 않는다
     *     (void)server.registerSessionTarget( connectionID, *HubAvatarAgent::StaticType(), &agent );      // 그 연결의 id 0
     *     (void)server.callSession( connectionID, "receiveProfile", { ReflectedValue::make( level ) } );
     * @endcode
     */
    class SW_KIT_API RemoteCallServer : public NetTableHandler<RemoteCallServer, NetRemoteCallMessage::Table, NetRemoteCallMessage::Call>
    {
    public:
        /** @brief 호출률 버킷이 읽는 단조 시계(밀리초)입니다. */
        using ClockFunction = int64 ( * )();

        RemoteCallServer();

        /**
         * @brief 호출률 버킷의 시계를 바꿉니다(기본은 `MonotonicClock`). 테스트는 고정 시계를 넣어 실행 속도(ASan · 부하)가 버킷 판정을 바꾸지 않게 한다.
         *        nullptr 은 받지 않는다(`SW_ASSERT`).
         */
        void setClock( ClockFunction pClock );

        /** @brief 보낼 창구 · 월드 세대를 묻는 라우터(직접 전달하는 테스트는 nullptr — 세대 0) · 호출률 상한을 받습니다. 상한이 0 이하면 오류를 남기고 false 입니다(기본값으로 대신하지 않는다). */
        [[nodiscard]] bool initialize( INetPeers* pPeers, const NetMessageRouter* pRouter, const RemoteCallServerConfig& config );
        /**
         * @brief 대상을 등록합니다. 연결 @p ownerConnectionID 만 이 대상의 `Server` 함수를 부를 수 있습니다.
         * @return 타입이 원격 호출 계약을 어기거나(`RemoteCallTargets::validateRemoteCallContract`), 인스턴스가 없거나, id 가 0(세션)이거나 이미 있으면 오류를 남기고 false 입니다
         */
        [[nodiscard]] bool registerTarget( uint32 targetID, const TypeInfo& type, void* pInstance, int32 ownerConnectionID );
        void               unregisterTarget( uint32 targetID );
        /**
         * @brief 연결 @p connectionID 의 세션 대상(id 0)을 등록합니다. 그 연결이 id 0 으로 부르면 이 대상이 받습니다.
         * @return 계약을 어기거나, 인스턴스가 없거나, 그 연결의 세션이 이미 있으면 오류를 남기고 false 입니다
         */
        [[nodiscard]] bool registerSessionTarget( int32 connectionID, const TypeInfo& type, void* pInstance );
        void               unregisterSessionTarget( int32 connectionID );
        /**
         * @brief 대상의 주인 클라이언트에게 `Client` 함수를 부릅니다. 채널은 함수의 `Reliable` 이 정합니다. 클라이언트에서도 같은 id 의 대상이 받습니다.
         * @return 대상이 없거나, 그 이름의 `Client` 함수가 없거나, 인자가 그 타입으로 바뀌지 않거나, 보내지 못하면 오류를 남기고 false 입니다
         */
        [[nodiscard]] bool callClient( uint32 targetID, string_view functionName, const vector<ReflectedValue>& listArg );
        /** @brief 연결의 세션 대상에서 `Client` 함수를 부릅니다 — 클라이언트의 id 0 대상이 받습니다. 실패는 `callClient` 와 같습니다. */
        [[nodiscard]] bool callSession( int32 connectionID, string_view functionName, const vector<ReflectedValue>& listArg );

        NetHandleResult receive( NetRemoteCallMessage::Call, const NetMessageContext& context, BitReader& body );
        /** @brief 보류의 열쇠는 대상 번호다 — 같은 대상의 호출끼리 순서를 지킨다. */
        uint64 readHoldKey( NetRemoteCallMessage::Call, BitReader& body ) const;
        /** @brief 클라이언트가 대상이 서기를 기다리다 끝내 부르지 못했습니다 — 세고 알린다. */
        void onMessageDeliveryFailed( int32 connectionID, uint8 kind, uint64 key, NetDeliveryFailure reason ) override;
        /** @brief 그 연결이 주인인 대상과 그 연결의 세션을 지웁니다(같은 연결 ID 를 다음 연결이 다시 쓴다). */
        void onConnectionClosed( int32 connectionID, NetDisconnectReason reason ) override;

        const RemoteCallStats& getStats() const { return _stats; }
        bool                   hasTarget( uint32 targetID ) const { return _targets.findTarget( targetID ) != nullptr; }
        bool                   hasSessionTarget( int32 connectionID ) const { return _mapSession.find( connectionID ) != _mapSession.end(); }

    private:
        /** @brief 호출이 가리키는 대상입니다. id 0 은 그 연결의 세션입니다. */
        const RemoteCallTargets::Target* findCallTarget( uint32 targetID, int32 connectionID ) const;
        /** @brief `Client` 함수 호출 하나를 @p target 의 주인에게 보냅니다. */
        [[nodiscard]] bool sendClientCall( const RemoteCallTargets::Target* pTarget, uint32 targetID, string_view functionName, const vector<ReflectedValue>& listArg );
        /** @brief 거절 알림을 보내고 셉니다. */
        void sendRejected( int32 connectionID, uint32 sequence, RemoteCallError error );
        /** @brief 규칙을 어긴 연결을 끊고 셉니다. */
        void disconnectViolator( int32 connectionID, uint32 functionID, const utf8* pWhy );
        /** @brief 연결의 호출 버킷에서 하나를 씁니다. 비었으면 false 입니다. */
        [[nodiscard]] bool tryConsumeCall( int32 connectionID, int64 nowMs );
        /** @brief 함수에 `MaxCallsPerSecond` 가 있으면 그 연결 · 그 함수의 버킷에서 하나를 씁니다. 없으면 늘 true(연결 상한만 받는다), 비었으면 false 입니다. */
        [[nodiscard]] bool tryConsumeFunctionCall( int32 connectionID, const FunctionInfo& function, int64 nowMs );

    private:
        RemoteCallTargets                               _targets;
        unordered_map<int32, RemoteCallTargets::Target> _mapSession;            ///< 연결마다 세션 대상(id 0)
        unordered_map<int32, TokenBucket>               _mapCallBucket;         ///< 연결마다 호출률 버킷. 연결이 닫히면 지운다
        unordered_map<uint64, TokenBucket>              _mapFunctionCallBucket; ///< (연결 << 32 | 함수 번호)마다 함수 호출률 버킷. 연결이 닫히면 지운다
        RemoteCallServerConfig                          _config;
        RemoteCallStats                                 _stats;
        NetMessageWriter                                _messageWriter;
        INetPeers*                                      _pPeers;
        const NetMessageRouter*                         _pRouter; ///< 월드 세대를 묻는다(소유하지 않음)
        ClockFunction                                   _pClock;
    };
} // namespace sw

namespace sw
{
    /**
     * @class RemoteCallClient
     * @brief 클라이언트 쪽입니다. 서버의 `Server` 함수를 부르고, 서버가 부른 `Client` · `Multicast` 함수를 등록한 대상에서 부릅니다.
     * @code
     *     client.initialize( &peers, &router );
     *     const uint32 sequence = client.callServer( avatarID, *HubAvatar::StaticType(), "attackCreature", { ReflectedValue::make( creatureID ) } );
     *     client.pollRejections( listNotification ); // sequence 가 있으면 서버가 받지 않았다
     * @endcode
     */
    class SW_KIT_API RemoteCallClient : public NetTableHandler<RemoteCallClient, NetRemoteCallMessage::Table, NetRemoteCallMessage::Call, NetRemoteCallMessage::Rejected>
    {
    public:
        RemoteCallClient();

        /** @param pRouter 월드 세대를 묻는 라우터. 없으면(직접 전달하는 테스트) 세대 0 이다 */
        void initialize( INetPeers* pPeers, const NetMessageRouter* pRouter )
        {
            _pPeers  = pPeers;
            _pRouter = pRouter;
        }
        /** @brief 서버가 부를 대상(`Client` · `Multicast` 함수를 가진)을 등록합니다. 계약을 어기거나 같은 id 가 있으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool registerTarget( uint32 targetID, const TypeInfo& type, void* pInstance );
        void               unregisterTarget( uint32 targetID );
        /**
         * @brief 서버 대상 @p targetID 의 `Server` 함수를 부릅니다. @p type 은 서버가 그 대상에 등록한 타입이며, 이름 · 역할 · 인자 수를 보내기 전에 여기서 봅니다.
         * @return 보낸 호출의 순번(1 부터)입니다. 그 이름의 `Server` 함수가 없거나, 인자가 많거나 그 타입으로 바뀌지 않거나, 비신뢰 호출이 1 KB 를 넘거나, 보내지 못하면 오류를 남기고 0 입니다
         */
        [[nodiscard]] uint32 callServer( uint32 targetID, const TypeInfo& type, string_view functionName, const vector<ReflectedValue>& listArg );

        NetHandleResult receive( NetRemoteCallMessage::Call, const NetMessageContext& context, BitReader& body );
        /** @brief 보류의 열쇠는 대상 번호다 — 같은 대상의 호출끼리 순서를 지킨다. */
        uint64          readHoldKey( NetRemoteCallMessage::Call, BitReader& body ) const;
        NetHandleResult receive( NetRemoteCallMessage::Rejected, const NetMessageContext& context, const RemoteCallRejectedBody& body );

        /** @brief 쌓인 거절 알림을 @p outListNotification 뒤에 옮깁니다. */
        void   pollRejections( vector<RemoteCallRejectNotification>& outListNotification );
        uint64 getInvokedCount() const { return _invokedCount; }

    private:
        RemoteCallTargets                         _targets;
        EventBuffer<RemoteCallRejectNotification> _rejections;
        NetMessageWriter                          _messageWriter;
        INetPeers*                                _pPeers;
        const NetMessageRouter*                   _pRouter; ///< 월드 세대를 묻는다(소유하지 않음)
        uint64                                    _invokedCount;
        uint32                                    _nextSequence;
    };
} // namespace sw
