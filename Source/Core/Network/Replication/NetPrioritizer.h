/**
 * @file NetPrioritizer.h
 * @brief 관찰자(연결) 하나가 보는 엔티티마다 우선도를 누적합니다. 언리얼의 `NetPriority` x 경과 시간, Iris prioritizer, 유니티 Netcode 의 고스트 중요도 x 나이에 해당합니다.
 * @details 보낼 차례가 오기 전까지 틱마다 `우선도 x 시간` 을 쌓고, 받는 쪽이 이미 최신이면 0으로 되돌린다.
 *          비신뢰로 실었으면 0으로 되돌리되 그 값을 "확인을 기다리는 전송"으로 기억해 둔다.
 *          받는 쪽이 받지 못했다고 판정되면(`resolveSend`) 기억해 둔 값을 되돌리므로, 잃은 상태가 한 차례를 통째로 다시 기다리지 않는다(언리얼의 NAK 후 re-dirty 에 해당한다).
 *          그래서 예산이 항상 차 있어도 낮은 우선도는 쌓여서 언젠가 높은 우선도를 넘는다(굶지 않는다). 우선도 1은 우선도 10 둘이 예산을 채워도 10틱쯤에 한 번 나간다.
 *          순서는 쌓인 값이 큰 것부터이고, 같으면 ID 오름차순이다. 결정적이므로 작업 스레드 수와 상관없이 같은 바이트가 나온다.
 * @code
 *     prioritizer.beginAccumulate();
 *     for ( each relevant entity ) prioritizer.accumulate( id, policy.computePriority( ... ), deltaTime );
 *     prioritizer.removeUntouched();                       // 더는 관련 없는 엔티티를 잊는다
 *     prioritizer.collectOrder( listOrder );               // 큰 것부터
 *     for ( id in listOrder ) if ( fits ) { write( id ); prioritizer.markSentUnconfirmed( id, tick ); }   // 확인이 오면 resolveSend
 * @endcode
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"

namespace sw
{
    /** @class NetPrioritizer @brief 엔티티마다 우선도를 누적합니다. 자세한 규칙은 파일 머리말을 참고하세요. 스레드 안전하지 않으며 관찰자마다 하나를 두고 한 번에 한 스레드만 씁니다. */
    class SW_API NetPrioritizer
    {
    public:
        NetPrioritizer();

        /** @brief 이번 틱의 누적을 시작합니다. 그 뒤 `accumulate` 되지 않은 엔티티는 `removeUntouched` 가 지웁니다. */
        void beginAccumulate();
        /** @brief @p entityID 에 `priority x deltaTime` 을 쌓습니다(처음이면 0에서 시작합니다). */
        void accumulate( uint32 entityID, float32 priority, float32 deltaTime );
        /** @brief 마지막 `beginAccumulate` 이후 쌓이지 않은 엔티티를 지웁니다. */
        void removeUntouched();
        /** @brief 쌓인 값이 큰 순서(같으면 ID 오름차순)로 엔티티 ID 를 채웁니다. */
        void collectOrder( vector<uint32>& outListEntity );
        /** @brief 받는 쪽이 이미 현재 상태를 가지고 있음을 알립니다. 쌓인 값과 확인 대기 값을 모두 0으로 되돌리며, 없는 엔티티는 무시합니다. */
        void markSent( uint32 entityID );
        /**
         * @brief 비신뢰로 @p sentTick 에 실었음을 알립니다. 쌓인 값을 0으로 되돌리되 그 값을 확인 대기 전송으로 기억하며, 없는 엔티티는 무시합니다.
         * @details 확인 전에 또 실으면 값을 더하고 틱을 새 것으로 바꾼다(새 전송이 앞 전송을 대신한다).
         */
        void markSentUnconfirmed( uint32 entityID, uint32 sentTick );
        /** @brief 확인을 기다리는 전송이 있으면 그 틱을 @p outSentTick 에 채웁니다. */
        [[nodiscard]] bool findUnconfirmedSendTick( uint32 entityID, uint32& outSentTick ) const;
        /** @brief 확인을 기다리는 전송의 결과를 판정합니다. 받았으면(@p bDelivered) 기억만 지우고, 잃었으면 기억한 값을 쌓인 값에 되돌립니다. */
        void resolveSend( uint32 entityID, bool bDelivered );
        void remove( uint32 entityID );
        void clear();

        /** @brief 쌓인 우선도를 반환합니다. 없는 엔티티는 0입니다. */
        float32 getAccumulated( uint32 entityID ) const;
        int32   getCount() const { return static_cast<int32>( _listEntry.size() ); }

    private:
        struct Entry
        {
            float32 _accumulated{ 0.0f };
            float32 _unconfirmedAccumulated{ 0.0f }; ///< 확인을 기다리는 전송이 0으로 되돌린 값. 잃으면 되돌린다.
            uint32  _entityID{ 0 };
            uint32  _unconfirmedTick{ 0 }; ///< 그 전송의 틱(`_bUnconfirmed` 일 때만 의미가 있다).
            uint8   _bTouched{ SW_FALSE };
            uint8   _bUnconfirmed{ SW_FALSE };
        };

        /** @brief @p entityID 이상인 첫 위치를 반환합니다(ID 오름차순 이분 탐색). */
        size_t findLowerIndex( uint32 entityID ) const;
        Entry* findEntry( uint32 entityID );

        vector<Entry> _listEntry;       ///< ID 오름차순.
        vector<Entry> _listRankScratch; ///< `collectOrder` 의 정렬용 스크래치 버퍼(틱마다 다시 쓴다).
    };
} // namespace sw
