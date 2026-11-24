/**
 * @file RhythmHUDViewModel.h
 * @brief 리듬 HUD 문서가 보이는 값입니다 — 클라이언트 뷰 컴포넌트는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다. 글은 문서의 번역 키다(뷰모델은 수와 보임만 든다).
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
     * @class RhythmHUDViewModel
     * @brief 리듬 HUD 의 뷰모델입니다. `RhythmViewComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 바인딩은 바뀐 칸만 씁니다. 게임 스레드만.
     */
    REFLECT( DisplayName = "Rhythm HUD View Model" )
    class SW_KIT_CLIENT_API RhythmHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        RhythmHUDViewModel();
        ~RhythmHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 점수입니다. */
        void setScore( int32 score );
        /** @brief 지금 콤보입니다. */
        void setCombo( int32 combo );
        /** @brief 정확도입니다(%). */
        void setAccuracy( int32 accuracy );
        /** @brief 남은 라이프 비율(0..1, 막대)입니다. */
        void setLifeRatio( float32 lifeRatio );
        /** @brief 곡을 깬 표시를 보이거나 숨깁니다. */
        void setClearedShown( bool bShown );
        /** @brief 실패 표시를 보이거나 숨깁니다. */
        void setFailedShown( bool bShown );

        int32            getScore() const { return _score; }
        int32            getCombo() const { return _combo; }
        int32            getAccuracy() const { return _accuracy; }
        float32          getLifeRatio() const { return _lifeRatio; }
        WidgetVisibility getClearedVisibility() const { return _clearedVisibility; }
        WidgetVisibility getFailedVisibility() const { return _failedVisibility; }

    private:
        /** @brief 보임 여부를 HUD 위젯의 보임 칸 값으로 바꿉니다(보이면 클릭을 받지 않는 보임, 아니면 접힘). */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Score" )
        int32 _score;
        PROPERTY( DisplayName = "Combo" )
        int32 _combo;
        PROPERTY( DisplayName = "Accuracy" )
        int32 _accuracy;
        PROPERTY( DisplayName = "Life Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _lifeRatio;
        PROPERTY( DisplayName = "Cleared Visibility" )
        WidgetVisibility _clearedVisibility;
        PROPERTY( DisplayName = "Failed Visibility" )
        WidgetVisibility _failedVisibility;
    };
} // namespace sw
