/**
 * @file Engine/Audio/AudioRenderListener.h
 * @brief 장치 없는 오프라인 렌더(믹서 최종 출력)를 보는 리스너 등록입니다 — 보는 쪽이 `SW_AUDIO_RENDER_LISTENER` 한 줄로 스스로 등록하고, 오디오 서비스는 렌더한 블록을 등록에 넘기기만 합니다.
 * @details 오프라인 렌더 덤프(`Audio/Dev/AudioDumpListener.cpp` 의 `-gv_audioDumpPath` — 배포 구성이 짓지 않는다)가 등록한다. 배포본에는 등록이 없어 서비스는 블록을 넘기지 않는다.
 *          예전에는 서비스가 테스트용 전역 변수를 읽고 덤프 표본 목록(`_listDumpSample`)을 직접 들었다. 언리얼 오디오 믹서의
 *          `ISubmixBufferListener`(마스터 서브믹스 출력에 붙는 관찰자 등록)와 같은 자리다.
 */
#pragma once
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Registry/RegistrationList.h"

#include "Engine/Audio/AudioChannelLayout.h"

namespace sw
{
    /**
     * @class IAudioRenderListener
     * @brief 오프라인 렌더 출력을 보는 쪽입니다. 오프라인 렌더를 돌리는 스레드(게임 스레드의 서비스 `update`)에서 불립니다.
     */
    class IAudioRenderListener
    {
    public:
        virtual ~IAudioRenderListener() = default;

        /** @brief 렌더한 블록 하나입니다. @p pSample 은 채널 교차 표본 @p frameCount × 채널 수(@p layout)이고 호출 안에서만 유효합니다. */
        virtual void onRenderedBlock( const float32* pSample, uint32 frameCount, AudioChannelLayout layout ) = 0;
        /** @brief 지금 레이아웃의 스트림이 끝났습니다(레이아웃을 바꾸기 직전 · 서비스를 내릴 때). 쌓은 것을 마무리합니다. */
        virtual void onStreamEnded( AudioChannelLayout layout ) = 0;
    };

    /** @struct AudioRenderListenerRegistration @brief 등록 항목 하나입니다. 이름과 리스너은 등록한 이미지의 정적 저장소에 있습니다. */
    struct AudioRenderListenerRegistration
    {
        const utf8*           _pName; ///< 리스너 이름(예: `AudioDump`)
        IAudioRenderListener* _pListener;
    };
} // namespace sw

namespace sw
{
    /**
     * @class AudioRenderListenerRegistry
     * @brief 오프라인 렌더 리스너 레지스트리입니다. 같은 이름이 둘이면 뒤의 것을 거절하고 오류를 남깁니다. 목록은 이름 순입니다. 게임 스레드와 정적 초기화에서만 쓴다.
     */
    class SW_API AudioRenderListenerRegistry
    {
    public:
        /** @brief 등록합니다. 같은 이름이 이미 있으면 false 이고 등록하지 않습니다. */
        [[nodiscard]] static bool registerListener( const AudioRenderListenerRegistration* pRegistration );
        static void               unregisterListener( const AudioRenderListenerRegistration* pRegistration );
        /** @brief 등록 수입니다. 0 이면 서비스가 블록을 넘기지 않는다. */
        static uint32 getCount();
        /** @brief 블록을 등록마다 넘깁니다. */
        static void notifyRenderedBlock( const float32* pSample, uint32 frameCount, AudioChannelLayout layout );
        /** @brief 스트림 끝을 등록마다 알립니다. */
        static void notifyStreamEnded( AudioChannelLayout layout );
    };

    /** @brief `SW_AUDIO_RENDER_LISTENER` 이 두는 정적 등록자입니다(공통 틀 `StaticRegistrar` — 거절된 등록은 내리지 않는다). */
    SW_DEFINE_STATIC_REGISTRAR( AudioRenderListenerRegistrar, const AudioRenderListenerRegistration*, &AudioRenderListenerRegistry::registerListener, &AudioRenderListenerRegistry::unregisterListener );
} // namespace sw

/** @brief 오프라인 렌더 리스너을 등록합니다(.cpp 의 파일 범위, `namespace sw` 안). @p pListener 은 정적 수명의 리스너 객체 주소입니다. */
#define SW_AUDIO_RENDER_LISTENER( id, pName, pListener ) SW_STATIC_REGISTRATION( audioRenderListener, ::sw::AudioRenderListenerRegistration, ::sw::AudioRenderListenerRegistrar, id, pName, pListener )
