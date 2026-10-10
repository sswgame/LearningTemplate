/**
 * @file HUDViewModel.h
 * @brief HUD 문서가 보이는 값(체력 · 탄약 · 무기 이름 · 조준선 · 맞음 표시)입니다 — 게임은 세터만 부르고, 문서의 `{bind:필드}` 가 위젯 칸에 잇습니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/UI/Base/WidgetTypes.h"
#include "Engine/UI/Binding/UIViewModel.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class HUDViewModel
     * @brief 플레이어 HUD 의 뷰모델입니다(Lyra 의 HUD 위젯이 보는 값 · UE5 MVVM ViewModel 자리). `HUDControllerComponent` 가 들고 HUD 화면에 겁니다.
     * @details 값은 보일 모양이 아니라 데이터입니다 — 탄약은 탄창 · 예비 두 수, 무기 이름은 현지화 키(글 위젯이 문화권으로 푼다 — 언어를 바꾸면 다시 풀린다).
     *          보임은 `WidgetVisibility` 칸이라 문서가 `_visibility="{bind:_crosshairVisibility}"` 로 그대로 잇습니다(보이면 `HitTestInvisible` — HUD 는 클릭을 받지 않는다).
     *          세터는 값이 같으면 알리지 않으므로 게임이 매 프레임 불러도 바인딩은 바뀐 칸만 씁니다. 게임 스레드만(틱 안이면 틱 뒤 큐에서).
     */
    REFLECT( DisplayName = "HUD View Model" )
    class SW_GF_API HUDViewModel : public UIViewModel
    {
    public:
        REFLECT_BODY();

        HUDViewModel();
        ~HUDViewModel() override;

        const TypeInfo* getTypeInfo() const override;

        /** @brief 체력(보이는 정수 — 소수는 올림)과 비율(0..1, 막대)입니다. */
        void setHealth( float32 health, float32 ratio );
        /** @brief 탄창 · 예비 탄약입니다. */
        void setAmmo( int32 magazineAmmo, int32 reserveAmmo );
        /** @brief 무기 이름입니다(현지화 키 — 표에 없으면 원문이 보인다). */
        void setWeaponName( string_view weaponName );
        /** @brief 조준선 · 맞음 표시를 보이거나 숨깁니다. */
        void setCrosshairShown( bool bShown );
        void setHitMarkerShown( bool bShown );

        int32            getHealth() const { return _health; }
        float32          getHealthRatio() const { return _healthRatio; }
        int32            getMagazineAmmo() const { return _magazineAmmo; }
        int32            getReserveAmmo() const { return _reserveAmmo; }
        const string&    getWeaponName() const { return _weaponName; }
        WidgetVisibility getCrosshairVisibility() const { return _crosshairVisibility; }
        WidgetVisibility getHitMarkerVisibility() const { return _hitMarkerVisibility; }

    private:
        /** @brief 보임 여부를 HUD 위젯의 보임 칸 값으로 바꿉니다(보이면 클릭을 받지 않는 보임, 아니면 접힘). */
        static WidgetVisibility toVisibility( bool bShown );

    private:
        PROPERTY( DisplayName = "Health" )
        int32 _health;
        PROPERTY( DisplayName = "Health Ratio", Min = 0.0, Max = 1.0, Units = ratio )
        float32 _healthRatio;
        PROPERTY( DisplayName = "Magazine Ammo" )
        int32 _magazineAmmo;
        PROPERTY( DisplayName = "Reserve Ammo" )
        int32 _reserveAmmo;
        PROPERTY( DisplayName = "Weapon Name", Tooltip = "Localization key (the source text when the table has no entry)" )
        string _weaponName;
        PROPERTY( DisplayName = "Crosshair Visibility" )
        WidgetVisibility _crosshairVisibility;
        PROPERTY( DisplayName = "Hit Marker Visibility" )
        WidgetVisibility _hitMarkerVisibility;
    };
} // namespace sw
