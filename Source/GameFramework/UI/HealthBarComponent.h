#pragma once
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Object/GameObject/SpriteInstanceBatch.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/Combat/HealthListenerComponent.h"
#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    /**
     * @class HealthBarComponent
     * @brief 소유 오브젝트 위(+ `_offsetPos`)에 떠 있는 월드 공간 HP 바입니다. 채움 · 피해 흔적 · 바탕 세 조각을 스프라이트로 그립니다.
     * @details 비율 셋의 뜻:
     *          - `_targetRatio` — 참 HP 비율입니다. 체력 시스템의 알림(`onHealthChanged` — `HealthListenerComponent::broadcast`)이 넣습니다.
     *          - `_hpRatio` — 채움 조각의 길이입니다. 줄 때는 바로 따라가고(맞은 순간 줄어든다) 늘 때는 `_lerpSpeed` 로 차오릅니다.
     *          - `_remainRatio` — 피해 흔적입니다. 채움보다 길면 `_lerpSpeed` 로 채움까지 줄어들어 "방금 잃은 만큼" 을 잠깐 보입니다.
     *          그리는 조각은 **겹치지 않습니다**: 채움 [0, hp], 흔적 [hp, remain], 바탕 [max(hp, remain), 1]. 같은 깊이의 반투명 조각이 겹치면
     *          그리는 순서가 카메라 거리 정렬에 맡겨져 앞뒤가 뒤바뀔 수 있는데, 겹치지 않으면 순서가 상관없습니다.
     *
     *          그리기는 `SpriteInstanceBatch`(조각 셋)이고 `onBeginPlay` 에서 만듭니다 — 저장되는 컴포넌트를 만들지 않습니다. 자리는 이번 프레임의
     *          트랜스폼 쓰기가 적용된 **뒤에** 잡습니다(틱 직후 큐) — 틱 안에서 읽는 월드 자리는 지난 프레임 것이라 움직이는 캐릭터를 한 프레임 늦게
     *          따라갑니다.
     */
    REFLECT( Category = "UI", DisplayName = "Health Bar Component", Tooltip = "Smooth lerping HP Bar floating UI component" )
    class SW_GF_API HealthBarComponent : public HealthListenerComponent
    {
    public:
        REFLECT_BODY();

        /** @brief 조각 번호입니다(스프라이트 배치의 항목 순서). */
        static constexpr uint32 kFillEntry       = 0;
        static constexpr uint32 kTrailEntry      = 1;
        static constexpr uint32 kBackgroundEntry = 2;
        static constexpr uint32 kEntryCount      = 3;

        HealthBarComponent();
        virtual ~HealthBarComponent() override = default;

        /** @brief 조각 셋을 만들고 비율을 지금 HP 비율로 맞춥니다(흔적 없음). */
        void onBeginPlay() override;
        /** @brief 조각을 놓습니다. */
        void onEndPlay() override;
        /** @brief 채움 · 흔적을 참 비율 쪽으로 옮기고, 틱 뒤에 조각 자리를 잡게 합니다. */
        void onTick( float32 deltaTime ) override;
        /** @brief 보임 · 색 · 크기 · 비율 칸을 고치면 조각을 다시 잡습니다(켜고 끄기 포함). */
        void onPropertyChanged( hashed_string propertyName ) override;
        /** @brief 소유 오브젝트가 꺼지면 조각을 숨깁니다(꺼진 오브젝트는 틱이 돌지 않습니다). */
        void onOwnerActiveInHierarchyChanged() override;
        /**
         * @brief 같은 오브젝트의 체력 시스템이 알린 변화입니다 — 다시 두기는 흔적 없이(`resetRatio`), 바뀜은 목표만(`setTargetRatio`).
         * @details `_bShowWhenHurt` 면 처음 줄 때 보이고, `_bHideWhenDead` 면 쓰러질 때 숨습니다(보이기 정책은 바가 정한다 — 체력 시스템은 바를 모른다).
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
        /** @brief 채움 조각의 비율입니다. */
        float32 getHpRatio() const { return _hpRatio; }
        /** @brief 피해 흔적의 끝 비율입니다(채움보다 짧지 않습니다). */
        float32 getRemainRatio() const { return _remainRatio; }

        /** @brief 바를 보이거나 숨깁니다(보통 처음 맞을 때 켭니다). */
        void setVisible( bool bVisible );
        /** @brief 바가 보이도록 정해져 있으면 true 입니다. */
        bool isVisible() const { return _bVisible; }

        /** @brief 조각을 그리는 스프라이트 배치입니다(시험이 항목을 읽습니다). */
        const SpriteInstanceBatch& getSpriteBatch() const { return _spriteBatch; }

    private:
        /** @brief 틱 직후(트랜스폼 적용 뒤) 게임 스레드에서 조각 자리를 잡게 합니다. 틱 밖이면 바로 잡습니다. */
        void scheduleLayout();
        /** @brief 소유 오브젝트의 월드 자리 + 오프셋에 조각 셋을 놓습니다. 숨김 · 꺼짐이면 모두 숨깁니다. */
        void layoutSprites();

        PROPERTY( Category = "Health", DisplayName = "HP Ratio", Tooltip = "Displayed fill ratio (0..1); drops at once, refills at the lerp speed", Min = 0.0, Max = 1.0,
                  Meta = "Slider, Units=ratio" )
        float32 _hpRatio;
        PROPERTY( Category = "Health", DisplayName = "Remain Ratio", Tooltip = "Delayed damage trail ratio (0..1)", Min = 0.0, Max = 1.0, Meta = "Slider, Units=ratio" )
        float32 _remainRatio;
        PROPERTY( Category = "Health", DisplayName = "Target Ratio", Tooltip = "True HP ratio (0..1) set by the health system", Min = 0.0, Max = 1.0,
                  Meta = "Slider, Units=ratio" )
        float32 _targetRatio;
        PROPERTY( Category = "Animation", DisplayName = "Lerp Speed", Tooltip = "How fast the trail shrinks and the fill refills (per second)", Min = 0.1, Max = 20.0 )
        float32 _lerpSpeed;
        PROPERTY( Category = "Layout", DisplayName = "Offset Position", Tooltip = "Offset of the bar center from the owner position", Meta = "Units=m" )
        float2 _offsetPos;
        PROPERTY( Category = "Layout", DisplayName = "Bar Size", Tooltip = "Bar width and height in world units", Min = 0.0, Meta = "Units=m" )
        float2 _barSize;
        PROPERTY( Category = "Style", DisplayName = "Fill Color", Meta = "Color", Tooltip = "Color of the current HP" )
        float4 _fillColor;
        PROPERTY( Category = "Style", DisplayName = "Trail Color", Meta = "Color", Tooltip = "Color of the HP just lost" )
        float4 _trailColor;
        PROPERTY( Category = "Style", DisplayName = "Background Color", Meta = "Color", Tooltip = "Color of the missing HP" )
        float4 _backgroundColor;
        PROPERTY( Category = "Style", DisplayName = "Sorting Layer", Tooltip = "Sorting layer of the bar (render2d.xml); world UI draws above sprites" )
        hashed_string _sortingLayer;
        PROPERTY( Category = "Layout", DisplayName = "Visible", Tooltip = "Toggle HP bar visibility" )
        bool _bVisible;
        PROPERTY( Category = "Layout", DisplayName = "Show When Hurt", Tooltip = "Show the bar the first time health drops" )
        bool _bShowWhenHurt;
        PROPERTY( Category = "Layout", DisplayName = "Hide When Dead", Tooltip = "Hide the bar when the owner dies" )
        bool                _bHideWhenDead;
        SpriteInstanceBatch _spriteBatch; ///< 조각 셋(채움 · 흔적 · 바탕). 저장하지 않습니다
    };
} // namespace sw
