/**
 * @file Engine/Audio/Dev/AudioDumpListener.cpp
 * @brief `-gv_audioDumpPath=<파일>.wav` — 장치 없는 오프라인 렌더를 스트림이 끝날 때(서비스를 내릴 때 · 레이아웃을 바꿀 때) WAV(채널 마스크 포함, 최대 60 초)로 남기는 개발 리스너입니다.
 * @details 개발 전용 소스다(Dev/ — 배포 구성이 짓지 않는다). 오디오 서비스는 이 파일을 모르고 렌더한 블록을 리스너 등록(`SW_AUDIO_RENDER_LISTENER`)에 넘길 뿐이다.
 *          5.1 · 7.1 의 결정적 오프라인 렌더를 파일로 남겨 분석할 때 쓴다(언리얼 `ISubmixBufferListener` 로 마스터 출력을 녹음하는 자리).
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/File/FileUtil.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Log/Logger.h"

#include "Engine/Audio/AudioChannelLayout.h"
#include "Engine/Audio/AudioPcm.h"
#include "Engine/Audio/AudioRenderListener.h"
#include "Engine/Audio/AudioTypes.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Module/ModuleSubsystemRegistrar.h"

namespace sw
{
    /** @brief `-gv_audioDumpPath=<파일>.wav`: 장치 없는 오프라인 렌더를 스트림이 끝날 때 이 WAV(채널 마스크 포함, 최대 60 초)로 남깁니다. 비면 쌓지 않습니다. */
    SW_TEST_GLOBAL_VARIABLE( sw::GlobalDiskPath, gv_audioDumpPath, "", "오프라인 오디오 렌더를 내릴 때 이 WAV 로 남김(비면 끄기)" );

    /**
     * @struct AudioDumpBuffer
     * @brief 쌓은 오프라인 렌더(채널 교차)입니다. 엔진 이미지의 모듈 부분이라 엔진 범위와 함께 생기고 사라진다 — 리스너(정적 등록 항목)는 칸을 들지 않는다.
     */
    struct AudioDumpBuffer
    {
        static constexpr const utf8* kModulePartName = "AudioDumpBuffer";

        vector<float32> _listSample;
    };

    SW_MODULE_PART( Engine, AudioDumpBuffer, AudioDumpBuffer );

    namespace
    {
        /**
         * @class AudioDumpListener
         * @brief 경로가 있으면 블록을 쌓고(상한까지), 스트림이 끝나면 WAV 로 씁니다. 게임 스레드에서만 불린다(오프라인 렌더). 쌓는 곳은 엔진 범위의 `AudioDumpBuffer` 다.
         */
        class AudioDumpListener final : public IAudioRenderListener
        {
        public:
            /** @brief 쌓는 최대 길이(초)입니다. 7.1 이면 약 110 MB 이며, 넘는 부분은 쌓지 않습니다. */
            static constexpr uint32 kMaxDumpSeconds = 60;

            void onRenderedBlock( const float32* pSample, uint32 frameCount, AudioChannelLayout layout ) override
            {
                if ( gv_audioDumpPath.empty() )
                    return;
                AudioDumpBuffer* const pBuffer = engine::findSubsystem<AudioDumpBuffer>();
                if ( pBuffer == nullptr )
                    return; // 엔진 범위가 없다(엔진 없는 오디오 테스트) — 쌓을 곳이 없다
                vector<float32>& listSample   = pBuffer->_listSample;
                const size_t     channelCount = AudioChannelLayoutUtil::getChannelCount( layout );
                const size_t     dumpLimit    = static_cast<size_t>( kMaxDumpSeconds ) * audio::kSampleRate * channelCount;
                if ( listSample.size() >= dumpLimit )
                    return;
                listSample.insert( listSample.end(), pSample, pSample + static_cast<size_t>( frameCount ) * channelCount );
            }

            void onStreamEnded( AudioChannelLayout layout ) override
            {
                AudioDumpBuffer* const pBuffer = engine::findSubsystem<AudioDumpBuffer>();
                if ( pBuffer == nullptr || pBuffer->_listSample.empty() )
                    return;
                vector<float32>& listSample   = pBuffer->_listSample;
                const string     path         = gv_audioDumpPath;
                const uint32     channelCount = AudioChannelLayoutUtil::getChannelCount( layout );
                vector<uint8>    bytes;
                AudioWAVEncoder::encodeFloat32( listSample.data(), static_cast<uint32>( listSample.size() / channelCount ), channelCount,
                                                AudioChannelLayoutUtil::getChannelMask( layout ), audio::kSampleRate, bytes );
                listSample.clear();
                if ( FileUtil::writeFile( path, bytes.data(), bytes.size() ) == false )
                {
                    SW_LOG_ERROR_ID( "AudioService.CouldNotWriteAudio", "Could not write the audio dump '%#'", path );
                    return;
                }
                SW_LOG_INFO_ID( "AudioService.AudioDumpWrittenChannels", "Audio dump written: %# (%# channels)", path, channelCount );
            }
        };

        AudioDumpListener s_audioDumpListener;
    } // namespace

    SW_AUDIO_RENDER_LISTENER( AudioDump, "AudioDump", &s_audioDumpListener );
} // namespace sw
