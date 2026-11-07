/**
 * @file AudioReloadCache.cpp
 * @brief 오디오 서비스가 가진 데이터 파일(믹서 그래프 · 이벤트 라이브러리 · 소리 그래프)을 에셋 캐시 레지스트리에 노출하는 리로드 어댑터입니다.
 *        에디터 핫 리로드가 다른 에셋과 같은 경로로 다시 읽게 합니다. 이 파일 밖에서 이름으로 부르는 곳이 없어 헤더 없이 등록만 한다.
 * @details 오디오를 짓지 않는 전용 서버는 이 폴더(`Client/`)도 짓지 않는다. 어댑터는 상태가 없다 — 데이터는 서비스가 가지고, 바뀐 파일을 서비스에 넘긴다.
 *          `clear()` 는 아무것도 비우지 않는다 — 캐시 비우기(재초기화)가 소리 데이터를 지우면 안 되고 수명은 서비스가 정한다.
 */
#include "pch.h"

#include "Engine/Asset/Cache/AssetCacheBase.h"
#include "Engine/Asset/Cache/BuiltInAssetCache.h"
#include "Engine/Audio/IAudioService.h"
#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/EngineServices.h"

namespace sw
{
    namespace
    {
        /**
         * @class AudioReloadCache
         * @brief 오디오 데이터 파일(`*.audiomixer.xml`, `*.audioevents.xml`)의 어댑터입니다. 그래프와 라이브러리는 `IAudioService` 가 가집니다.
         * @details 에디터의 핫 리로드 경로 표가 이 접미사를 "경로를 든 캐시가 읽는다" 경로로 돌리면 `isCached` 를 아는 이 캐시가 받는다.
         */
        class AudioReloadCache final : public AssetCacheBase
        {
        public:
            /** @brief 조회가 없는 어댑터라 적중 · 실패를 세지 않습니다(`CacheTracking::uncounted`). */
            AudioReloadCache()
                : AssetCacheBase{ CacheTracking::uncounted( "adapter without lookups - the audio service owns the data" ) }
            {
            }

            /** @brief 레지스트리의 종류 이름("AudioData")입니다. */
            const utf8* getAssetKindName() const override { return "AudioData"; }

            /** @brief 그 경로가 지금 쓰는 믹서 그래프나 이벤트 라이브러리의 파일이면 true 입니다. */
            bool isCached( string_view relativePath ) const override
            {
                const IAudioService* pAudio = engine::findSubsystem<IAudioService>();
                return pAudio != nullptr && pAudio->isDataFile( relativePath );
            }

            /** @brief 그 파일을 다시 읽어 바꿉니다(`IAudioService::reloadDataFile`). 실패하면 지금 것을 그대로 씁니다. */
            void reload( const AssetID& assetID )
            {
                if ( IAudioService* pAudio = engine::findSubsystem<IAudioService>() )
                    (void)pAudio->reloadDataFile( assetID ); // 실패는 서비스가 오류로 남기고 지금 것을 유지한다
            }

            /** @brief 서비스가 들고 있는 데이터 파일 수의 하한(믹서 그래프가 파일이면 1)입니다. */
            size_t getCachedCount() const override
            {
                const IAudioService* pAudio = engine::findSubsystem<IAudioService>();
                return pAudio != nullptr && pAudio->getMixerPath().empty() == false ? 1u : 0u;
            }

            /** @brief 아무것도 하지 않습니다(파일 설명). */
            void clear() override {}
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_BUILT_IN_ASSET_CACHE( AudioReloadCache );
} // namespace sw
