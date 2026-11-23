/**
 * @file EmoteComponent.h
 * @brief 이모트(감정 표현)입니다. 폰 의도의 버튼이 같은 오브젝트의 애니메이터에 동작을 틀고, 움직이면 그칩니다.
 */
#pragma once
#include "Core/Container/vector.h"
#include "Core/Foundation/Expected.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/hashed_string.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class SkeletalAnimatorComponent;

    /** @brief 이모트 하나입니다. */
    REFLECT()
    struct SW_GF_API EmoteDef
    {
        REFLECT_BODY();

        PROPERTY( Tooltip = "Intent button of this object's pawn that plays it" )
        hashed_string _button{};
        PROPERTY( Tooltip = "Animator state or clip name" )
        hashed_string _state{};
        PROPERTY( Tooltip = "Loops until the body moves (dance); off: plays once and returns" )
        bool _bLoop{ false };
    };
} // namespace sw

namespace sw
{
    /**
     * @class EmoteComponent
     * @brief 폰 의도의 버튼이 발동하면 그 이모트의 동작을 틀고, 이동 의도 · 공중 · 매달림에서 그쳐 그 전 상태로 돌아갑니다. 한 번짜리는 동작이 끝나면 돌아갑니다.
     * @details 입력이 의도라 로컬 · 원격 · 재생이 같은 이모트를 낸다 — 이모트를 따로 복제하지 않는다(의도가 이미 실린다). 언리얼 Lyra 의 이모트 어빌리티(입력 태그 → 몽타주, 이동이 끊음) 자리다.
     *          다른 동작(상호작용 몽타주 · 페어드 애니메이션)이 애니메이터 상태를 바꾸면 이모트는 손을 뗀다(되돌리지 않는다).
     */
    REFLECT( Category = "Control", DisplayName = "Emote", Tooltip = "Pawn intent buttons play emotes on the animator; moving stops them" )
    class SW_GF_API EmoteComponent : public Component
    {
    public:
        REFLECT_BODY();

        EmoteComponent();
        ~EmoteComponent() override = default;

        /** @brief 버튼 번호를 찾아 둡니다. 폰 · 애니메이터 · 버튼이 없으면 오류입니다. */
        void onBeginPlay() override;
        void onTick( TickContext& context ) override;

        /** @brief 이모트 목록을 정합니다. 코드로 만든 폰 · 테스트가 시작 전에 부릅니다. 씬에 놓은 것은 PROPERTY 가 채웁니다. */
        void setEmotes( const vector<EmoteDef>& listEmote ) { _listEmote = listEmote; }
        /** @brief 지금 튼 이모트의 동작 이름입니다. 없으면 빈 이름입니다. */
        const hashed_string& getActiveState() const { return _activeState; }

    private:
        /** @brief 이모트를 그치고 그 전 상태로 돌아갑니다. */
        void stop( SkeletalAnimatorComponent& animator );

    private:
        PROPERTY( Category = "Emote", DisplayName = "Emotes" )
        vector<EmoteDef> _listEmote;
        PROPERTY( RuntimeState, Category = "Emote", DisplayName = "Active State", Tooltip = "Runtime" )
        hashed_string _activeState;
        PROPERTY( RuntimeState, Category = "Emote", DisplayName = "Resume State", Tooltip = "Animator state to return to (runtime)" )
        hashed_string           _resumeState;
        vector<optional<int32>> _listButtonIndex; ///< `_listEmote` 와 같은 차례의 버튼 번호
    };
} // namespace sw
