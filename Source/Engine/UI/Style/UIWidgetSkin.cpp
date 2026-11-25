#include "pch.h"

#include "Engine/UI/Style/UIWidgetSkin.h"

#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Asset/Cache/BuiltInAssetCache.h"
#include "Engine/Asset/EngineDefaultAssetsSubsystem.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Reflection/TypeInfo.h"
#include "Engine/UI/Style/UIStyleSheet.h"
#include "Engine/UI/Style/WidgetStyle.h"
#include "Engine/UI/WidgetTree/WidgetTypes.h"

namespace sw
{
    namespace
    {
        struct UIWidgetSkinInternal
        {
            /** @brief 시트에서 푼 자리 하나(타입 · 부품 클래스 · 상태)의 값입니다. */
            struct Slot
            {
                const utf8* _pTypeName{ nullptr };
                const utf8* _pPart{ nullptr }; ///< 부품 클래스. nullptr 이면 위젯 자신
                uint32      _states{ 0 };
                WidgetStyle _value{};
                uint32      _setMask{ 0 }; ///< 정해진 필드(상태의 바탕 규칙까지)
                uint32      _ownMask{ 0 }; ///< 이 상태를 적은 규칙이 정한 필드
            };

            /** @brief 시트를 읽어 칸을 채우고 첫 빠진 칸을 기억합니다. */
            struct Reader
            {
                const UIStyleSheetAsset& _sheet;
                string                   _missing{}; ///< 첫 빠진 칸(`ButtonWidget:hover _backgroundColor`). 없으면 빈 글

                Slot find( const utf8* pTypeName, const utf8* pPart = nullptr, uint32 states = UIStyleState::kNone ) const
                {
                    Slot slot;
                    slot._pTypeName = pTypeName;
                    slot._pPart     = pPart;
                    slot._states    = states;
                    // 특정도가 낮은 것부터(상태 없는 규칙 → 상태 규칙) 쓴다. 규칙의 조건은 타입 · 부품 · 상태뿐이라 상태 비트가 많은 것이 특정도가 높다.
                    applyRules( slot, UIStyleState::kNone );
                    if ( states != UIStyleState::kNone )
                        applyRules( slot, states );
                    return slot;
                }

                template <UIStyleField Field, typename T>
                void read( const Slot& slot, T& outValue, bool bOwnState = false )
                {
                    using Member = UIStyleFieldMember<Field>;
                    static_assert( std::is_same_v<T, typename Member::Type>, "the value type must match the WidgetStyle member of this style field" );
                    if ( ( ( bOwnState ? slot._ownMask : slot._setMask ) & UIStyleFieldTable::makeBit( Field ) ) != 0 )
                        outValue = slot._value.*Member::kMember;
                    else
                        reportMissing( slot, Field );
                }

                /** @brief 배경색과 모서리입니다. 상태 자리는 자기 규칙이 배경색을 적어야 한다 — 빠진 상태가 바탕 색을 말없이 빌리지 않게. */
                void readBrush( const Slot& slot, UIBrush& outBrush )
                {
                    read<UIStyleField::BackgroundColor>( slot, outBrush._color, slot._states != UIStyleState::kNone );
                    read<UIStyleField::CornerRadius>( slot, outBrush._cornerRadius );
                }
                void readBorderBrush( const Slot& slot, UIBrush& outBrush )
                {
                    readBrush( slot, outBrush );
                    read<UIStyleField::BorderColor>( slot, outBrush._borderColor );
                    read<UIStyleField::BorderWidth>( slot, outBrush._borderWidth );
                }
                void readStateBrushes( const utf8* pTypeName, UIWidgetSkin::StateBrushes& outBrushes )
                {
                    readBrush( find( pTypeName ), outBrushes._normal );
                    readBrush( find( pTypeName, nullptr, UIStyleState::kHover ), outBrushes._hovered );
                    readBrush( find( pTypeName, nullptr, UIStyleState::kPressed ), outBrushes._pressed );
                    readBrush( find( pTypeName, nullptr, UIStyleState::kDisabled ), outBrushes._disabled );
                }
                void readArrow( const utf8* pTypeName, UIWidgetSkin::Arrow& outArrow )
                {
                    const Slot slot = find( pTypeName, "arrow" );
                    read<UIStyleField::BackgroundColor>( slot, outArrow._color );
                    float2 size{};
                    read<UIStyleField::Size>( slot, size );
                    outArrow._areaWidth = size._x;
                    read<UIStyleField::Opacity>( find( pTypeName, "arrow", UIStyleState::kDisabled ), outArrow._disabledAlpha, true );
                }

            private:
                void applyRules( Slot& inoutSlot, uint32 states ) const
                {
                    const hashed_string typeName( inoutSlot._pTypeName );
                    for ( const UIStyleRule& rule : _sheet._listRule )
                    {
                        if ( rule._selector._listPart.size() != 1 )
                            continue;
                        const UIStyleSelectorPart& part  = rule._selector._listPart[0];
                        const bool                 bPart = inoutSlot._pPart == nullptr ? part._listClass.empty() : part._listClass.size() == 1 && part._listClass[0] == hashed_string( inoutSlot._pPart );
                        if ( part._typeName != typeName || part._name.empty() == false || bPart == false || part._stateMask != states )
                            continue;
                        for ( const UIStyleAssignment& assignment : rule._listAssignment )
                        {
                            UIStyleSheetAsset::applyAssignment( assignment, inoutSlot._value );
                            inoutSlot._setMask |= UIStyleFieldTable::makeBit( assignment._field );
                            if ( states == inoutSlot._states )
                                inoutSlot._ownMask |= UIStyleFieldTable::makeBit( assignment._field );
                        }
                    }
                }

                void reportMissing( const Slot& slot, UIStyleField field )
                {
                    if ( _missing.empty() == false )
                        return;
                    StringBuilder<constant::buffer::kMax128> builder;
                    builder.appendFormat( "%#%#%#%# %#", slot._pTypeName, slot._pPart != nullptr ? "." : "", slot._pPart != nullptr ? slot._pPart : "", getStateText( slot._states ),
                                          WidgetStyle::StaticType()->getPropertiesWithBase()[static_cast<size_t>( field )]._name.view() );
                    _missing = string( builder.view() );
                }

                static const utf8* getStateText( uint32 states )
                {
                    switch ( states )
                    {
                        case UIStyleState::kHover:
                            return ":hover";
                        case UIStyleState::kPressed:
                            return ":pressed";
                        case UIStyleState::kDisabled:
                            return ":disabled";
                        default:
                            return "";
                    }
                }
            };

            static expected<shared_ptr<const void>> parseSkin( string_view text, string_view path, const ResourceContext& context )
            {
                UIStyleSheetAsset sheet;
                if ( expected<void> parsed = UIStyleSheetAsset::parse( text, path, context, sheet ); parsed.has_value() == false )
                    return unexpected( parsed.error() );
                expected<UIWidgetSkin> skin = UIWidgetSkin::build( sheet );
                if ( skin.has_value() == false )
                    return unexpected( skin.error() );
                return shared_ptr<const void>( make_shared<UIWidgetSkin>( std::move( *skin ) ) );
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    expected<UIWidgetSkin> UIWidgetSkin::build( const UIStyleSheetAsset& sheet )
    {
        using Reader = UIWidgetSkinInternal::Reader;
        using Field  = UIStyleField;
        Reader       reader{ sheet };
        UIWidgetSkin skin;

        reader.readStateBrushes( "ButtonWidget", skin._button._brush );
        reader.read<Field::Padding>( reader.find( "ButtonWidget" ), skin._button._padding );

        reader.readStateBrushes( "CheckBoxWidget", skin._checkBox._brush );
        reader.read<Field::Padding>( reader.find( "CheckBoxWidget" ), skin._checkBox._padding );
        reader.readBorderBrush( reader.find( "CheckBoxWidget", "box" ), skin._checkBox._box );
        reader.read<Field::Size>( reader.find( "CheckBoxWidget", "box" ), skin._checkBox._boxSize );
        reader.read<Field::Padding>( reader.find( "CheckBoxWidget", "box" ), skin._checkBox._boxPadding );
        reader.read<Field::Opacity>( reader.find( "CheckBoxWidget", "box", UIStyleState::kDisabled ), skin._checkBox._disabledAlpha, true );
        reader.readBrush( reader.find( "CheckBoxWidget", "mark" ), skin._checkBox._mark );
        float4 labelPadding{};
        reader.read<Field::Padding>( reader.find( "CheckBoxWidget", "label" ), labelPadding );
        skin._checkBox._labelGap = labelPadding._x;

        reader.read<Field::Padding>( reader.find( "ComboBoxWidget" ), skin._comboBox._padding );
        reader.readBorderBrush( reader.find( "ComboBoxWidget", "popup" ), skin._comboBox._popup );
        reader.read<Field::Padding>( reader.find( "ComboBoxWidget", "popup" ), skin._comboBox._popupPadding );
        reader.read<Field::ShadowColor>( reader.find( "ComboBoxWidget", "popup" ), skin._comboBox._popupShadowColor );
        reader.read<Field::ShadowOffset>( reader.find( "ComboBoxWidget", "popup" ), skin._comboBox._popupShadowOffset );
        reader.read<Field::ShadowBlur>( reader.find( "ComboBoxWidget", "popup" ), skin._comboBox._popupShadowBlur );
        float2 optionSize{};
        float4 optionCornerRadius{};
        reader.read<Field::Size>( reader.find( "ComboBoxWidget", "option" ), optionSize );
        reader.read<Field::Padding>( reader.find( "ComboBoxWidget", "option" ), skin._comboBox._optionPadding );
        reader.read<Field::CornerRadius>( reader.find( "ComboBoxWidget", "option" ), optionCornerRadius );
        skin._comboBox._optionHeight       = optionSize._y;
        skin._comboBox._optionCornerRadius = optionCornerRadius._x;
        reader.readArrow( "ComboBoxWidget", skin._comboBox._arrow );

        reader.read<Field::Padding>( reader.find( "RotatorWidget" ), skin._rotator._padding );
        reader.readArrow( "RotatorWidget", skin._rotator._arrow );

        float2 trackSize{};
        float2 thumbSize{};
        reader.read<Field::Size>( reader.find( "SliderWidget" ), skin._slider._size );
        reader.read<Field::Opacity>( reader.find( "SliderWidget", nullptr, UIStyleState::kDisabled ), skin._slider._disabledOpacity, true );
        reader.read<Field::BackgroundColor>( reader.find( "SliderWidget", "track" ), skin._slider._trackColor );
        reader.read<Field::Size>( reader.find( "SliderWidget", "track" ), trackSize );
        reader.read<Field::BackgroundColor>( reader.find( "SliderWidget", "fill" ), skin._slider._fillColor );
        reader.read<Field::BackgroundColor>( reader.find( "SliderWidget", "thumb" ), skin._slider._thumbColor );
        reader.read<Field::Size>( reader.find( "SliderWidget", "thumb" ), thumbSize );
        skin._slider._trackThickness = trackSize._y;
        skin._slider._thumbSize      = thumbSize._x;

        float4 progressCornerRadius{};
        reader.read<Field::Size>( reader.find( "ProgressBarWidget" ), skin._progressBar._size );
        reader.read<Field::BackgroundColor>( reader.find( "ProgressBarWidget" ), skin._progressBar._backgroundColor );
        reader.read<Field::CornerRadius>( reader.find( "ProgressBarWidget" ), progressCornerRadius );
        reader.read<Field::BackgroundColor>( reader.find( "ProgressBarWidget", "fill" ), skin._progressBar._fillColor );
        skin._progressBar._cornerRadius = progressCornerRadius._x;

        float2 caretSize{};
        float2 underlineSize{};
        reader.readBorderBrush( reader.find( "TextInputWidget" ), skin._textInput._background );
        reader.read<Field::Padding>( reader.find( "TextInputWidget" ), skin._textInput._padding );
        reader.read<Field::TextColor>( reader.find( "TextInputWidget" ), skin._textInput._textColor );
        reader.read<Field::TextColor>( reader.find( "TextInputWidget", "hint" ), skin._textInput._hintColor );
        reader.read<Field::BackgroundColor>( reader.find( "TextInputWidget", "selection" ), skin._textInput._selectionColor );
        reader.read<Field::BackgroundColor>( reader.find( "TextInputWidget", "caret" ), skin._textInput._caretColor );
        reader.read<Field::Size>( reader.find( "TextInputWidget", "caret" ), caretSize );
        reader.read<Field::Size>( reader.find( "TextInputWidget", "underline" ), underlineSize );
        skin._textInput._caretWidth         = caretSize._x;
        skin._textInput._underlineThickness = underlineSize._y;

        float4 focusRingPadding{};
        reader.read<Field::FocusRingColor>( reader.find( "Widget", "focusring" ), skin._focusRing._color );
        reader.read<Field::CornerRadius>( reader.find( "Widget", "focusring" ), skin._focusRing._cornerRadius );
        reader.read<Field::BorderWidth>( reader.find( "Widget", "focusring" ), skin._focusRing._width );
        reader.read<Field::Padding>( reader.find( "Widget", "focusring" ), focusRingPadding );
        skin._focusRing._outset = focusRingPadding._x;

        if ( reader._missing.empty() == false )
            return unexpected( Error{ ErrorKind::InvalidArgument, "the engine widget skin sheet is missing a value", reader._missing } );
        return skin;
    }

    const UIWidgetSkin& UIWidgetSkin::get()
    {
        return engine::getSubsystem<UIWidgetSkinService>().getSkin();
    }
} // namespace sw

namespace sw
{
    UIWidgetSkinService::UIWidgetSkinService()
        : _cache{ "UIWidgetSkin", "UI widget skin", &UIWidgetSkinInternal::parseSkin }
        , _skin{}
        , _onSkinChanged{}
        , _sheetReloadedSubscription{}
        , _assetCacheRegistration{}
    {
    }

    UIWidgetSkinService::~UIWidgetSkinService() = default;

    SubsystemInitResult UIWidgetSkinService::initializeSubsystem()
    {
        _assetCacheRegistration    = engine::getSubsystem<AssetManager>().registerAssetCache( &_cache, getAssetCacheReloadFunction<UITextAssetCache>() );
        _sheetReloadedSubscription = _cache.onReloaded().add( SW_DELEGATE_METHOD( UIAssetReloadedDelegate, &UIWidgetSkinService::onSheetReloaded, this ) );
        // 위젯은 겉모습 수치를 들지 않는다 — 시트가 없거나 칸이 빠지면 대신 쓸 값이 없으므로 기동을 멈춘다.
        return applySheet() ? SubsystemInitResult::Succeeded : SubsystemInitResult::Failed;
    }

    void UIWidgetSkinService::shutdownSubsystem()
    {
        _sheetReloadedSubscription.reset();
        _assetCacheRegistration.reset();
        _cache.clear();
        _skin.reset();
    }

    span<const utf8* const> UIWidgetSkinService::getSubsystemDependencies() const
    {
        static constexpr const utf8* kArrDependency[] = { AssetManager::kSubsystemName, EngineDefaultAssetsSubsystem::kSubsystemName };
        return kArrDependency;
    }

    const UIWidgetSkin& UIWidgetSkinService::getSkin() const
    {
        if ( _skin == nullptr )
            reportFatalError( "UIWidgetSkin: a widget was created before the engine widget skin was loaded (or on a host without UI)" );
        return *_skin;
    }

    void UIWidgetSkinService::onSheetReloaded( string_view /*sheetPath*/ )
    {
        if ( applySheet() )
            _onSkinChanged.broadcast();
    }

    bool UIWidgetSkinService::applySheet()
    {
        const string_view                sheetPath = engine::getSubsystem<EngineDefaultAssets>()._uiWidgetSkin.get();
        expected<shared_ptr<const void>> skin      = _cache.acquireAsset( sheetPath, ResourceTagSet{} );
        if ( skin.has_value() == false )
        {
            SW_LOG_ERROR_ID( "UIWidgetSkin.SheetNotUsable", "[UI] Engine widget skin '%#': %# (%#)", sheetPath, skin.error().message(), skin.error().detail() );
            return false;
        }
        _skin = std::static_pointer_cast<const UIWidgetSkin>( *skin );
        return true;
    }
} // namespace sw

namespace sw
{
    SW_ENGINE_SERVICE_SUBSYSTEM( UIWidgetSkinService, HostOnly, Process, Startup, (), SW_ENGINE_DEFAULT_FACTORY( UIWidgetSkinService ) );
} // namespace sw
