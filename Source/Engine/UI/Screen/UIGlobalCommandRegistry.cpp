#include "pch.h"

#include "Engine/UI/Screen/UIGlobalCommandRegistry.h"

#include "Core/Container/VectorUtil.h"

namespace sw
{
    namespace
    {
        struct UIGlobalCommandRegistryInternal
        {
            /** @brief 처리 함수의 코드가 [_pBegin, _pEnd) 안인 항목을 고릅니다(`removeCodeWithin`). */
            struct IsCodeWithin
            {
                const void* _pBegin;
                const void* _pEnd;

                bool operator()( const UICommandDelegate& handler ) const
                {
                    return handler.isCodeWithin( _pBegin, _pEnd );
                }
            };
        };
    } // namespace
} // namespace sw

namespace sw
{
    UIGlobalCommandRegistry::UIGlobalCommandRegistry()
        : _registry{}
        , _listOwner{}
        , _nextOwnerID{ 1 }
    {
    }

    Subscription UIGlobalCommandRegistry::registerCommand( const hashed_string& command, const UICommandDelegate& handler )
    {
        _registry.addOrReplace( command, handler, "UI global command" );
        const uint64 id = _nextOwnerID++;
        if ( CommandOwner* pOwner = VectorUtil::findBy<&CommandOwner::_command>( _listOwner, command ) )
            pOwner->_id = id; // 교체 — 옛 구독은 이제 아무것도 거두지 않는다
        else
            _listOwner.push_back( CommandOwner{ command, id } );
        return _subscriptions.issue( id );
    }

    void UIGlobalCommandRegistry::releaseSubscription( uint64 id )
    {
        const std::optional<size_t> index = VectorUtil::findIndexBy<&CommandOwner::_id>( _listOwner, id );
        if ( index.has_value() == false )
            return; // 다른 쪽이 그 사이 교체했다(핫 리로드의 새 인스턴스) — 그 등록을 지우지 않는다
        // 이름 표에는 이름으로 빼는 길이 없어 빈 처리 함수로 바꾼다(`hasCommand` · `tryExecute` 는 묶이지 않은 함수를 없는 것으로 본다).
        _registry.addOrReplace( _listOwner[*index]._command, UICommandDelegate{}, "UI global command" );
        _listOwner.erase( _listOwner.begin() + static_cast<ptrdiff_t>( *index ) );
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

    uint32 UIGlobalCommandRegistry::removeCodeWithin( const void* pBegin, const void* pEnd )
    {
        return _registry.removeIf( UIGlobalCommandRegistryInternal::IsCodeWithin{ pBegin, pEnd } );
    }
} // namespace sw
