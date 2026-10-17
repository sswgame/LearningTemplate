/**
 * @file ServerBusTestUtil.h
 * @brief 서버 버스 주제 정의의 왕복 도우미입니다. 버스 전용 타입은 키트 `MessageTypes`(B) 목록에 없으므로 주제로 왕복합니다.
 */
#pragma once
#include "Engine/Serialization/Fixed/FixedLayout.h"

#include "GameFramework/Base/Online/Bus/LocalServerBus.h"
#include "GameFramework/Base/Online/Bus/ServerBusTopic.h"

namespace test
{
    struct ServerBusTestUtil
    {
        /** @brief @p message 를 프로세스 안 버스 둘로 발행하고 받아 같은 값으로 읽히면 true 입니다. 범위 밖 값이면 발행이 false 입니다. */
        template <typename T>
        static bool roundTrips( const sw::ServerBusTopic<T>& topic, const T& message )
        {
            sw::LocalServerBusHub hub;
            sw::LocalServerBus    sender{ &hub, 1 };
            sw::LocalServerBus    receiver{ &hub, 2 };
            sw::ServerBus         bus{ sender };
            receiver.subscribe( topic.getName() );
            if ( topic.publish( bus, message ) == false )
                return false;
            sw::vector<sw::ServerBusMessage> listMessage;
            if ( receiver.pollMessages( listMessage ) != 1 )
                return false;
            T decoded{};
            return topic.read( listMessage[0], decoded ) == sw::ServerBusReadResult::Read && sw::FixedLayout::equals( decoded, message );
        }

        /** @brief 접미사 주제 가족판입니다. */
        template <typename T>
        static bool roundTrips( const sw::ServerBusTopicFamily<T>& family, sw::string_view suffix, const T& message )
        {
            sw::LocalServerBusHub hub;
            sw::LocalServerBus    sender{ &hub, 1 };
            sw::LocalServerBus    receiver{ &hub, 2 };
            sw::ServerBus         bus{ sender };
            receiver.subscribe( family.makeTopic( suffix ) );
            if ( family.publish( bus, suffix, message ) == false )
                return false;
            sw::vector<sw::ServerBusMessage> listMessage;
            if ( receiver.pollMessages( listMessage ) != 1 )
                return false;
            T decoded{};
            return family.read( listMessage[0], decoded ) == sw::ServerBusReadResult::Read && sw::FixedLayout::equals( decoded, message );
        }
    };
} // namespace test
