#include "pch.h"

#include "GameFramework/Base/UI/UI/TutorialHintComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/UI/Screens/UiNotificationService.h"
#include "Engine/UI/UiSystem.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "GameFramework/Base/Foundation/Framework/GameService.h"

namespace sw
{
    namespace
    {
        struct TutorialHintComponentInternal
        {
            static constexpr utf8 kShowTutorialsSetting[] = "gameplay.showTutorials";
        };
    } // namespace

    TutorialHintComponent::TutorialHintComponent()
        : _pUiSystem{ nullptr }
        , _text{}
        , _durationSeconds{ 6.0f }
        , _activatorTag{}
        , _bOnce{ true }
        , _bFired{ false }
    {
    }

    void TutorialHintComponent::onOverlapBegin( const OverlapInfo& overlap )
    {
        Component::onOverlapBegin( overlap );
        // 상대의 트리거(감지 범위)는 활성자가 아니다 — 몸이 들어와야 한다.
        if ( overlap._bOtherTrigger == SW_FALSE )
            (void)activate( overlap._pOther );
    }

    void TutorialHintComponent::configure( string_view text, TagID activatorTag )
    {
        _text         = text;
        _activatorTag = activatorTag;
        _bFired       = false;
    }

    bool TutorialHintComponent::activate( const GameObject* pActivator )
    {
        if ( pActivator == nullptr || _text.empty() || ( _bOnce && _bFired ) )
            return false;
        if ( _activatorTag.isValid() && pActivator->hasTag( _activatorTag ) == false )
            return false;
        // 끈 플레이어에게는 띄우지 않는다 — 한 번 켜기를 바라는 볼륨도 발동한 것으로 치지 않는다(나중에 켜면 그때 보인다).
        const UserSettingsManager* pSettings = game::getService<UserSettingsManager>();
        const hashed_string        settingId( TutorialHintComponentInternal::kShowTutorialsSetting );
        if ( pSettings != nullptr && pSettings->findSetting( settingId ) != nullptr && pSettings->getBoolValue( settingId ) == false )
            return false;
        UiSystem* pUi = _pUiSystem != nullptr ? _pUiSystem : game::getService<UiSystem>();
        if ( pUi == nullptr )
            return false; // 서버처럼 UI 가 없다
        UiNotificationDesc desc{};
        desc._text            = _text;
        desc._durationSeconds = _durationSeconds;
        desc._kind            = UiNotificationKind::Hint;
        desc._priority        = 1; // 힌트는 지금 할 일이라 쌓인 알림보다 먼저
        pUi->getNotifications().post( desc );
        _bFired = true;
        return true;
    }
} // namespace sw
