/**
 * @file WebAudioBackend.h
 * @brief 웹(브라우저) 오디오 장치 백엔드입니다. 믹서 스레드(pthread)가 엔진을 렌더해 공유 메모리 링 버퍼(`CircularAudioBuffer`)에 넣고,
 *        AudioWorklet 처리기(`WebAudioBackend.js`)가 브라우저 오디오 스레드에서 꺼내 출력합니다(단일 생산자 · 단일 소비자).
 * @details 예전에는 Web 클라이언트에 백엔드가 없어 구성이 CMake 에서 멈췄다(감사 P0-11). Wasm 메모리가 SharedArrayBuffer(`-pthread`)라 워클릿이 링을 그대로 읽는다 —
 *          페이지가 교차 출처 격리(COOP · COEP)여야 한다(`Scripts/package/WebPackage.py` 의 서버가 준다).
 *          엔진을 워클릿 안(Emscripten Wasm Audio Worklet)에서 돌리지 않는다 — 그 스레드는 pthread 가 아니라 엔진의 잠금 · 할당이 닿지 않고, 렌더가 128 프레임 기한을 넘기면 바로 끊긴다.
 *          Godot 웹(AudioWorklet + 링 버퍼, 스레드 판)과 같은 모양이다. Unity WebGL 은 자체 믹서 대신 브라우저 Web Audio 노드로 소리를 낸다(기억).
 *          장치 배치를 알 수 없어 스테레오로 알린다. 브라우저는 사용자 몸짓 전까지 AudioContext 를 멈춰 두므로 첫 클릭 · 키 · 터치에서 `resume` 한다 — 그 사이 링이 차면 믹서는 기다린다.
 */
#pragma once
#include "Core/Concurrency/StopToken.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Audio/IAudioService.h"

namespace sw
{
    struct WebAudioBackendImpl;

    /**
     * @class WebAudioBackend
     * @brief 웹 오디오 백엔드입니다.
     */
    class SW_API WebAudioBackend : public IAudioService
    {
    public:
        /** @brief 장치를 잡지 않은 채로 만듭니다. */
        WebAudioBackend();
        /** @brief 장치를 닫고 엔진을 내립니다. */
        ~WebAudioBackend() override;

    protected:
        /** @brief 브라우저에 Web Audio(AudioContext · AudioWorklet)가 있는지 봅니다. 스테레오를 알립니다. */
        [[nodiscard]] bool openDevice( AudioOutputDevice& outDevice ) override;
        /** @brief 링 버퍼를 만들고 48 kHz AudioContext · 워클릿 노드를 연 뒤 믹서 스레드를 시작합니다. */
        [[nodiscard]] bool startOutput( AudioChannelLayout layout ) override;
        /** @brief 믹서 스레드를 멈추고 AudioContext 를 닫습니다. 링은 닫기가 끝난 뒤 브라우저 쪽이 놓습니다. */
        void stopOutput() override;
        /** @brief 출력을 멈춥니다(장치를 따로 잡지 않는다). */
        void closeDevice() override;

    private:
        /** @brief 믹서 스레드 본문입니다. 링에 블록 하나만큼 자리가 날 때까지 기다렸다가 렌더해 넣습니다. */
        void runMixer( StopToken stopToken );

    private:
        unique_ptr<WebAudioBackendImpl> _impl; /**< 웹 전용 상태입니다. */
    };
} // namespace sw
