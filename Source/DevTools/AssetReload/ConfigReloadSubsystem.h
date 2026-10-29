/**
 * @file ConfigReloadSubsystem.h
 * @brief 실행 중 `Config/` 의 설정 JSON 이 바뀌면 다시 읽습니다(개발 빌드만 — 배포 빌드는 이 폴더를 짓지 않는다).
 * @details 예전에는 편집기(`ConfigHotReload`)에만 있어 편집기 없이 띄운 개발 실행은 설정을 다시 읽지 못했다. 엔진 서브시스템이라 App · 전용 서버 · 편집기가 모두 같다.
 *          - 호스트 설정(`ConfigManager` 가 파일에서 읽은 것)은 `ConfigManager::reloadConfigFile` 이 제자리에서 다시 읽고 알린다.
 *          - 그 밖의 파일은 등록된 처리기(`registerFileHandler`)가 맡는다 — 편집기는 도구 기본값 파일을 여기에 건다.
 *          - 앱이 다시 쓰는 파일(`EditorPreferences.json` · 레이아웃)은 처리기가 맡지 않으면 지나간다(`NotTracked`).
 *          언리얼 DirectoryWatcher 모듈 + 설정 리로드와 같은 나눔이다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Foundation/Macros.h"
#include "Core/Memory/Memory.h"
#include "Core/Subsystem/ISubsystem.h"

#include "DevTools/DevToolsAPI.h"

#include "Engine/Serialization/Serializer/ReloadOutcome.h"

namespace sw
{
    class ConfigManager;
    class FileWatchDispatcher;
    struct FileChangeEvent;

    /** @brief 설정 파일 하나를 맡는 처리기입니다. 맡은 파일이 아니면 `NotTracked` 를 돌려준다. */
    using ConfigFileReloadDelegate = Delegate<ReloadOutcome( string_view fullPath )>;

    /** @class ConfigReloadSubsystem @brief `Config/` 감시와 설정 파일 리로드입니다. */
    class SW_DEVTOOLS_API ConfigReloadSubsystem final : public ISubsystem
    {
    public:
        static constexpr const utf8* kSubsystemName = "ConfigReload";

        /** @param configManager 호스트 설정의 주인 — 이 서브시스템보다 먼저 서고 늦게 사라진다(의존 칸). */
        explicit ConfigReloadSubsystem( ConfigManager& configManager );
        ~ConfigReloadSubsystem() override;
        ConfigReloadSubsystem( const ConfigReloadSubsystem& )            = delete;
        ConfigReloadSubsystem& operator=( const ConfigReloadSubsystem& ) = delete;

        /** @brief 프로젝트의 `Config/` 를 감시하기 시작합니다. 폴더가 없으면 꺼진 채로 둔다(기동을 멈추지 않는다). */
        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override;
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;

        /** @brief 프레임마다 부릅니다(바뀐 파일을 다시 읽는다). */
        void update();

        /** @brief 호스트 설정 밖의 파일을 맡는 처리기를 겁니다. 구독이 사라지면 빠진다. */
        [[nodiscard]] Subscription registerFileHandler( const ConfigFileReloadDelegate& handler );

        /**
         * @brief 바뀐 파일 하나를 처리합니다 — 호스트 설정이면 `ConfigManager` 가, 아니면 처리기가 다시 읽습니다. 설정 화면의 저장도 이것을 부른다.
         * @return 다시 읽었으면 `Reloaded`, 맡은 곳이 없으면 `NotTracked`, 깨진 파일이면(오류를 남기고 이전 값을 지켰다) `Failed`
         */
        [[nodiscard]] ReloadOutcome reloadChangedFile( string_view fullPath );

    private:
        void onConfigFileChanged( const FileChangeEvent& changeEvent );

        ConfigManager&                  _configManager;
        unique_ptr<FileWatchDispatcher> _pFileWatchDispatcher;
        Subscription                    _configWatch; ///< 설정 폴더 감시 구독
        /** @brief 처리기 목록입니다 — 공용 멀티캐스트라 처리기가 순회 중 자기 구독을 놓아도 순회가 끝난 뒤 빠진다. 먼저 맡은 처리기에서 멈춘다(`broadcastUntil`). */
        MulticastDelegate<ReloadOutcome( string_view )> _fileHandlers;
    };
} // namespace sw
