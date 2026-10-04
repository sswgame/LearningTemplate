/**
 * @file AssetHotReload.h
 * @brief 실행 중 `Resource/` 변경을 감지해 엔진 캐시를 다시 읽게 합니다(**에디터 전용 개발 기능** — 배포본에는 감시가 없습니다).
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Workspace/FileWatchDispatcher.h"

namespace sw
{
    class AssetManager;
    class GameObjectManager;
    class IRHIDevice;
} // namespace sw

namespace sw::editor
{
    /**
     * @class AssetHotReload
     * @brief `Resource/` 하위 변경을 폴링해, 그 파일을 들고 있는 엔진 캐시(`IAssetCache`)가 다시 읽게 합니다.
     * @details 종류별 코드가 없습니다. 어느 캐시가 읽을지 · 먼저 쿠킹할지는 `EditorAssetTypeRegistry::findReloadRoute` 의 표가 정합니다.
     *          엔진 캐시는 자기 런타임 파일을 제자리로 다시 읽을 뿐이고(로드), 소스 임포트 · 감시 · 씬 알림은 여기(에디터)에 있습니다.
     *          새 에셋 종류는 이 파일을 고치지 않습니다.
     */
    class AssetHotReload
    {
    public:
        /** @brief 감시를 시작하지 않은 채 만듭니다. */
        AssetHotReload();
        /** @brief 감시를 정리합니다. */
        ~AssetHotReload();

        AssetHotReload( const AssetHotReload& )            = delete;
        AssetHotReload& operator=( const AssetHotReload& ) = delete;

        /**
         * @brief 파일 감시를 시작하고 `Resource/` 워치를 겁니다.
         * @return 감시자를 세웠으면 true. 리소스 루트가 없으면(팩만 있는 실행) false.
         */
        bool initialize();

        /** @brief 워치를 해제하고 감시 스레드를 멈춥니다. */
        void shutdown();

        /** @brief 에디터 프레임마다 불러 변경 이벤트를 꺼냅니다. */
        void update();

        /**
         * @brief 바뀐 에셋 하나를 다시 읽습니다: 임포터(있으면) → 엔진 캐시 `reload` → 활성 씬 `notifyAssetUsers`. 게임 스레드에서, 틱 밖에서 부릅니다.
         * @return 핫 리로드 대상이고 처리했으면 true. 대상이 아니거나 그 이름의 캐시가 등록되지 않았으면 false 입니다.
         */
        [[nodiscard]] static bool reloadChangedAsset( string_view relativePath );

        /**
         * @brief @p resources 의 등록부에서 그 경로를 든 캐시(`IAssetCache::isCached`)마다 `reload` 를 부릅니다 — 이름을 모르는 모듈 캐시의 길입니다.
         * @return 다시 읽게 한 캐시 수입니다.
         */
        static uint32 reloadInCachesHolding( AssetManager& resources, string_view relativePath, IRHIDevice* pDevice );

        /**
         * @brief @p objects 에서 그 에셋을 쓰는 컴포넌트를 찾아, 인스펙터에서 그 프로퍼티를 고쳤을 때처럼 `onPropertyChanged` 를 부릅니다(언리얼의 PostEditChange).
         * @details 찾는 기준은 리플렉션입니다 — 값이 그 경로인 에셋 경로 프로퍼티(`PROPERTY( AssetPath )` · `AssetType = …`, 문자열 · hashed_string).
         *          컴포넌트에는 리로드 전용 코드가 필요 없습니다. 에셋에서 계산한 상태는 `onPropertyChanged` 가 값이 같아도 다시 맞춰야 합니다.
         * @return `onPropertyChanged` 를 부른 프로퍼티 수입니다.
         */
        static uint32 notifyAssetUsers( GameObjectManager& objects, string_view relativePath );

    private:
        /** @brief 바뀐 파일 하나를 `reloadChangedAsset` 으로 보냅니다(수정 이벤트만). */
        void onResourceFileChanged( const FileChangeEvent& changeEvent );

        unique_ptr<FileWatchDispatcher> _pFileWatchDispatcher;
        FileWatchHandle                 _resourceWatchHandle;
    };
} // namespace sw::editor
