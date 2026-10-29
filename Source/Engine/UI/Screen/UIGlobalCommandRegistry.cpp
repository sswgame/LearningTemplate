#include "pch.h"

#include "Engine/UI/Screen/UIGlobalCommandRegistry.h"

namespace sw
{
    UIGlobalCommandRegistry::UIGlobalCommandRegistry()
        : _registry{}
    {
    }

    Subscription UIGlobalCommandRegistry::registerCommand( const hashed_string& command, const UICommandDelegate& handler )
    {
        return _registry.addOwned( command, handler, "UI global command" );
    }

    bool UIGlobalCommandRegistry::hasCommand( const hashed_string& command ) const
    {
        const UICommandDelegate* pHandler = _registry.find( command );
        return pHandler != nullptr && pHandler->isBound();
    }

    bool UIGlobalCommandRegistry::tryExecute( const hashed_string& command, Widget& source ) const
    {
        const UICommandDelegate* pHandler = _registry.find( command );
        if ( pHandler == nullptr || pHandler->isBound() == false )
            return false;
        // 처리 함수가 이 표를 고쳐도(명령 안에서 등록 · 해제) 돌고 있는 함수가 사라지지 않게 사본을 부른다.
        const UICommandDelegate handler = *pHandler;
        handler( command, source );
        return true;
    }

} // namespace sw
