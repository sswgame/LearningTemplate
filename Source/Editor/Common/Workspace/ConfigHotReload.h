/**
 * @file ConfigHotReload.h
 * @brief 실행 중 `Config/` 의 설정 JSON(EngineConfig · GameConfig · editortooldefaults …)이 바뀌면 다시 읽습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/EditorSubsystem.h"
#include "Editor/Common/Workspace/FileWatchDispatcher.h"

#include "Engine/Serialization/Base/ReloadOutcome.h"

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
     *            App 은 프레임 시간 정책을, Engine 는 선호 수직 동기화를 다시 맞춘다. 엔진 설정 · 게임 프리셋은 서비스 표가 가리키는 그 자리에서 바뀌므로
     *            읽는 쪽이 따로 갱신할 사본이 없다(`engine::getSubsystem<EngineConfig>()` 가 다음에 읽을 때 새 값이다).
     *          - 에디터 도구 시드(`editortooldefaults.json`)는 여기서 다시 읽는다(IDE 명령 · 핫 리로드 확장자 …).
     *          - 앱이 다시 쓰는 `EditorPreferences.json` · 레이아웃 파일(`.ini`)은 보지 않는다 — 저장할 때마다 자기 변경을 다시 읽게 된다.
     */
    class ConfigHotReload : public EditorSubsystem
    {
    public:
        ConfigHotReload();
        ~ConfigHotReload() override;

        // EditorSubsystem — EditorContext 가 의존 순서로 초기화하고 역순으로 종료 · 소멸한다.
        SubsystemInitResult initializeSubsystem() override
        {
            (void)initialize(); // Config 폴더가 없으면 꺼진 채로 둔다
            return SubsystemInitResult::Succeeded;
        }
        void shutdownSubsystem() override { shutdown(); }
        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "ConfigHotReload";
        const utf8*                  getSubsystemName() const override { return kSubsystemName; }

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
         * @return 다시 읽었으면 `Reloaded`, 맡은 설정 파일이 아니면 `NotTracked`, 깨진 파일이면(오류를 남기고 이전 값을 지켰다) `Failed`
         */
        [[nodiscard]] static ReloadOutcome reloadChangedFile( ConfigManager* pConfigManager, string_view fullPath );

    private:
        void onConfigFileChanged( const FileChangeEvent& changeEvent );

        unique_ptr<FileWatchDispatcher> _pFileWatchDispatcher;
        Subscription                    _configWatch; ///< 설정 폴더 감시 구독
    };
} // namespace sw::editor
