/**
 * @file AudioDataHotReload.h
 * @brief 실행 중 오디오 데이터(`*.audiomixer.xml` · `*.audioevents.xml`)가 바뀌면 오디오 엔진이 다시 읽게 합니다(ImGui 없음, EditorTest 가 본다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/FileWatchDispatcher.h"

namespace sw
{
    class IAudioService;
} // namespace sw

namespace sw::editor
{
    /**
     * @class AudioDataHotReload
     * @brief `Resource/` 의 오디오 데이터 변경을 `FileWatchDispatcher` 로 보고, 믹서 그래프와 이벤트 라이브러리를 그 자리에서 다시 읽습니다.
     * @details 믹서는 `IAudioService::loadMixer`(재생 중인 보이스는 같은 이름의 버스로 옮겨 가고 사용자 볼륨은 이름으로 남는다), 이벤트 라이브러리는 같은 이름으로
     *          다시 로드해 교체합니다(`AudioEngine::loadEventLibrary` — 쿨다운과 순서 커서는 이름으로 이어진다). 게임이 로드하지 않은 라이브러리는 건드리지 않는다 —
     *          다른 이름으로 로드되면 이벤트 이름이 겹쳐 오류가 되기 때문이다. 깨진 파일은 경고를 남기고 지금 데이터를 그대로 둔다(엔진의 로드 검사가 막는다).
     *          언리얼 Wwise · FMOD 연동의 뱅크 핫 리로드와 같은 흐름이다.
     */
    class AudioDataHotReload : public EditorSubsystem
    {
    public:
        /** @brief 감시를 시작하지 않은 채 만듭니다. */
        AudioDataHotReload();
        ~AudioDataHotReload() override;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        void        shutdownSubsystem() override { shutdown(); }
        const utf8* getSubsystemName() const override { return "AudioDataHotReload"; }

        AudioDataHotReload( const AudioDataHotReload& )            = delete;
        AudioDataHotReload& operator=( const AudioDataHotReload& ) = delete;

        /** @brief 리소스 루트의 오디오 데이터 감시를 시작합니다. 리소스 루트가 없으면 false 입니다. */
        [[nodiscard]] bool initialize();
        /** @brief 감시를 멈춥니다. */
        void shutdown();
        /** @brief 에디터 프레임마다 부릅니다(바뀐 파일을 다시 읽는다). */
        void update();
        /** @brief 감시 중이면 true 입니다. */
        bool isWatching() const { return _pFileWatchDispatcher != nullptr; }
        /** @brief 지금까지 다시 읽어 적용한 파일 수입니다(탐침 `Editor.AudioDataReloadCount`). */
        uint32 getReloadCount() const { return _reloadCount; }

        /** @brief 경로가 오디오 데이터 파일(`.audiomixer.xml` · `.audioevents.xml`)이면 true 입니다. 대소문자를 무시합니다. */
        [[nodiscard]] static bool isAudioDataPath( string_view path );
        /**
         * @brief 바뀐 파일 하나를 다시 읽습니다. 게임 스레드에서 부릅니다.
         * @param resourceRelativePath `Resource/` 기준 경로(게임이 라이브러리를 로드할 때 준 이름과 같다)
         * @return 다시 읽어 적용했으면 true 입니다. 오디오 데이터가 아니거나, 게임이 로드하지 않은 라이브러리거나, 파일이 틀렸으면 false 입니다.
         */
        [[nodiscard]] static bool reloadChangedFile( IAudioService& audio, string_view resourceRelativePath );

    private:
        void onResourceFileChanged( const FileChangeEvent& changeEvent );

    private:
        unique_ptr<FileWatchDispatcher> _pFileWatchDispatcher;
        FileWatchHandle                 _watchHandle;
        uint32                          _reloadCount;
    };
} // namespace sw::editor
