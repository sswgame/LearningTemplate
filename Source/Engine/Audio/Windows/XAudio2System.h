/**
 * @file XAudio2System.h
 * @brief Windows 전용 오디오 퍼사드 (XAudio2 + Media Foundation 디코드).
 *
 * @note 이 클래스는 Windows 에서만 생성된다 — IAudioSystem::create() 가 다른 플랫폼에서는
 *       NullAudioSystem 을 돌려준다. 그래서 구현 .cpp 는 파일 전체가 SW_PLATFORM_WINDOWS 가드
 *       안에 있다(Window/Windows · Input/Windows 와 같은 형태). 예전에는 이 파일이 모든
 *       플랫폼에서 컴파일되느라 몸통 안에 #if 가 22개 들어 있었다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Memory/Memory.h"

#include "Engine/Audio/IAudioSystem.h"

namespace sw
{
    struct XAudio2SystemImpl;

    class TaskArgs;

    /**
     * @class XAudio2System
     * @brief initialize / update / play (Windows에서 WAV PCM 또는 MF 디코드 MP3).
     */
    class SW_API XAudio2System : public IAudioSystem
    {
    public:
        /** @brief 빈 오디오 시스템. initialize 전에 쓰지 마세요. */
        XAudio2System();
        /** @brief 오디오 시스템을 해제합니다. */
        ~XAudio2System() override;

        /** @brief 복사를 금지합니다. */
        XAudio2System( const XAudio2System& ) = delete;
        /** @brief 대입을 금지합니다. */
        XAudio2System& operator=( const XAudio2System& ) = delete;

        /** @brief 오디오 백엔드를 초기화합니다. */
        bool initialize() override;
        /** @brief 오디오 백엔드를 종료합니다. */
        void shutdown() override;
        /** @brief 초기화 여부를 반환합니다. */
        bool isInitialized() const override;
        /** @brief 재생이 끝난 보이스를 정리합니다. */
        void update( float32 deltaSeconds ) override;

        /** @brief 리소스 상대/절대 경로를 해석해 한 번 재생합니다 (SFX). */
        bool play( string_view path ) override;

        /** @brief BGM을 루프 재생합니다. 이전 루프 보이스를 멈춥니다. 같은 경로는 재시작하지 않습니다. */
        bool playMusic( string_view path ) override;

        /** @brief 루프 음악 보이스를 멈춥니다. */
        void stopMusic() override;

        /** @brief 배경음악을 일시정지합니다. */
        void pauseMusic() override;
        /** @brief 일시정지된 배경음악을 재개합니다. */
        void resumeMusic() override;

        /** @brief 마지막으로 요청된 배경음악 경로입니다. */
        string getMusicPath() const override;

    protected:
        /** @brief 바뀐 볼륨·음소거를 마스터·음악·효과음 보이스에 반영합니다. */
        void applyVolume() override;

    private:
        /** @brief 경로를 해석해 보이스를 재생합니다. */
        bool playInternal( string_view path, bool bLoop );
        /** @brief TaskArgs: 요청 경로, 루프 여부, 음악 세대 번호. */
        void playDecodedClipTask( const TaskArgs& args );

    private:
        unique_ptr<XAudio2SystemImpl> _impl; /**< Windows 전용 상태입니다. 헤더에서 XAudio2 를 가립니다. */
    };
} // namespace sw
