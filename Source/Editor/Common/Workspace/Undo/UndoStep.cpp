#include "pch.h"

#include "Editor/Common/Workspace/Undo/UndoStep.h"

#include "Core/Container/map.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Workspace/Undo/CommandStack.h"

namespace sw::editor
{
    SW_LOG_CALLER( "UndoStep" );

    namespace
    {
        struct UndoStepRegistryInternal
        {
            /** @brief 표의 한 칸 — 같은 함수를 올린 곳 수(편집기와 확장 DLL 이 같은 헤더 함수를 쓰면 둘)를 센다. */
            struct Entry
            {
                UndoStepInvoker _invoker{ nullptr };
                uint32          _count{ 0 };
            };

            /** @brief 표입니다. 정적 초기화 순서와 상관없게 함수 안에 둔다. 편집기 UI 스레드와 DLL 적재 · 해제(같은 스레드)만 만진다. */
            static map<uint64, Entry>& getTable()
            {
                static map<uint64, Entry> s_table;
                return s_table;
            }
        };
    } // namespace

    void UndoStepRegistry::add( uint64 functionHash, UndoStepInvoker invoker )
    {
        UndoStepRegistryInternal::Entry& entry = UndoStepRegistryInternal::getTable()[functionHash];
        if ( entry._count == 0 )
            entry._invoker = invoker;
        ++entry._count;
    }

    void UndoStepRegistry::remove( uint64 functionHash )
    {
        map<uint64, UndoStepRegistryInternal::Entry>& table = UndoStepRegistryInternal::getTable();
        const auto                                    it    = table.find( functionHash );
        if ( it == table.end() )
            return;
        if ( --it->second._count == 0 )
            table.erase( it );
    }

    UndoStepInvoker UndoStepRegistry::find( uint64 functionHash )
    {
        const map<uint64, UndoStepRegistryInternal::Entry>& table = UndoStepRegistryInternal::getTable();
        const auto                                          it    = table.find( functionHash );
        return it != table.end() ? it->second._invoker : nullptr;
    }

    UndoStepOwner::UndoStepOwner()
        : _id{ 0 }
        , _released{}
    {
        static uint64 s_nextID = 1; // 편집기 UI 스레드만 만든다
        _id                    = s_nextID++;
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
            SW_LOG_WARNING( "Undo step %# has no handler in this build - skipped", _functionHash );
            return;
        }
        Archive arguments( _arguments.data(), _arguments.size() );
        if ( invoker( arguments, context ) == false )
            SW_LOG_WARNING( "Undo step %# could not read its arguments - skipped", _functionHash );
    }
} // namespace sw::editor
