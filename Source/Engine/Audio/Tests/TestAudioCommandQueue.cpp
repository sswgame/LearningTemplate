// AudioCommandQueueTest — 명령 큐의 생산자는 게임 스레드 하나다: 워커 요청은 다음 프레임(`flushCommands`)에 들어가고, 오디오 스레드가 적용한 명령의 `shared_ptr` 는 게임 스레드가 놓는다(nogpu).
#include "pch.h"

#include "Core/Concurrency/thread.h"
#include "Core/TestFramework/TestFramework.h"
#include "Core/TestFramework/ThreadTestAccess.h"

#include "Engine/Audio/AudioMusic.h"
#include "Engine/Audio/Tests/OfflineAudioEngine.h"

namespace
{
    struct AudioCommandQueueTestUtil
    {
        /** @brief 워커 스레드 본문 — 이벤트 하나를 내고 재생 id 를 적는다(병렬 틱이 워커에서 소리를 내는 모양). */
        static void postFromWorker( sw::AudioEngine* pEngine, sw::AudioPlayingID* pOutPlayingID )
        {
            *pOutPlayingID = pEngine->postEvent( sw::hashed_string( sw::test::OfflineAudioEngine::kLoopEvent ) );
        }

        /** @brief 픽스처의 클립 한 레이어 · 한 구간짜리 음악입니다. */
        static sw::shared_ptr<const sw::AudioMusicDef> makeOneLayerMusic()
        {
            sw::AudioMusicLayerDef layer;
            layer._path = sw::test::OfflineAudioEngine::kClipPath;
            sw::AudioMusicSegmentDef segment;
            segment._name = sw::hashed_string( "A" );
            segment._listLayer.push_back( layer );
            sw::shared_ptr<sw::AudioMusicDef> pMusic = sw::make_shared<sw::AudioMusicDef>();
            pMusic->_bus                             = sw::hashed_string( sw::AudioBusNames::kMusic );
            pMusic->_listSegment.push_back( segment );
            return pMusic;
        }
    };
} // namespace

/**
 * @brief [AudioCommandQueueTest] 워커가 낸 요청은 게임 스레드가 프레임에 넣기 전까지 큐에 없다 — 그동안도 그 id 는 살아 있는 것으로 읽힌다(큐 순서 = id 순서)
 * @details 변이: 워커 요청을 바로 SPSC 에 넣으면(`isOnGameThread` 를 빼면) 첫 블록에 인스턴스가 생겨 첫 단언이 진다 — 생산자가 둘인 데이터 경합이다.
 */
SW_TEST_CASE( AudioCommandQueueTest, WorkerRequestEntersAtFlush )
{
    using Util = AudioCommandQueueTestUtil;
    sw::test::OfflineAudioEngine offline;
    sw::AudioEngine&             engine    = offline.getEngine();
    sw::AudioPlayingID           playingID = 0;
    {
        sw::thread worker( sw::ThreadTestAccess::getKey(), &Util::postFromWorker, &engine, &playingID );
        worker.join();
    }
    SW_REQUIRE_TRUE( playingID != 0 );

    offline.renderBlocks( 1 );
    SW_EXPECT_EQUAL( 0u, offline.getInstanceCount() );
    SW_EXPECT_TRUE( engine.isPlaying( playingID ) ); // 아직 적용되지 않은 id

    engine.flushCommands();
    offline.renderBlocks( 1 );
    SW_EXPECT_EQUAL( 1u, offline.getInstanceCount() );
    SW_EXPECT_TRUE( engine.isPlaying( playingID ) );
}

/**
 * @brief [AudioCommandQueueTest] 오디오 스레드가 적용한 명령의 `shared_ptr` 는 반납 큐에 남았다가 게임 스레드의 `flushCommands` 에서 놓인다
 * @details 시작한 음악의 참조 수: 테스트 하나 + 플레이어 하나 + 반납 큐 하나 → `flushCommands` 뒤 둘. 변이: 적용 뒤 명령을 그대로 버리면(반납하지 않으면) 렌더 직후 둘이라 첫 단언이 진다.
 */
SW_TEST_CASE( AudioCommandQueueTest, AppliedCommandObjectsAreReleasedOnTheGameThread )
{
    using Util = AudioCommandQueueTestUtil;
    sw::test::OfflineAudioEngine                  offline;
    sw::AudioEngine&                              engine = offline.getEngine();
    const sw::shared_ptr<const sw::AudioMusicDef> pMusic = Util::makeOneLayerMusic();
    SW_REQUIRE_TRUE( engine.startMusic( pMusic, 0.0f ) != 0 );

    offline.renderBlocks( 1 );
    SW_EXPECT_EQUAL( 3, static_cast<int32>( pMusic.use_count() ) );
    engine.flushCommands();
    SW_EXPECT_EQUAL( 2, static_cast<int32>( pMusic.use_count() ) );
    SW_EXPECT_EQUAL( 0u, static_cast<uint32>( engine.getRetireOverflowCount().get() ) );
}
