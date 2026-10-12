/**
 * @file OnlineReplyUtil.h
 * @brief 키트 클라이언트가 받은 응답(`OnlineResponse`)을 키트 응답 구조체로 읽는 공통 처리를 정의합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Network/BitStream.h"

#include "GameFramework/Base/Online/Service/OnlineServiceClient.h"

namespace sw
{
    /**
     * @struct OnlineReplyUtil
     * @brief "오류 코드면 결과로 바꾸고, 본문을 못 읽으면 Unavailable" 을 키트마다 적지 않게 모은 도우미입니다.
     * @details @p TProtocol 은 `fromErrorCode( uint16 )` 과 `readReply( BitReader&, TReply& )`(또는 메서드를 받는 판)를 가진 키트 프로토콜 구조체다.
     *          @p TReply 는 `_result` 필드를 가지고, 그 열거형에 `Unavailable` 이 있어야 한다.
     * @code
     *     if ( OnlineReplyUtil::readResponse<EconomyProtocol>( response, reply._reply ) && reply._reply._result == EconomyResult::Ok )
     *         applyBalances( ... ); // 본문을 읽은 성공 응답에만 하는 일
     * @endcode
     */
    struct OnlineReplyUtil
    {
        /**
         * @brief @p response 를 @p outReply 로 읽습니다. 본문을 읽었으면 true 입니다.
         * @details 전송 · 서비스 오류면 `_result` 만 `TProtocol::fromErrorCode` 로 채우고 false 다.
         *          본문을 못 읽으면 @p outReply 를 비우고 `_result` 를 `Unavailable` 로 두며 false 다.
         */
        template <typename TProtocol, typename TReply>
        [[nodiscard]] static bool readResponse( const OnlineResponse& response, TReply& outReply )
        {
            if ( response.isOk() == false )
            {
                outReply._result = TProtocol::fromErrorCode( response._errorCode );
                return false;
            }
            BitReader body( response._pBody, response._bodySize );
            if ( TProtocol::readReply( body, outReply ) )
                return true;
            markUnavailable( outReply );
            return false;
        }

        /** @brief `readResponse` 에서 본문 읽기가 요청 메서드를 받는 키트(채팅)용 판입니다. */
        template <typename TProtocol, typename TReply>
        [[nodiscard]] static bool readResponse( const OnlineResponse& response, uint16 method, TReply& outReply )
        {
            if ( response.isOk() == false )
            {
                outReply._result = TProtocol::fromErrorCode( response._errorCode );
                return false;
            }
            BitReader body( response._pBody, response._bodySize );
            if ( TProtocol::readReply( body, method, outReply ) )
                return true;
            markUnavailable( outReply );
            return false;
        }

    private:
        template <typename TReply>
        static void markUnavailable( TReply& outReply )
        {
            using TResult    = decltype( outReply._result );
            outReply         = TReply{};
            outReply._result = TResult::Unavailable;
        }
    };
} // namespace sw
