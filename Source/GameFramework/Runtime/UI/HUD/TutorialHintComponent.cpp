#include "pch.h"

#include "GameFramework/Runtime/UI/HUD/TutorialHintComponent.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/UI/Screen/UINotificationService.h"
#include "Engine/UI/UISystem.h"
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
        : _pUISystem{ nullptr }
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
            const std::optional<UserSettingKey<bool>> showTutorialsKey = pSettings->resolveKey<bool>( hashed_string( TutorialHintComponentInternal::kShowTutorialsSetting ) );
            if ( showTutorialsKey.has_value() == false || pSettings->getBoolValue( *showTutorialsKey ) == false )
                return false;
        }
        UISystem* pUI = _pUISystem != nullptr ? _pUISystem : game::findSubsystem<UISystem>();
        if ( pUI == nullptr )
            return false; // 서버처럼 UI 가 없다
        UINotificationDef desc{};
        desc._text            = _text;
        desc._durationSeconds = _durationSeconds;
        desc._kind            = UINotificationKind::Hint;
        desc._priority        = 1; // 힌트는 지금 할 일이라 쌓인 알림보다 먼저
        game::getSubsystem<UINotificationService>().post( desc );
        return true;
    }
} // namespace sw
