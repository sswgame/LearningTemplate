/**
 * @file TutorialHintComponent.h
 * @brief 닿으면 튜토리얼 힌트("[action=Interact] 로 문 열기")를 알림으로 띄우는 볼륨입니다. 글의 행동 태그는 지금 장치의 버튼 글리프가 됩니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Foundation/Types.h"
#include "Core/Name/TagID.h"

#include "Engine/Reflection/ReflectionMacros.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Runtime/Foundation/Framework/ActivatorVolumeComponent.h"

namespace sw
{
    class IPlayerNotificationSink;

    /**
     * @class TutorialHintComponent
     * @brief 활성자가 들어오면(`ActivatorVolumeComponent` — 겹침 또는 거리) 플레이어 알림 창구(`IPlayerNotificationSink`, 종류 Hint — 구현은 UI 의 알림 화면)로 힌트를 띄웁니다(기본 한 번). UI 를 모르므로 전용 서버도 짓고, 서버는 창구가 없어 발동하지 않는다.
     * @details 사용자 설정 `gameplay.showTutorials` 가 꺼져 있으면 띄우지 않습니다(설정 서비스가 없으면 켜진 것으로 본다). 글은 현지화 키 또는 글 그대로이고
     *          리치 텍스트 행동 태그 `[action=이름]` 을 쓸 수 있다. 키보드면 `[ E ]`, 패드면 그 패드의 버튼(입력 장치가 바뀌면 다시 배치된다).
     *          언리얼 Lyra 의 튜토리얼 프롬프트, 유니티 튜토리얼 트리거 스크립트에 해당합니다.
     */
    REFLECT( Category = "UI", DisplayName = "Tutorial Hint", Tooltip = "Shows a tutorial hint notification when the activator enters this object's trigger" )
    class SW_GF_API TutorialHintComponent : public ActivatorVolumeComponent
    {
    public:
        REFLECT_BODY();

        TutorialHintComponent();
        ~TutorialHintComponent() override = default;

        /** @brief 글과 활성자 태그를 정하고 발동 표시를 지웁니다. 코드로 만든 볼륨이 씁니다. */
        void configure( string_view text, TagID activatorTag );
        /** @brief 알림을 보낼 곳을 정합니다. 테스트가 쓰며, 없으면 게임 서비스(`IPlayerNotificationSink`)를 씁니다. */
        void setNotificationSink( IPlayerNotificationSink* pNotificationSink ) { _pNotificationSink = pNotificationSink; }

    protected:
        [[nodiscard]] bool onVolumeActivated( const GameObject& activator ) override;

    private:
        IPlayerNotificationSink* _pNotificationSink; ///< 테스트가 넘긴 알림 창구(없으면 게임 서비스)
        PROPERTY( Category = "Hint", DisplayName = "Text", Meta = "Localizable", Tooltip = "Hint text or localization key; [action=Name] shows the current input glyph" )
        string _text;
        PROPERTY( Category = "Hint", DisplayName = "Duration", Min = 0.5, Units = s )
        float32 _durationSeconds;
    };
} // namespace sw
