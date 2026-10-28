/**
 * @file AssetManager.h
 * @brief 팩 에셋(GUID, 스키마, Material, Texture, Prefab, Mesh)을 한곳에서 관리하는 파사드입니다. `Engine` 가 `EngineServiceCollection` 으로 소유합니다.
 * @note
 *   포함: AssetDatabase, PackManager, 에셋 캐시 레지스트리.
 *   기능 캐시(Material, Texture, Prefab, Mesh, Skeleton, Fracture 등)는 **각 기능 폴더가 등록합니다**(`SW_BUILT_IN_ASSET_CACHE`, `Cache/BuiltInAssetCache.h`).
 *   매니저는 생성자에서 등록 테이블을 훑어 소유 캐시를 만들고, 코드로 만드는 값 테이블(`WeakInternCache`)은 빌려 와서 등록합니다. 타입으로 찾을 때는 `getAssetCache<T>()` 를 씁니다.
 *   제외(수명이 다름):
 *   - AssetPaths: Content/ 경로 해석만 합니다(소유권 없음)
 *   - ShaderCache: 셰이더 컴파일 결과 캐시(RHI)
 *   - RenderPipelineAssetCache: FrameRenderer 가 소유
 *   - ConfigManager: Config/ 호스트 JSON(Content/ 아님)
 *   - StringTable, SceneManager
 *   - 파일 감시와 에셋 핫 리로드: **개발 기능이라 에디터가 소유**합니다(Editor/Common/Workspace).
 *     배포본에는 에디터가 없으므로 감시 스레드도 없습니다.
 */
#pragma once
#include "Core/Common/Macros.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Subscription.h"
#include "Core/Memory/Memory.h"
#include "Core/Module/ModuleImageUtil.h"
#include "Core/String/RegistrationList.h"
#include "Core/Subsystem/ISubsystem.h"

#include "Engine/Asset/AssetDatabase.h"
#include "Engine/Asset/AssetFormat.h"
#include "Engine/Asset/Cache/IAssetCache.h"

namespace sw
{
    class PackManager;

    /** @brief `AssetManager::mountContent` 가 무엇을 콘텐츠로 올릴지 정합니다. */
    enum class ContentSource : uint8
    {
        Cooked,     ///< 실행용이다. 쿠킹한 팩을 마운트하고, 느슨한 `Content/` 파일은 개발 구성에서만 읽는다
        SourceTree, ///< 쿠킹용이다. 소스 트리(`Content/`)를 읽고, 팩(지난 쿠킹의 산출물)은 마운트하지 않으며, 프리팹도 소스(XML, JSON)를 읽는다
    };

    /**
     * @class AssetManager
     * @brief 에셋 식별자, 스키마, 인스턴스 캐시와 VFS 팩 매니저를 한 객체로 소유합니다.
     */
    class SW_API AssetManager final : public ISubsystem, private SubscriptionSource
    {
    public:
        /** @brief 빈 매니저로 만듭니다. initialize() 전에 바인딩해도 됩니다. */
        AssetManager();
        /** @brief 캐시를 해제합니다. */
        ~AssetManager() override;

        AssetManager( const AssetManager& )            = delete;
        AssetManager& operator=( const AssetManager& ) = delete;

        /**
         * @brief Content/ 검색 루트를 잡고 서비스를 준비합니다. 팩 마운트는 `mountContent` 가 합니다.
         * @return 프로젝트 루트를 찾으면 true 입니다.
         */
        [[nodiscard]] bool initialize();

        /**
         * @brief 콘텐츠를 로드합니다. 검색 우선순위 적용, 시작 팩 마운트, 에셋 레지스트리 적재 순서로 진행합니다.
         * @param listSearchPriority 검색 우선순위 토큰 목록. 비우면 지금 설정된 것을 그대로 씁니다.
         * @param source 쿠킹(`ContentSource::SourceTree`)이면 팩을 마운트하지 않고 느슨한 소스 파일을 읽게 합니다. 배포 구성도 같습니다. 쿠킹의
         *               입력은 소스이고 팩은 그 산출물이라, 팩을 올리면 지난 빌드의 결과(엔진 데이터, 프리팹, GUID 테이블)를 다시 읽거나 첫 빌드에서는
         *               아무것도 읽지 못합니다.
         * @return 팩을 하나라도 마운트했으면 true 입니다(느슨한 `Content/` 트리만 있으면 false).
         *
         * @details **호스트 게임 프리셋(`ConfigManager` 가 `GameConfig` 를 읽은 뒤)에 불러야 한다.** "game" 토큰은 `GameConfig._packRoot`
         *          로 풀리므로, 그 전에 부르면 게임 도메인이 통째로 빠진 채 팩이 로드되고 GUID 테이블도
         *          engine 과 common 만 채워진다.
         *
         *          `initialize()` 와 나눈 이유가 그것이다. "설정이 먼저"라는 전제가 부르는 쪽에 드러나고,
         *          우선순위 적용과 마운트가 한 호출로 묶여 사이가 벌어지지 않는다.
         */
        [[nodiscard]] bool mountContent( const vector<string>& listSearchPriority, ContentSource source = ContentSource::Cooked );
        /** @brief 마지막 `mountContent` 가 로드한 콘텐츠의 출처입니다. 마운트 전에는 `ContentSource::Cooked` 입니다. */
        ContentSource getContentSource() const { return _contentSource; }

        /** @brief 마운트된 팩을 언마운트하고, 등록된 에셋 캐시와 GUID 테이블을 비웁니다. */
        void shutdown();

        /** @brief 서브시스템 이름입니다(다른 선언의 의존 칸이 이 상수를 가리킨다). */
        static constexpr const utf8* kSubsystemName = "Asset";
        /**
         * @brief 기동 순서의 자리 — 리소스 루트를 잡고 활성 엔진 설정의 우선순위로 콘텐츠를 마운트합니다(`-cook-scenes` 면 소스 트리 — 팩은 쿠킹의 산출물이다).
         * @details 팩 루트는 GameConfig, 팩 스트림은 코덱이다. 종료(`shutdown`)가 에셋 캐시를 비운다 — 에셋을 드는 서브시스템이 모두 이 뒤에 서므로,
         *          역순 해제에서 그들의 소멸자가 에셋을 놓은 뒤다.
         */
        SubsystemInitResult     initializeSubsystem() override;
        void                    shutdownSubsystem() override { shutdown(); }
        const utf8*             getSubsystemName() const override { return kSubsystemName; }
        span<const utf8* const> getSubsystemDependencies() const override;
        MemoryTag               getSubsystemMemoryTag() const override { return MemoryTag::Asset; }
        /**
         * @brief 에셋 식별자(GUID) 테이블을 채웁니다. 반환값은 등록 수입니다.
         * @details 팩이면 쿠커가 도메인마다 넣는 `assetregistry.txt` 를 읽고(배포본은 .meta 를 싣지 않는다), 하나도
         *          없으면(느슨한 트리) 리소스 루트의 `.meta` 를 훑는다. 보통은 `mountContent` 가 부른다.
         *          전역 VFS 를 건드린 테스트가 되돌릴 때 직접 부른다.
         */
        uint32 loadAssetRegistries();

        /**
         * @brief 실행 파일 옆(또는 프로젝트)의 `Packs/` 를 찾아 모두 마운트합니다.
         * @details 보통은 `mountContent` 가 부른다. 전역 VFS 를 건드리는 테스트가 시작 시점 상태로
         *          되돌릴 때도 이 함수를 쓴다. 후보 경로 목록을 두 곳에 복사해 두면 한쪽만 바뀌기 때문이다.
         * @return 하나라도 마운트했으면 true 입니다.
         */
        [[nodiscard]] bool mountStartupPacks();

        /** @brief 스트리밍 큐의 완료 기록을 비웁니다. 캐시는 참조가 0 이 되는 시점에 스스로 지우므로 여기서는 할 일이 없습니다. */
        void garbageCollectUnusedAssets();

        // ----------------------------------------------------------------------
        // 에셋 캐시 레지스트리: 종류를 추가하는 곳
        // ----------------------------------------------------------------------
        /**
         * @brief 경로 키 에셋 캐시를 레지스트리에 등록하고 그 구독을 돌려줍니다. 소유하지 않습니다. 구독이 사라지면 빠집니다.
         * @details 내장 캐시(기능 폴더가 `SW_BUILT_IN_ASSET_CACHE` 로 등록한 것)는 생성자가 등록한다. 모듈이 자기 에셋
         *          종류를 추가할 때 이 함수를 부른다. 그러면 종료, 진단, 재초기화가 **자동으로** 그 캐시까지 훑는다.
         *          **모듈은 언로드되기 전에 구독을 놓는다** — 레지스트리는 포인터만 가지고 있어 모듈 DLL 이 내려가면 포인터도,
         *          가상 함수 테이블도 같이 사라진다(남기면 다음 종료나 비우기가 사라진 코드로 점프한다. 언로드 훑기가 마지막 그물이다).
         *          nullptr · 같은 포인터 · 같은 종류 이름의 둘째 캐시면 빈 구독입니다.
         * @param pCache 구독보다 오래 사는 캐시.
         */
        [[nodiscard]] Subscription registerAssetCache( IAssetCache* pCache );
        /** @brief 등록된 캐시 목록을 반환합니다. 소유하지 않습니다. */
        vector<IAssetCache*> getAllAssetCache() const;
        /**
         * @brief 종류 이름으로 캐시를 찾습니다("Material", "Texture", "Prefab"). 없으면 nullptr 입니다.
         * @details 이름으로 도는 코드(진단, 도구)가 캐시 종류를 다시 나열하지 않게 하는 함수다.
         */
        IAssetCache* findAssetCache( string_view assetKindName ) const;
        /**
         * @brief 내장 캐시를 타입으로 받습니다(`getAssetCache<MaterialCache>()`). 타입은 `kAssetKindName` 을 제공하고, 부르는 쪽이 그 헤더를 include 합니다.
         * @details 언리얼의 `GetSubsystem<T>()` 에 해당한다. `Resource` 가 기능 캐시 타입을 모르므로 타입을 아는 쪽이 고른다. 내장 캐시는
         *          매니저와 수명이 같아 항상 있다(없으면 단언한다).
         */
        template <typename TCache>
        TCache& getAssetCache()
        {
            return static_cast<TCache&>( getBuiltInAssetCache( TCache::kAssetKindName ) );
        }
        template <typename TCache>
        const TCache& getAssetCache() const
        {
            return static_cast<const TCache&>( getBuiltInAssetCache( TCache::kAssetKindName ) );
        }
        /**
         * @brief 경로의 종류를 다루는 캐시에 로드를 걸고 그 에셋을 붙드는 토큰을 받습니다(`IAssetCache::pinAsset`). 고정하는 캐시가 없는 종류면 nullptr 입니다.
         * @details 프리로드 세트의 고정(`AssetPinSet`)이 쓴다. 토큰은 이 매니저보다 먼저 놓는다.
         */
        shared_ptr<const void> pinAsset( string_view relativePath );
        /**
         * @brief 등록된 캐시를 모두 비웁니다.
         * @details `shutdown` 이 이 함수를 쓴다. 종료 경로가 캐시 이름을 직접 나열하면 하나가 빠져 재초기화 뒤에도 이전 내용이 남는다.
         */
        void clearAssetCaches();
        /**
         * @brief 모듈이 등록해 두고 제거하지 않은 캐시를 이름으로 경고합니다. 종료 때 부릅니다.
         * @details 포인터는 이미 죽었을 수 있어 **역참조하지 않는다.** 등록할 때 복사해 둔
         *          이름만 쓴다. 조용히 지나가면 다음 실행에서 같은 일이 또 일어난다.
         */
        void warnAboutRemainingModuleCaches() const;

        /**
         * @brief 객체나 vtable 이 [@p pBegin, @p pEnd) 안인 캐시(모듈이 등록하고 제거하지 않은 것)를 레지스트리에서 제거합니다. 비우지는 않습니다.
         *        캐시는 그것을 만든 모듈이 소유하고, 이미지가 아직 로드되어 있는 동안 모듈이 스스로 지웁니다. 내장 캐시는 보지 않습니다.
         */
        uint32 onModuleUnloading( const void* pBegin, const void* pEnd, bool& outKeepImageMapped );

        /** @brief VFS 에 팩을 마운트하는 리소스 팩 매니저를 반환합니다. */
        PackManager&       getPackManager();
        const PackManager& getPackManager() const;

        /** @brief 경로와 GUID(.meta)를 대응시키는 테이블을 반환합니다. */
        AssetDatabase&       getAssetDatabase() { return _assetDatabase; }
        const AssetDatabase& getAssetDatabase() const { return _assetDatabase; }

    private:
        /** @brief 내장 캐시(`_listBuiltInAssetCache`)인지 확인합니다. 종료 경고와 모듈 코드 순회에서 제외합니다. */
        bool isBuiltInAssetCache( const IAssetCache* pCache ) const;
        /** @brief 캐시를 목록에 올립니다(이름은 지금 복사). 올렸으면 true 입니다. 내장 캐시는 구독 없이 이것으로 올린다 — 매니저와 수명이 같다. */
        bool addAssetCache( IAssetCache* pCache );
        /** @brief 캐시 등록 구독이 풀렸습니다(@p id 는 캐시 주소). 목록에서 뺍니다. 언로드 훑기가 먼저 뺐으면 할 일이 없다. */
        void releaseSubscription( uint64 id ) override;
        /** @brief `getAssetCache<T>()` 의 본체입니다. 종류 이름으로 내장 캐시를 찾습니다. 없으면 단언합니다(등록하지 않은 타입을 물은 경우). */
        IAssetCache& getBuiltInAssetCache( string_view assetKindName ) const;

        AssetDatabase                   _assetDatabase;
        vector<unique_ptr<IAssetCache>> _listOwnedAssetCache; ///< 기능 폴더가 등록한 소유 캐시(`SW_BUILT_IN_ASSET_CACHE`)다. 매니저마다 하나씩 가진다
        unique_ptr<PackManager>         _pPackManager;
        vector<IAssetCache*>            _listBuiltInAssetCache; ///< 내장 캐시 전부다. 소유 캐시와, 빌려 온 공유 캐시(Engine.dll 의 정적 값 테이블)를 종류 이름 순으로 올린다
        /**
         * @brief 등록된 캐시 목록입니다. 소유하지 않습니다(내장 캐시는 위 멤버가, 모듈이 등록한 것은 그 모듈이 소유합니다).
         * @details 종류 이름(`getAssetKindName`)은 **등록할 때** 복사한다. 모듈이 자기 캐시를 제거하지 않고 사라지면 그 포인터의 가상 함수 테이블도
         *          같이 사라지므로, 진단에서 이름을 다시 물으면 그 진단이 죽는다. 사본이 있으면 무엇을 두고 갔는지 안전하게 말할 수 있다.
         *          같은 종류 이름의 둘째 캐시는 거절한다(이름으로 찾는 쪽이 어느 것을 받을지 정해지지 않는다).
         */
        RegistrationList<IAssetCache> _registeredAssetCache;
        /** @brief 마지막 `mountContent` 가 로드한 콘텐츠의 출처입니다. */
        ContentSource _contentSource;

    private:
        /** @brief 모듈 언로드 처리기 구독(`ModuleImageUtil::registerUnloadHandler`)입니다. 맨 끝 멤버라 먼저 사라져, 다른 멤버가 사라지는 동안 불리지 않는다. */
        Subscription _moduleUnloadSubscription{ ModuleImageUtil::registerUnloadHandler( { "asset caches" }, ModuleUnloadFunction::create<&AssetManager::onModuleUnloading>( this ) ) };
    };
} // namespace sw
