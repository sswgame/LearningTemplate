/**
 * @file ServerBusTopic.h
 * @brief 서버 버스 주제 정의입니다. 주제 하나가 이름과 본문 타입을 함께 가지고, 그 타입으로만 쓰고 읽습니다(docs/09_Decisions.md 5-9 서버 버스 주제).
 * @details - `ServerBusTopic<T>` 는 고정 이름 주제, `ServerBusTopicFamily<T>` 는 접미사가 붙는 주제(`chat.channel.<채널>`)입니다. `static constexpr` 객체로 선언합니다.
 *          - 본문은 `T::kBusVersion`(타입 안의 `static constexpr uint32`, 의미 버전) 머리 + A 입니다(`MixedBuildCodec`). 없으면 컴파일이 멈춥니다.
 *            필드 이름 짝짓기로 감당할 수 없는 변경(뜻이 바뀜, 기본값을 처리할 수 없는 새 필드)에서만 버전을 올립니다. 다르면 섞인 동안 거절합니다(이관 없음).
 *          - 읽기 실패는 (타입, 결과, 받은 버전)마다 한 번 오류 로그를 남기고 `ServerBusReadLog::getRejectedCount` 를 올립니다. 부르는 쪽은 결과만 보고 버립니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "GameFramework/Base/Online/Bus/MixedBuildCodec.h"
#include "GameFramework/Base/Online/Bus/ServerBus.h"
#include "GameFramework/GameFrameworkExports.h"

#include <type_traits>

namespace sw
{
    /** @brief 주제 정의의 `read` 결과입니다. */
    enum class [[nodiscard]] ServerBusReadResult : uint8
    {
        Read,            ///< 읽었습니다
        OtherTopic,      ///< 이 주제의 메시지가 아닙니다(로그 없음)
        VersionMismatch, ///< 의미 버전이 다릅니다(섞인 빌드)
        Unreadable,      ///< 아는 필드를 읽지 못했거나(모르는 열거자, 바뀐 타입) 바이트가 깨졌습니다
        OutOfRange       ///< 선언된 범위(`Min`, `Max`, `MaxCount`) 밖입니다
    };
} // namespace sw

namespace sw
{
    /** @struct ServerBusReadLog @brief 버스 읽기 거절의 로그와 카운터입니다. 프로세스 하나에 하나입니다. */
    struct SW_GF_API ServerBusReadLog
    {
        /** @brief 거절을 셉니다. 로그는 (타입, 결과, 받은 버전)마다 한 번입니다. 배포 중 채팅 중계에서 메시지마다 로그를 쓰지 않으려는 것입니다. */
        static void noteRejected( const hashed_string& typeFqn, string_view topic, ServerBusReadResult result, uint32 readVersion );
        /** @brief 크기 상한을 넘어 발행하지 않은 메시지를 알립니다(매번). */
        static void noteTooLarge( const hashed_string& typeFqn, string_view topic, int32 byteCount );
        /** @brief 이 프로세스가 지금까지 거절한 버스 메시지 수입니다(진단, 테스트). */
        static uint64 getRejectedCount();
    };
} // namespace sw

namespace sw
{
    /** @brief @p T 에 `static constexpr uint32 kBusVersion` 이 있는가입니다. */
    template <typename T, typename = void>
    struct HasServerBusVersion : std::false_type
    {
    };

    template <typename T>
    struct HasServerBusVersion<T, std::void_t<decltype( T::kBusVersion )>> : std::is_same<std::remove_cv_t<decltype( T::kBusVersion )>, uint32>
    {
    };
} // namespace sw

namespace sw
{
    /** @class ServerBusTopic @brief 고정 이름 주제입니다. */
    template <typename T>
    class ServerBusTopic
    {
        static_assert( HasServerBusVersion<T>::value, "a server bus body type declares its semantic version as 'static constexpr uint32 kBusVersion'" );

    public:
        explicit constexpr ServerBusTopic( const utf8* pName )
            : _pName{ pName }
        {
        }

        const utf8* getName() const { return _pName; }
        bool        matches( const ServerBusMessage& message ) const { return message._topic == _pName; }

        /** @brief @p message 를 발행합니다. 쓰지 못했거나 크기 상한을 넘으면 오류 로그를 남기고 false 입니다(고칠 곳은 값을 만든 코드입니다). */
        [[nodiscard]] bool publish( ServerBus& bus, const T& message ) const
        {
            vector<uint8> bytes;
            if ( MixedBuildCodec::encode( T::kBusVersion, message, bytes ) == false )
                return false;
            if ( static_cast<int32>( bytes.size() ) > IServerBusTransport::kMaxMessageSize )
            {
                ServerBusReadLog::noteTooLarge( T::StaticType()->_fullyQualifiedName, _pName, static_cast<int32>( bytes.size() ) );
                return false;
            }
            bus.publishBytes( _pName, bytes );
            return true;
        }

        /** @brief 이 주제의 메시지이면 @p outMessage 로 읽습니다. `Read` 일 때만 @p outMessage 를 바꿉니다. */
        ServerBusReadResult read( const ServerBusMessage& message, T& outMessage ) const
        {
            if ( matches( message ) == false )
                return ServerBusReadResult::OtherTopic;
            return ServerBusTopic<T>::decode( message, outMessage );
        }

    private:
        template <typename U>
        friend class ServerBusTopicFamily;

        /** @brief 주제를 이미 확인한 메시지를 읽고, 실패를 알립니다. 두 주제 정의가 함께 씁니다. */
        static ServerBusReadResult decode( const ServerBusMessage& message, T& outMessage )
        {
            uint32                     readVersion = 0;
            const MixedBuildReadResult result      = MixedBuildCodec::decode( message._bytes.data(), message._bytes.size(), T::kBusVersion, outMessage, readVersion );
            if ( result == MixedBuildReadResult::Read )
                return ServerBusReadResult::Read;
            const ServerBusReadResult busResult = toBusResult( result );
            ServerBusReadLog::noteRejected( T::StaticType()->_fullyQualifiedName, message._topic, busResult, readVersion );
            return busResult;
        }

        static ServerBusReadResult toBusResult( const MixedBuildReadResult result )
        {
            switch ( result )
            {
                case MixedBuildReadResult::Read:
                    return ServerBusReadResult::Read;
                case MixedBuildReadResult::VersionMismatch:
                    return ServerBusReadResult::VersionMismatch;
                case MixedBuildReadResult::Unreadable:
                    return ServerBusReadResult::Unreadable;
                case MixedBuildReadResult::OutOfRange:
                    return ServerBusReadResult::OutOfRange;
            }
            return ServerBusReadResult::Unreadable;
        }

        const utf8* _pName;
    };
} // namespace sw

namespace sw
{
    /** @class ServerBusTopicFamily @brief 접두사 뒤에 접미사가 붙는 주제입니다. 접두사는 `.` 으로 끝납니다(`"chat.channel."`). */
    template <typename T>
    class ServerBusTopicFamily
    {
        static_assert( HasServerBusVersion<T>::value, "a server bus body type declares its semantic version as 'static constexpr uint32 kBusVersion'" );

    public:
        explicit constexpr ServerBusTopicFamily( const utf8* pPrefix )
            : _pPrefix{ pPrefix }
        {
        }

        const utf8* getPrefix() const { return _pPrefix; }
        string      makeTopic( string_view suffix ) const
        {
            string topic( _pPrefix );
            topic += suffix;
            return topic;
        }

        /** @brief @p topic 이 이 가족의 주제(접두사 + 빈 아닌 접미사)이면 true 입니다. */
        bool matchesTopic( string_view topic ) const
        {
            const string_view prefix( _pPrefix );
            return topic.size() > prefix.size() && topic.substr( 0, prefix.size() ) == prefix;
        }
        bool matches( const ServerBusMessage& message ) const { return matchesTopic( message._topic ); }

        /** @brief 이 가족의 메시지이면 접미사, 아니면 빈 글입니다. */
        string_view findSuffix( const ServerBusMessage& message ) const
        {
            if ( matches( message ) == false )
                return {};
            return string_view( message._topic ).substr( string_view( _pPrefix ).size() );
        }

        /** @brief `makeTopic( suffix )` 로 @p message 를 발행합니다. 실패 규칙은 `ServerBusTopic::publish` 와 같습니다. */
        [[nodiscard]] bool publish( ServerBus& bus, string_view suffix, const T& message ) const
        {
            const string  topic = makeTopic( suffix );
            vector<uint8> bytes;
            if ( MixedBuildCodec::encode( T::kBusVersion, message, bytes ) == false )
                return false;
            if ( static_cast<int32>( bytes.size() ) > IServerBusTransport::kMaxMessageSize )
            {
                ServerBusReadLog::noteTooLarge( T::StaticType()->_fullyQualifiedName, topic, static_cast<int32>( bytes.size() ) );
                return false;
            }
            bus.publishBytes( topic, bytes );
            return true;
        }

        /** @brief 이 가족의 메시지이면 @p outMessage 로 읽습니다. `Read` 일 때만 @p outMessage 를 바꿉니다. */
        ServerBusReadResult read( const ServerBusMessage& message, T& outMessage ) const
        {
            if ( matches( message ) == false )
                return ServerBusReadResult::OtherTopic;
            return ServerBusTopic<T>::decode( message, outMessage );
        }

    private:
        const utf8* _pPrefix;
    };
} // namespace sw
