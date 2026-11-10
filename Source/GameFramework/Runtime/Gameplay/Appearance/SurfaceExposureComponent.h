/**
 * @file SurfaceExposureComponent.h
 * @brief 날씨 · 물이 캐릭터 몸 표면을 바꿉니다. 비(날씨 값)와 물속 깊이가 서로 다른 입력으로 `Wet` 를 올리고, 물은 씻김 채널(흙 · 피)을 씻습니다.
 * @details 같은 오브젝트의 외형(`CharacterAppearanceComponent`)이 가진 표면 상태(`CharacterSurfaceState`)에 씁니다 — 머티리얼로 올리기와 마르기(채널의 초당 감쇠)는 외형이 한다.
 *          - **비**: 씬의 첫 열린 판 상태(`SessionStateComponent`)의 날씨 값 `_precipitationValue`(기본 `wetness`)에 `_rainWetPerSecond` 를 곱한다. 머리 위로 쏜 광선이
 *            `_shelterDistance` 안에서 무엇을 맞으면 지붕 아래라 비를 맞지 않는다(노출 데이터가 없는 실내 판정의 첫 단계 — 0 이면 보지 않는다). 날씨가 없는 씬은 비가 없다.
 *          - **물**: 발 자리가 물(`WaterBodyComponent`)의 수면 아래면 잠긴 비율 = 깊이 ÷ `_bodyHeight`(0..1)이다. 젖음은 `_immersionWetPerSecond` × 비율로 오르고,
 *            씻김은 그 비율로 `CharacterSurfaceState::applyWater` 가 한다(발목 · 허리 · 전신에서 빠르기가 다르다). 몸 전체가 같은 빠르기로 젖는 시작점이고
 *            부위별 침수 · 옷의 방수는 요구가 생길 때 영역마다 높이를 두어 넓힌다.
 *          - **시간**: 틱의 게임 초(일시정지 · 시간 배율과 같다). 틱은 시간만 쌓고, 물리 질의와 표면 쓰기는 표면 노출 프레임 시스템(`SurfaceExposureSystem`)이
 *            틱 뒤 게임 스레드에서 등록 순서로 한 번에 한다(`applyExposure`).
 *          언리얼에는 이 자리의 내장 기능이 없고(게임이 Material Parameter Collection 에 젖음을 쓴다) 유니티 HDRP 의 Wetness 도 머티리얼 값만 준다 — 값을 정하는 규칙은 이 컴포넌트다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class SurfaceExposureComponent
     * @brief 비와 물이 몸 표면(젖음 · 씻김)을 바꾸는 규칙입니다(파일 머리말 참고). 외형 컴포넌트와 같은 오브젝트에 둡니다.
     */
    REFLECT( Category = "Character", DisplayName = "Surface Exposure", Tooltip = "Rain and water change the body surface: wetness rises, water washes dirt and blood" )
    class SW_GF_API SurfaceExposureComponent : public Component
    {
    public:
        REFLECT_BODY();

        SurfaceExposureComponent();
        virtual ~SurfaceExposureComponent() override = default;

        /** @brief 씬의 컴포넌트 레지스트리에 오르고 표면 노출 프레임 시스템(`SurfaceExposureSystem`)을 붙입니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 레지스트리에서 내리고, 마지막 노출 컴포넌트면 시스템을 뗍니다. */
        void onUnregister( GameObjectManager& manager ) override;
        /** @brief 게임 초를 쌓기만 합니다 — 적용은 시스템이 틱 뒤에 한다. */
        void onTick( TickContext& context ) override;

        /** @brief 쌓인 게임 초만큼 비 · 물을 몸 표면에 적용합니다(`SurfaceExposureSystem` 이 틱 뒤 게임 스레드에서 부른다, 테스트도 부른다). 쌓인 시간이 없으면 아무것도 하지 않는다. */
        void applyExposure( GameObjectManager& manager );

        /** @brief 마지막으로 잰 잠긴 비율(0..1)입니다. */
        float32 getSubmersion() const { return _submersion; }
        /** @brief 마지막으로 잰 비 노출(날씨 값, 지붕 아래면 0)입니다. */
        float32 getRainExposure() const { return _rainExposure; }
        /** @brief 마지막으로 잰 발 자리의 물 깊이(m)입니다. */
        float32 getWaterDepth() const { return _waterDepth; }

    private:
        PROPERTY( Required, Category = "Surface", DisplayName = "Wet Channel", Tooltip = "Surface channel rain and water raise" )
        hashed_string _wetChannel;
        PROPERTY( Required, Category = "Rain", DisplayName = "Precipitation Value", Tooltip = "Weather value read as rain strength (WeatherCatalog <Values>)" )
        hashed_string _precipitationValue;
        PROPERTY( Category = "Rain", DisplayName = "Rain Wet Rate", Min = 0.0, Tooltip = "Wetness gained per second at precipitation 1 while under the open sky" )
        float32 _rainWetPerSecond;
        PROPERTY( Category = "Rain", DisplayName = "Shelter Distance", Min = 0.0, Units = m, Tooltip = "A hit within this distance straight above the head keeps the rain off (0 = no shelter test)" )
        float32 _shelterDistance;
        PROPERTY( Category = "Water", DisplayName = "Immersion Wet Rate", Min = 0.0, Tooltip = "Wetness gained per second when fully under water (scaled by the submerged fraction)" )
        float32 _immersionWetPerSecond;
        PROPERTY( Category = "Water", DisplayName = "Body Height", Min = 0.1, Units = m, Tooltip = "Height that counts as fully submerged (water depth at the feet / this = submerged fraction)" )
        float32 _bodyHeight;

        float32 _pendingSeconds; ///< 틱이 쌓고 틱 뒤 적용이 쓰는 게임 초
        float32 _submersion;
        float32 _rainExposure;
        float32 _waterDepth;
    };
} // namespace sw
