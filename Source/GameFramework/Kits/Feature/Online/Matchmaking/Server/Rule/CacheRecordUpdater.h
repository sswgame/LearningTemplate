/**
 * @file CacheRecordUpdater.h
 * @brief 캐시 기록 하나의 비교 후 쓰기 고리 — 읽기 → 바꾸기(함수 객체) → CompareAndSet(없던 기록은 IfAbsent Set, 지우기는 CompareAndErase) → 어긋나면 다시(4 번).
 *        서비스 스레드 하나에서 씁니다(델리게이트는 라우터 `pump` 안).
 * @details - 바꾸기 객체(`ICacheRecordMutation`)는 순수해야 한다 — 다시 할 때 새로 읽은 값으로 다시 불린다. 끝나면 `onFinished` 가 정확히 한 번(`shutdown` 으로 버린 것은 빼고).
 *          - 규칙 실패(Ok 가 아닌 `mutate`)는 그 읽기로 판정한다 — 쓰지 않으므로 그사이 다른 서버의 쓰기는 보지 못한다(낙관적 — 다시 요청하면 새 값으로 판정).
 *          EOS · PlayFab 로비가 서비스 쪽 공유 상태를 판(etag) 조건으로 바꾸는 것과 같은 모양이다.
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
         * @brief 옛 값으로 새 값을 정합니다. Ok 가 아니면 쓰지 않고 끝낸다.
         * @param bExists 기록이 있었나
         * @param outbErase 참이면 지운다(비교 후 지우기)
         */
        virtual MatchmakingResult mutate( bool bExists, const vector<uint8>& oldBytes, vector<uint8>& outNewBytes, bool& outbErase ) = 0;
        /** @brief 끝 — 결과와 마지막에 쓴 값(지웠으면 빈 값, 규칙 실패면 읽은 값)입니다. */
        virtual void onFinished( MatchmakingResult result, const vector<uint8>& finalBytes ) = 0;
    };
} // namespace sw

namespace sw
{
    /**
     * @class CacheRecordUpdater
     * @brief 비교 후 쓰기 고리입니다.
     */
    class SW_GF_API CacheRecordUpdater
    {
    public:
        static constexpr int32 kMaxAttempt = 4;

        CacheRecordUpdater();
        ~CacheRecordUpdater();

        CacheRecordUpdater( const CacheRecordUpdater& )            = delete;
        CacheRecordUpdater& operator=( const CacheRecordUpdater& ) = delete;

        void initialize( EphemeralStoreRouter* pRouter );
        /** @brief 기다리던 캐시 요청을 취소하고 바꾸기 객체를 버립니다(`onFinished` 없음 — 주인이 내려간다). */
        void shutdown();

        /** @brief @p key 의 기록을 @p mutation 으로 바꿉니다. 새로 쓰는 값의 만료는 @p ttlMs 입니다. */
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
