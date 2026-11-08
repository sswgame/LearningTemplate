#include "pch.h"

#include "Editor/Common/Workspace/Undo/UndoStep.h"

#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/Undo/CommandStack.h"

namespace sw::editor
{
    namespace
    {
        struct UndoStepRegistryInternal
        {
            /** @brief 처리기 줄 목록입니다(올린 순서, 이름 없음 — 같은 해시가 여러 DLL 에서 올 수 있다). 편집기 UI 스레드와 DLL 적재 · 해제(같은 스레드)만 만진다. */
            static SW_DEFINE_REGISTRATION_LIST( getHandlers, const UndoStepHandler, "UndoStepRegistry" )
        };
    } // namespace

    bool UndoStepRegistry::add( const UndoStepHandler* pHandler )
    {
        const bool bInvalidHandler = pHandler == nullptr || pHandler->_functionHash == 0 || pHandler->_invoker == nullptr;
        if ( bInvalidHandler )
            return false;
        const RegistrationResult result = UndoStepRegistryInternal::getHandlers().addLogged( pHandler, "Undo step handler" );
        return result == RegistrationResult::Added || result == RegistrationResult::AlreadyPresent;
    }

    void UndoStepRegistry::remove( const UndoStepHandler* pHandler )
    {
        (void)UndoStepRegistryInternal::getHandlers().remove( pHandler ); // 올라 있지 않은 줄을 빼는 것은 할 일이 없다
    }

    UndoStepInvoker UndoStepRegistry::find( uint64 functionHash )
    {
        for ( const UndoStepHandler* pHandler : UndoStepRegistryInternal::getHandlers().getItems() )
        {
            if ( pHandler->_functionHash == functionHash )
                return pHandler->_invoker;
        }
        return nullptr;
    }

    UndoStepOwner::UndoStepOwner()
        : _id{ reinterpret_cast<uint64>( this ) } // 살아 있는 주인끼리만 달라면 된다 — 주인이 사라지며 알려 그 id 의 명령이 먼저 빠지므로 같은 주소의 다음 주인과 섞이지 않는다
        , _released{}
    {
    }

    UndoStepOwner::~UndoStepOwner()
    {
        _released.broadcast( _id ); // 이 주인의 코드 단계를 받은 스택들이 그 명령을 뺀다
    }

    void UndoStep::execute( UndoStepContext& context ) const
    {
        if ( _transient.isBound() )
        {
            _transient();
            return;
        }
        if ( _functionHash == 0 )
            return;
        const UndoStepInvoker invoker = UndoStepRegistry::find( _functionHash );
        if ( invoker == nullptr )
        {
            // 그 함수가 바뀌었거나 사라진 뒤의 기록이다(리로드 · 파일). 이 단계만 건너뛴다.
            SW_LOG_WARNING_ID( "UndoStep.UndoStepNoHandler", "Undo step %# has no handler in this build - skipped", _functionHash );
            return;
        }
        Archive arguments( _arguments.data(), _arguments.size() );
        if ( invoker( arguments, context ) == false )
            SW_LOG_WARNING_ID( "UndoStep.UndoStepCouldNot", "Undo step %# could not read its arguments - skipped", _functionHash );
    }
} // namespace sw::editor
