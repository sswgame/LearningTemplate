/**
 * @file CacheRecordUpdater.h
 * @brief 캐시 기록 하나를 비교 후 쓰기로 바꾸는 반복 처리입니다. 읽기, 바꾸기(함수 객체), CompareAndSet 순서로 진행하며, 없던 기록은 IfAbsent Set 으로, 지우기는 CompareAndErase 로 처리합니다. 어긋나면 최대 4번 다시 시도합니다.
 *        서비스 스레드 하나에서 쓰며 델리게이트는 라우터 `pump` 안에서 불립니다.
 * @details - 바꾸기 객체(`ICacheRecordMutation`)는 순수해야 한다. 다시 시도할 때 새로 읽은 값으로 다시 호출되기 때문이다. 끝나면 `onFinished` 를 정확히 한 번 부른다. `shutdown` 으로 버린 요청은 제외한다.
 *          - 규칙 실패(`mutate` 가 Ok 가 아닌 경우)는 그때 읽은 값으로 판정한다. 쓰지 않으므로 그사이 다른 서버의 쓰기는 보지 못한다. 낙관적 방식이며 다시 요청하면 새 값으로 판정한다.
 *
 *          EOS 와 PlayFab 로비가 서비스 쪽 공유 상태를 버전(etag) 조건으로 바꾸는 것과 같은 구조이다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Shared/MatchmakingTypes.h"

namespace sw
{
    struct EphemeralReply;

    class EphemeralStoreRouter;

    /**
     * @class ICacheRecordMutation
     * @brief 기록 하나를 바꾸는 규칙입니다.
     */
    class ICacheRecordMutation
    {
    public:
        virtual ~ICacheRecordMutation() = default;
        /**
         * @brief 이전 값으로 새 값을 정합니다. Ok 가 아니면 쓰지 않고 끝냅니다.
         * @param bExists 기록이 있었는지 나타냅니다.
         * @param outbErase 참이면 기록을 지웁니다. 비교 후 지우기로 처리합니다.
         */
        virtual MatchmakingResult mutate( bool bExists, const vector<uint8>& oldBytes, vector<uint8>& outNewBytes, bool& outbErase ) = 0;
        /** @brief 끝났을 때 불립니다. 결과와 마지막에 쓴 값을 받으며, 지웠으면 빈 값이고 규칙이 실패했으면 읽은 값입니다. */
        virtual void onFinished( MatchmakingResult result, const vector<uint8>& finalBytes ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CacheRecordUpdater
     * @brief 비교 후 쓰기를 반복하는 클래스입니다.
     */
    class SW_KIT_SERVER_API CacheRecordUpdater
    {
    public:
        static constexpr int32 kMaxAttempt = 4;

        CacheRecordUpdater();
        ~CacheRecordUpdater();

        CacheRecordUpdater( const CacheRecordUpdater& )            = delete;
        CacheRecordUpdater& operator=( const CacheRecordUpdater& ) = delete;

        void initialize( EphemeralStoreRouter* pRouter );
        /** @brief 기다리던 캐시 요청을 취소하고 바꾸기 객체를 버립니다. 소유자가 종료되므로 `onFinished` 는 부르지 않습니다. */
        void shutdown();

        /** @brief @p key 의 기록을 @p mutation 으로 바꿉니다. 새로 쓰는 값의 만료 시간은 @p ttlMs 입니다. */
        void update( string_view key, int64 ttlMs, unique_ptr<ICacheRecordMutation> mutation );

        int32 getPendingCount() const { return static_cast<int32>( _mapRequestToOperation.size() ); }

    private:
        struct Operation
        {
            unique_ptr<ICacheRecordMutation> _mutation{};
            vector<uint8>                    _oldBytes{};
            vector<uint8>                    _newBytes{};
            string                           _key{};
            int64                            _ttlMs{ 0 };
            int32                            _attempt{ 0 };
            uint8                            _bExists{ SW_FALSE };
            uint8                            _bErase{ SW_FALSE };
        };

        void startRead( unique_ptr<Operation> operation );
        void onReadReply( const EphemeralReply& reply );
        void onWriteReply( const EphemeralReply& reply );

        unordered_map<uint64, unique_ptr<Operation>> _mapRequestToOperation;
        EphemeralStoreRouter*                        _pRouter;
    };
} // namespace sw
