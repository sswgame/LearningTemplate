#include "pch.h"

#include "Editor/Common/Commands/EditorBackgroundJob.h"

#include "TestFramework/TestFramework.h"

using namespace sw;
using namespace sw::editor;

namespace
{
    /** @brief 테스트용 입력 — 워커가 실제로 받아 쓰는지 확인할 값 하나. */
    struct TestJobInput
    {
        int32 _seed{ 0 };
    };

    /**
     * @class TestBackgroundJob
     * @brief 워커를 흉내 내어 규약(세대·완료 플래그)만 동기적으로 검증하는 잡.
     * @details 실제 잡은 TaskManager 워커에서 runJob 을 돌리지만, 검증 대상은 스케줄링이 아니라
     *          잠금·세대 처리이므로 여기서는 워커 단계를 손으로 부른다.
     */
    class TestBackgroundJob final : public EditorBackgroundJob<TestJobInput, vector<int32>>
    {
    public:
        /** @brief 게임 스레드: 요청을 올리고 워커에 넘길 세대를 돌려줍니다. */
        uint32 request( int32 seed )
        {
            TestJobInput input;
            input._seed = seed;
            return beginRequest( input );
        }

        /** @brief 워커: 이 세대의 입력을 읽습니다. 낡은 세대면 false. */
        bool workerReadInput( uint32 generation, TestJobInput& outInput ) const
        {
            return readInput( _pState, generation, outInput );
        }

        /** @brief 워커: 이 세대의 결과를 싣습니다. */
        void workerPublish( uint32 generation, vector<int32> listResult )
        {
            publish( _pState, generation, std::move( listResult ) );
        }
    };
} // namespace

/**
 * @brief [EditorBackgroundJobTest] 요청 → 워커 발행 → 수거의 정상 흐름과 플래그 전이 검증
 */
SW_TEST_CASE( EditorBackgroundJobTest, RequestPublishTakeRoundTrip )
{
    TestBackgroundJob job;

    vector<int32> taken;
    SW_EXPECT_FALSE( job.isPending() );
    SW_EXPECT_FALSE( job.take( taken ) );

    const uint32 generation = job.request( 7 );
    SW_EXPECT_TRUE( job.isPending() );
    // 아직 워커가 싣지 않았으므로 가져갈 것이 없다.
    SW_EXPECT_FALSE( job.take( taken ) );

    TestJobInput input;
    SW_ASSERT_TRUE( job.workerReadInput( generation, input ) );
    SW_EXPECT_EQUAL( 7, input._seed );

    job.workerPublish( generation, vector<int32>{ input._seed, input._seed * 2 } );
    SW_EXPECT_FALSE( job.isPending() );

    SW_ASSERT_TRUE( job.take( taken ) );
    SW_ASSERT_EQUAL( size_t( 2 ), taken.size() );
    SW_EXPECT_EQUAL( 7, taken[0] );
    SW_EXPECT_EQUAL( 14, taken[1] );

    // 같은 결과를 두 번 주지 않는다.
    SW_EXPECT_FALSE( job.take( taken ) );
}

/**
 * @brief [EditorBackgroundJobTest] 재요청이 낡은 세대의 워커 결과를 버리는지 검증
 * @details 폴더를 빠르게 옮겨 다니면 이전 스캔이 나중에 끝나 도착한다. 세대가 이것을 막지 못하면
 *          Content Browser 가 방금 연 폴더 대신 이전 폴더의 목록을 보여준다.
 */
SW_TEST_CASE( EditorBackgroundJobTest, StaleGenerationResultIsDropped )
{
    TestBackgroundJob job;

    const uint32 staleGeneration = job.request( 1 );
    const uint32 freshGeneration = job.request( 2 );
    SW_EXPECT_TRUE( staleGeneration != freshGeneration );

    // 낡은 워커는 입력조차 읽지 못한다.
    TestJobInput staleInput;
    SW_EXPECT_FALSE( job.workerReadInput( staleGeneration, staleInput ) );

    // 늦게 도착한 낡은 결과는 버려지고, 요청은 여전히 대기 상태로 남는다.
    job.workerPublish( staleGeneration, vector<int32>{ 111 } );
    vector<int32> taken;
    SW_EXPECT_FALSE( job.take( taken ) );
    SW_EXPECT_TRUE( job.isPending() );

    // 새 세대의 결과만 통과한다.
    TestJobInput freshInput;
    SW_ASSERT_TRUE( job.workerReadInput( freshGeneration, freshInput ) );
    SW_EXPECT_EQUAL( 2, freshInput._seed );

    job.workerPublish( freshGeneration, vector<int32>{ 222 } );
    SW_ASSERT_TRUE( job.take( taken ) );
    SW_ASSERT_EQUAL( size_t( 1 ), taken.size() );
    SW_EXPECT_EQUAL( 222, taken[0] );
}

/**
 * @brief [EditorBackgroundJobTest] 재요청이 이전 결과를 지워 낡은 값이 새 요청에 섞이지 않는지 검증
 */
SW_TEST_CASE( EditorBackgroundJobTest, RequestClearsPreviousResult )
{
    TestBackgroundJob job;

    const uint32 firstGeneration = job.request( 1 );
    job.workerPublish( firstGeneration, vector<int32>{ 42 } );

    // 가져가지 않은 채로 다시 요청하면 이전 결과는 사라진다.
    const uint32  secondGeneration = job.request( 2 );
    vector<int32> taken;
    SW_EXPECT_FALSE( job.take( taken ) );
    SW_EXPECT_TRUE( job.isPending() );

    job.workerPublish( secondGeneration, vector<int32>{ 43 } );
    SW_ASSERT_TRUE( job.take( taken ) );
    SW_ASSERT_EQUAL( size_t( 1 ), taken.size() );
    SW_EXPECT_EQUAL( 43, taken[0] );
}
