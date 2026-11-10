/**
 * @file JumpEscapeReactionComponent.h
 * @brief 반응 — 뛰어서 빠져나옵니다. 디딘 채로 떠오르는 순간(점프)마다 쌓인 이펙트(가라앉음)의 스택을 줄입니다.
 * @details 탈출도 반응이다 — 어느 지역이 스택을 쌓았는지 모르고 이펙트 id 만 안다. 스택이 0 이 되면 이펙트가 끝나 속성(`SinkDepth` · `MoveSpeedScale`)이 돌아온다.
 *          삼켜진 뒤(`_blockedTag`, 기본 `State.Engulfed`)에는 뛰어도 줄지 않는다. 점프는 같은 오브젝트의 캐릭터 컨트롤러가 지난 프레임에 디뎠고 지금 떠서 오르는 순간이다.
 *          보기는 지역 프레임 시스템(`ZoneSystem`)이 틱 뒤 게임 스레드에서 한 번에 한다 — 컴포넌트는 칸과 등록만 든다.
 */
#pragma once
#include "Core/Foundation/Types.h"
#include "Core/Name/TagID.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class GameObjectManager;

    /**
     * @class JumpEscapeReactionComponent
     * @brief 점프로 스택을 줄이는 반응입니다(파일 머리말 참고).
     */
    REFLECT( Category = "Gameplay", DisplayName = "Jump Escape Reaction", Tooltip = "Each jump removes stacks of a stacking effect (climbing out of a swamp or quicksand)" )
    class SW_GF_API JumpEscapeReactionComponent : public Component
    {
    public:
        REFLECT_BODY();

        JumpEscapeReactionComponent();
        ~JumpEscapeReactionComponent() override = default;

        /** @brief 씬의 컴포넌트 레지스트리에 오르고 지역 프레임 시스템(`ZoneSystem`)을 붙입니다. */
        void onRegister( GameObjectManager& manager ) override;
        /** @brief 레지스트리에서 내리고, 마지막 지역 컴포넌트면 시스템을 뗍니다. */
        void onUnregister( GameObjectManager& manager ) override;

        /** @brief 뛰어서 스택을 줄인 수입니다(감시 값 · 진단). */
        uint32 getEscapeCount() const { return _escapeCount; }
        /** @brief 점프를 보고 스택을 줄입니다(`ZoneSystem` 이 틱 뒤 게임 스레드에서 부른다, 테스트도 부른다). */
        void updateJumpEscape();

    private:
        PROPERTY( Required, Category = "Reaction", DisplayName = "Effect", Tooltip = "Stacking effect id whose stacks a jump removes" )
        hashed_string _effect;
        PROPERTY( Category = "Reaction", DisplayName = "Stacks Per Jump", Min = 1, Tooltip = "Stacks removed by each jump" )
        int32 _stacksPerJump;
        PROPERTY( Required, Category = "Reaction", DisplayName = "Blocked Tag", Tooltip = "While the object has this tag, jumping does not help (engulfed)" )
        TagID _blockedTag;

        uint32 _escapeCount;
        uint8  _bWasGrounded : 1; ///< 지난 프레임에 디디고 있었다(떠오르는 순간을 점프로 본다)
        uint8  _reserved     : 7;
    };
} // namespace sw
