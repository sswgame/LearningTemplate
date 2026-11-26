/**
 * @file AudioWatches.cpp
 * @brief 오디오의 자동화 감시 값(리버브 세기 · 에미터 거리 · 인스턴스 · 언더런 · 클립 캐시)입니다.
 * @details 기능 .cpp 의 등록 블록과 그 블록만 부르던 Internal 함수를 옮겼다 — 배포 엔진에 컴파일되지 않게 개발 도구가 짓는다. 본문은 기능 형식의 공개 조회만 읽는다.
 */
#include "pch.h"

#include "Core/Log/LogOnce.h"
#include "Core/Log/ScopedDeferredLog.h"
#include "Core/Math/MathUtil.h"
#include "Core/Subsystem/SubsystemScope.h"

#include "Engine/Audio/AudioEngine.h"
#include "Engine/Audio/AudioSubsystem.h"
#include "Engine/Audio/Component/AudioEmitterComponent.h"
#include "Engine/Audio/Component/AudioListenerComponent.h"
#include "Engine/Audio/Component/AudioReverbZoneComponent.h"
#include "Engine/Audio/Scene/PhysicsAudioOcclusionQuery.h"
#include "Engine/Audio/Scene/SceneAudio.h"
#include "Engine/Automation/Watch/AutomationWatch.h"
#include "Engine/Config/EngineConfig.h"
#include "Engine/Foundation/EngineParallel.h"
#include "Engine/Object/Component/CameraComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Subsystem/SceneEngineSubsystems.h"
#include "Engine/Object/GameObject/Subsystem/ScenePreviewPolicy.h"
#include "Engine/Object/GameObject/Subsystem/SceneSubsystemRegistry.h"
#include "Engine/Physics/IPhysicsScene.h"
#include "Engine/Physics/Scene/ScenePhysics.h"

// Engine/Audio/Scene/SceneAudio.cpp 에서 옮긴 개발 등록(배포 엔진에 컴파일되지 않게).
namespace sw
{
    namespace
    {
        struct SceneAudioWatchesInternal
        {
            /** @brief `Audio.InstanceCount` · `Audio.RenderedSeconds` 의 활동 수 — 오디오 스레드가 지금까지 렌더한 프레임 수입니다(게시가 멈추면 값도 멈춘다). */
            [[nodiscard]] static uint64 readAudioActivity( const GameObjectManager* pManager )
            {
                const AudioEngine* pEngine = pManager != nullptr ? pManager->getSubsystem<SceneAudio>().findAudioEngine() : nullptr;
                return pEngine != nullptr ? pEngine->getStats()._renderedFrameCount : 0u;
            }

            /** @brief `Audio.ClipCacheBytes` 의 활동 수 — 디코드해 캐시에 든 바이트 그 자체입니다(워커가 디코드를 넣을 때마다 바뀐다). */
            [[nodiscard]] static uint64 readClipCacheActivity( const GameObjectManager* pManager )
            {
                const AudioEngine* pEngine = pManager != nullptr ? pManager->getSubsystem<SceneAudio>().findAudioEngine() : nullptr;
                return pEngine != nullptr ? pEngine->getClipStore().getCachedBytes() : 0u;
            }

            /** @brief 감시 값 `Audio.ClipCacheBytes` 입니다. 클립 캐시에 든 디코드 바이트의 합입니다(예산은 `EngineConfig::_audio._clipCacheMegabytes`). */
            [[nodiscard]] static bool readClipCacheBytes( const GameObjectManager* pManager, float64& outValue )
            {
                const AudioEngine* pEngine = pManager != nullptr ? pManager->getSubsystem<SceneAudio>().findAudioEngine() : nullptr;
                if ( pEngine == nullptr )
                    return false;
                outValue = static_cast<float64>( pEngine->getClipStore().getCachedBytes() );
                return true;
            }

            /** @brief 감시 값 `Audio.EmitterDistance` 입니다. 대상 에미터가 지난 프레임에 엔진에 넣은 소리 자리와 리스너 사이 거리(m)입니다 — 영역 앰비언트는 안에 서면 0 입니다. */
            [[nodiscard]] static bool readEmitterDistance( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                const GameObject*            pTarget  = query.findTargetObject();
                const AudioEmitterComponent* pEmitter = pTarget != nullptr ? pTarget->getComponent<AudioEmitterComponent>() : nullptr;
                if ( pEmitter == nullptr || pEmitter->_lastAudioPosition.has_value() == false )
                {
                    outValue.setText( query.describeMissing( "audio emitter updated by the scene audio" ) );
                    return false;
                }
                const float3& listener = query._pManager->getSubsystem<SceneAudio>().getPrimaryListenerPosition();
                outValue.setNumber( static_cast<float64>( float3::getDistance( *pEmitter->_lastAudioPosition, listener ) ) );
                return true;
            }

            /** @brief 감시 값 `Audio.InstanceCount` 입니다. 오디오 엔진에 살아 있는 이벤트 인스턴스 수입니다. */
            [[nodiscard]] static bool readInstanceCount( const GameObjectManager* pManager, float64& outValue )
            {
                const AudioEngine* pEngine = pManager != nullptr ? pManager->getSubsystem<SceneAudio>().findAudioEngine() : nullptr;
                if ( pEngine == nullptr )
                    return false;
                outValue = static_cast<float64>( pEngine->getStats()._instanceCount );
                return true;
            }

            /** @brief 감시 값 `Audio.OutputUnderruns` 입니다. 지금 스트림이 연 뒤 장치가 알린 언더런 수입니다(출력이 닫혀 있으면 0). */
            [[nodiscard]] static bool readOutputUnderruns( const GameObjectManager* pManager, float64& outValue )
            {
                const AudioSubsystem* pService = pManager != nullptr ? pManager->getSubsystem<SceneAudio>().findAudioSubsystem() : nullptr;
                if ( pService == nullptr )
                    return false;
                outValue = static_cast<float64>( pService->getOutputUnderrunCount() );
                return true;
            }

            /** @brief 감시 값 `Audio.RenderedSeconds` 입니다. 오디오 스레드가 지금까지 렌더한 시간(초)입니다 — 멈춘 소리와 견줄 벽시계 쪽 시간이다. */
            [[nodiscard]] static bool readRenderedSeconds( const GameObjectManager* pManager, float64& outValue )
            {
                const AudioEngine* pEngine = pManager != nullptr ? pManager->getSubsystem<SceneAudio>().findAudioEngine() : nullptr;
                if ( pEngine == nullptr )
                    return false;
                outValue = static_cast<float64>( pEngine->getStats()._renderedFrameCount ) / static_cast<float64>( audio::kSampleRate );
                return true;
            }

            /** @brief 감시 값 `Audio.RetireOverflows` 입니다. 반납 큐가 차서 오디오 스레드가 직접 놓은 명령 수입니다. */
            [[nodiscard]] static bool readRetireOverflows( const GameObjectManager* pManager, float64& outValue )
            {
                const AudioSubsystem* pService = pManager != nullptr ? pManager->getSubsystem<SceneAudio>().findAudioSubsystem() : nullptr;
                if ( pService == nullptr )
                    return false;
                outValue = static_cast<float64>( pService->getEngine().getRetireOverflowCount().get() );
                return true;
            }

            /** @brief 감시 값 `Audio.ReverbIntensity` 입니다. 리버브 존들이 스냅샷 `key` 에 건 세기입니다. */
            [[nodiscard]] static bool readReverbIntensity( const AutomationWatchQuery& query, AutomationWatchValue& outValue )
            {
                if ( query._pManager == nullptr )
                    return false;
                outValue.setNumber( static_cast<float64>( query._pManager->getSubsystem<SceneAudio>().findZoneIntensity( hashed_string{ query._key } ) ) );
                return true;
            }
        };
    } // namespace

    SW_AUTOMATION_QUERY_WATCH( audioReverbIntensity, "Audio.ReverbIntensity", "Intensity the reverb zones put on the mix snapshot named key (0..1)", &SceneAudioWatchesInternal::readReverbIntensity, false, true );
    SW_AUTOMATION_QUERY_WATCH( audioEmitterDistance, "Audio.EmitterDistance", "Distance from the listener to where the target emitter sounds (m); 0 inside an ambient volume", &SceneAudioWatchesInternal::readEmitterDistance, true,
                               false );
    // 오디오 스레드가 명령을 옮겨야 값이 바뀐다 — 벽시계 시한으로 기다린다.
    SW_AUTOMATION_WALL_WATCH( audioInstanceCount, "Audio.InstanceCount", "Sound event instances alive in the audio engine", &SceneAudioWatchesInternal::readInstanceCount, &SceneAudioWatchesInternal::readAudioActivity );
    SW_AUTOMATION_WALL_WATCH( audioRenderedSeconds, "Audio.RenderedSeconds", "Seconds the audio engine rendered since it started, paused sounds included", &SceneAudioWatchesInternal::readRenderedSeconds, &SceneAudioWatchesInternal::readAudioActivity );
    // 디코드 결과는 워커가 넣는다 — 같은 까닭으로 벽시계 시한이다.
    // 장치 콜백의 할당 · 잠금은 감시 값이 아니라 단언이다(`AudioRenderThread::read` 의 실시간 구간).
    SW_AUTOMATION_WATCH( audioOutputUnderruns, "Audio.OutputUnderruns", "Underruns (xruns, glitches) the output device reported since the stream opened", &SceneAudioWatchesInternal::readOutputUnderruns );
    SW_AUTOMATION_WATCH( audioRetireOverflows, "Audio.RetireOverflows", "Commands whose shared objects the audio thread freed itself because the retire queue was full", &SceneAudioWatchesInternal::readRetireOverflows );
    SW_AUTOMATION_WALL_WATCH( audioClipCacheBytes, "Audio.ClipCacheBytes", "Decoded bytes held by the audio clip cache", &SceneAudioWatchesInternal::readClipCacheBytes,
                              &SceneAudioWatchesInternal::readClipCacheActivity );
} // namespace sw
