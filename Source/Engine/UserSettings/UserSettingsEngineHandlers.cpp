#include "pch.h"

#include "Engine/UserSettings/UserSettingsEngineHandlers.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/formatString.h"

#include "Engine/Audio/IAudioService.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/UserSettings/HardwareProbe.h"
#include "Engine/UserSettings/UserSettingsSchema.h"

namespace sw
{
    SW_LOG_CALLER( "UserSettings" );

    namespace
    {
        struct UserSettingsEngineHandlersInternal
        {
            static constexpr utf8 kHold[]       = "hold";
            static constexpr utf8 kToggle[]     = "toggle";
            static constexpr utf8 kWindowed[]   = "windowed";
            static constexpr utf8 kBorderless[] = "borderless";
            /** @brief 스피커 모드의 설정 값입니다(`AudioSpeakerMode` 순서). */
            static constexpr const utf8* kArrSpeakerModeName[] = { "auto", "stereo", "surround51", "surround71", "headphones" };
            /** @brief 해상도 한 변의 범위입니다. 0 이나 터무니없는 값은 데이터 오타로 봅니다. */
            static constexpr uint32 kMinResolution = 320;
            static constexpr uint32 kMaxResolution = 16384;

            static float32 toFloat( string_view value, float32 fallback )
            {
                float32 number = fallback;
                return StringUtil::parseFloat( value, number ) ? number : fallback;
            }

            static bool toBool( string_view value ) { return StringUtil::parseBool( value, false ); }
        };
    } // namespace
} // namespace sw

namespace sw
{
    UserSettingsEngineHandlers::UserSettingsEngineHandlers()
        : _targets{}
        , _listBindingInputMap{}
        , _displayRequest{}
        , _listDisplayResolution{}
        , _bDisplayDirty{ false }
        , _bResolutionProbed{ false }
    {
    }

    void UserSettingsEngineHandlers::initialize( const UserSettingsTargets& targets, UserSettingsHandlerRegistry& registry )
    {
        _targets = targets;
        registry.registerHandler( "audio.busVolume", SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsEngineHandlers::applyBusVolume, this ) );
        registry.registerHandler( "audio.mute", SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsEngineHandlers::applyMute, this ) );
        registry.registerHandler( "audio.speakerMode", SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsEngineHandlers::applySpeakerMode, this ),
                                  SW_DELEGATE_FUNCTION( UserSettingValueFilterDelegate, &UserSettingsEngineHandlers::isSpeakerModeName ) );
        registry.registerHandler( "input.mouseSensitivity", SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsEngineHandlers::applyMouseSensitivity, this ) );
        registry.registerHandler( "input.gamepadSensitivity", SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsEngineHandlers::applyGamepadSensitivity, this ) );
        registry.registerHandler( "input.invertX", SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsEngineHandlers::applyInvertX, this ) );
        registry.registerHandler( "input.invertY", SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsEngineHandlers::applyInvertY, this ) );
        registry.registerHandler( "input.stickDeadzone", SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsEngineHandlers::applyStickDeadzone, this ) );
        registry.registerHandler( "input.toggleMode", SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsEngineHandlers::applyToggleMode, this ),
                                  SW_DELEGATE_FUNCTION( UserSettingValueFilterDelegate, &UserSettingsEngineHandlers::isToggleModeName ) );
        registry.registerHandler( "input.keyBinding", SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsEngineHandlers::applyKeyBinding, this ) );
        registry.registerHandler( "localization.language", SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsEngineHandlers::applyLanguage, this ) );
        registry.registerHandler( "display.windowMode", SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsEngineHandlers::applyWindowMode, this ),
                                  SW_DELEGATE_FUNCTION( UserSettingValueFilterDelegate, &UserSettingsEngineHandlers::isWindowModeName ) );
        registry.registerHandler( "display.resolution", SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsEngineHandlers::applyResolution, this ),
                                  SW_DELEGATE_FUNCTION( UserSettingValueFilterDelegate, &UserSettingsEngineHandlers::isResolutionText ) );
        registry.registerHandler( "display.vsync", SW_DELEGATE_METHOD( UserSettingHandlerDelegate, &UserSettingsEngineHandlers::applyVSync, this ) );
        registry.registerOptionProvider( "display.resolutions",
                                         SW_DELEGATE_METHOD( UserSettingOptionProviderDelegate, &UserSettingsEngineHandlers::collectResolutions, this ) );
        registry.registerOptionProvider( "localization.languages",
                                         SW_DELEGATE_METHOD( UserSettingOptionProviderDelegate, &UserSettingsEngineHandlers::collectLanguages, this ) );
    }

    void UserSettingsEngineHandlers::shutdown()
    {
        _targets = UserSettingsTargets{};
        _listBindingInputMap.clear();
        _bDisplayDirty = false;
    }

    void UserSettingsEngineHandlers::addBindingInputMap( InputMap& inputMap )
    {
        for ( const InputMap* pInputMap : _listBindingInputMap )
        {
            if ( pInputMap == &inputMap )
                return;
        }
        _listBindingInputMap.push_back( &inputMap );
    }

    void UserSettingsEngineHandlers::removeBindingInputMap( const InputMap& inputMap )
    {
        for ( size_t index = 0; index < _listBindingInputMap.size(); ++index )
        {
            if ( _listBindingInputMap[index] != &inputMap )
                continue;
            _listBindingInputMap.erase( _listBindingInputMap.begin() + static_cast<ptrdiff_t>( index ) );
            return;
        }
    }

    InputMap* UserSettingsEngineHandlers::findBindingInputMap( const hashed_string& action ) const
    {
        if ( _targets._pInputMap != nullptr && _targets._pInputMap->hasAction( action ) )
            return _targets._pInputMap;
        for ( InputMap* pInputMap : _listBindingInputMap )
        {
            if ( pInputMap->hasAction( action ) )
                return pInputMap;
        }
        return nullptr;
    }

    bool UserSettingsEngineHandlers::consumeDisplayRequest( DisplaySettingsRequest& outRequest )
    {
        if ( _bDisplayDirty == false )
            return false;
        _bDisplayDirty = false;
        outRequest     = _displayRequest;
        return true;
    }

    bool UserSettingsEngineHandlers::tryParseResolution( string_view text, uint32& outWidth, uint32& outHeight )
    {
        using Internal    = UserSettingsEngineHandlersInternal;
        const size_t xPos = text.find_first_of( "xX" );
        if ( xPos == string_view::npos )
            return false;
        int32 width{ 0 };
        int32 height{ 0 };
        if ( StringUtil::parseInt( text.substr( 0, xPos ), width ) == false || StringUtil::parseInt( text.substr( xPos + 1 ), height ) == false )
            return false;
        const bool bWidthInRange  = static_cast<int32>( Internal::kMinResolution ) <= width && width <= static_cast<int32>( Internal::kMaxResolution );
        const bool bHeightInRange = static_cast<int32>( Internal::kMinResolution ) <= height && height <= static_cast<int32>( Internal::kMaxResolution );
        if ( bWidthInRange == false || bHeightInRange == false )
            return false;
        outWidth  = static_cast<uint32>( width );
        outHeight = static_cast<uint32>( height );
        return true;
    }

    bool UserSettingsEngineHandlers::tryParseWindowMode( string_view text, WindowDisplayMode& outMode )
    {
        if ( StringUtil::equals( text, UserSettingsEngineHandlersInternal::kWindowed, true ) )
        {
            outMode = WindowDisplayMode::Windowed;
            return true;
        }
        if ( StringUtil::equals( text, UserSettingsEngineHandlersInternal::kBorderless, true ) )
        {
            outMode = WindowDisplayMode::BorderlessFullscreen;
            return true;
        }
        return false;
    }

    bool UserSettingsEngineHandlers::applyBusVolume( const UserSettingApplyParams& context )
    {
        if ( _targets._pAudioService == nullptr )
            return true;
        _targets._pAudioService->setBusVolume( hashed_string( context._param ), UserSettingsEngineHandlersInternal::toFloat( context._value, 1.0f ) );
        return true;
    }

    bool UserSettingsEngineHandlers::applyMute( const UserSettingApplyParams& context )
    {
        if ( _targets._pAudioService != nullptr )
            _targets._pAudioService->setMute( UserSettingsEngineHandlersInternal::toBool( context._value ) );
        return true;
    }

    bool UserSettingsEngineHandlers::tryParseSpeakerMode( string_view text, AudioSpeakerMode& outMode )
    {
        using Internal = UserSettingsEngineHandlersInternal;
        for ( uint32 modeIndex = 0; modeIndex < SW_COUNT_OF( Internal::kArrSpeakerModeName ); ++modeIndex )
        {
            if ( StringUtil::equals( text, Internal::kArrSpeakerModeName[modeIndex], true ) )
            {
                outMode = static_cast<AudioSpeakerMode>( modeIndex );
                return true;
            }
        }
        return false;
    }

    bool UserSettingsEngineHandlers::applySpeakerMode( const UserSettingApplyParams& context )
    {
        AudioSpeakerMode mode{ AudioSpeakerMode::Auto };
        if ( tryParseSpeakerMode( context._value, mode ) == false )
            return false;
        if ( _targets._pAudioService == nullptr )
            return true;
        // 장치가 그만큼의 채널을 갖지 않으면 서비스가 오류를 남기고 거절한다(지금 구성을 그대로 쓴다).
        return _targets._pAudioService->setSpeakerMode( mode );
    }

    bool UserSettingsEngineHandlers::isSpeakerModeName( string_view value )
    {
        AudioSpeakerMode mode{ AudioSpeakerMode::Auto };
        return tryParseSpeakerMode( value, mode );
    }

    bool UserSettingsEngineHandlers::applyMouseSensitivity( const UserSettingApplyParams& context )
    {
        const float32 sensitivity = UserSettingsEngineHandlersInternal::toFloat( context._value, 1.0f );
        if ( _targets._pInputMap != nullptr )
            _targets._pInputMap->getPlayerSettings()._mouseSensitivity = float2{ sensitivity, sensitivity };
        return true;
    }

    bool UserSettingsEngineHandlers::applyGamepadSensitivity( const UserSettingApplyParams& context )
    {
        const float32 sensitivity = UserSettingsEngineHandlersInternal::toFloat( context._value, 1.0f );
        if ( _targets._pInputMap != nullptr )
            _targets._pInputMap->getPlayerSettings()._gamepadSensitivity = float2{ sensitivity, sensitivity };
        return true;
    }

    bool UserSettingsEngineHandlers::applyInvertX( const UserSettingApplyParams& context )
    {
        if ( _targets._pInputMap != nullptr )
            _targets._pInputMap->getPlayerSettings()._bInvertX = UserSettingsEngineHandlersInternal::toBool( context._value );
        return true;
    }

    bool UserSettingsEngineHandlers::applyInvertY( const UserSettingApplyParams& context )
    {
        if ( _targets._pInputMap != nullptr )
            _targets._pInputMap->getPlayerSettings()._bInvertY = UserSettingsEngineHandlersInternal::toBool( context._value );
        return true;
    }

    bool UserSettingsEngineHandlers::applyStickDeadzone( const UserSettingApplyParams& context )
    {
        if ( _targets._pInputMap != nullptr )
            _targets._pInputMap->getPlayerSettings()._stickDeadzoneOverride = UserSettingsEngineHandlersInternal::toFloat( context._value, -1.0f );
        return true;
    }

    bool UserSettingsEngineHandlers::applyToggleMode( const UserSettingApplyParams& context )
    {
        const hashed_string action( context._param );
        // 액션은 게임이 입력 맵을 만든 뒤에야 있다. 없으면 건너뛰고, 게임이 맵을 만든 뒤의 다시 적용(`reapplyAll`)에서 적용된다.
        if ( _targets._pInputMap == nullptr || _targets._pInputMap->hasAction( action ) == false )
            return true;
        _targets._pInputMap->setToggleMode( action, StringUtil::equals( context._value, UserSettingsEngineHandlersInternal::kToggle, true ) );
        return true;
    }

    bool UserSettingsEngineHandlers::applyKeyBinding( const UserSettingApplyParams& context )
    {
        if ( context._pDef == nullptr )
            return true;
        const UserSettingDef& def = *context._pDef;
        // 액션은 게임 · UI 가 입력 맵을 만든 뒤에야 있다. 없으면 건너뛰고, 맵이 생긴 뒤의 다시 적용(`reapplyAll` · `addKeyBindingInputMap`)에서 적용된다.
        InputMap* pInputMap = findBindingInputMap( def._action );
        if ( pInputMap == nullptr )
            return true;

        // 빈 값은 "입력 맵의 기본 바인딩" 이다. 기본으로 되돌린 설정이 지난번 리바인딩을 그대로 남기면 안 된다.
        InputSlot  slot;
        const bool bHasSlot = context._value.empty() ? pInputMap->findDefaultRebindSlot( def._action, def._bindIndex, slot ) : InputSlotUtil::tryParse( context._value, slot );
        if ( bHasSlot == false )
        {
            SW_LOG_WARNING( "key binding '%#': action '%#' binding %# cannot take '%#'", def._id.c_str(), def._action.c_str(), def._bindIndex, context._value );
            return false;
        }
        return pInputMap->rebindSlot( def._action, slot, def._bindIndex );
    }

    bool UserSettingsEngineHandlers::applyLanguage( const UserSettingApplyParams& context )
    {
        if ( _targets._pLocalizationManager == nullptr || context._value.empty() )
            return true;
        LocalizationManager& localization = *_targets._pLocalizationManager;
        if ( localization.hasLanguage( context._value ) == false )
        {
            // 기동 적용은 게임이 언어 팩을 읽기 전이다. 게임이 읽은 뒤의 다시 적용에서 적용된다.
            if ( context._bStartup == false )
                SW_LOG_WARNING( "language '%#' is not loaded", context._value );
            return context._bStartup;
        }
        return localization.setCurrentLanguage( context._value );
    }

    bool UserSettingsEngineHandlers::applyWindowMode( const UserSettingApplyParams& context )
    {
        WindowDisplayMode mode{ WindowDisplayMode::Windowed };
        if ( tryParseWindowMode( context._value, mode ) == false )
            return false;
        _displayRequest._mode     = mode;
        _displayRequest._bHasMode = _displayRequest._bHasMode || context._bDefault == false;
        _bDisplayDirty            = _bDisplayDirty || context._bStartup == false;
        return true;
    }

    bool UserSettingsEngineHandlers::applyResolution( const UserSettingApplyParams& context )
    {
        uint32 width{ 0 };
        uint32 height{ 0 };
        if ( tryParseResolution( context._value, width, height ) == false )
            return false;
        _displayRequest._width          = width;
        _displayRequest._height         = height;
        _displayRequest._bHasResolution = _displayRequest._bHasResolution || context._bDefault == false;
        _bDisplayDirty                  = _bDisplayDirty || context._bStartup == false;
        return true;
    }

    bool UserSettingsEngineHandlers::applyVSync( const UserSettingApplyParams& context )
    {
        _displayRequest._bVSync    = UserSettingsEngineHandlersInternal::toBool( context._value );
        _displayRequest._bHasVSync = _displayRequest._bHasVSync || context._bDefault == false;
        _bDisplayDirty             = _bDisplayDirty || context._bStartup == false;
        return true;
    }

    void UserSettingsEngineHandlers::collectLanguages( vector<UserSettingOption>& outListOption )
    {
        outListOption.clear();
        if ( _targets._pLocalizationManager == nullptr )
            return;
        for ( const string& language : _targets._pLocalizationManager->getAvailableLanguages() )
        {
            UserSettingOption option;
            option._value = language;
            outListOption.push_back( option );
        }
    }

    void UserSettingsEngineHandlers::collectResolutions( vector<UserSettingOption>& outListOption )
    {
        outListOption.clear();
        // 모니터 모드 열거는 수백 줄을 훑는다 — 메뉴가 그릴 때마다 부르므로 처음 한 번만 읽는다.
        if ( _bResolutionProbed == false )
        {
            HardwareProbe::probeDisplayResolutions( _listDisplayResolution );
            _bResolutionProbed = true;
        }
        for ( const int2& resolution : _listDisplayResolution )
        {
            UserSettingOption option;
            option._value = formatResolution( resolution._x, resolution._y );
            outListOption.push_back( option );
        }
    }

    string UserSettingsEngineHandlers::formatResolution( int32 width, int32 height )
    {
        utf8 arrText[constant::kMaxBuffer32]{};
        formatstring( arrText, constant::kMaxBuffer32, "%#x%#", width, height );
        return string( arrText );
    }

    bool UserSettingsEngineHandlers::isToggleModeName( string_view value )
    {
        return StringUtil::equals( value, UserSettingsEngineHandlersInternal::kHold, true ) ||
               StringUtil::equals( value, UserSettingsEngineHandlersInternal::kToggle, true );
    }

    bool UserSettingsEngineHandlers::isWindowModeName( string_view value )
    {
        WindowDisplayMode mode{ WindowDisplayMode::Windowed };
        return tryParseWindowMode( value, mode );
    }

    bool UserSettingsEngineHandlers::isResolutionText( string_view value )
    {
        uint32 width{ 0 };
        uint32 height{ 0 };
        return tryParseResolution( value, width, height );
    }
} // namespace sw
