/**
 * @file AsymmetricHorrorHUDViewModel.h
 * @brief 비대칭 공포 HUD 문서가 보이는 값입니다 — 뷰 컴포넌트는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다.
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
     * @class AsymmetricHorrorHUDViewModel
     * @brief 비대칭 공포 HUD 의 뷰모델입니다. `AsymmetricHorrorAvatarComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 글은 문서의 번역 키다 — 여기는 수와 보임만 든다. 세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 된다. 게임 스레드만.
     */
    REFLECT( DisplayName = "Asymmetric Horror HUD View Model" )
    class SW_KIT_CLIENT_API AsymmetricHorrorHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        AsymmetricHorrorHUDViewModel();
        ~AsymmetricHorrorHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 전원까지 남은 발전기 수입니다. */
        void setRemainingGenerators( int32 value );
        /** @brief 생존자 0 이 하고 있는 일의 진행입니다. 하는 일이 없으면 0 입니다. */
        void setActivityRatio( float32 ratio );
        /** @brief 심장 소리 세기입니다. */
        void setHeartbeat( float32 ratio );
        /** @brief 스킬 체크가 떴으면 안내를 보입니다. */
        void setSkillCheckShown( bool bShown );
        /** @brief 이긴 글을 보입니다. */
        void setWinShown( bool bShown );
        /** @brief 비긴 글을 보입니다. */
        void setDrawShown( bool bShown );
        /** @brief 진 글을 보입니다. */
        void setLoseShown( bool bShown );

    private:
        /** @brief 보이면 클릭을 받지 않는 보임, 아니면 접힘입니다. */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Remaining Generators" )
        int32 _remainingGenerators;
        PROPERTY( DisplayName = "Activity Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _activityRatio;
        PROPERTY( DisplayName = "Heartbeat", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _heartbeat;
        PROPERTY( DisplayName = "Skill Check Visibility" )
        WidgetVisibility _skillCheckVisibility;
        PROPERTY( DisplayName = "Win Visibility" )
        WidgetVisibility _winVisibility;
        PROPERTY( DisplayName = "Draw Visibility" )
        WidgetVisibility _drawVisibility;
        PROPERTY( DisplayName = "Lose Visibility" )
        WidgetVisibility _loseVisibility;
    };
} // namespace sw
