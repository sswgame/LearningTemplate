#pragma once
#include "Core/Common/Types.h"
#include "Core/Math/VectorMath.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Base/Combat/HealthListenerComponent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class ProgressBarWidget;
    class Widget;
    class WidgetComponent;

    /**
     * @class HealthBarComponent
     * @brief 소유 오브젝트 위에 떠 있는 HP 바입니다 — 같은 오브젝트의 `WidgetComponent`(Screen — 화면 마커: 크기 · 머리 위 오프셋 · 피벗)에 막대 위젯을 넣고 값을 채웁니다.
     * @details 비율 셋의 뜻:
     *          - `_targetRatio` — 참 HP 비율입니다. 체력 시스템의 알림(`onHealthChanged` — `HealthListenerComponent::broadcast`)이 넣습니다.
     *          - `_hpRatio` — 채움 막대의 길이입니다. 줄 때는 바로 따라가고(맞은 순간 줄어든다) 늘 때는 `_lerpSpeed` 로 차오릅니다.
     *          - `_remainRatio` — 피해 흔적입니다. 채움보다 길면 `_lerpSpeed` 로 채움까지 줄어들어 "방금 잃은 만큼" 을 잠깐 보입니다.
     *          그림은 겹친 진행 막대 둘입니다 — 아래 막대(바탕 색 위에 흔적 색이 `_remainRatio` 까지), 위 막대(바탕 없음, 채움 색이 `_hpRatio` 까지).
     *          UI 그리기 순서는 위젯 순서라 겹쳐도 앞뒤가 바뀌지 않습니다(월드 스프라이트처럼 카메라 거리 정렬에 맡기지 않는다).
     *
     *          막대 위젯은 `onBeginPlay` 에서 코드로 짓습니다(화면 마커는 한 트리에 모이므로 위젯 이름을 쓰지 않고 자식 순서로 찾는다). 값은 이번 프레임의
     *          틱 **뒤에** 넣습니다(틱 직후 큐 — 위젯은 게임 스레드만 고친다). 같은 오브젝트에 `WidgetComponent` 가 없으면 경고 한 번 뒤 계산만 합니다.
     *          값 바인딩(뷰모델)이 들어오면 막대 값 넣기(`refreshWidgets`)가 그 바인딩이 된다.
     */
    REFLECT( Category = "UI", DisplayName = "Health Bar Component", Tooltip = "Smooth lerping HP bar drawn by the object's screen-marker WidgetComponent" )
    class SW_GF_API HealthBarComponent : public HealthListenerComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 막대 위젯 안의 자식 순서입니다(겹침 패널의 아래 → 위). */
        static constexpr uint32 kTrailBarIndex = 0;
        static constexpr uint32 kFillBarIndex  = 1;

        HealthBarComponent();
        virtual ~HealthBarComponent() override = default;

        /** @brief 막대 위젯을 같은 오브젝트의 `WidgetComponent` 에 넣고 비율을 같은 오브젝트의 체력 원천(`HealthSourceComponent`)에 맞춥니다 — 원천이 없으면 저장된 `_hpRatio`(흔적 없음). */
        void onBeginPlay() override;
        /** @brief 채움 · 흔적을 참 비율 쪽으로 옮기고, 틱 뒤에 막대 값을 넣게 합니다. */
        void onTick( float32 deltaTime ) override;
        /** @brief 보임 · 색 · 비율 칸을 고치면 막대를 다시 채웁니다(켜고 끄기 포함). */
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 소유 오브젝트가 꺼지면 막대를 숨깁니다(꺼진 오브젝트는 틱이 돌지 않습니다). */
        void onOwnerActiveInHierarchyChanged() override;
        /**
         * @brief 같은 오브젝트의 체력 시스템이 알린 변화입니다 — 다시 두기는 흔적 없이(`resetRatio`), 바뀜은 목표만(`setTargetRatio`).
         * @details `_bShowWhenHurt` 면 처음 줄 때 보이고, `_bHideWhenDead` 면 쓰러질 때 숨고, 아니면 쓰러짐도 `_bShowWhenHurt` 에 따라 보입니다(한 방에 쓰러진 적)(보이기 정책은 바가 정한다 — 체력 시스템은 바를 모른다).
         */
        void onHealthChanged( const HealthChangedEvent& event ) override;

        /**
         * @brief 참 HP 비율(0..1)을 넣습니다. 체력 시스템의 입력은 이것 하나입니다. 범위 밖은 묶습니다.
         * @details 줄면 다음 틱에 채움이 바로 줄고 흔적이 남습니다. 늘면 채움이 `_lerpSpeed` 로 차오릅니다. 게임 스레드(틱 밖 · 틱 직후 큐)에서
         *          부르십시오 — 다른 오브젝트의 병렬 틱에서 부르면 이 컴포넌트의 틱과 같은 칸을 씁니다(데미지는 `deferPostTick` 으로 옵니다).
         */
        void setTargetRatio( float32 ratio );
        /** @brief 셋(참 · 채움 · 흔적)을 한꺼번에 @p ratio 로 둡니다. 흔적 없이 바로 그 길이입니다(스폰 · 부활). */
        void resetRatio( float32 ratio );
        /** @brief 참 HP 비율입니다. */
        float32 getTargetRatio() const { return _targetRatio; }
        /** @brief 채움 막대의 비율입니다. */
        float32 getHpRatio() const { return _hpRatio; }
        /** @brief 피해 흔적의 끝 비율입니다(채움보다 짧지 않습니다). */
        float32 getRemainRatio() const { return _remainRatio; }

        /** @brief 바를 보이거나 숨깁니다(보통 처음 맞을 때 켭니다). */
        void setVisible( bool bVisible );
        /** @brief 바가 보이도록 정해져 있으면 true 입니다. */
        bool isVisible() const { return _bVisible; }

        /** @brief 같은 오브젝트의 화면 마커 컴포넌트입니다(없으면 nullptr). */
        WidgetComponent* findWidgetComponent() const;
        /** @brief 아래(흔적) 막대입니다(위젯을 아직 넣지 않았으면 nullptr — 시험이 값을 읽는다). */
        const ProgressBarWidget* findTrailBar() const { return findBar( kTrailBarIndex ); }
        /** @brief 위(채움) 막대입니다. */
        const ProgressBarWidget* findFillBar() const { return findBar( kFillBarIndex ); }

        /** @brief 막대 위젯(겹침 패널 + 진행 막대 둘)을 짓습니다. */
        static unique_ptr<Widget> createBarWidget();

    private:
        /** @brief 틱 직후(트랜스폼 적용 뒤) 게임 스레드에서 막대 값을 넣게 합니다. 틱 밖이면 바로 넣습니다. */
        void scheduleRefresh();
        /** @brief 막대 둘에 비율 · 색을 넣고, 숨김 · 꺼짐이면 마커를 숨깁니다. */
        void refreshWidgets();
        /** @brief 마커 위젯의 막대 @p index(아래 → 위)입니다. 없으면 nullptr 입니다. */
        ProgressBarWidget* findBar( uint32 index ) const;

        PROPERTY( Category = "Health", DisplayName = "HP Ratio", Tooltip = "Displayed fill ratio (0..1); drops at once, refills at the lerp speed", Min = 0.0, Max = 1.0,
                  Units = ratio, Meta = "Slider" )
        float32 _hpRatio;
        PROPERTY( Category = "Health", DisplayName = "Remain Ratio", Tooltip = "Delayed damage trail ratio (0..1)", Min = 0.0, Max = 1.0, Units = ratio, Meta = "Slider" )
        float32 _remainRatio;
        PROPERTY( Category = "Health", DisplayName = "Target Ratio", Tooltip = "True HP ratio (0..1) set by the health system", Min = 0.0, Max = 1.0,
                  Units = ratio, Meta = "Slider" )
        float32 _targetRatio;
        PROPERTY( Category = "Animation", DisplayName = "Lerp Speed", Tooltip = "How fast the trail shrinks and the fill refills (per second)", Min = 0.1, Max = 20.0 )
        float32 _lerpSpeed;
        PROPERTY( Category = "Style", DisplayName = "Fill Color", Meta = "Color", Tooltip = "Color of the current HP" )
        float4 _fillColor;
        PROPERTY( Category = "Style", DisplayName = "Trail Color", Meta = "Color", Tooltip = "Color of the HP just lost" )
        float4 _trailColor;
        PROPERTY( Category = "Style", DisplayName = "Background Color", Meta = "Color", Tooltip = "Color of the missing HP" )
        float4 _backgroundColor;
        PROPERTY( Category = "Layout", DisplayName = "Visible", Tooltip = "Toggle HP bar visibility" )
        bool _bVisible;
        PROPERTY( Category = "Layout", DisplayName = "Show When Hurt", Tooltip = "Show the bar the first time health drops" )
        bool _bShowWhenHurt;
        PROPERTY( Category = "Layout", DisplayName = "Hide When Dead", Tooltip = "Hide the bar when the owner dies" )
        bool _bHideWhenDead;
        bool _bWarnedNoWidgetComponent; ///< 같은 오브젝트에 WidgetComponent 가 없다고 한 번 알렸다
    };
} // namespace sw
