/**
 * @file NetPrioritizer.h
 * @brief 관찰자(연결) 하나가 보는 엔티티마다의 누적 우선도입니다 — 언리얼 `NetPriority` × 지난 시간 · Iris prioritizer, 유니티 Netcode 의 고스트 중요도 × 나이.
 * @details 보낼 차례가 오기 전까지 틱마다 `우선도 × 시간` 을 쌓고, 받는 쪽이 이미 최신이면 0 으로 돌린다. 비신뢰로 실었으면 0 으로 돌리되 그 몫을
 *          "확인 기다리는 보냄" 으로 기억하고, 받는 쪽이 받지 못했다고 판정되면(`resolveSend`) 되돌린다 — 잃은 상태가 한 차례를 통째로 다시 기다리지 않는다
 *          (언리얼 NAK 재-dirty 자리). 그래서 예산이 늘 차도 낮은 우선도는 쌓여서 언젠가 높은 것을 넘는다(굶지 않는다 — 우선도 1 은 우선도 10 둘이 예산을 채워도 10 틱쯤에 한 번 간다).
 *          순서는 쌓인 것이 큰 것부터, 같으면 id 오름차순이다(결정적 — 작업 스레드 수와 상관없이 같은 바이트).
 * @code
 *     prioritizer.beginAccumulate();
 *     for ( each relevant entity ) prioritizer.accumulate( id, policy.computePriority( ... ), deltaTime );
 *     prioritizer.removeUntouched();                       // 더는 관련 없는 엔티티를 잊는다
 *     prioritizer.collectOrder( listOrder );               // 큰 것부터
 *     for ( id in listOrder ) if ( fits ) { write( id ); prioritizer.markSentUnconfirmed( id, tick ); }   // 확인이 오면 resolveSend
 * @endcode
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/vector.h"

namespace sw
{
    /** @class NetPrioritizer @brief 엔티티마다 누적 우선도 — 파일 머리말 참고. 스레드 안전하지 않다(관찰자마다 하나, 한 번에 한 스레드). */
    class SW_API NetPrioritizer
    {
    public:
        NetPrioritizer();

        /** @brief 이번 틱의 쌓기를 시작합니다 — 그 뒤 `accumulate` 되지 않은 엔티티를 `removeUntouched` 가 지운다. */
        void beginAccumulate();
        /** @brief @p entityID 에 `priority × deltaTime` 을 쌓습니다(처음이면 0 에서). */
        void accumulate( uint32 entityID, float32 priority, float32 deltaTime );
        /** @brief 마지막 `beginAccumulate` 뒤로 쌓이지 않은 엔티티를 지웁니다. */
        void removeUntouched();
        /** @brief 쌓인 것이 큰 순서(같으면 id 오름차순)로 엔티티 id 를 채웁니다. */
        void collectOrder( vector<uint32>& outListEntity );
        /** @brief 받는 쪽이 이미 지금 상태다 — 쌓인 것과 확인 기다리는 몫을 모두 0 으로 돌립니다. 없는 엔티티는 무시한다. */
        void markSent( uint32 entityID );
        /**
         * @brief 비신뢰로 @p sentTick 에 실었다 — 쌓인 것을 0 으로 돌리되 그 몫을 확인 기다리는 보냄으로 기억합니다. 없는 엔티티는 무시한다.
         * @details 확인 전에 또 실으면 몫을 더하고 틱을 새 것으로 바꾼다(새 보냄이 앞 것을 대신한다).
         */
        void markSentUnconfirmed( uint32 entityID, uint32 sentTick );
        /** @brief 확인 기다리는 보냄이 있으면 그 틱을 @p outSentTick 에 줍니다. */
        [[nodiscard]] bool findUnconfirmedSendTick( uint32 entityID, uint32& outSentTick ) const;
        /** @brief 확인 기다리는 보냄을 판정합니다 — 받았으면(@p bDelivered) 기억만 지우고, 잃었으면 기억한 몫을 쌓인 것에 되돌립니다. */
        void resolveSend( uint32 entityID, bool bDelivered );
        void remove( uint32 entityID );
        void clear();

        /** @brief 쌓인 우선도입니다. 없는 엔티티는 0 입니다. */
        float32 getAccumulated( uint32 entityID ) const;
        int32   getCount() const { return static_cast<int32>( _listEntry.size() ); }

    private:
        struct Entry
        {
            float32 _accumulated{ 0.0f };
            float32 _unconfirmedAccumulated{ 0.0f }; ///< 확인 기다리는 보냄이 0 으로 돌린 몫 — 잃으면 되돌린다
            uint32  _entityID{ 0 };
            uint32  _unconfirmedTick{ 0 }; ///< 그 보냄의 틱(`_bUnconfirmed` 일 때만 뜻이 있다)
            uint8   _bTouched{ SW_FALSE };
            uint8   _bUnconfirmed{ SW_FALSE };
        };

        /** @brief @p entityID 이상인 첫 자리입니다(id 오름차순 이분 탐색). */
        size_t findLowerIndex( uint32 entityID ) const;
        Entry* findEntry( uint32 entityID );

        vector<Entry> _listEntry;       ///< id 오름차순
        vector<Entry> _listRankScratch; ///< `collectOrder` 의 정렬 자리(틱마다 다시 쓴다)
    };
} // namespace sw
