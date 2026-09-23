/**
 * @file XAudio2System.h
 * @brief Windows 전용 오디오 파사드입니다(XAudio2 + Media Foundation 디코드).
 *
 * @note 이 클래스는 Windows 에서만 만들어집니다. IAudioSystem::create() 가 다른 플랫폼에서는
 *       NullAudioSystem 을 반환합니다. 그래서 구현 .cpp 는 파일 전체가 SW_PLATFORM_WINDOWS 가드
 *       안에 있습니다(Window/Windows · Input/Windows 와 같은 형태). 예전에는 이 파일이 모든
 *       플랫폼에서 컴파일되느라 몸통 안에 #if 가 22개 들어 있었습니다.
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
     * @brief Windows 오디오 백엔드입니다. WAV 는 직접 파싱하고, 그 밖의 형식(MP3 등)은 Media Foundation 으로 디코딩해 재생합니다.
     */
    class SW_API XAudio2System : public IAudioSystem
    {
    public:
        /** @brief 빈 오디오 시스템으로 만듭니다. initialize 를 부르기 전에는 쓸 수 없습니다. */
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

        /** @brief 리소스 상대 · 절대 경로를 해석해 효과음(SFX)을 한 번 재생합니다. */
        bool play( string_view path ) override;

        /** @brief 배경음악(BGM)을 루프 재생하고 이전 음악 보이스는 멈춥니다. 같은 곡이 이미 재생 중이면 다시 시작하지 않습니다. */
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
        /** @brief 바뀐 볼륨 · 음소거를 마스터 · 음악 · 효과음 보이스에 반영합니다. */
        void applyVolume() override;

    private:
        /** @brief 경로를 확인하고 디코드 · 재생을 워커 태스크로 넘깁니다. */
        bool playInternal( string_view path, bool bLoop );
        /** @brief 워커에서 클립을 디코드해 재생하는 태스크 본문입니다. TaskArgs 는 요청 경로 · 루프 여부 · 음악 요청 번호입니다. */
        void playDecodedClipTask( const TaskArgs& args );

    private:
        unique_ptr<XAudio2SystemImpl> _impl; /**< Windows 전용 상태입니다. 헤더에서 XAudio2 를 가립니다. */
    };
} // namespace sw
