#include "pch.h"

#include "Engine/Input/ShellInputActions.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/InputSlotUtil.h"
#include "Engine/Input/Map/InputMap.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw
{
    SW_LOG_CALLER( "ShellInputActions" );

    namespace
    {
        constexpr const utf8* kRootElement  = "ShellBindings";
        constexpr const utf8* kBindElement  = "bind";
        constexpr const utf8* kActionAttr   = "action";
        constexpr const utf8* kIndexAttr    = "index";
        constexpr const utf8* kSlotAttr     = "slot";
        constexpr int32       kMaxBindIndex = 16; ///< 한 액션이 가질 수 있는 바인딩 번호의 상한입니다(잘못된 파일이 큰 번호를 주지 못하게).

        /** @brief 지금 도는 셸 액션입니다(엔진 루프가 값 멤버로 가진다). 개발 명령이 엔진 루프를 모르고 찾는 길입니다. */
        ShellInputActions* s_pActive = nullptr;
    } // namespace

    ShellInputActions::ShellInputActions()
        : _mapDebugAction{ nullptr }
        , _userBindingsPath{}
        , _bBound{ false }
    {
    }

    ShellInputActions::~ShellInputActions()
    {
        if ( s_pActive == this )
            s_pActive = nullptr;
    }

    ShellInputActions* ShellInputActions::findActive()
    {
        return s_pActive;
    }

    void ShellInputActions::update( InputManager& inputManager, float32 deltaTime )
    {
        if ( _bBound == false )
        {
            _mapDebugAction = createInputMap( engine::getSubsystem<EngineDefaultAssets>()._shellInputMap.get() );
            _bBound         = true;
            // 플레이어가 셸 키(콘솔 여는 키 …)를 바꿨으면 리소스의 기본 위에 입힌다. 읽지 못한 파일은 로그가 알리고 기본 키로 돈다.
            if ( _userBindingsPath.empty() == false )
                (void)loadUserBindings( *_mapDebugAction, _userBindingsPath ); // 실패는 loadUserBindings 가 경고로 남긴다
            s_pActive = this;
        }

        // _mapDebugAction 은 위에서 처음 한 번 만든 뒤로 계속 있으므로 nullptr 일 수 없다.

        if ( _mapDebugAction->hasLayer( InputMapDefaults::kTitleLayerName ) )
            _mapDebugAction->setLayerEnabled( InputMapDefaults::kTitleLayerName, false );
        _mapDebugAction->setInputManager( &inputManager );
        _mapDebugAction->update( deltaTime );
    }

    void ShellInputActions::reset()
    {
        _mapDebugAction.reset();
        _bBound = false;
        if ( s_pActive == this )
            s_pActive = nullptr;
    }

    unique_ptr<InputMap> ShellInputActions::createInputMap( string_view inputMapPath )
    {
        SW_MEMORY_SCOPE( EngineMisc );
        unique_ptr<InputMap> map = make_unique<InputMap>();
        // 셸 단축키와 개발 콘솔은 콘솔이 키보드를 쥔 동안에도 키를 받아야 한다(닫는 키 · 편집 키).
        map->setKeyboardFocusIgnored( true );
        if ( inputMapPath.empty() || map->loadFromResource( inputMapPath ) == false )
        {
            map->clear();
            SW_LOG_ERROR( "Shell input map '%#' could not be loaded - shell debug actions stay unbound", inputMapPath );
        }
        return map;
    }

    bool ShellInputActions::wasActionTriggered( const hashed_string& actionName ) const
    {
        if ( _mapDebugAction == nullptr )
            return false;
        return _mapDebugAction->wasActionTriggered( actionName );
    }

    string ShellInputActions::makeUserBindingsPath( string_view userSettingsFilePath )
    {
        if ( userSettingsFilePath.empty() )
            return {};
        return FileUtil::joinPath( FileUtil::getDirectoryPart( userSettingsFilePath ), kUserBindingsFileName );
    }

    bool ShellInputActions::saveUserBindings( const InputMap& map, string_view filePath )
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
                if ( current == defaultSlot )
                    continue;
                const string slotText = InputSlotUtil::toText( current );
                if ( slotText.empty() )
                    continue; // 키보드, 마우스, 패드가 아닌 슬롯은 문자열로 적을 수 없다
                XMLNode bindNode = root.appendChild( kBindElement );
                bindNode.appendAttribute( kActionAttr, action.c_str() );
                bindNode.appendAttribute( kIndexAttr, static_cast<int32>( bindIndex ) );
                bindNode.appendAttribute( kSlotAttr, slotText.c_str() );
                ++changedCount;
            }
        }

        // 바꾼 것이 없으면 파일을 두지 않는다 — 기본 키로 되돌린 플레이어에게 낡은 파일이 남지 않게 한다.
        if ( changedCount == 0 )
            return FileUtil::exists( filePath ) == false || FileUtil::removeFile( filePath );
        if ( FileUtil::ensureParentDirectoryExists( filePath ) == false )
            return false;
        return doc.saveFile( filePath );
    }

    bool ShellInputActions::loadUserBindings( InputMap& map, string_view filePath )
    {
        if ( filePath.empty() || FileUtil::exists( filePath ) == false )
            return true;

        // 플레이어 파일(`saveUserBindings` 가 디스크에 쓴 것)이라 디스크 입구로 읽는다.
        XMLDocument doc;
        if ( doc.loadFile( filePath ) == false )
        {
            SW_LOG_WARNING( "Shell user bindings '%#' could not be read - the default keys are used", filePath );
            return false;
        }
        const XMLNode root = doc.getRoot( kRootElement );
        if ( root.isValid() == false )
        {
            SW_LOG_WARNING( "Shell user bindings '%#' has no <%#> root - the default keys are used", filePath, kRootElement );
            return false;
        }
        for ( XMLNode bindNode = root.findChild( kBindElement ); bindNode.isValid(); bindNode = bindNode.findNextSibling( kBindElement ) )
        {
            const string_view actionText = bindNode.getAttributeText( kActionAttr );
            const string_view slotText   = bindNode.getAttributeText( kSlotAttr );
            // 저장할 때 바인딩 번호를 늘 적는다 — 빠진 번호를 0 번으로 보지 않는다.
            const std::optional<int32> index      = bindNode.readRequiredAttribute<int32>( kIndexAttr );
            const int32                bindIndex  = index.has_value() ? *index : -1;
            const bool                 bIndexOkay = 0 <= bindIndex && bindIndex <= kMaxBindIndex;
            InputSlot                  slot{};
            const bool                 bUnreadableBind = actionText.empty() || bIndexOkay == false || InputSlotUtil::tryParse( slotText, slot ) == false;
            if ( bUnreadableBind )
            {
                SW_LOG_WARNING( "Shell user bindings '%#': a <bind> with action '%#' and slot '%#' cannot be read - skipped", filePath, string( actionText ).c_str(),
                                string( slotText ).c_str() );
                continue;
            }
            const hashed_string action( string( actionText ).c_str() );
            if ( map.hasAction( action ) == false || map.rebindSlot( action, slot, static_cast<uint32>( bindIndex ) ) == false )
                SW_LOG_WARNING( "Shell user bindings '%#': action '%#' cannot take '%#' as binding %# - skipped", filePath, string( actionText ).c_str(), string( slotText ).c_str(), bindIndex );
        }
        return true;
    }

    bool ShellInputActions::saveUserBindings() const
    {
        if ( _mapDebugAction == nullptr )
            return false;
        return saveUserBindings( *_mapDebugAction, _userBindingsPath );
    }
} // namespace sw

#if SW_DEV_COMMANDS_ENABLED
namespace sw
{
    namespace
    {
        struct ShellInputActionsDevCommandsInternal
        {
            /** @brief 셸 맵과 그 사용자 파일이 있는 활성 셸 액션입니다. 없으면 답에 까닭을 적고 nullptr 입니다. */
            static ShellInputActions* findShell( string& outReply )
            {
                ShellInputActions* pShell = ShellInputActions::findActive();
                if ( pShell == nullptr || pShell->getInputMap() == nullptr )
                    outReply = "the shell input map is not running";
                return pShell != nullptr && pShell->getInputMap() != nullptr ? pShell : nullptr;
            }

            static bool runRebind( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() < 2 || listArgument.size() > 3 )
                    return false;
                ShellInputActions* pShell = findShell( outReply );
                if ( pShell == nullptr )
                    return false;
                InputSlot slot{};
                if ( InputSlotUtil::tryParse( listArgument[1], slot ) == false )
                {
                    outReply = "unknown slot '" + listArgument[1] + "' (e.g. Key.F2, Gamepad.Back)";
                    return false;
                }
                int32 bindIndex = 0;
                if ( listArgument.size() == 3 && StringUtil::parseInt( listArgument[2], bindIndex ) == false )
                    return false;
                const hashed_string action( listArgument[0].c_str() );
                InputMap&           map             = *pShell->getInputMap();
                const bool          bRebindRejected = bindIndex < 0 || map.hasAction( action ) == false || map.rebindSlot( action, slot, static_cast<uint32>( bindIndex ) ) == false;
                if ( bRebindRejected )
                {
                    outReply = "'" + listArgument[0] + "' has no rebindable binding " + to_string( bindIndex );
                    return false;
                }
                const bool bSaved = pShell->getUserBindingsPath().empty() || pShell->saveUserBindings();
                outReply          = listArgument[0] + "[" + to_string( bindIndex ) + "] = " + InputSlotUtil::toText( slot ) + ( bSaved ? "" : " (not saved - see the log)" );
                return true;
            }

            static bool runReset( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() > 1 )
                    return false;
                ShellInputActions* pShell = findShell( outReply );
                if ( pShell == nullptr )
                    return false;
                InputMap& map = *pShell->getInputMap();
                if ( listArgument.empty() )
                {
                    map.resetAllBindingsToDefault();
                }
                else if ( map.resetActionToDefault( hashed_string( listArgument[0].c_str() ) ) == false )
                {
                    outReply = "unknown action '" + listArgument[0] + "'";
                    return false;
                }
                const bool bSaved = pShell->getUserBindingsPath().empty() || pShell->saveUserBindings();
                outReply          = listArgument.empty() ? "shell keys are back to the defaults" : listArgument[0] + " is back to its default";
                outReply += bSaved ? "" : " (the user file could not be updated - see the log)";
                return true;
            }
        };
    } // namespace

    SW_DEV_COMMAND( ShellRebind, "shell.rebind", "shell.rebind <action> <slot> [index]",
                    "Rebind a shell key (console toggle, shader reload ...) for this player, e.g. shell.rebind DevConsoleToggle Key.F2 - kept in shellbindings.xml next to usersettings.json",
                    &ShellInputActionsDevCommandsInternal::runRebind );
    SW_DEV_COMMAND( ShellRebindReset, "shell.rebind.reset", "shell.rebind.reset [action]", "Put one shell action (or all) back to the default key and update the user file",
                    &ShellInputActionsDevCommandsInternal::runReset );
} // namespace sw
#endif
