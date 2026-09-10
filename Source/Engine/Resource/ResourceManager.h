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
        /** @brief Material/GUID 맵을 비우고 마운트된 팩을 정리합니다. */
        void shutdown();
        /**
         * @brief 에셋 식별자(GUID) 표를 채웁니다. 돌려주는 값은 등록 수.
         * @details 팩이면 쿠커가 도메인마다 넣는 `assetregistry.txt` 를 읽고(배포본은 .meta 를 싣지 않는다), 하나도
         *          없으면(느슨한 트리) 리소스 루트의 `.meta` 를 훑는다. `initialize` 가 한 번 부르고, EngineLoop 이
         *          GameConfig 를 활성화하고 팩을 마운트한 **뒤** 다시 부른다 — 게임 도메인(`_packRoot`)은 그때야
         *          알 수 있다. 두 번 불러도 같은 매핑을 덮어쓸 뿐이다.
         */
        uint32 loadAssetRegistries();

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
