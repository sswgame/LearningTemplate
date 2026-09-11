/**
 * @file ResourceManager.h
 * @brief 팩 에셋(GUID · 스키마 · Material · Prefab)의 App 소유 파사드
 * @note
 *   포함: AssetDatabase, AssetFormatRegistry, MaterialCache, PrefabManager.
 *   제외(수명이 다름):
 *   - ResourceUtil — Resource/ 경로 해석만 (소유권 없음)
 *   - ShaderCache — 셰이더 컴파일 결과 캐시 (RHI)
 *   - RenderPassManager — GPU 디바이스가 소유
 *   - ConfigManager — Config/ 호스트 JSON (Resource/ 아님)
 *   - StringTable · ReloadFileManager · SceneManager
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Memory/Memory.h"

#include "Engine/Module/ReloadFileManager.h"
#include "Engine/Resource/AssetDatabase.h"
#include "Engine/Resource/AssetFormat.h"

namespace sw
{
    class MaterialCache;
    class PrefabManager;
    class ResourcePackManager;
    class TextureCache;

    /**
     * @class ResourceManager
     * @brief 팩 에셋 식별·스키마·인스턴스 캐시 및 VFS 팩 매니저를 한 객체로 소유합니다.
     */
    class SW_API ResourceManager
    {
    public:
        /** @brief 빈 매니저. initialize() 전에 바인딩해도 됩니다. */
        ResourceManager();
        /** @brief 캐시를 해제합니다. */
        ~ResourceManager();

        ResourceManager( const ResourceManager& )            = delete;
        ResourceManager& operator=( const ResourceManager& ) = delete;

        /**
         * @brief Resource/ 검색 루트를 잡고 VFS 팩 마운트 및 내장 XML migrator를 등록합니다.
         * @return 프로젝트 루트를 찾으면 true.
         */
        bool initialize();

        /**
         * @brief 콘텐츠를 올립니다 — 검색 우선순위 적용 → 시작 팩 마운트 → 에셋 레지스트리 적재.
         * @param listSearchPriority 검색 우선순위 토큰 목록. 비우면 지금 설정된 것을 그대로 쓴다.
         * @return 팩을 하나라도 마운트했으면 true (느슨한 `Resource/` 트리만 있으면 false).
         *
         * @details **`GameConfig::setActive` 뒤에 불러야 한다.** "game" 토큰은 `GameConfig._packRoot`
         *          로 풀리므로, 그 전에 부르면 게임 도메인이 통째로 빠진 채 팩이 실리고 GUID 표도
         *          engine/common 만 채워진다.
         *
         *          `initialize()` 에서 갈라낸 이유가 그것이다. 예전에는 초기화가 이 일까지 같이 해서
         *          "설정이 먼저" 라는 전제가 호출부에 드러나지 않았고, 그래서 순서가 뒤집힌 채로
         *          **마운트와 레지스트리 적재를 뒤에서 한 번 더** 해서 메우고 있었다. 이제 전제가
         *          인자로 드러나고, 우선순위 적용과 마운트가 한 호출로 묶여 사이가 벌어지지 않는다.
         */
        bool mountContent( const vector<string>& listSearchPriority );

        /** @brief Material/GUID 맵을 비우고 마운트된 팩을 정리합니다. */
        void shutdown();
        /**
         * @brief 에셋 식별자(GUID) 표를 채웁니다. 돌려주는 값은 등록 수.
         * @details 팩이면 쿠커가 도메인마다 넣는 `assetregistry.txt` 를 읽고(배포본은 .meta 를 싣지 않는다), 하나도
         *          없으면(느슨한 트리) 리소스 루트의 `.meta` 를 훑는다. 보통은 `mountContent` 가 부른다.
         *          전역 VFS 를 헤집은 테스트가 되돌릴 때 직접 부른다.
         */
        uint32 loadAssetRegistries();

        /**
         * @brief 실행 파일 옆(또는 프로젝트)의 `Packs/` 를 찾아 전부 마운트합니다.
         * @details 보통은 `mountContent` 가 부른다. 전역 VFS 를 헤집는 테스트가 시작 시점 상태로
         *          되돌릴 때도 이것을 쓴다 — 후보 경로 목록을 두 곳에 복사해 두면 한쪽만 바뀐다.
         * @return 하나라도 마운트했으면 true.
         */
        bool mountStartupPacks();

        /** @brief 핫리로드 감시를 위해 ReloadFileManager를 연결합니다. */
        void attachReloadFileManager( ReloadFileManager& reloadFiles );
        /** @brief ReloadFileManager 연결을 해제합니다. */
        void detachReloadFileManager();

        /** @brief 불필요한 캐시 및 스트리밍 큐 대기 내역을 정리하여 메모리를 반환합니다. */
        void garbageCollectUnusedAssets();

        /** @brief VFS 마운트된 리소스 팩 매니저 반환. */
        ResourcePackManager&       getPackManager();
        const ResourcePackManager& getPackManager() const;

        /** @brief 경로 ↔ GUID (.meta). */
        AssetDatabase&       getAssetDatabase() { return _assetDatabase; }
        const AssetDatabase& getAssetDatabase() const { return _assetDatabase; }

        /** @brief XML formatVersion 마이그레이션. */
        AssetFormatRegistry&       getAssetFormatRegistry() { return _assetFormatRegistry; }
        const AssetFormatRegistry& getAssetFormatRegistry() const { return _assetFormatRegistry; }

        /** @brief 경로 키 Material 인스턴스 + GPU 수명. */
        MaterialCache&       getMaterialManager();
        const MaterialCache& getMaterialManager() const;
        /** @brief 경로 키 Texture2D 인스턴스 + GPU 수명. 머티리얼의 Texture2D 프로퍼티(assetPath)가 여기서 빌린다. */
        TextureCache&       getTextureManager();
        const TextureCache& getTextureManager() const;

        /** @brief Prefab 로드/스폰 캐시. */
        PrefabManager&       getPrefabManager();
        const PrefabManager& getPrefabManager() const;

    private:
        void onResourceFileChanged( const FileChangeEvent& changeEvent );

    private:
        AssetDatabase                   _assetDatabase;
        AssetFormatRegistry             _assetFormatRegistry;
        unique_ptr<MaterialCache>       _materialCache;
        unique_ptr<TextureCache>        _textureCache;
        unique_ptr<PrefabManager>       _prefabManager;
        unique_ptr<ResourcePackManager> _pPackManager;
        FileWatchHandle                 _resourceWatchHandle;
        ReloadFileManager*              _pReloadFileManager;
    };
} // namespace sw
