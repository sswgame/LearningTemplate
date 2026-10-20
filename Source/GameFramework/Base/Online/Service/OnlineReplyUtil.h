/**
 * @file OnlineReplyUtil.h
 * @brief 키트 클라이언트가 받은 응답(`OnlineResponse`)을 키트 응답 구조체로 읽는 공통 처리를 정의합니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Network/BitStream.h"

#include "Engine/Serialization/Fixed/FixedLayout.h"

#include "GameFramework/Base/Online/Service/OnlineProtocol.h"

namespace sw
{
    /**
     * @struct OnlineReplyUtil
     * @brief "오류 코드면 결과로 바꾸고, 본문을 못 읽으면 Unavailable" 을 키트마다 적지 않게 모은 도우미입니다.
     * @details @p TResponse 는 `OnlineResponse`(`isOk`, `_errorCode`, `_pBody`, `_bodySize`)다. 템플릿 인자라 이 헤더는 `OnlineServiceClient.h` 를 끌어오지 않는다.
     *          @p TProtocol 은 `fromErrorCode( uint16 )` 과 메시지 타입 목록 `MessageTypes`(`OnlineMessageTypes`, @p TReply 를 담는다)를 가진 키트 프로토콜 구조체다.
     *          @p TReply 는 `_result` 필드를 가지고, 그 열거형에 `Unavailable` 과 `UpdateRequired` 가 있어야 한다.
     * @code
     *     if ( OnlineReplyUtil::readResponse<EconomyProtocol>( response, reply._reply ) && reply._reply._result == EconomyResult::Ok )
     *         applyBalances( ... ); // 본문을 읽은 성공 응답에만 하는 일
     * @endcode
     */
    struct OnlineReplyUtil
    {
        /**
         * @brief @p response 를 @p outReply 로 읽습니다. 본문을 읽었으면 true 입니다.
         * @details 전송 · 서비스 오류면 `_result` 만 `markError` 로 채우고 false 다(`kVersionMismatch` 는 `UpdateRequired`, 나머지는 `TProtocol::fromErrorCode`).
         *          본문을 못 읽으면(`FixedReadResult` 가 `Ok` 가 아니면) @p outReply 를 비우고 `_result` 를 `Unavailable` 로 두며 false 다.
         */
        template <typename TProtocol, typename TReply, typename TResponse>
        [[nodiscard]] static bool readResponse( const TResponse& response, TReply& outReply )
        {
            if ( response.isOk() == false )
            {
                markError<TProtocol>( response._errorCode, outReply );
                return false;
            }
            BitReader body( response._pBody, response._bodySize );
            if ( TProtocol::MessageTypes::read( body, outReply ) == FixedReadResult::Ok )
                return true;
            markUnavailable( outReply );
            return false;
        }

        /**
         * @brief 오류 코드를 결과로 바꿉니다. 키트 표보다 먼저 공통 오류 `kVersionMismatch` 를 `UpdateRequired` 로 바꿉니다.
         * @details 키트 표(`fromErrorCode`)는 모르는 코드를 대개 `Unavailable`(다시 시도)로 바꾼다. 해시 불일치는 다시 접속하지 않으므로 재시도를 부르면 안 된다.
         *          그래서 키트 표가 아니라 이 한 곳에서 막는다. 결과 열거형에 `UpdateRequired` 가 없으면 컴파일되지 않는다.
         *          `readResponse` 를 쓰지 않는 키트 클라이언트(거래)도 오류 응답은 이 함수로 바꾼다.
         */
        template <typename TProtocol, typename TReply>
        static void markError( uint16 errorCode, TReply& outReply )
        {
            using TResult    = decltype( outReply._result );
            outReply._result = errorCode == OnlineError::kVersionMismatch ? TResult::UpdateRequired : TProtocol::fromErrorCode( errorCode );
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
