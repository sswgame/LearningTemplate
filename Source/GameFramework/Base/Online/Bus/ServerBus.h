/**
 * @file ServerBus.h
 * @brief 서버 프로세스끼리의 알림입니다. 주제(topic) 발행과 구독이며, 최대 한 번 배달합니다.
 * @details 놓치면 안 되는 것은 영속 저장 + 다시 읽기(버스는 "빨리 알기" 용). 받은 메시지에 자기 서버가 보낸 것도 온다(`getOriginServerID` 로 거른다).
 *          주제는 `[0-9a-z_.]`(200 B 이하)이다. 예: "account.revoke", "chat.channel.<이름>". 틀린 주제의 발행과 구독은 경고를 남기고 버린다.
 *          - 바이트 전송(`IServerBusTransport`)과 타입 있는 앞(`ServerBus`)이 나뉜다. 전송은 호스트와 서버 조립만 가지고, 키트는 앞만 받는다.
 *            앞의 바이트 발행과 메시지의 본문 바이트는 주제 정의(`ServerBusTopic`, `ServerBusTopicFamily`)만 쓰고 읽는다. 키트가 바이트를 직접 쓰거나
 *            `BitReader` 로 읽는 코드는 컴파일되지 않는다(docs/plans/ServerBusFormat.md 3.1).
 *          - 본문은 자기 기술형(A, 의미 버전 머리 + `BinarySerializer`)이다. 서버 사이에는 핸드셰이크가 없어 배포 중에 빌드가 섞이기 때문이다.
 *          구현은 `EphemeralServerBus`(캐시 발행/구독 위, 서버 여럿)와 `LocalServerBus`(프로세스 안, 서버 한 대와 테스트)다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    template <typename T>
    class ServerBusTopic;
    template <typename T>
    class ServerBusTopicFamily;

    /** @brief 버스 메시지 하나입니다. 본문 바이트는 주제 정의만 읽습니다. */
    class ServerBusMessage
    {
    public:
        ServerBusMessage()
            : _bytes{}
            , _topic{}
            , _originServerID{ 0 }
            , _sequence{ 0 }
        {
        }
        /** @brief 전송 구현과 테스트가 받은(또는 흉내 낸) 메시지를 만듭니다. */
        ServerBusMessage( vector<uint8> bytes, string topic, uint64 originServerID, uint64 sequence )
            : _bytes{ std::move( bytes ) }
            , _topic{ std::move( topic ) }
            , _originServerID{ originServerID }
            , _sequence{ sequence }
        {
        }

        const string& getTopic() const { return _topic; }
        uint64        getOriginServerID() const { return _originServerID; }
        /** @brief 보낸 서버 안의 순번입니다. 1 부터 세며, 빠짐을 감지하는 진단에만 씁니다. */
        uint64 getSequence() const { return _sequence; }
        int32  getByteCount() const { return static_cast<int32>( _bytes.size() ); }

    private:
        template <typename T>
        friend class ServerBusTopic;
        template <typename T>
        friend class ServerBusTopicFamily;

        vector<uint8> _bytes;
        string        _topic;
        uint64        _originServerID;
        uint64        _sequence;
    };
} // namespace sw

namespace sw
{
    /**
     * @class IServerBusTransport
     * @brief 서버 간 버스의 바이트 전송입니다. 서비스 스레드 하나가 발행하고 틱마다 회수합니다.
     * @details 서비스는 이것을 직접 받지 않는다. 호스트가 비우고 `subscribeServerBus` 한 서비스에 나눠 준다. 소비자가 둘이면 서로의 메시지를 가져가기 때문이다.
     */
    class SW_GF_API IServerBusTransport
    {
    public:
        static constexpr int32 kMaxTopicSize   = 200;
        static constexpr int32 kMaxMessageSize = 60 * 1024; ///< 봉투를 더해도 캐시 값 상한(64 KiB) 안에 들어가는 크기

        IServerBusTransport()          = default;
        virtual ~IServerBusTransport() = default;

        IServerBusTransport( const IServerBusTransport& )            = delete;
        IServerBusTransport& operator=( const IServerBusTransport& ) = delete;

        virtual void publish( string_view topic, const uint8* pData, int32 size ) = 0;
        virtual void subscribe( string_view topic )                               = 0;
        virtual void unsubscribe( string_view topic )                             = 0;
        /** @brief 받은 메시지를 뒤에 붙입니다. 서비스 스레드에서 틱마다 부릅니다. 붙인 수를 돌려줍니다. */
        virtual int32  pollMessages( vector<ServerBusMessage>& outListMessage ) = 0;
        virtual uint64 getServerID() const                                      = 0;

        /** @brief 주제 규칙(`[0-9a-z_.]`, 1..200 B)을 지키는가입니다. */
        static bool isValidTopic( string_view topic );
    };
} // namespace sw

namespace sw
{
    /**
     * @class ServerBus
     * @brief 타입 있는 버스의 앞입니다. 키트 의존성은 이것만 받습니다. 전송은 빌려 쓰며 이 객체보다 오래 살아야 합니다.
     * @details 공개 함수는 서버 id 하나입니다. 발행은 주제 정의(`ServerBusTopic::publish`)로만 합니다.
     */
    class ServerBus final
    {
    public:
        explicit ServerBus( IServerBusTransport& transport )
            : _pTransport{ &transport }
        {
        }

        ServerBus( const ServerBus& )            = delete;
        ServerBus& operator=( const ServerBus& ) = delete;

        uint64 getServerID() const { return _pTransport->getServerID(); }

    private:
        template <typename T>
        friend class ServerBusTopic;
        template <typename T>
        friend class ServerBusTopicFamily;

        void publishBytes( string_view topic, const vector<uint8>& bytes ) { _pTransport->publish( topic, bytes.data(), static_cast<int32>( bytes.size() ) ); }

        IServerBusTransport* _pTransport;
    };
} // namespace sw
