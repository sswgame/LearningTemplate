/**
 * @file DataFileReloadCaches.cpp
 * @brief 엔진 서비스가 가진 데이터 파일(로컬라이제이션 테이블 · 프로젝트)을 에셋 캐시 레지스트리에 노출하는 리로드 어댑터입니다. 오디오 데이터는 클라이언트 쪽(`Client/AssetReload/AudioReloadCache.cpp`)에 있다.
 *        에디터 핫 리로드가 다른 에셋과 같은 경로로 다시 읽게 합니다. 이 파일 밖에서 이름으로 부르는 곳이 없어 헤더 없이 등록만 한다.
 * @details 파일 이름이 `Reload.cpp` 로 끝나면 `SW_DEV_ONLY_SOURCE_PATTERNS` 가 빼므로 `Caches` 로 끝낸다.
 *          어댑터는 상태가 없다 — 데이터는 서비스가 가지고, 어댑터는 바뀐 파일을 그 서비스에 넘긴다.
 *          `clear()` 는 아무것도 비우지 않는다 — 캐시 비우기(재초기화)가 화면의 텍스트를 지우면 안 되고 수명은 서비스가 정한다.
 */
#include "pch.h"

#include "Engine/Asset/Cache/AssetCacheBase.h"
#include "Engine/Asset/Cache/BuiltInAssetCache.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"

namespace sw
{
    namespace
    {
        /**
         * @class LocalizationReloadCache
         * @brief 로컬라이제이션 파일(원문 테이블, 번역 테이블, 프로젝트)의 어댑터입니다. 테이블은 엔진 서비스 `LocalizationManager` 가 가집니다.
         */
        class LocalizationReloadCache final : public AssetCacheBase
        {
        public:
            /** @brief 조회가 없는 어댑터라 적중 · 실패를 세지 않습니다(`CacheTracking::uncounted`). */
            LocalizationReloadCache()
                : AssetCacheBase{ CacheTracking::uncounted( "adapter without lookups - LocalizationManager owns the tables" ) }
            {
            }

            /** @brief 레지스트리의 종류 이름("StringTable")입니다. */
            const utf8* getAssetKindName() const override { return "StringTable"; }

            /** @brief 그 경로가 로드한 프로젝트의 파일이면 true 입니다. */
            bool isCached( string_view relativePath ) const override
            {
                return engine::areEngineServicesBound() && engine::getSubsystem<LocalizationManager>().isProjectFile( relativePath );
            }

            /** @brief 그 파일이 든 프로젝트를 다시 읽습니다(`LocalizationManager::reloadChangedFile`). */
            void reload( const AssetID& assetID )
            {
                const string_view relativePath = assetID.view();
                if ( engine::areEngineServicesBound() )
                    (void)engine::getSubsystem<LocalizationManager>().reloadChangedFile( relativePath ); // Failed 는 관리자가 오류로 남겼다 — 캐시 `reload` 는 결과를 돌려주지 않는다
            }

            /** @brief 로드한 프로젝트 개수입니다. */
            size_t getCachedCount() const override
            {
                return engine::areEngineServicesBound() ? engine::getSubsystem<LocalizationManager>().getMountedProjectNames().size() : 0u;
            }

            /** @brief 아무것도 하지 않습니다(파일 설명). */
            void clear() override {}
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( LocalizationReloadCache );
} // namespace sw
