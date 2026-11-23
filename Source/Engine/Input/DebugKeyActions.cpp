#include "pch.h"

#include "Engine/Input/DebugKeyActions.h"

#include "Core/File/FileUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"
#include "Engine/Input/IInputDevice.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Input/Map/InputRemapSerializer.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/Serialization/XML/XMLFields.h"

namespace sw
{
    namespace
    {
        constexpr const utf8* kRootElement = "DebugKeyBindings";
        constexpr const utf8* kBindElement = "bind";
        /**
         * @brief 키보드 키 · 단축키 바인딩의 수정 키(`Ctrl+Shift`, 없으면 `None`)입니다. 이 속성이 있으면 수정 키까지 바꾼 재배치다(`InputMap::rebindKeyShortcut`).
         * @details 없는 줄은 방아쇠 키만 바꾼다(`rebindSlot`) — 수정 키를 적기 전에 저장한 파일의 뜻 그대로다.
         */
        constexpr const utf8* kModifierAttr = "modifier";
        constexpr const utf8* kNoModifier   = "None";
    } // namespace

    DebugKeyActions::DebugKeyActions()
        : _mapDebugAction{ nullptr }
        , _userBindingsPath{}
        , _bBound{ SW_FALSE }
    {
    }

    DebugKeyActions::~DebugKeyActions() = default;

    void DebugKeyActions::update( InputManager& inputManager, float32 deltaTime )
    {
        if ( _bBound == false )
        {
            _mapDebugAction = createInputMap( AssetID{ engine::getSubsystem<EngineDefaultAssets>()._debugKeyInputMap.get() } );
            _bBound         = SW_TRUE;
            // 플레이어가 디버그 키(콘솔 여는 키 …)를 바꿨으면 리소스의 기본 위에 입힌다. 읽지 못한 파일은 로그가 알리고 기본 키로 돈다.
            if ( _userBindingsPath.empty() == false )
                (void)loadUserBindings( *_mapDebugAction, _userBindingsPath ); // 실패는 loadUserBindings 가 경고로 남긴다
        }

        // _mapDebugAction 은 위에서 처음 한 번 만든 뒤로 계속 있으므로 nullptr 일 수 없다.

        if ( _mapDebugAction->hasLayer( InputMapDefaults::kTitleLayerName ) )
            _mapDebugAction->setLayerEnabled( InputMapDefaults::kTitleLayerName, false );
        _mapDebugAction->setInputManager( &inputManager );
        _mapDebugAction->update( deltaTime );
    }

    void DebugKeyActions::reset()
    {
        _mapDebugAction.reset();
        _bBound = SW_FALSE;
    }

    unique_ptr<InputMap> DebugKeyActions::createInputMap( const AssetID& assetID )
    {
        const string_view     inputMapPath = assetID.view();
        const ScopedMemoryTag memoryScope{ kSubsystemMemoryTag };
        unique_ptr<InputMap>  map = make_unique<InputMap>();
        // 셸 단축키와 개발 콘솔은 콘솔이 키보드를 쥔 동안에도 키를 받아야 한다(닫는 키 · 편집 키).
        map->setKeyboardFocusIgnored( true );
        if ( inputMapPath.empty() || map->loadFromAsset( assetID ) == false )
        {
            map->clear();
            SW_LOG_ERROR_ID( "DebugKeyActions.DebugKeyMapCould", "Debug key map '%#' could not be loaded - debug key actions stay unbound", inputMapPath );
        }
        return map;
    }

    bool DebugKeyActions::wasActionTriggered( const hashed_string& actionName ) const
    {
        if ( _mapDebugAction == nullptr )
            return false;
        return _mapDebugAction->wasActionTriggered( actionName );
    }

    string DebugKeyActions::makeUserBindingsPath( string_view userSettingsFilePath )
    {
        if ( userSettingsFilePath.empty() )
            return {};
        return FileUtil::joinPath( FileUtil::getDirectoryPart( userSettingsFilePath ), kUserBindingsFileName );
    }

    bool DebugKeyActions::saveUserBindings( const InputMap& map, string_view filePath )
    {
        if ( filePath.empty() )
            return false;

        XMLDocument doc;
        XMLNode     root         = doc.appendRoot( kRootElement );
        uint32      changedCount = 0;
        for ( const hashed_string& action : map.getActionNames() )
        {
            const uint32 bindingCount = map.getBindingCount( action );
            for ( uint32 bindIndex = 0; bindIndex < bindingCount; ++bindIndex )
            {
                InputSlot current{};
                InputSlot defaultSlot{};
                // 키 하나로 바꿀 수 없는 바인딩(합성 축, 스틱)은 재배치 대상이 아니다.
                if ( map.findRebindSlot( action, bindIndex, current ) == false || map.findDefaultRebindSlot( action, bindIndex, defaultSlot ) == false )
                    continue;
                // 키보드 키 · 단축키는 수정 키도 재배치의 일부다 — Ctrl+D 를 Ctrl+Shift+D 로 바꾸면 방아쇠 키가 같아도 바뀐 것이다.
                const ActionBinding* pCurrent       = map.getBinding( action, bindIndex );
                const ActionBinding* pDefault       = map.getDefaultBinding( action, bindIndex );
                const bool           bKeyboardChord = pCurrent != nullptr && pDefault != nullptr && pCurrent->_arrSlot[0]._deviceKind == InputDeviceKind::Keyboard &&
                                            ( pCurrent->_kind == BindingKind::SingleSlot || pCurrent->_kind == BindingKind::Shortcut );
                const bool bModifierChanged = bKeyboardChord && pCurrent->_modifierMask != pDefault->_modifierMask;
                if ( current == defaultSlot && bModifierChanged == false )
                    continue;
                const string slotText = InputSlot::toText( current );
                if ( slotText.empty() )
                    continue; // 키보드, 마우스, 패드가 아닌 슬롯은 문자열로 적을 수 없다
                DebugKeyRebindParams params;
                params._action         = action;
                params._index          = static_cast<int32>( bindIndex );
                params._slot           = slotText;
                const XMLNode bindNode = root.appendChild( kBindElement );
                XMLFields::writeProperties( bindNode, params );
                if ( bKeyboardChord )
                {
                    const string modifierText = InputRemapSerializer::formatModifierNames( pCurrent->_modifierMask );
                    bindNode.appendAttribute( kModifierAttr, modifierText.empty() ? kNoModifier : modifierText.c_str() );
                }
                ++changedCount;
            }
        }

        // 바꾼 것이 없으면 파일을 두지 않는다 — 기본 키로 되돌린 플레이어에게 낡은 파일이 남지 않게 한다.
        if ( changedCount == 0 )
            return FileUtil::exists( FileUtil::toDiskPath( filePath ) ) == false || FileUtil::removeFile( FileUtil::toDiskPath( filePath ) );
        if ( FileUtil::ensureParentDirectoryExists( FileUtil::toDiskPath( filePath ) ) == false )
            return false;
        return doc.saveCompleteToFile( filePath );
    }

    bool DebugKeyActions::loadUserBindings( InputMap& map, string_view filePath )
    {
        if ( filePath.empty() || FileUtil::exists( FileUtil::toDiskPath( filePath ) ) == false )
            return true;

        // 플레이어 파일(`saveUserBindings` 가 디스크에 쓴 것)이라 디스크 입구로 읽는다.
        XMLDocument doc;
        if ( doc.loadFromFile( filePath ) == false )
        {
            SW_LOG_WARNING_ID( "DebugKeyActions.DebugKeyUserBindings", "Debug key user bindings '%#' could not be read - the default keys are used", filePath );
            return false;
        }
        const XMLNode root = doc.getRoot( kRootElement );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING_ID( "DebugKeyActions.DebugKeyUserBindings2", "Debug key user bindings '%#' has no <%#> root - the default keys are used", filePath, kRootElement );
            return false;
        }
        for ( XMLNode bindNode = root.findChild( kBindElement ); bindNode.isValid(); bindNode = bindNode.findNextSibling( kBindElement ) )
        {
            // `modifier` 는 적지 않음(슬롯만 바꿈) · `None`(수정 키 없음) · 이름 셋이라 스키마 칸이 아니다.
            DebugKeyRebindParams params;
            InputSlot            slot{};
            if ( XMLFields::readProperties( bindNode, params, filePath, { kModifierAttr } ) == false || InputSlot::tryParse( params._slot, slot ) == false )
            {
                SW_LOG_WARNING_ID( "DebugKeyActions.DebugKeyUserBindings3", "Debug key user bindings '%#': a <bind> with action '%#' and slot '%#' cannot be read - skipped", filePath, params._action.c_str(),
                                   params._slot.c_str() );
                continue;
            }
            const hashed_string& action        = params._action;
            const int32          bindIndex     = params._index;
            const utf8*          pModifierText = bindNode.findAttribute( kModifierAttr );
            bool                 bApplied      = false;
            if ( map.hasAction( action ) && pModifierText == nullptr )
            {
                bApplied = map.rebindSlot( action, slot, static_cast<uint32>( bindIndex ) );
            }
            else if ( map.hasAction( action ) )
            {
                uint8      modifierMask = ModifierKey::None;
                const bool bNoModifier  = StringUtil::equals( pModifierText, kNoModifier, true );
                const bool bKnown       = bNoModifier || InputRemapSerializer::tryParseModifierNames( pModifierText, modifierMask );
                bApplied                = bKnown && slot._deviceKind == InputDeviceKind::Keyboard &&
                           map.rebindKeyShortcut( action, static_cast<uint32>( bindIndex ), static_cast<Key>( slot._controlIndex ), modifierMask );
            }
            if ( bApplied == false )
                SW_LOG_WARNING_ID( "DebugKeyActions.DebugKeyUserBindings4", "Debug key user bindings '%#': action '%#' cannot take '%#' as binding %# - skipped", filePath, action.c_str(), params._slot.c_str(), bindIndex );
        }
        return true;
    }

    bool DebugKeyActions::saveUserBindings() const
    {
        if ( _mapDebugAction == nullptr )
            return false;
        return saveUserBindings( *_mapDebugAction, _userBindingsPath );
    }
} // namespace sw

namespace sw
{
    // 셸 디버그 키 맵 — 호스트 루프가 프레임마다 돌리고 개발 도구 · 개발 명령이 읽는다. 맵은 처음 돌 때 만든다(기동 단계의 입력 매니저에 붙는다).
    SW_ENGINE_SERVICE_SUBSYSTEM( DebugKeyActions, HostOnly, Services, (), []( engine::EngineSubsystemFactoryScope& scope, engine::EngineSubsystemBuild& build )
    {
        build._pServiceParts->_pDebugKeyActions = &scope.emplaceSubsystem<DebugKeyActions>();
    } );
} // namespace sw
