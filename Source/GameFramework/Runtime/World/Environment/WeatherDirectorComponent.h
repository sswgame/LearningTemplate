/**
 * @file WeatherDirectorComponent.h
 * @brief 키트 디렉터 없이 씬에 날씨를 여는 컴포넌트입니다. 같은 오브젝트의 판 상태(`SessionStateComponent`)를 날씨 카탈로그로 엽니다.
 * @details 날씨는 판 상태 하나가 소유한다(`WeatherSystem` — 세이브 · 시계 배율 · 계절이 거기 있다). 키트 게임은 디렉터가 판 상태를 열며 카탈로그를 넘기고,
 *          디렉터가 없는 씬(시연 · 표면 상호작용 무대)은 이 컴포넌트가 같은 일을 한다 — 날씨를 소유하는 둘째 객체를 만들지 않는다.
 *          판 상태 다음, 같은 오브젝트에 붙인다(`SessionStateComponent` 의 "여는 쪽" 규칙). 날씨 값은 `SurfaceExposureComponent`(비 → 젖음) · 게임 규칙이 읽는다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Asset/AssetRef.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class WeatherCatalog;

    /**
     * @class WeatherDirectorComponent
     * @brief 판 상태를 날씨 카탈로그로 열고, 새 판이면 시작 날씨를 곧바로 정합니다(파일 머리말 참고).
     */
    REFLECT( Category = "Environment", DisplayName = "Weather Director", Tooltip = "Opens the session state on this object with a weather catalog (scenes without a kit director)" )
    class SW_GF_API WeatherDirectorComponent : public Component
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
        hashed_string _startWeather;
        PROPERTY( Category = "Weather", DisplayName = "Seed", Min = 1, Tooltip = "Seed of the weather sequence" )
        uint32 _seed;

        shared_ptr<const WeatherCatalog> _pCatalog; ///< 판 상태가 빌려 쓴다 — 이 컴포넌트가 쥐고 있어야 한다
    };
} // namespace sw
