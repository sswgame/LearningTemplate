/**
 * @file GhostHuntHUDViewModel.h
 * @brief 유령 사냥 HUD 문서가 보이는 값입니다 — 뷰 컴포넌트는 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다.
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
     * @class GhostHuntHUDViewModel
     * @brief 유령 사냥 HUD 의 뷰모델입니다. `GhostHuntAvatarComponent` 가 소유하고 `IHUDViewModelBinder` 로 HUD 화면에 겁니다.
     * @details 글은 문서의 번역 키다 — 여기는 수와 보임만 든다. 세터는 값이 같으면 알리지 않으므로 매 프레임 불러도 된다. 게임 스레드만.
     */
    REFLECT( DisplayName = "Ghost Hunt HUD View Model" )
    class SW_KIT_CLIENT_API GhostHuntHUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        GhostHuntHUDViewModel();
        ~GhostHuntHUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 불을 켜야 할 남은 방 수입니다. */
        void setDarkRooms( int32 value );
        /** @brief 모은 동전입니다. */
        void setCoins( int32 value );
        /** @brief 가방의 열쇠 수입니다. */
        void setKeys( int32 value );
        /** @brief 체력 비율입니다. */
        void setHealthRatio( float32 ratio );
        /** @brief 서지 게이지입니다. */
        void setSurgeRatio( float32 ratio );
        /** @brief 모든 방을 밝힌 글을 보입니다. */
        void setWinShown( bool bShown );
        /** @brief 쓰러진 글을 보입니다. */
        void setLoseShown( bool bShown );

    private:
        /** @brief 보이면 클릭을 받지 않는 보임, 아니면 접힘입니다. */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Dark Rooms" )
        int32 _darkRooms;
        PROPERTY( DisplayName = "Coins" )
        int32 _coins;
        PROPERTY( DisplayName = "Keys" )
        int32 _keys;
        PROPERTY( DisplayName = "Health Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _healthRatio;
        PROPERTY( DisplayName = "Surge Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _surgeRatio;
        PROPERTY( DisplayName = "Win Visibility" )
        WidgetVisibility _winVisibility;
        PROPERTY( DisplayName = "Lose Visibility" )
        WidgetVisibility _loseVisibility;
    };
} // namespace sw
