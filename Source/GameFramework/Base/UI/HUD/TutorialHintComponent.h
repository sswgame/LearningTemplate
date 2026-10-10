/**
 * @file TutorialHintComponent.h
 * @brief 닿으면 튜토리얼 힌트("[action=Interact] 로 문 열기")를 알림으로 띄우는 볼륨입니다 — 글의 행동 태그는 지금 장치의 버튼 글리프가 됩니다.
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/String/TagID.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"

namespace sw
{
    class UiSystem;

    /**
     * @class TutorialHintComponent
     * @brief 같은 오브젝트의 트리거 콜라이더에 활성자가 들어오면 `UiSystem` 알림(종류 Hint)으로 힌트를 띄웁니다(기본 한 번).
     * @details 사용자 설정 `gameplay.showTutorials` 가 꺼져 있으면 띄우지 않습니다(설정 서비스가 없으면 켜진 것으로 본다). 글은 현지화 키 또는 글 그대로이고
     *          리치 텍스트 행동 태그 `[action=이름]` 을 쓸 수 있다 — 키보드면 `[ E ]`, 패드면 그 패드의 버튼(입력 장치가 바뀌면 다시 배치된다).
     *          언리얼 Lyra 의 튜토리얼 프롬프트 · 유니티 튜토리얼 트리거 스크립트의 자리입니다.
     */
    REFLECT( Category = "UI", DisplayName = "Tutorial Hint", Tooltip = "Shows a tutorial hint notification when the activator enters this object's trigger" )
    class SW_GF_API TutorialHintComponent : public Component
    {
    public:
        REFLECT_BODY();

        TutorialHintComponent();
        ~TutorialHintComponent() override = default;

        void onOverlapBegin( const OverlapInfo& overlap ) override;

        /** @brief 글 · 활성자 태그를 둡니다(코드로 세운 볼륨). 발동 표시를 지운다. */
        void configure( string_view text, TagID activatorTag );
        /** @brief 활성자가 들어온 것으로 칩니다(겹침 이벤트 · 시험). 띄웠으면 true 입니다. */
        bool activate( const GameObject* pActivator );
        /** @brief 띄울 UI 시스템을 정합니다(시험 — 없으면 게임 서비스). */
        void setUiSystem( UiSystem* pUiSystem ) { _pUiSystem = pUiSystem; }
        bool hasFired() const { return _bFired; }

    private:
        UiSystem* _pUiSystem; ///< 시험이 넘긴 UI(없으면 게임 서비스)
        PROPERTY( Category = "Hint", DisplayName = "Text", Meta = "Localizable", Tooltip = "Hint text or localization key; [action=Name] shows the current input glyph" )
        string _text;
        PROPERTY( Category = "Hint", DisplayName = "Duration", Min = 0.5, Units = s )
        float32 _durationSeconds;
        PROPERTY( Category = "Hint", DisplayName = "Activator Tag", Tooltip = "Only objects with this tag activate it (empty: any object)" )
        TagID _activatorTag;
        PROPERTY( Category = "Hint", DisplayName = "Once", Tooltip = "Show only the first time" )
        bool _bOnce;
        bool _bFired;
    };
} // namespace sw
