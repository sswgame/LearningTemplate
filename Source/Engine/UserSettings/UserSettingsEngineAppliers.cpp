#include "pch.h"

#include "Engine/UserSettings/UserSettingsEngineAppliers.h"

#include "Core/String/StringUtil.h"

#include "Engine/Audio/IAudioSystem.h"
#include "Engine/Input/InputMap.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Localization/LocalizationManager.h"
#include "Engine/UserSettings/UserSettingsSchema.h"

namespace sw
{
    SW_LOG_CALLER( "UserSettings" );

    namespace
    {
        struct UserSettingsEngineAppliersInternal
        {
            static constexpr utf8 kHold[]       = "hold";
            static constexpr utf8 kToggle[]     = "toggle";
            static constexpr utf8 kWindowed[]   = "windowed";
            static constexpr utf8 kBorderless[] = "borderless";
            /** @brief 해상도 한 변의 범위입니다 — 0 · 터무니없는 값은 데이터 오타다. */
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
    UserSettingsEngineAppliers::UserSettingsEngineAppliers()
        : _targets{}
        , _displayRequest{}
        , _bDisplayDirty{ false }
    {
    }

    void UserSettingsEngineAppliers::initialize( const UserSettingsTargets& targets, UserSettingApplierRegistry& registry )
    {
        _targets = targets;
        registry.registerApplier( "audio.busVolume", SW_DELEGATE_METHOD( UserSettingApplierDelegate, &UserSettingsEngineAppliers::applyBusVolume, this ) );
        registry.registerApplier( "audio.mute", SW_DELEGATE_METHOD( UserSettingApplierDelegate, &UserSettingsEngineAppliers::applyMute, this ) );
        registry.registerApplier( "input.mouseSensitivity", SW_DELEGATE_METHOD( UserSettingApplierDelegate, &UserSettingsEngineAppliers::applyMouseSensitivity, this ) );
        registry.registerApplier( "input.gamepadSensitivity", SW_DELEGATE_METHOD( UserSettingApplierDelegate, &UserSettingsEngineAppliers::applyGamepadSensitivity, this ) );
        registry.registerApplier( "input.invertX", SW_DELEGATE_METHOD( UserSettingApplierDelegate, &UserSettingsEngineAppliers::applyInvertX, this ) );
        registry.registerApplier( "input.invertY", SW_DELEGATE_METHOD( UserSettingApplierDelegate, &UserSettingsEngineAppliers::applyInvertY, this ) );
        registry.registerApplier( "input.stickDeadzone", SW_DELEGATE_METHOD( UserSettingApplierDelegate, &UserSettingsEngineAppliers::applyStickDeadzone, this ) );
        registry.registerApplier( "input.toggleMode", SW_DELEGATE_METHOD( UserSettingApplierDelegate, &UserSettingsEngineAppliers::applyToggleMode, this ),
                                  SW_DELEGATE_FUNCTION( UserSettingValueFilterDelegate, &UserSettingsEngineAppliers::isToggleModeName ) );
        registry.registerApplier( "input.keyBinding", SW_DELEGATE_METHOD( UserSettingApplierDelegate, &UserSettingsEngineAppliers::applyKeyBinding, this ) );
        registry.registerApplier( "localization.language", SW_DELEGATE_METHOD( UserSettingApplierDelegate, &UserSettingsEngineAppliers::applyLanguage, this ) );
        registry.registerApplier( "display.windowMode", SW_DELEGATE_METHOD( UserSettingApplierDelegate, &UserSettingsEngineAppliers::applyWindowMode, this ),
                                  SW_DELEGATE_FUNCTION( UserSettingValueFilterDelegate, &UserSettingsEngineAppliers::isWindowModeName ) );
        registry.registerApplier( "display.resolution", SW_DELEGATE_METHOD( UserSettingApplierDelegate, &UserSettingsEngineAppliers::applyResolution, this ),
                                  SW_DELEGATE_FUNCTION( UserSettingValueFilterDelegate, &UserSettingsEngineAppliers::isResolutionText ) );
        registry.registerApplier( "display.vsync", SW_DELEGATE_METHOD( UserSettingApplierDelegate, &UserSettingsEngineAppliers::applyVSync, this ) );
        registry.registerOptionProvider( "localization.languages",
                                         SW_DELEGATE_METHOD( UserSettingOptionProviderDelegate, &UserSettingsEngineAppliers::collectLanguages, this ) );
    }

    void UserSettingsEngineAppliers::shutdown()
    {
        _targets       = UserSettingsTargets{};
        _bDisplayDirty = false;
    }

    bool UserSettingsEngineAppliers::consumeDisplayRequest( DisplaySettingsRequest& outRequest )
    {
        if ( _bDisplayDirty == false )
            return false;
        _bDisplayDirty = false;
        outRequest     = _displayRequest;
        return true;
    }

    bool UserSettingsEngineAppliers::tryParseResolution( string_view text, uint32& outWidth, uint32& outHeight )
    {
        using Internal    = UserSettingsEngineAppliersInternal;
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

    bool UserSettingsEngineAppliers::tryParseWindowMode( string_view text, WindowDisplayMode& outMode )
    {
        if ( StringUtil::equals( text, UserSettingsEngineAppliersInternal::kWindowed, true ) )
        {
            outMode = WindowDisplayMode::Windowed;
            return true;
        }
        if ( StringUtil::equals( text, UserSettingsEngineAppliersInternal::kBorderless, true ) )
        {
            outMode = WindowDisplayMode::BorderlessFullscreen;
            return true;
        }
        return false;
    }

    bool UserSettingsEngineAppliers::applyBusVolume( const UserSettingApplyContext& context )
    {
        if ( _targets._pAudioSystem == nullptr )
            return true;
        _targets._pAudioSystem->setBusVolume( hashed_string( context._param ), UserSettingsEngineAppliersInternal::toFloat( context._value, 1.0f ) );
        return true;
    }

    bool UserSettingsEngineAppliers::applyMute( const UserSettingApplyContext& context )
    {
        if ( _targets._pAudioSystem != nullptr )
            _targets._pAudioSystem->setMute( UserSettingsEngineAppliersInternal::toBool( context._value ) );
        return true;
    }

    bool UserSettingsEngineAppliers::applyMouseSensitivity( const UserSettingApplyContext& context )
    {
        const float32 sensitivity = UserSettingsEngineAppliersInternal::toFloat( context._value, 1.0f );
        if ( _targets._pInputMap != nullptr )
            _targets._pInputMap->setMouseSensitivity( float2{ sensitivity, sensitivity } );
        return true;
    }

    bool UserSettingsEngineAppliers::applyGamepadSensitivity( const UserSettingApplyContext& context )
    {
        const float32 sensitivity = UserSettingsEngineAppliersInternal::toFloat( context._value, 1.0f );
        if ( _targets._pInputMap != nullptr )
            _targets._pInputMap->setGamepadSensitivity( float2{ sensitivity, sensitivity } );
        return true;
    }

    bool UserSettingsEngineAppliers::applyInvertX( const UserSettingApplyContext& context )
    {
        if ( _targets._pInputMap != nullptr )
            _targets._pInputMap->setInvertX( UserSettingsEngineAppliersInternal::toBool( context._value ) );
        return true;
    }

    bool UserSettingsEngineAppliers::applyInvertY( const UserSettingApplyContext& context )
    {
        if ( _targets._pInputMap != nullptr )
            _targets._pInputMap->setInvertY( UserSettingsEngineAppliersInternal::toBool( context._value ) );
        return true;
    }

    bool UserSettingsEngineAppliers::applyStickDeadzone( const UserSettingApplyContext& context )
    {
        if ( _targets._pInputMap != nullptr )
            _targets._pInputMap->setStickDeadzoneOverride( UserSettingsEngineAppliersInternal::toFloat( context._value, -1.0f ) );
        return true;
    }

    bool UserSettingsEngineAppliers::applyToggleMode( const UserSettingApplyContext& context )
    {
        const hashed_string action( context._param );
        // 액션은 게임이 입력 맵을 세운 뒤에야 있다. 없으면 건너뛰고, 게임이 맵을 세운 뒤의 다시 적용(`reapplyAll`)에서 닿는다.
        if ( _targets._pInputMap == nullptr || _targets._pInputMap->hasAction( action ) == false )
            return true;
        _targets._pInputMap->setToggleMode( action, StringUtil::equals( context._value, UserSettingsEngineAppliersInternal::kToggle, true ) );
        return true;
    }

    bool UserSettingsEngineAppliers::applyKeyBinding( const UserSettingApplyContext& context )
    {
        if ( _targets._pInputMap == nullptr || context._pDef == nullptr )
            return true;
        const UserSettingDef& def = *context._pDef;
        if ( _targets._pInputMap->hasAction( def._action ) == false )
            return true;

        // 빈 값은 "입력 맵의 기본 바인딩" 이다 — 기본으로 되돌린 설정이 지난번 리바인딩을 그대로 남기면 안 된다.
        InputSlot  slot;
        const bool bHasSlot = context._value.empty() ? _targets._pInputMap->findDefaultRebindSlot( def._action, def._bindIndex, slot )
                                                     : InputSlotUtil::tryParse( context._value, slot );
        if ( bHasSlot == false )
        {
            SW_LOG_WARNING( "key binding '%#': action '%#' binding %# cannot take '%#'", def._id.c_str(), def._action.c_str(), def._bindIndex, context._value );
            return false;
        }
        return _targets._pInputMap->rebindSlot( def._action, slot, def._bindIndex );
    }

    bool UserSettingsEngineAppliers::applyLanguage( const UserSettingApplyContext& context )
    {
        if ( _targets._pLocalizationManager == nullptr || context._value.empty() )
            return true;
        LocalizationManager& localization = *_targets._pLocalizationManager;
        if ( localization.hasLanguage( context._value ) == false )
        {
            // 기동 적용은 게임이 언어 팩을 읽기 전이다 — 게임이 읽은 뒤의 다시 적용에서 닿는다.
            if ( context._bStartup == false )
                SW_LOG_WARNING( "language '%#' is not loaded", context._value );
            return context._bStartup;
        }
        return localization.setCurrentLanguage( context._value );
    }

    bool UserSettingsEngineAppliers::applyWindowMode( const UserSettingApplyContext& context )
    {
        WindowDisplayMode mode{ WindowDisplayMode::Windowed };
        if ( tryParseWindowMode( context._value, mode ) == false )
            return false;
        _displayRequest._mode     = mode;
        _displayRequest._bHasMode = _displayRequest._bHasMode || context._bDefault == false;
        _bDisplayDirty            = _bDisplayDirty || context._bStartup == false;
        return true;
    }

    bool UserSettingsEngineAppliers::applyResolution( const UserSettingApplyContext& context )
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

    bool UserSettingsEngineAppliers::applyVSync( const UserSettingApplyContext& context )
    {
        _displayRequest._bVSync    = UserSettingsEngineAppliersInternal::toBool( context._value );
        _displayRequest._bHasVSync = _displayRequest._bHasVSync || context._bDefault == false;
        _bDisplayDirty             = _bDisplayDirty || context._bStartup == false;
        return true;
    }

    void UserSettingsEngineAppliers::collectLanguages( vector<UserSettingOption>& outListOption )
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

    bool UserSettingsEngineAppliers::isToggleModeName( string_view value )
    {
        return StringUtil::equals( value, UserSettingsEngineAppliersInternal::kHold, true ) ||
               StringUtil::equals( value, UserSettingsEngineAppliersInternal::kToggle, true );
    }

    bool UserSettingsEngineAppliers::isWindowModeName( string_view value )
    {
        WindowDisplayMode mode{ WindowDisplayMode::Windowed };
        return tryParseWindowMode( value, mode );
    }

    bool UserSettingsEngineAppliers::isResolutionText( string_view value )
    {
        uint32 width{ 0 };
        uint32 height{ 0 };
        return tryParseResolution( value, width, height );
    }
} // namespace sw
