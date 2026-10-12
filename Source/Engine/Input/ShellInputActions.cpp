#include "pch.h"

#include "Engine/Input/ShellInputActions.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Input/InputManager.h"
#include "Engine/Input/Map/InputMap.h"

namespace sw
{
    SW_LOG_CALLER( "ShellInputActions" );

    ShellInputActions::ShellInputActions()
        : _mapDebugAction{ nullptr }
        , _bBound{ false }
    {
    }

    ShellInputActions::~ShellInputActions() = default;

    void ShellInputActions::update( InputManager& inputManager, float32 deltaTime )
    {
        if ( _bBound == false )
        {
            _mapDebugAction = createInputMap( engine::getEngineDefaultAssets()._shellInputMap );
            _bBound         = true;
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
} // namespace sw
