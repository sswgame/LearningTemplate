/**
 * @file ScreenReactionComponent.h
 * @brief 반응 — 태그(기본 `State.Poisoned`)를 가진 동안 이 오브젝트를 따라가는 카메라의 화면을 바꿉니다(시야 흐림 · 비네트 · 채도 · 노출). 독가스 · 유황 · 늪 · 연기의 "시야" 효과입니다.
 * @details 반응은 속성 · 태그만 읽는 작은 컴포넌트다 — 어느 지역이 태그를 주었는지 모른다(지역은 이펙트로 태그를 걸 뿐이다). 그래서 같은 반응이 독화살 · 마법의 중독에도 듣는다.
 *          태그가 있으면 가중치를 `_fadeInTime` 동안 1 로 올리고, 없으면 `_fadeOutTime` 동안 0 으로 내린다. 이 오브젝트를 대상으로 하는 카메라 디렉터마다 후처리 덧칠
 *          (`CameraDirector::setPostProcessOverlay` — 출처는 이 오브젝트)을 그 가중치로 건다. 대상이 아닌 디렉터의 덧칠은 뺀다(대상이 바뀌면 화면이 남지 않는다).
 *          화면 값은 이 컴포넌트의 데이터(`_postProcess` — 카메라 프리셋 `<PostProcess>` 와 같은 칸)다. 언리얼 PostProcessVolume 의 BlendWeight · 유니티 Volume weight 와 같은
 *          자리지만, 볼륨이 아니라 받은 상태가 화면을 정한다(지역 밖으로 나와도 중독이 남은 동안 흐리다).
 *          보기는 지역 프레임 시스템(`ZoneSystem`)이 틱 뒤 게임 스레드에서 한 번에 한다 — 컴포넌트는 칸과 등록만 든다(디렉터 목록을 보는 일은 틱 밖이다).
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/String/TagID.h"

#include "Engine/Graphics/View/CameraPostProcessValues.h"
#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class ScreenReactionComponent
     * @brief 태그를 가진 동안 따라가는 카메라의 화면을 바꾸는 반응입니다(파일 머리말 참고).
     */
    REFLECT( Category = "Gameplay", DisplayName = "Screen Reaction", Tooltip = "While the object has a tag (poisoned, blinded), cameras following it blend in a post-process look" )
    class SW_GF_API ScreenReactionComponent : public Component
    {
    public:
        REFLECT_BODY();

        ScreenReactionComponent();
        ~ScreenReactionComponent() override = default;

        /** @brief 씬의 컴포넌트 레지스트리에 오르고 지역 프레임 시스템(`ZoneSystem`)을 붙입니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 걸어 둔 덧칠을 모든 디렉터에서 빼고, 레지스트리에서 내리며, 마지막 지역 컴포넌트면 시스템을 뗍니다. */
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief 지금 화면 가중치(0..1)입니다(감시 값 · 진단). */
        float32 getWeight() const { return _weight; }
        /** @brief 태그를 보고 가중치를 옮긴 뒤 따라가는 디렉터에 덧칠을 겁니다(`ZoneSystem` 이 틱 뒤 게임 스레드에서 부른다, 테스트도 부른다). */
        void updateScreen( GameObjectManager& manager, float32 deltaTime );

    private:
        /** @brief 디렉터마다 덧칠을 겁니다 — 이 오브젝트를 따라가면 @p weight, 아니면 뺀다. */
        void applyToDirectors( GameObjectManager& manager, float32 weight ) const;

    private:
        PROPERTY( Category = "Reaction", DisplayName = "Tag", Tooltip = "While the object has this tag the screen look blends in" )
        TagID _tag;
        PROPERTY( Category = "Reaction", DisplayName = "Post Process", Tooltip = "Look at full weight (vignette, saturation, contrast, exposure); neutral fields change nothing" )
        CameraPostProcessValues _postProcess;
        PROPERTY( Category = "Reaction", DisplayName = "Fade In Time", Min = 0.0, Units = s, Tooltip = "Seconds from no look to full look after the tag appears (0 = at once)" )
        float32 _fadeInTime;
        PROPERTY( Category = "Reaction", DisplayName = "Fade Out Time", Min = 0.0, Units = s, Tooltip = "Seconds from full look to none after the tag goes (0 = at once)" )
        float32 _fadeOutTime;

        float32 _weight;
    };
} // namespace sw
