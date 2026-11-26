/**
 * @file OfflineAudioEngine.h
 * @brief 장치 없이 테스트 스레드가 렌더하는 오디오 엔진 픽스처입니다 — 가짜 클립(`AudioClipCache::addClip`)과 이벤트 하나짜리 라이브러리를 올려 둡니다.
 * @details 오디오 서비스(장치 백엔드)를 거치지 않고 엔진을 직접 세운다. 엔진은 `render` 를 부르는 스레드를 오디오 스레드로 받으므로 테스트 스레드가 블록을 돌린다
 *          (언리얼 오디오 믹서의 Null 장치 · 유니티 `AudioSettings` 의 Disabled 출력처럼 렌더만 하고 소리를 내지 않는다). 씬 오디오에 붙일 때는 `SceneAudio::setAudioEngine`.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/String/Name/hashed_string.h"
#include "Core/TestFramework/TestFramework.h"

#include "Engine/Audio/AudioChannelLayout.h"
#include "Engine/Audio/AudioClip.h"
#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioEvent.h"
#include "Engine/Audio/AudioMixerDef.h"
#include "Engine/Audio/AudioTypes.h"

namespace sw::test
{
    /**
     * @class OfflineAudioEngine
     * @brief 기본 믹서 · 가짜 클립 · 반복 이벤트(`kLoopEvent`, 2D, 페이드 없음)를 올린 오프라인 엔진입니다.
     */
    class OfflineAudioEngine
    {
    public:
        static constexpr const utf8* kLoopEvent = "test.loop";
        static constexpr const utf8* kClipPath  = "test/loop.wav";

        OfflineAudioEngine()
            : _engine{}
            , _listOutput( static_cast<size_t>( audio::kBlockFrameCount ) * 2, 0.0f )
        {
            SW_REQUIRE_TRUE( _engine.initialize( AudioMixerDef::makeDefault(), AudioChannelLayout::Stereo ) );
            shared_ptr<AudioClipData> pClip = make_shared<AudioClipData>();
            pClip->_listSample.assign( audio::kSampleRate, 0.25f ); // 1 초 모노
            pClip->_frameCount   = audio::kSampleRate;
            pClip->_sampleRate   = audio::kSampleRate;
            pClip->_channelCount = 1;
            _engine.getClipStore().addClip( hashed_string( kClipPath ), std::move( pClip ) );

            AudioClipEntry clip;
            clip._path = kClipPath;
            AudioEventDef event;
            event._name           = hashed_string( kLoopEvent );
            event._bus            = hashed_string( AudioBusNames::kSFX );
            event._bLoop          = SW_TRUE;
            event._fadeOutSeconds = 0.0f;
            event._listClip.push_back( clip );
            AudioEventLibrary library;
            library._listEvent.push_back( event );
            SW_REQUIRE_TRUE( _engine.loadEventLibrary( hashed_string( "test.audioevents.xml" ), library ) );
        }
        ~OfflineAudioEngine() { _engine.shutdown(); }
        OfflineAudioEngine( const OfflineAudioEngine& )            = delete;
        OfflineAudioEngine& operator=( const OfflineAudioEngine& ) = delete;

        AudioEngine& getEngine() { return _engine; }

        /** @brief 블록 @p blockCount 개를 렌더합니다 — 쌓인 명령(재생 · 멈춤)이 블록 시작에 적용되고 상태가 게시된다. */
        void renderBlocks( uint32 blockCount )
        {
            for ( uint32 blockIndex = 0; blockIndex < blockCount; ++blockIndex )
                _engine.render( _listOutput.data(), audio::kBlockFrameCount );
        }

        /** @brief 마지막 블록에 살아 있던 이벤트 인스턴스 수입니다. */
        uint32 getInstanceCount() const { return _engine.getStats()._instanceCount; }

    private:
        AudioEngine     _engine;
        vector<float32> _listOutput; ///< 스테레오 한 블록(버린다)
    };
} // namespace sw::test
