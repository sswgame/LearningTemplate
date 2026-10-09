// 캐시 기록 비교 후 쓰기 고리 — 읽기와 쓰기 사이에 다른 서버가 같은 키를 바꾸면 다시 읽어 새 값으로 바꾸고, 계속 끼어들면 네 번 뒤 Conflict, 규칙 실패는 쓰지 않음.
#include "pch.h"

#include "GameFramework/Base/Online/Cache/EphemeralStoreRouter.h"
#include "GameFramework/Base/Online/Cache/MemoryEphemeralStore.h"
#include "GameFramework/Kits/Feature/Online/Matchmaking/Server/Rule/CacheRecordUpdater.h"

#include "TestFramework/TestFramework.h"

using namespace sw;

namespace
{
    /** @brief 값 끝에 바이트 하나를 붙이는 바꾸기 — `_interferenceCount` 번까지는 mutate 안에서 다른 앞으로 같은 키를 먼저 바꾼다(다른 서버의 쓰기). */
    class AppendMutation final : public ICacheRecordMutation
    {
    public:
        AppendMutation( MemoryEphemeralStore* pOtherFront, int32 interferenceCount, uint8 appended, bool bReject )
            : _pOtherFront{ pOtherFront }
            , _pResult{ nullptr }
            , _pFinalBytes{ nullptr }
            , _pMutateCount{ nullptr }
            , _interferenceCount{ interferenceCount }
            , _appended{ appended }
            , _bReject{ bReject }
        {
        }

        void bindOutputs( MatchmakingResult* pResult, vector<uint8>* pFinalBytes, int32* pMutateCount )
        {
            _pResult      = pResult;
            _pFinalBytes  = pFinalBytes;
            _pMutateCount = pMutateCount;
        }

        MatchmakingResult mutate( bool bExists, const vector<uint8>& oldBytes, vector<uint8>& outNewBytes, bool& outbErase ) override
        {
            (void)bExists;
            (void)outbErase;
            ++*_pMutateCount;
            if ( _bReject )
                return MatchmakingResult::Invalid;
            if ( _interferenceCount > 0 )
            {
                --_interferenceCount;
                vector<uint8> listInterfering = oldBytes;
                listInterfering.push_back( 0xEE );
                (void)_pOtherFront->submit( EphemeralRequest::makeSet( "mm/test", listInterfering, 0 ) );
            }
            outNewBytes = oldBytes;
            outNewBytes.push_back( _appended );
            return MatchmakingResult::Ok;
        }

        void onFinished( MatchmakingResult result, const vector<uint8>& finalBytes ) override
        {
            *_pResult     = result;
            *_pFinalBytes = finalBytes;
        }

    private:
        MemoryEphemeralStore* _pOtherFront;
        MatchmakingResult*    _pResult;
        vector<uint8>*        _pFinalBytes;
        int32*                _pMutateCount;
        int32                 _interferenceCount;
        uint8                 _appended;
        bool                  _bReject;
    };

    struct UpdaterNode
    {
        MemoryEphemeralStore _cache;
        MemoryEphemeralStore _otherFront;
        EphemeralStoreRouter _router;
        CacheRecordUpdater   _updater;
        MatchmakingResult    _result{ MatchmakingResult::Count };
        vector<uint8>        _finalBytes{};
        int32                _mutateCount{ 0 };

        explicit UpdaterNode( MemoryEphemeralDatabase* pDatabase )
            : _cache{ pDatabase }
            , _otherFront{ pDatabase }
            , _router{}
            , _updater{}
        {
            _router.initialize( &_cache );
            _updater.initialize( &_router );
        }

        ~UpdaterNode()
        {
            _updater.shutdown();
            _router.shutdown();
        }

        void run( int32 interferenceCount, uint8 appended, bool bReject = false )
        {
            _result                             = MatchmakingResult::Count;
            _mutateCount                        = 0;
            unique_ptr<AppendMutation> mutation = make_unique<AppendMutation>( &_otherFront, interferenceCount, appended, bReject );
            mutation->bindOutputs( &_result, &_finalBytes, &_mutateCount );
            _updater.update( "mm/test", 60000, std::move( mutation ) );
            for ( int32 round = 0; round < 16; ++round )
            {
                (void)_router.pump();
            }
        }
    };
} // namespace

SW_TEST_CASE( CacheRecordUpdaterTest, RetriesAfterConcurrentWrite )
{
    MemoryEphemeralDatabase database;
    UpdaterNode             node( &database );
    node.run( 0, 0x01 ); // 없던 기록 — IfAbsent 로 만든다
    SW_ASSERT_TRUE( node._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( node._mutateCount, 1 );

    node.run( 1, 0x02 ); // 첫 읽기 뒤 다른 서버가 끼어듦 — 다시 읽어 그 값에 붙인다
    SW_ASSERT_TRUE( node._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( node._mutateCount, 2 );
    SW_ASSERT_EQUAL( node._finalBytes.size(), size_t( 3 ) );
    SW_EXPECT_EQUAL( node._finalBytes[0], uint8( 0x01 ) );
    SW_EXPECT_EQUAL( node._finalBytes[1], uint8( 0xEE ) ); // 끼어든 쓰기를 잃지 않았다
    SW_EXPECT_EQUAL( node._finalBytes[2], uint8( 0x02 ) );
    SW_EXPECT_EQUAL( node._updater.getPendingCount(), 0 );
}

SW_TEST_CASE( CacheRecordUpdaterTest, GivesUpAfterMaxAttemptAndRejectWritesNothing )
{
    MemoryEphemeralDatabase database;
    UpdaterNode             node( &database );
    node.run( 0, 0x01 );
    node.run( CacheRecordUpdater::kMaxAttempt, 0x02 ); // 매번 끼어듦
    SW_EXPECT_TRUE( node._result == MatchmakingResult::Conflict );
    SW_EXPECT_EQUAL( node._mutateCount, CacheRecordUpdater::kMaxAttempt );

    node.run( 0, 0x03, true ); // 규칙 실패 — 쓰지 않고 읽은 값을 돌려준다
    SW_EXPECT_TRUE( node._result == MatchmakingResult::Invalid );
    const vector<uint8> beforeBytes = node._finalBytes;
    node.run( 0, 0x04 );
    SW_ASSERT_TRUE( node._result == MatchmakingResult::Ok );
    SW_EXPECT_EQUAL( node._finalBytes.size(), beforeBytes.size() + 1 );
}
