/**
 * @file AssetManager.h
 * @brief 팩 에셋(GUID · 스키마 · Material · Texture · Prefab)을 한곳에서 들고 있는 파사드입니다. `EngineLoop` 가 `EngineServiceCollection` 로 소유합니다.
 * @note
 *   포함: AssetDatabase, AssetFormatRegistry, MaterialCache, TextureCache, PrefabCache, ResourcePackManager.
 *   제외(수명이 다름):
 *   - ResourceUtil: Resource/ 경로 해석만 합니다(소유권 없음)
 *   - ShaderCache: 셰이더 컴파일 결과 캐시(RHI)
 *   - RenderPipelineAssetCache: FrameRenderer 가 소유
 *   - ConfigManager: Config/ 호스트 JSON(Resource/ 아님)
 *   - StringTable · SceneManager
 *   - 파일 감시 · 에셋 핫 리로드: **개발 기능이라 에디터가 소유**합니다(Editor/Common/Workspace).
 *     배포본에는 에디터가 없으므로 감시 스레드도 없습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/RegistrationList.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"
#include "Core/Module/ModuleUnloadListener.h"

#include "Engine/Resource/AssetDatabase.h"
#include "Engine/Resource/AssetFormat.h"

namespace sw
{
    class IAssetCache;
    class MaterialCache;
    class PrefabCache;
    class ResourcePackManager;
    class SpriteClipCache;
    class TextureCache;

    /** @brief `AssetManager::mountContent` 가 무엇을 콘텐츠로 올리는지입니다. */
    enum class ContentSource : uint8
    {
        Cooked,     ///< 실행 — 쿠킹한 팩을 마운트한다. 느슨한 `Resource/` 파일은 개발 구성에서만 읽는다
        SourceTree, ///< 쿠킹 — 소스 트리(`Resource/`)를 읽는다. 팩(지난 쿠킹의 산출물)은 마운트하지 않고, 프리팹도 소스(XML · JSON)를 읽는다
    };

    /**
     * @class AssetManager
     * @brief 에셋 식별 · 스키마 · 인스턴스 캐시와 VFS 팩 매니저를 한 객체로 소유합니다.
     */
    class SW_API AssetManager final : public IModuleUnloadListener
    {
    public:
        /** @brief 빈 매니저로 만듭니다. initialize() 전에 바인딩해도 됩니다. */
        AssetManager();
        /** @brief 캐시를 해제합니다. */
        ~AssetManager() override;

        AssetManager( const AssetManager& )            = delete;
        AssetManager& operator=( const AssetManager& ) = delete;

        /**
         * @brief Resource/ 검색 루트를 잡고 내장 XML migrator 를 등록합니다. 팩 마운트는 `mountContent` 가 합니다.
         * @return 프로젝트 루트를 찾으면 true 입니다.
         */
        bool initialize();

        /**
         * @brief 콘텐츠를 올립니다. 검색 우선순위 적용 → 시작 팩 마운트 → 에셋 레지스트리 적재 순서입니다.
         * @param listSearchPriority 검색 우선순위 토큰 목록. 비우면 지금 설정된 것을 그대로 씁니다.
         * @param source 쿠킹(`ContentSource::SourceTree`)이면 팩을 마운트하지 않고 느슨한 소스 파일을 읽게 합니다 — 배포 구성도 같습니다. 쿠킹의
         *               입력은 소스이고 팩은 그 산출물이라, 팩을 올리면 지난 빌드의 결과(엔진 데이터 · 프리팹 · GUID 표)를 다시 읽거나 첫 빌드에서는
         *               아무것도 읽지 못합니다.
         * @return 팩을 하나라도 마운트했으면 true 입니다(느슨한 `Resource/` 트리만 있으면 false).
         *
         * @details **`GameConfig::setActive` 뒤에 불러야 합니다.** "game" 토큰은 `GameConfig._packRoot`
         *          로 풀리므로, 그 전에 부르면 게임 도메인이 통째로 빠진 채 팩이 실리고 GUID 표도
         *          engine/common 만 채워집니다.
         *
         *          `initialize()` 와 나뉜 이유가 그것입니다 — "설정이 먼저" 라는 전제가 부르는 쪽에 드러나고,
         *          우선순위 적용과 마운트가 한 호출로 묶여 사이가 벌어지지 않습니다.
         */
        bool mountContent( const vector<string>& listSearchPriority, ContentSource source = ContentSource::Cooked );
        /** @brief 마지막 `mountContent` 가 올린 콘텐츠의 출처입니다. 마운트 전에는 `ContentSource::Cooked` 입니다. */
        ContentSource getContentSource() const { return _contentSource; }

        /** @brief 마운트된 팩을 내리고, 등록된 에셋 캐시와 GUID 표를 비웁니다. */
        void shutdown();
        /**
         * @brief 에셋 식별자(GUID) 표를 채웁니다. 반환값은 등록 수입니다.
         * @details 팩이면 쿠커가 도메인마다 넣는 `assetregistry.txt` 를 읽고(배포본은 .meta 를 싣지 않습니다), 하나도
         *          없으면(느슨한 트리) 리소스 루트의 `.meta` 를 훑습니다. 보통은 `mountContent` 가 부릅니다.
         *          전역 VFS 를 헤집은 테스트가 되돌릴 때 직접 부릅니다.
         */
        uint32 loadAssetRegistries();

        /**
         * @brief 실행 파일 옆(또는 프로젝트)의 `Packs/` 를 찾아 모두 마운트합니다.
         * @details 보통은 `mountContent` 가 부릅니다. 전역 VFS 를 헤집는 테스트가 시작 시점 상태로
         *          되돌릴 때도 이것을 씁니다. 후보 경로 목록을 두 곳에 복사해 두면 한쪽만 바뀝니다.
         * @return 하나라도 마운트했으면 true 입니다.
         */
        bool mountStartupPacks();

        /** @brief 스트리밍 큐의 완료 기록을 비웁니다. 캐시는 참조가 0 이 되는 자리에서 스스로 지우므로 여기서는 할 일이 없습니다. */
        void garbageCollectUnusedAssets();

        // ----------------------------------------------------------------------
        // 에셋 캐시 등록부: 종류를 늘리는 자리
        // ----------------------------------------------------------------------
        /**
         * @brief 경로 키 에셋 캐시를 등록부에 올립니다. 소유하지 않습니다.
         * @details 내장 셋(Material · Texture · Prefab)은 생성자가 등록합니다. 모듈이 자기 에셋
         *          종류를 더할 때 이것을 부릅니다. 그러면 종료 · 진단 · 재초기화가 **자동으로**
         *          그 캐시까지 훑습니다. 같은 포인터를 두 번 올리면 무시합니다.
         * @param pCache 매니저보다 오래 사는 캐시. nullptr 은 무시합니다.
         */
        void registerAssetCache( IAssetCache* pCache );
        /**
         * @brief 등록부에서 캐시를 내립니다. **모듈은 내려가기 전에 반드시 이것을 부릅니다.**
         * @details 등록부는 포인터만 듭니다. 모듈 DLL 이 내려가면 그 포인터도, 가상 함수 표도 같이
         *          사라집니다(CLAUDE.md 의 "Statics die on hot reload" 와 같은 자리입니다). 남겨 두면 다음
         *          종료 · 비우기가 죽은 코드로 뛰어듭니다.
         * @param pCache 등록했던 그 포인터. 등록된 적이 없으면 아무 일도 하지 않습니다.
         */
        void unregisterAssetCache( const IAssetCache* pCache );
        /** @brief 등록된 캐시 목록입니다. 소유하지 않습니다. */
        vector<IAssetCache*> getAllAssetCache() const;
        /**
         * @brief 종류 이름으로 캐시를 찾습니다("Material" · "Texture" · "Prefab"). 없으면 nullptr 입니다.
         * @details 이름으로 도는 코드(진단 · 도구)가 캐시 셋을 다시 적지 않게 하는 창구입니다.
         */
        IAssetCache* findAssetCache( string_view assetKindName ) const;
        /**
         * @brief 등록된 캐시를 모두 비웁니다.
         * @details `shutdown` 이 이것을 씁니다. 종료 경로가 캐시 이름을 손으로 적으면 하나가 빠져 재초기화 뒤에도 옛 내용이 남습니다.
         */
        void clearAssetCaches();
        /**
         * @brief 모듈이 올려 두고 내리지 않은 캐시를 이름으로 경고합니다. 종료가 부릅니다.
         * @details 포인터는 이미 죽었을 수 있어 **역참조하지 않습니다.** 등록 시점에 복사해 둔
         *          이름만 씁니다. 조용히 지나가면 다음 실행에서 같은 일이 또 일어납니다.
         */
        void warnAboutRemainingModuleCaches() const;

        /** @brief 언로드 리스너 목록의 이름입니다. */
        const utf8* getModuleUnloadListenerName() const override { return "asset caches"; }
        /**
         * @brief 객체나 vtable 이 [@p pBegin, @p pEnd) 안인 캐시(모듈이 올리고 내리지 않은 것)를 등록부에서 내립니다. 비우지 않습니다 —
         *        캐시는 그것을 만든 모듈이 소유하고, 이미지가 아직 올라 있는 동안 모듈이 스스로 지웁니다. 내장 캐시는 보지 않습니다.
         */
        uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped ) override;

        /** @brief VFS 에 팩을 마운트하는 리소스 팩 매니저를 반환합니다. */
        ResourcePackManager&       getPackManager();
        const ResourcePackManager& getPackManager() const;

        /** @brief 경로 ↔ GUID(.meta) 표를 반환합니다. */
        AssetDatabase&       getAssetDatabase() { return _assetDatabase; }
        const AssetDatabase& getAssetDatabase() const { return _assetDatabase; }

        /** @brief XML formatVersion 이관 등록부를 반환합니다. */
        AssetFormatRegistry&       getAssetFormatRegistry() { return _assetFormatRegistry; }
        const AssetFormatRegistry& getAssetFormatRegistry() const { return _assetFormatRegistry; }

        /** @brief 경로 키 Material 인스턴스 캐시(GPU 수명 포함)를 반환합니다. */
        MaterialCache&       getMaterialManager();
        const MaterialCache& getMaterialManager() const;
        /** @brief 경로 키 Texture2D 인스턴스 캐시(GPU 수명 포함)를 반환합니다. 머티리얼의 Texture2D 프로퍼티(assetPath)가 여기서 빌립니다. */
        TextureCache&       getTextureManager();
        const TextureCache& getTextureManager() const;

        /** @brief Prefab 로드 · 스폰 캐시를 반환합니다. */
        PrefabCache&       getPrefabCache();
        const PrefabCache& getPrefabCache() const;

    private:
        /** @brief 이 매니저가 소유한 내장 캐시(머티리얼 · 텍스처 · 프리팹 · 스프라이트 클립)인지 봅니다. 종료 경고 · 모듈 코드 걷기에서 뺍니다. */
        bool isBuiltInAssetCache( const IAssetCache* pCache ) const;

        AssetDatabase                   _assetDatabase;
        AssetFormatRegistry             _assetFormatRegistry;
        unique_ptr<MaterialCache>       _materialCache;
        unique_ptr<TextureCache>        _textureCache;
        unique_ptr<PrefabCache>         _prefabCache;
        unique_ptr<SpriteClipCache>     _spriteClipCache; ///< 등록부에 보이는 창구 — 표는 프로세스에 하나다
        unique_ptr<ResourcePackManager> _pPackManager;
        /**
         * @brief 등록된 캐시 목록입니다. 소유하지 않습니다(내장 넷은 위 멤버가, 모듈이 올린 것은 그 모듈이 소유합니다).
         * @details 종류 이름(`getAssetKindName`)은 **올릴 때** 복사합니다. 모듈이 자기 캐시를 내리지 않고 사라지면 그 포인터의 가상 함수 표도
         *          같이 사라지므로, 진단에서 이름을 다시 물으면 그 진단이 죽습니다. 사본이 있으면 무엇을 두고 갔는지 안전하게 말할 수 있습니다.
         *          같은 종류 이름의 둘째 캐시는 거절합니다(이름으로 찾는 쪽이 어느 것을 받을지 정해지지 않습니다).
         */
        RegistrationList<IAssetCache> _registeredAssetCache;
        /** @brief 마지막 `mountContent` 가 올린 콘텐츠의 출처입니다. */
        ContentSource _contentSource;
    };
} // namespace sw
