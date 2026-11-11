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
 *          - **인자**: 글로 실어 보내고 받는 쪽이 인자 타입으로 읽는다(`ReflectionInvoke::callWithText`). 그래서 원격 함수의 인자는 글 · 수 · bool · 열거형만 된다 —
 *            등록이 확인한다. 인자 수 · 길이 · 함수 이름 길이는 상한이 있고 넘는 메시지는 손상된 것으로 버린다. 함수 이름은 전역 이름 표에 넣지 않고 찾기만 한다(`findInterned`).
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

#include "Engine/Reflection/ReflectedValue.h"
#include "Engine/Reflection/ReflectionMetadataBox.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Network/NetKitMessageRange.h"

namespace sw
{
    class INetPeers;

    struct FunctionInfo;
    struct TypeInfo;

    /** @brief 반사 함수 호출의 메시지 표입니다. 종류 · 마스크 · 와이어 버전은 표에서 나온다(`NetMessageTable`). */
    struct NetRemoteCallMessage
    {
        /** @brief 호출 하나 — 순번(varuint) · 대상 id(varuint) · 함수 이름(blob) · 인자 수(varuint) · 인자 글(blob 들). 클라이언트 → 서버는 `Server` 함수, 서버 → 클라이언트는 `Client` · `Multicast` 함수다 */
        using Call = NetMessageDef<0, NetMessageDirection::Both, NetChannelType::ReliableOrdered>;
        /** @brief 서버 → 클라이언트. 받지 못한 호출 — 순번(varuint) · 까닭(1 바이트 `RemoteCallError`) */
        using Rejected = NetMessageDef<1, NetMessageDirection::ToClient, NetChannelType::ReliableOrdered>;
        using Table    = NetMessageTable<NetKitMessageRange::kRemoteCall, Call, Rejected>;

        static constexpr uint8 kCall     = Table::getKindOf<Call>();
        static constexpr uint8 kRejected = Table::getKindOf<Rejected>();

        static constexpr int32 kMaxFunctionNameBytes = 64;  ///< 함수 이름 글의 상한
        static constexpr int32 kMaxArgumentBytes     = 256; ///< 인자 글 하나의 상한
        static constexpr int32 kMaxArgumentCount     = 8;   ///< 인자 수의 상한
        static_assert( 32 + kMaxFunctionNameBytes + kMaxArgumentCount * ( kMaxArgumentBytes + 4 ) <= NetConnection::kMaxReliableMessageSize, "a call must fit one reliable message" );
    };
} // namespace sw

namespace sw
{
    /** @brief 받지 못한 호출의 까닭입니다. 값은 와이어 형식(`Rejected` 의 1 바이트)이라 바꾸면 `Rejected` 의 본문 판을 올린다. */
    enum class RemoteCallError : uint8
    {
        UnknownTarget = 0, ///< 그 id 의 대상이 없다(이미 사라졌다)
        UnknownFunction,   ///< 대상 타입에 그 이름의 함수가 없다
        ArgumentMismatch   ///< 인자를 그 타입으로 읽지 못했거나 수가 맞지 않는다
    };
} // namespace sw

namespace sw
{
    /** @brief 서버가 돌려준 거절 하나입니다(`RemoteCallClient::pollRejections`). */
    struct RemoteCallRejectNotice
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

        unordered_map<uint32, Target> _mapTarget{};

        const Target* findTarget( uint32 targetID ) const;
        /**
         * @brief @p type 의 함수 가운데 @p role 의 원격 함수가 원격 호출 계약을 지키는지 봅니다 — 인자는 글 · 수 · bool · 열거형, `Validate` 면 짝 `validate<이름>` 이 bool 을 돌려주고 인자 타입이 같다.
         * @details 어긴 함수마다 오류를 남기고 false 를 반환합니다. 등록이 부른다 — 실행 중에 부를 수 없는 함수를 발견하는 일이 없게.
         */
        static bool validateRemoteCallContract( const TypeInfo& type, FunctionNetRole role );
        /** @brief @p function 의 검증 짝(`validate<이름>`)을 @p type 의 계층에서 찾습니다. 없으면 nullptr 입니다. */
        static const FunctionInfo* findValidator( const TypeInfo& type, const FunctionInfo& function );
    };
} // namespace sw

namespace sw
{
    /**
     * @class RemoteCallServer
     * @brief 서버 쪽입니다. 클라이언트가 부른 `Server` 함수를 주인 · 역할 · 채널 · 검증을 거쳐 부르고, 주인 클라이언트의 `Client` 함수를 부릅니다.
     * @code
     *     (void)server.initialize( &peers );
     *     _subscription = router.registerHandler( &server );
     *     if ( server.registerTarget( creatureID, *Creature::StaticType(), &creature, connectionID ) == false ) ...; // 계약을 어긴 타입은 등록되지 않는다
     *     (void)server.registerSessionTarget( connectionID, *ValeAvatarAgent::StaticType(), &agent );      // 그 연결의 id 0
     *     (void)server.callSession( connectionID, "receiveProfile", { ReflectedValue::make( level ) } );
     * @endcode
     */
    class SW_KIT_API RemoteCallServer : public NetTableHandler<RemoteCallServer, NetRemoteCallMessage::Table, NetRemoteCallMessage::Call>
    {
    public:
        RemoteCallServer();

        void initialize( INetPeers* pPeers ) { _pPeers = pPeers; }
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
         * @return 대상이 없거나, 그 이름의 `Client` 함수가 없거나, 인자를 글로 싣지 못하거나, 보내지 못하면 오류를 남기고 false 입니다
         */
        [[nodiscard]] bool callClient( uint32 targetID, string_view functionName, const vector<ReflectedValue>& listArg );
        /** @brief 연결의 세션 대상에서 `Client` 함수를 부릅니다 — 클라이언트의 id 0 대상이 받습니다. 실패는 `callClient` 와 같습니다. */
        [[nodiscard]] bool callSession( int32 connectionID, string_view functionName, const vector<ReflectedValue>& listArg );

        NetHandleResult receive( NetRemoteCallMessage::Call, const NetMessageContext& context, BitReader& body );
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
        void disconnectViolator( int32 connectionID, string_view functionName, const utf8* pWhy );

    private:
        RemoteCallTargets                               _targets;
        unordered_map<int32, RemoteCallTargets::Target> _mapSession; ///< 연결마다 세션 대상(id 0)
        RemoteCallStats                                 _stats;
        NetMessageWriter                                _messageWriter;
        vector<string>                                  _listReceivedScratch; ///< 받은 호출의 인자 글. 함수 몸이 다시 보내도(`callSession` · `callServer`) 덮이지 않게 보내기와 따로 둔다
        vector<string>                                  _listSendScratch;
        INetPeers*                                      _pPeers;
    };
} // namespace sw

namespace sw
{
    /**
     * @class RemoteCallClient
     * @brief 클라이언트 쪽입니다. 서버의 `Server` 함수를 부르고, 서버가 부른 `Client` · `Multicast` 함수를 등록한 대상에서 부릅니다.
     * @code
     *     client.initialize( &peers );
     *     const uint32 sequence = client.callServer( avatarID, *ValeAvatar::StaticType(), "attackCreature", { ReflectedValue::make( creatureID ) } );
     *     client.pollRejections( listNotice ); // sequence 가 있으면 서버가 받지 않았다
     * @endcode
     */
    class SW_KIT_API RemoteCallClient : public NetTableHandler<RemoteCallClient, NetRemoteCallMessage::Table, NetRemoteCallMessage::Call, NetRemoteCallMessage::Rejected>
    {
    public:
        RemoteCallClient();

        void initialize( INetPeers* pPeers ) { _pPeers = pPeers; }
        /** @brief 서버가 부를 대상(`Client` · `Multicast` 함수를 가진)을 등록합니다. 계약을 어기거나 같은 id 가 있으면 오류를 남기고 false 입니다. */
        [[nodiscard]] bool registerTarget( uint32 targetID, const TypeInfo& type, void* pInstance );
        void               unregisterTarget( uint32 targetID );
        /**
         * @brief 서버 대상 @p targetID 의 `Server` 함수를 부릅니다. @p type 은 서버가 그 대상에 등록한 타입이며, 이름 · 역할 · 인자 수를 보내기 전에 여기서 봅니다.
         * @return 보낸 호출의 순번(1 부터)입니다. 그 이름의 `Server` 함수가 없거나, 인자가 많거나 글로 싣지 못하거나, 비신뢰 호출이 1 KB 를 넘거나, 보내지 못하면 오류를 남기고 0 입니다
         */
        [[nodiscard]] uint32 callServer( uint32 targetID, const TypeInfo& type, string_view functionName, const vector<ReflectedValue>& listArg );

        NetHandleResult receive( NetRemoteCallMessage::Call, const NetMessageContext& context, BitReader& body );
        NetHandleResult receive( NetRemoteCallMessage::Rejected, const NetMessageContext& context, BitReader& body );

        /** @brief 쌓인 거절 알림을 @p outListNotice 뒤에 옮깁니다. */
        void   pollRejections( vector<RemoteCallRejectNotice>& outListNotice );
        uint64 getInvokedCount() const { return _invokedCount; }

    private:
        RemoteCallTargets                   _targets;
        EventBuffer<RemoteCallRejectNotice> _rejections;
        NetMessageWriter                    _messageWriter;
        vector<string>                      _listReceivedScratch; ///< 받은 호출의 인자 글. 함수 몸이 다시 보내도(`callSession` · `callServer`) 덮이지 않게 보내기와 따로 둔다
        vector<string>                      _listSendScratch;
        INetPeers*                          _pPeers;
        uint64                              _invokedCount;
        uint32                              _nextSequence;
    };
} // namespace sw
