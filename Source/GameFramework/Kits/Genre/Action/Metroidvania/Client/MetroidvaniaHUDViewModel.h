/**
 * @file MetroidvaniaHUDViewModel.h
 * @brief 메트로배니아 HUD 문서가 보이는 값(체력 · 스태미나 · 물약 · 통화 · 완료율 · 안내 · 결과)입니다 — 뷰 컴포넌트는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다.
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
     * @class MetroidvaniaHUDViewModel
     * @brief 메트로배니아 HUD 의 뷰모델입니다. `MetroidvaniaViewComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 글은 문서가 들고(번역 키) 여기는 수와 보임만 정합니다. 세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 됩니다. 게임 스레드만.
     */
    REFLECT( DisplayName = "Metroidvania HUD View Model" )
    class SW_KIT_CLIENT_API MetroidvaniaHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        MetroidvaniaHUDViewModel();
        ~MetroidvaniaHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 체력 · 스태미나 비율(0..1)과 남은 물약입니다. */
        void setVitals( float32 healthRatio, float32 staminaRatio, int32 flaskCount );
        /** @brief 가진 통화와 완료율(%)입니다. 보이는 완료율은 내린 정수다. */
        void setProgress( int32 currency, float32 completionPercent );
        /** @brief 죽음 · 승리 글을 보이거나 숨깁니다. */
        void setResult( bool bDead, bool bWon );

    private:
        /** @brief 보임 여부를 HUD 위젯의 보임 칸 값으로 바꿉니다(보이면 클릭을 받지 않는 보임, 아니면 접힘). */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Health Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _healthRatio;
        PROPERTY( DisplayName = "Stamina Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _staminaRatio;
        PROPERTY( DisplayName = "Flasks" )
        int32 _flaskCount;
        PROPERTY( DisplayName = "Currency" )
        int32 _currency;
        PROPERTY( DisplayName = "Completion Percent" )
        int32 _completion;
        PROPERTY( DisplayName = "Dead Visibility" )
        WidgetVisibility _deadVisibility;
        PROPERTY( DisplayName = "Win Visibility" )
        WidgetVisibility _winVisibility;
    };
} // namespace sw
