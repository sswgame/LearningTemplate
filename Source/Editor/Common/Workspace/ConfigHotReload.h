/**
 * @file ConfigHotReload.h
 * @brief 실행 중 `Config/` 의 설정 JSON(EngineConfig · GameConfig · editortooldefaults …)이 바뀌면 다시 읽습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Workspace/FileWatchDispatcher.h"

namespace sw
{
    class ConfigManager;
} // namespace sw

namespace sw::editor
{
    /**
     * @class ConfigHotReload
     * @brief `Config/` 폴더를 감시해 바뀐 설정 파일을 다시 읽습니다(에셋 핫 리로드와 같은 `FileWatchDispatcher`).
     * @details - 호스트 설정(`ConfigManager` 가 파일에서 읽은 것)은 `ConfigManager::reloadConfigFile` 이 제자리에서 다시 읽고 알린다 —
     *            App 은 프레임 시간 정책을, EngineLoop 는 게임 설정 활성본 · 선호 수직 동기화를 다시 맞춘다.
     *          - 에디터 도구 시드(`editortooldefaults.json`)는 여기서 다시 읽는다(IDE 명령 · 핫 리로드 확장자 …).
     *          - 앱이 다시 쓰는 `EditorConfig.json` · 레이아웃 파일(`.ini`)은 보지 않는다 — 저장할 때마다 자기 변경을 다시 읽게 된다.
     */
    class ConfigHotReload
    {
    public:
        ConfigHotReload();
        ~ConfigHotReload();

        ConfigHotReload( const ConfigHotReload& )            = delete;
        ConfigHotReload& operator=( const ConfigHotReload& ) = delete;

        /** @brief 프로젝트의 `Config/` 를 감시하기 시작합니다. 폴더가 없으면 false 입니다. */
        bool initialize();
        /** @brief 감시를 멈춥니다. */
        void shutdown();
        /** @brief 에디터 프레임마다 부릅니다(바뀐 파일을 다시 읽습니다). */
        void update();

        /**
         * @brief 바뀐 파일 하나를 처리합니다 — 호스트 설정이면 @p pConfigManager 가, 에디터 도구 시드면 에디터가 다시 읽습니다.
         * @return 다시 읽은 설정이 있으면 true
         */
        [[nodiscard]] static bool reloadChangedFile( ConfigManager* pConfigManager, string_view fullPath );

    private:
        void onConfigFileChanged( const FileChangeEvent& changeEvent );

        unique_ptr<FileWatchDispatcher> _pFileWatchDispatcher;
        FileWatchHandle                 _configWatchHandle;
    };
} // namespace sw::editor
