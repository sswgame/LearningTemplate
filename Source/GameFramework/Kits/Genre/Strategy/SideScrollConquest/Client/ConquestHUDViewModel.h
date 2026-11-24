/**
 * @file ConquestHUDViewModel.h
 * @brief 정복 HUD 문서가 보이는 값입니다 — 클라이언트 뷰 컴포넌트는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다. 글은 문서의 번역 키다(뷰모델은 수와 보임만 든다).
 */
#pragma once
#include "Core/Foundation/Types.h"

#include "Engine/Presentation/UIViewModel.h"
#include "Engine/Presentation/WidgetVisibility.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class ConquestHUDViewModel
     * @brief 정복 HUD 의 뷰모델입니다. `ConquestViewComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 바인딩은 바뀐 칸만 씁니다. 게임 스레드만.
     */
    REFLECT( DisplayName = "SideScrollConquest HUD View Model" )
    class SW_KIT_CLIENT_API ConquestHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        ConquestHUDViewModel();
        ~ConquestHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 내 거점 수입니다. */
        void setTerritory( int32 territory );
        /** @brief 모든 거점 수입니다. */
        void setSiteCount( int32 siteCount );
        /** @brief 부대원 수입니다. */
        void setSquad( int32 squad );
        /** @brief 인구입니다(훈련 대기 포함). */
        void setPopulation( int32 population );
        /** @brief 인구 한도입니다. */
        void setPopulationCap( int32 populationCap );
        /** @brief 훈련에 쓰는 자원입니다. */
        void setResource( int32 resource );
        /** @brief 지휘관 체력 비율(0..1, 막대)입니다. */
        void setHealthRatio( float32 healthRatio );
        /** @brief 승리 표시를 보이거나 숨깁니다. */
        void setWonShown( bool bShown );
        /** @brief 패배 표시를 보이거나 숨깁니다. */
        void setLostShown( bool bShown );

        int32            getTerritory() const { return _territory; }
        int32            getSiteCount() const { return _siteCount; }
        int32            getSquad() const { return _squad; }
        int32            getPopulation() const { return _population; }
        int32            getPopulationCap() const { return _populationCap; }
        int32            getResource() const { return _resource; }
        float32          getHealthRatio() const { return _healthRatio; }
        WidgetVisibility getWonVisibility() const { return _wonVisibility; }
        WidgetVisibility getLostVisibility() const { return _lostVisibility; }

    private:
        /** @brief 보임 여부를 HUD 위젯의 보임 칸 값으로 바꿉니다(보이면 클릭을 받지 않는 보임, 아니면 접힘). */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Territory" )
        int32 _territory;
        PROPERTY( DisplayName = "Site Count" )
        int32 _siteCount;
        PROPERTY( DisplayName = "Squad" )
        int32 _squad;
        PROPERTY( DisplayName = "Population" )
        int32 _population;
        PROPERTY( DisplayName = "Population Cap" )
        int32 _populationCap;
        PROPERTY( DisplayName = "Resource" )
        int32 _resource;
        PROPERTY( DisplayName = "Health Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _healthRatio;
        PROPERTY( DisplayName = "Won Visibility" )
        WidgetVisibility _wonVisibility;
        PROPERTY( DisplayName = "Lost Visibility" )
        WidgetVisibility _lostVisibility;
    };
} // namespace sw
