/**
 * @file ServicePendingTable.h
 * @brief 응답 대기 테이블입니다. 서비스 로직에 넘긴 요청의 티켓과 `NetRequestToken` 을 짝짓습니다. 바인딩(`IOnlineServerService`)이 요청을 받을 때 `add` 해 티켓으로 로직에 넘기고,
 *        로직의 완료를 거둘 때 `take` 해 `host.respondOk` · `respondError` 합니다.
 * @details - 서비스 스레드 하나(호스트 `tick`)에서 쓴다. 티켓은 1 부터 오르는 수다(0 은 쓰지 않으므로 로직이 "티켓 없음" 으로 쓸 수 있다).
 *          - 연결이 닫혀도 지우지 않는다. 완료가 오면 꺼내 답하고, 닫힌 연결의 답은 호스트가 버린다(`respond*` 가 false).
 */
#pragma once
#include "Core/Container/unordered_map.h"
#include "Core/Foundation/Types.h"
#include "Core/Network/Message/NetRequest.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class ServicePendingTable
     * @brief 꼬리표 ↔ 요청 표입니다.
     */
    class SW_GF_API ServicePendingTable
    {
    public:
        ServicePendingTable();

        /** @brief @p token 을 넣고 새 꼬리표를 돌려줍니다. */
        uint64 add( const NetRequestToken& token );
        /** @brief 꼬리표의 요청을 꺼냅니다(한 번만). 없으면 false. */
        [[nodiscard]] bool take( uint64 requestTag, NetRequestToken& outToken );
        /** @brief 모두 지웁니다. 바인딩을 해제할 때 쓰며 답하지 않습니다. */
        void clear() { _mapTagToToken.clear(); }

        int32 getCount() const { return static_cast<int32>( _mapTagToToken.size() ); }

    private:
        unordered_map<uint64, NetRequestToken> _mapTagToToken;
        uint64                                 _nextTag;
    };
} // namespace sw
