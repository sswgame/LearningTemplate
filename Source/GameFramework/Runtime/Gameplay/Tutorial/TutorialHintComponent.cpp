#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Tutorial/TutorialHintComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Presentation/IPlayerNotificationSink.h"
#include "Engine/UserSettings/UserSettingsManager.h"

#include "GameFramework/Runtime/Foundation/Framework/GameService.h"

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
        : _pNotificationSink{ nullptr }
        , _text{}
        , _durationSeconds{ 6.0f }
    {
    }

    void TutorialHintComponent::configure( string_view text, TagID activatorTag )
    {
        _text = text;
        resetActivation( activatorTag );
    }

    bool TutorialHintComponent::onVolumeActivated( const GameObject& activator )
    {
        (void)activator;
        if ( _text.empty() )
            return false;
        // 끈 플레이어에게는 띄우지 않는다 — 한 번 켜기를 바라는 볼륨도 발동한 것으로 치지 않는다(나중에 켜면 그때 보인다).
        // 설정은 엔진 스키마에 있다 — 없거나 타입이 다르면 `resolveKey` 가 오류로 알리고 띄우지 않는다(데이터 결함을 "켜짐" 으로 덮지 않는다).
        const UserSettingsManager* pSettings = game::findSubsystem<UserSettingsManager>();
        if ( pSettings != nullptr )
        {
            const auto showTutorialsKey = pSettings->resolveKey<bool>( hashed_string( TutorialHintComponentInternal::kShowTutorialsSetting ) );
            if ( showTutorialsKey.has_value() == false || pSettings->getBoolValue( *showTutorialsKey ) == false )
                return false;
        }
        IPlayerNotificationSink* pNotices = _pNotificationSink != nullptr ? _pNotificationSink : game::findSubsystem<IPlayerNotificationSink>();
        if ( pNotices == nullptr )
            return false; // 서버처럼 알림 화면이 없다
        pNotices->postNotice( _text, _durationSeconds, PlayerNotificationKind::Hint );
        return true;
    }
} // namespace sw
