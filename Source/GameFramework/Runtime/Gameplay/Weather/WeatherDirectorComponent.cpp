#include "pch.h"

#include "Core/Foundation/Types.h"
#include "Core/Math/DeterministicRandom.h"
#include "Core/Memory/Memory.h"
#include "Core/String/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Asset/Cache/SharedAssetRef.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/Serialization/Reference/CatalogKey.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"
#include "GameFramework/Runtime/Gameplay/Weather/DecalRainWashSystem.h"
#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"

namespace sw
{
    struct WeatherDef;
    class WeatherCatalog;

    /**
     * @class WeatherDirectorComponent
     * @brief 판 상태를 날씨 카탈로그로 열고, 새 판이면 시작 날씨를 곧바로 정합니다(파일 머리말 참고).
     */
    REFLECT( Category = "Environment", DisplayName = "Weather Director", Tooltip = "Opens the session state on this object with a weather catalog (scenes without a kit director)" )
    class WeatherDirectorComponent : public Component
    {
    public:
        REFLECT_BODY();

        WeatherDirectorComponent();
        virtual ~WeatherDirectorComponent() override;

        /** @brief 카탈로그를 받아 판 상태를 엽니다. 카탈로그를 읽지 못하거나 판 상태가 없으면 오류를 남기고 열지 않습니다. 날씨가 열리면 비가 데칼을 씻는다(`DecalRainWashSystem`). */
        void onBeginPlay() override;
        /** @brief 데칼 비 씻김을 뗍니다. */
        void onEndPlay() override;

    private:
        PROPERTY( Category = "Weather", DisplayName = "Weather Data", Tooltip = "Weather catalog (*.weather.xml)" )
        AssetRef<WeatherCatalog> _weatherDataPath;
        PROPERTY( Category = "Weather", DisplayName = "Start Weather", Tooltip = "Weather set at once on a fresh game (empty: the catalog picks by season weights)" )
        CatalogKey<WeatherDef> _startWeather;
        PROPERTY( Category = "Weather", DisplayName = "Seed", Min = 1, Tooltip = "Seed of the weather sequence" )
        uint32 _seed;

        SharedAssetRef<WeatherCatalog> _pCatalog; ///< 판 상태가 빌려 쓴다 — 이 컴포넌트가 쥐고 있어야 한다
    };

    WeatherDirectorComponent::WeatherDirectorComponent()
        : _weatherDataPath{}
        , _startWeather{}
        , _seed{ DeterministicRandom32::kDefaultSeed }
        , _pCatalog{}
    {
        setCanEverTick( false );
    }

    WeatherDirectorComponent::~WeatherDirectorComponent() = default;

    void WeatherDirectorComponent::onBeginPlay()
    {
        Component::onBeginPlay();
        GameObject* pOwner     = getOwner();
        const utf8* pOwnerName = pOwner != nullptr ? pOwner->getName().c_str() : "?";
        _pCatalog              = _weatherDataPath.acquire();
        if ( _pCatalog == nullptr )
        {
            SW_LOG_ERROR_ID( "WeatherDirector.CatalogMissing", "'%#': weather catalog '%#' could not be read - the scene has no weather", pOwnerName, _weatherDataPath.path().c_str() );
            return;
        }
        GameStateSettings settings{};
        settings._pWeatherCatalog  = _pCatalog.snapshot();
        settings._weatherSeed      = _seed;
        GameStateComponent* pState = GameStateComponent::openOnOwner( *this, settings );
        if ( pState != nullptr )
        {
            if ( GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getMutableManager() : nullptr; pManager != nullptr )
                (void)DecalRainWashSystem::getOrCreate( *pManager ); // 강수를 가진 쪽이 비 씻김도 붙인다 — 데칼(엔진)은 날씨를 모른다
        }
        if ( pState == nullptr || pState->isFreshGame() == false || _startWeather.empty() )
            return; // 열지 못한 까닭은 판 상태가 남겼다. 복원한 판은 날씨도 복원했다
        pState->getWeather().forceWeather( _startWeather.key(), 0.0f, true );
        if ( pState->getWeather().getCurrent() != _startWeather.key() )
            SW_LOG_ERROR_ID( "WeatherDirector.StartWeatherUnknown", "'%#': start weather '%#' is not in '%#'", pOwnerName, _startWeather.c_str(), _weatherDataPath.path().c_str() );
    }

    void WeatherDirectorComponent::onEndPlay()
    {
        if ( GameObject* const pOwner = getOwner(); pOwner != nullptr && pOwner->getManager() != nullptr )
            DecalRainWashSystem::release( *pOwner->getMutableManager() );
        Component::onEndPlay();
    }
} // namespace sw

#include "WeatherDirectorComponent.cpp.gen.inl"
