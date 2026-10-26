/**
 * @file LiveEventControl.h
 * @brief 라이브 이벤트를 운영하는 인터페이스입니다. 라이브 운영 키트가 구현해 서버 조립 문맥(`OnlineServerKitContext::_kits`)에 내놓고, GM 키트가 읽어 부릅니다.
 * @details - 키트끼리 include 하지 않도록 이벤트 정의는 리플렉션 JSON 글(`LiveEventDefinition` 의 필드 이름)로 주고받습니다. 정의의 형식과 검사는 라이브 운영 키트 한 곳에 있습니다.
 *          - 바꾸기는 비동기입니다. 결과는 요청 태그와 함께 `drainControlCompletions` 로 받습니다. 감사 기록은 구현이 변경과 같은 트랜잭션에 씁니다.
 *          - 서비스 스레드 하나에서만 씁니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

#include "GameFramework/Base/Online/Identity/AccountDirectory.h"

namespace sw
{
    /** @brief 이벤트 바꾸기의 결과입니다. */
    enum class LiveEventControlResult : uint8
    {
        Ok = 0,
        Invalid,    ///< JSON 이 아니거나 정의가 규칙에 맞지 않는다
        NotFound,   ///< 없는 이벤트이다
        Unavailable ///< 저장소를 쓸 수 없다. 다시 하면 된다
    };

    /** @brief 바꾸기 하나의 완료입니다. */
    struct LiveEventControlCompletion
    {
        uint64                 _requestTag{ 0 };
        LiveEventControlResult _result{ LiveEventControlResult::Ok };
    };
} // namespace sw

namespace sw
{
    /**
     * @class ILiveEventControl
     * @brief 라이브 이벤트 운영 창입니다. @p actorID 는 감사 기록의 GM 계정입니다.
     */
    class ILiveEventControl
    {
    public:
        ILiveEventControl()          = default;
        virtual ~ILiveEventControl() = default;

        ILiveEventControl( const ILiveEventControl& )            = delete;
        ILiveEventControl& operator=( const ILiveEventControl& ) = delete;

        /** @brief JSON 정의 하나를 올리거나 같은 id 를 덮어씁니다. */
        virtual void putEventJSON( string_view definitionJSON, AccountID actorID, string_view memo, int64 nowMs, uint64 requestTag ) = 0;
        virtual void removeEvent( string_view eventID, AccountID actorID, string_view memo, int64 nowMs, uint64 requestTag )         = 0;
        /** @brief 이 서버가 마지막으로 읽은 정의를 JSON 글로 @p outListJSON 에 담습니다(비운 뒤 채운다). */
        virtual void collectEventJSON( vector<string>& outListJSON ) const = 0;
        /** @brief 끝난 바꾸기를 @p outListCompletion 뒤에 붙입니다. */
        virtual void drainControlCompletions( vector<LiveEventControlCompletion>& outListCompletion ) = 0;
    };
} // namespace sw
