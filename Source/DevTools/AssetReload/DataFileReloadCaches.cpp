/**
 * @file DataFileReloadCaches.cpp
 * @brief 엔진 서비스가 가진 데이터 파일(오디오 믹서 그래프 · 이벤트 라이브러리, 로컬라이제이션 테이블 · 프로젝트)을 에셋 캐시 레지스트리에 노출하는 리로드 어댑터들입니다.
 *        에디터 핫 리로드가 다른 에셋과 같은 경로로 다시 읽게 합니다. 이 파일 밖에서 이름으로 부르는 곳이 없어 헤더 없이 등록만 한다.
 * @details 파일 이름이 `Reload.cpp` 로 끝나면 `SW_DEV_ONLY_SOURCE_PATTERNS` 가 빼므로 `Caches` 로 끝낸다.
 *          두 어댑터 모두 상태가 없다 — 데이터는 서비스가 가지고, 어댑터는 바뀐 파일을 그 서비스에 넘긴다.
 *          `clear()` 는 아무것도 비우지 않는다 — 캐시 비우기(재초기화)가 소리 데이터나 화면의 텍스트를 지우면 안 되고 수명은 서비스가 정한다.
 */
#include "pch.h"

#include "Engine/Asset/Cache/BuiltInAssetCache.h"
#include "Engine/Asset/Cache/IAssetCache.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/FileSystem/AssetID.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Localization/LocalizationManager.h"

namespace sw
{
    namespace
    {
        /**
         * @class AudioReloadCache
         * @brief 오디오 데이터 파일(`*.audiomixer.xml`, `*.audioevents.xml`)의 어댑터입니다. 그래프와 라이브러리는 `IAudioService` 가 가집니다.
         * @details 에디터의 핫 리로드 경로 표가 이 접미사를 "경로를 든 캐시가 읽는다" 경로로 돌리면 `isCached` 를 아는 이 캐시가 받는다.
         */
        class AudioReloadCache final : public IAssetCache
        {
        public:
            /** @brief 조회가 없는 어댑터라 적중 · 실패를 세지 않습니다(`CacheTracking::uncounted`). */
            AudioReloadCache()
                : IAssetCache{ CacheTracking::uncounted( "adapter without lookups - the audio service owns the data" ) }
            {
            }

            /** @brief 레지스트리의 종류 이름("AudioData")입니다. */
            const utf8* getAssetKindName() const override { return "AudioData"; }

            /** @brief 그 경로가 지금 쓰는 믹서 그래프나 이벤트 라이브러리의 파일이면 true 입니다. */
            bool isCached( string_view relativePath ) const override
            {
                return engine::areEngineServicesBound() && engine::getSubsystem<IAudioService>().isDataFile( relativePath );
            }

            /** @brief 그 파일을 다시 읽어 바꿉니다(`IAudioService::reloadDataFile`). 실패하면 지금 것을 그대로 씁니다. 디바이스는 쓰지 않습니다. */
            void reload( const AssetID& assetID, IRHIDevice* /*pDevice*/ )
            {
                if ( engine::areEngineServicesBound() )
                    (void)engine::getSubsystem<IAudioService>().reloadDataFile( assetID ); // 실패는 서비스가 오류로 남기고 지금 것을 유지한다
            }

            /** @brief 서비스가 들고 있는 데이터 파일 수의 하한(믹서 그래프가 파일이면 1)입니다. */
            size_t getCachedCount() const override
            {
                return engine::areEngineServicesBound() && engine::getSubsystem<IAudioService>().getMixerPath().empty() == false ? 1u : 0u;
            }

            /** @brief 아무것도 하지 않습니다(파일 설명). */
            void clear() override {}
        };

        /**
         * @class LocalizationReloadCache
         * @brief 로컬라이제이션 파일(원문 테이블, 번역 테이블, 프로젝트)의 어댑터입니다. 테이블은 엔진 서비스 `LocalizationManager` 가 가집니다.
         */
        class LocalizationReloadCache final : public IAssetCache
        {
        public:
            /** @brief 조회가 없는 어댑터라 적중 · 실패를 세지 않습니다(`CacheTracking::uncounted`). */
            LocalizationReloadCache()
                : IAssetCache{ CacheTracking::uncounted( "adapter without lookups - LocalizationManager owns the tables" ) }
            {
            }

            /** @brief 레지스트리의 종류 이름("StringTable")입니다. */
            const utf8* getAssetKindName() const override { return "StringTable"; }

            /** @brief 그 경로가 로드한 프로젝트의 파일이면 true 입니다. */
            bool isCached( string_view relativePath ) const override
            {
                return engine::areEngineServicesBound() && engine::getSubsystem<LocalizationManager>().isProjectFile( relativePath );
            }

            /** @brief 그 파일이 든 프로젝트를 다시 읽습니다(`LocalizationManager::reloadChangedFile`). 디바이스는 쓰지 않습니다. */
            void reload( const AssetID& assetID, IRHIDevice* /*pDevice*/ )
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
    SW_BUILT_IN_ASSET_CACHE( AudioReloadCache );
    SW_BUILT_IN_ASSET_CACHE( LocalizationReloadCache );
} // namespace sw
