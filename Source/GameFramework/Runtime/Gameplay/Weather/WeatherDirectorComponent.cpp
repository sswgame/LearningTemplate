#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Weather/WeatherDirectorComponent.h"

#include "Core/Math/DeterministicRandom.h"

#include "Engine/Asset/Cache/SharedAssetCache.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"

#include "GameFramework/Runtime/Gameplay/SessionState/SessionStateComponent.h"
#include "GameFramework/Runtime/Gameplay/Weather/DecalRainWashSystem.h"
#include "GameFramework/Runtime/World/Environment/WeatherSystem.h"

namespace sw
{
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
        SessionStateSettings settings{};
        settings._pWeatherCatalog     = _pCatalog.get();
        settings._weatherSeed         = _seed;
        SessionStateComponent* pState = SessionStateComponent::openOnOwner( *this, settings );
        if ( pState != nullptr )
        {
            if ( GameObjectManager* const pManager = pOwner != nullptr ? pOwner->getManager() : nullptr; pManager != nullptr )
                (void)DecalRainWashSystem::ensureFor( *pManager ); // 강수를 가진 쪽이 비 씻김도 붙인다 — 데칼(엔진)은 날씨를 모른다
        }
        if ( pState == nullptr || pState->isFreshGame() == false || _startWeather.empty() )
            return; // 열지 못한 까닭은 판 상태가 남겼다. 복원한 판은 날씨도 복원했다
        pState->getWeather().forceWeather( _startWeather, 0.0f, true );
        if ( pState->getWeather().getCurrent() != _startWeather )
            SW_LOG_ERROR_ID( "WeatherDirector.StartWeatherUnknown", "'%#': start weather '%#' is not in '%#'", pOwnerName, _startWeather.c_str(), _weatherDataPath.path().c_str() );
    }

    void WeatherDirectorComponent::onEndPlay()
    {
        if ( GameObject* const pOwner = getOwner(); pOwner != nullptr && pOwner->getManager() != nullptr )
            DecalRainWashSystem::release( *pOwner->getManager() );
        Component::onEndPlay();
    }
} // namespace sw
