#include "pch.h"

#include "Editor/Common/Workspace/Undo/CommandStack.h"

#include "Core/Algorithm/VectorUtil.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/FormatDef.h"

namespace sw
{
    namespace
    {
        struct CommandStackInternal
        {
            /** @brief 기록 형식의 판입니다. 명령 · 단계의 모양이 바뀌면 올린다(옛 판의 기록은 버린다). */
            static constexpr FormatDef kSaveFormat{ "EDUN" };

            static void writeStep( Archive& archive, const editor::UndoStep& step )
            {
                archive << step._functionHash;
                archive << step._arguments;
            }

            [[nodiscard]] static bool readStep( Archive& archive, editor::UndoStep& outStep )
            {
                archive >> outStep._functionHash;
                archive >> outStep._arguments;
                return archive.isOk();
            }

            static void writeCommand( Archive& archive, const CommandStack::Command& cmd )
            {
                archive.writeString( cmd._label );
                const uint32 childCount = cmd._listChild != nullptr ? static_cast<uint32>( cmd._listChild->size() ) : 0u;
                archive << childCount;
                if ( childCount > 0 )
                {
                    for ( const CommandStack::Command& child : *cmd._listChild )
                    {
                        writeCommand( archive, child );
                    }
                    return;
                }
                writeStep( archive, cmd._undo );
                writeStep( archive, cmd._redo );
            }

            /** @brief 명령 하나의 최소 바이트입니다(이름표 길이 + 자식 수). */
            static constexpr uint32 kMinCommandBytes = sizeof( uint32 ) * 2;

            [[nodiscard]] static bool readCommand( Archive& archive, CommandStack::Command& outCmd )
            {
                uint32 childCount{ 0 };
                auto   loadedLabel = archive.readString();
                if ( archive.isError() )
                    return false;
                outCmd._label = std::move( loadedLabel );
                childCount    = archive.readCount( kMinCommandBytes );
                if ( archive.isError() )
                    return false;
                if ( childCount > 0 )
                {
                    vector<CommandStack::Command> listChild( childCount );
                    for ( CommandStack::Command& child : listChild )
                    {
                        if ( readCommand( archive, child ) == false )
                            return false;
                    }
                    outCmd._listChild = sw::make_shared<const vector<CommandStack::Command>>( std::move( listChild ) );
                    return true;
                }
                return readStep( archive, outCmd._undo ) && readStep( archive, outCmd._redo );
            }

            /** @brief 맡길 수 있는 명령만 쓰고, 위치(@p index)는 그 앞에 남은 수로 맞춰 쓴다. */
            static void writeList( Archive& archive, const vector<CommandStack::Command>& listCommand, size_t index,
                                   bool ( *isPersistable )( const CommandStack::Command& ) )
            {
                uint32 keptCount{ 0 };
                uint32 keptBeforeIndex{ 0 };
                for ( size_t commandIndex = 0; commandIndex < listCommand.size(); ++commandIndex )
                {
                    if ( isPersistable( listCommand[commandIndex] ) == false )
                        continue;
                    ++keptCount;
                    if ( commandIndex < index )
                        ++keptBeforeIndex;
                }
                archive << keptCount << keptBeforeIndex;
                for ( const CommandStack::Command& cmd : listCommand )
                {
                    if ( isPersistable( cmd ) )
                        writeCommand( archive, cmd );
                }
            }

            [[nodiscard]] static bool readList( Archive& archive, vector<CommandStack::Command>& outListCommand, size_t& outIndex )
            {
                uint32 count{ 0 };
                uint32 index{ 0 };
                count = archive.readCount( kMinCommandBytes );
                if ( archive.isError() )
                    return false;
                archive >> index;
                if ( archive.isOk() == false || index > count )
                    return false;
                outListCommand.resize( count );
                for ( CommandStack::Command& cmd : outListCommand )
                {
                    if ( readCommand( archive, cmd ) == false )
                        return false;
                }
                outIndex = index;
                return true;
            }
        };
    } // namespace

    CommandStack::CommandStack( const UndoBufferLimit& limit )
        : _limit{ limit }
        , _listCommand{}
        , _listPendingTransactionCommand{}
        , _listSuspendedCommand{}
        , _objectEditListener{}
        , _transactionLabel{}
        , _empty{}
        , _index{ 0 }
        , _suspendedIndex{ 0 }
        , _transactionDepth{ 0 }
        , _interactionDepth{ 0 }
        , _bIsExecuting{ SW_FALSE }
        , _bHistorySuspended{ SW_FALSE }
        , _pAlive{ sw::make_shared<CommandStack*>( this ) }
        , _listOwnerSubscription{}
    {
        SW_ASSERT( limit._maxCommandCount > 0 && limit._maxBytes > 0 ); // 0 이면 기록이 남지 않는다 — 설정 값이 틀렸다
    }

    CommandStack::~CommandStack()
    {
        *_pAlive = nullptr; // 남은 상호작용 핸들은 이제 아무것도 하지 않는다
    }

    void CommandStackInteractionScope::reset()
    {
        if ( _pStack == nullptr )
            return;
        if ( *_pStack != nullptr )
            ( *_pStack )->closeInteraction();
        _pStack.reset();
    }

    CommandStackInteractionScope CommandStack::openInteraction()
    {
        ++_interactionDepth;
        return CommandStackInteractionScope{ _pAlive };
    }

    void CommandStack::subscribeOwners( const Command& cmd )
    {
        for ( const editor::UndoStep* pStep : { &cmd._undo, &cmd._redo } )
        {
            if ( pStep->_pOwner == nullptr )
                continue;
            const uint64 ownerID     = pStep->_ownerID;
            const bool   bSubscribed = VectorUtil::containsIf( _listOwnerSubscription, [ownerID]( const OwnerSubscription& subscription )
              { return subscription._ownerID == ownerID; } );
            if ( bSubscribed == false )
                _listOwnerSubscription.push_back( OwnerSubscription{ ownerID, pStep->_pOwner->subscribeReleased( SW_DELEGATE_METHOD( editor::UndoStepOwner::ReleasedDelegate, &CommandStack::onOwnerReleased, this ) ) } );
        }
        if ( cmd._listChild != nullptr )
        {
            for ( const Command& child : *cmd._listChild )
                subscribeOwners( child );
        }
    }

    void CommandStack::onOwnerReleased( uint64 ownerID )
    {
        (void)removeOwnedBy( ownerID );
        // 알림 안에서 구독을 놓는다 — 이 주인은 다시 알리지 않는다(사라지는 중이다).
        _listOwnerSubscription.erase( std::remove_if( _listOwnerSubscription.begin(), _listOwnerSubscription.end(), [ownerID]( const OwnerSubscription& subscription )
        { return subscription._ownerID == ownerID; } ),
                                      _listOwnerSubscription.end() );
    }

    void CommandStack::push( Command cmd )
    {
        if ( isExecutable( cmd ) == false || _bIsExecuting )
            return;
        subscribeOwners( cmd );

        if ( _transactionDepth != 0 )
        {
            _listPendingTransactionCommand.push_back( std::move( cmd ) );
            return;
        }

        if ( _index < _listCommand.size() )
            _listCommand.erase( _listCommand.begin() + static_cast<std::ptrdiff_t>( _index ), _listCommand.end() );

        _listCommand.push_back( std::move( cmd ) );
        ++_index;
        evictOverLimit();
    }

    uint64 CommandStack::measureHeldBytes( const Command& cmd )
    {
        uint64 bytes = static_cast<uint64>( cmd._label.size() ) + cmd._undo.getHeldBytes() + cmd._redo.getHeldBytes();
        if ( cmd._listChild != nullptr )
        {
            for ( const Command& child : *cmd._listChild )
                bytes += measureHeldBytes( child );
        }
        return bytes;
    }

    void CommandStack::evictOverLimit()
    {
        // 기록 전체를 센다 — 명령 수가 한도(수백) 안이라 push 마다 세도 싸고, 따로 든 합계가 빼기 · 비우기 · 맡김 길과 어긋날 일이 없다.
        uint64 totalBytes{ 0 };
        for ( const Command& cmd : _listCommand )
            totalBytes += measureHeldBytes( cmd );
        size_t dropCount{ 0 };
        // 가장 새 명령 하나는 남긴다 — 그 하나가 바이트 한도보다 커도 방금 한 편집은 되돌릴 수 있어야 한다.
        while ( _listCommand.size() - dropCount > 1 &&
                ( _listCommand.size() - dropCount > _limit._maxCommandCount || totalBytes > _limit._maxBytes ) )
        {
            totalBytes -= measureHeldBytes( _listCommand[dropCount] );
            ++dropCount;
        }
        if ( dropCount == 0 )
            return;
        _listCommand.erase( _listCommand.begin(), _listCommand.begin() + static_cast<std::ptrdiff_t>( dropCount ) );
        _index = _index > dropCount ? _index - dropCount : 0;
    }

    void CommandStack::beginTransaction( string_view label )
    {
        // 중첩 호출은 가장 바깥 트랜잭션에 합친다. 여기서 목록을 비우면 바깥이 쌓아 둔 기록이 사라진다.
        if ( _transactionDepth == 0 )
        {
            _transactionLabel = label;
            _listPendingTransactionCommand.clear();
        }
        else if ( _transactionLabel.empty() )
            _transactionLabel = label; // 이름 없는 바깥 구간(명령 레지스트리가 연 것)은 안쪽 구간의 이름을 단다 — 명령이 스스로 지은 이름이 남는다
        ++_transactionDepth;
    }

    void CommandStack::endTransaction()
    {
        if ( _transactionDepth == 0 )
            return;

        // 가장 바깥이 끝날 때만 실제로 커밋한다.
        --_transactionDepth;
        if ( _transactionDepth != 0 )
            return;

        if ( _listPendingTransactionCommand.empty() )
            return;

        // 트랜잭션 레이블은 명령이 몇 개든 그 트랜잭션의 이름이다("Move 3 objects" 로 묶었는데 명령이 하나여도 그 이름).
        if ( _listPendingTransactionCommand.size() == 1 )
        {
            Command singleCmd = std::move( _listPendingTransactionCommand[0] );
            _listPendingTransactionCommand.clear();
            if ( _transactionLabel.empty() == false )
                singleCmd._label = _transactionLabel;
            push( std::move( singleCmd ) );
            return;
        }

        Command compoundCmd;
        compoundCmd._label     = _transactionLabel.empty() == false
                                   ? _transactionLabel
                                   : _listPendingTransactionCommand[0]._label;
        compoundCmd._listChild = sw::make_shared<const vector<Command>>( std::move( _listPendingTransactionCommand ) );
        _listPendingTransactionCommand.clear();
        push( std::move( compoundCmd ) );
    }

    void CommandStack::cancelTransaction()
    {
        // 취소는 중첩 깊이와 무관하게 전체 트랜잭션을 버린다.
        _transactionDepth = 0;
        _transactionLabel.clear();
        _listPendingTransactionCommand.clear();
    }

    void CommandStack::closeInteraction()
    {
        SW_ASSERT( _interactionDepth != 0 ); // 핸들 하나가 한 번 닫는다
        if ( _interactionDepth != 0 )
            --_interactionDepth;
    }

    bool CommandStack::canUndo() const
    {
        return _index > 0 && isEditOpen() == false;
    }

    bool CommandStack::canRedo() const
    {
        return _index < _listCommand.size() && isEditOpen() == false;
    }

    bool CommandStack::undo()
    {
        // 열린 편집(거래 · 상호작용)이 있으면 거절한다 — 입구(Ctrl+Z · History · 시나리오)가 몇이든 스택 하나가 막는다(감사 E-2).
        if ( canUndo() == false || _bIsExecuting )
            return false;
        --_index;
        _bIsExecuting = SW_TRUE;
        execute( _listCommand[_index], true );
        _bIsExecuting = SW_FALSE;
        return true;
    }

    void CommandStack::discardRedo()
    {
        if ( _bIsExecuting || _index >= _listCommand.size() )
            return;
        _listCommand.erase( _listCommand.begin() + static_cast<std::ptrdiff_t>( _index ), _listCommand.end() );
    }

    bool CommandStack::redo()
    {
        if ( canRedo() == false || _bIsExecuting )
            return false;
        const size_t targetIndex = _index;
        ++_index;
        _bIsExecuting = SW_TRUE;
        execute( _listCommand[targetIndex], false );
        _bIsExecuting = SW_FALSE;
        return true;
    }

    void CommandStack::notifyObjectEdit( const ObjectEditEvent& event ) const
    {
        if ( _objectEditListener.isBound() )
            _objectEditListener.get()( event );
    }

    uint32 CommandStack::removeOwnedBy( uint64 ownerID )
    {
        uint32 droppedCount = dropOwnedBy( _listCommand, _index, ownerID );
        droppedCount += dropOwnedBy( _listSuspendedCommand, _suspendedIndex, ownerID );
        size_t keptPendingCount{ 0 };
        for ( size_t pendingIndex = 0; pendingIndex < _listPendingTransactionCommand.size(); ++pendingIndex )
        {
            if ( isOwnedBy( _listPendingTransactionCommand[pendingIndex], ownerID ) )
            {
                ++droppedCount;
                continue;
            }
            if ( keptPendingCount != pendingIndex )
                _listPendingTransactionCommand[keptPendingCount] = std::move( _listPendingTransactionCommand[pendingIndex] );
            ++keptPendingCount;
        }
        _listPendingTransactionCommand.resize( keptPendingCount );
        return droppedCount;
    }

    uint32 CommandStack::dropOwnedBy( vector<Command>& inoutListCommand, size_t& inoutIndex, uint64 ownerID )
    {
        uint32 droppedCount{ 0 };
        size_t keptCount{ 0 };
        size_t keptBeforeIndex{ 0 };
        for ( size_t commandIndex = 0; commandIndex < inoutListCommand.size(); ++commandIndex )
        {
            if ( isOwnedBy( inoutListCommand[commandIndex], ownerID ) )
            {
                ++droppedCount;
                continue;
            }
            if ( commandIndex < inoutIndex )
                ++keptBeforeIndex;
            if ( keptCount != commandIndex )
                inoutListCommand[keptCount] = std::move( inoutListCommand[commandIndex] );
            ++keptCount;
        }
        inoutListCommand.resize( keptCount );
        inoutIndex = keptBeforeIndex;
        return droppedCount;
    }

    void CommandStack::execute( const Command& cmd, bool bUndo )
    {
        if ( cmd._listChild != nullptr )
        {
            const vector<Command>& listChild = *cmd._listChild;
            if ( bUndo )
            {
                for ( size_t childIndex = listChild.size(); childIndex > 0; --childIndex )
                {
                    execute( listChild[childIndex - 1], true );
                }
            }
            else
            {
                for ( const Command& child : listChild )
                {
                    execute( child, false );
                }
            }
            return;
        }

        editor::UndoStepContext context{ *this }; // 처리기가 이 스택에 알린다(전역 실행 중 칸을 두지 않는다)
        ( bUndo ? cmd._undo : cmd._redo ).execute( context );
    }

    bool CommandStack::isOwnedBy( const Command& cmd, uint64 ownerID )
    {
        if ( cmd._undo._ownerID == ownerID || cmd._redo._ownerID == ownerID )
            return true;
        if ( cmd._listChild == nullptr )
            return false;
        for ( const Command& child : *cmd._listChild )
        {
            if ( isOwnedBy( child, ownerID ) )
                return true;
        }
        return false;
    }

    bool CommandStack::isPersistable( const Command& cmd )
    {
        if ( cmd._listChild != nullptr )
        {
            for ( const Command& child : *cmd._listChild )
            {
                if ( isPersistable( child ) == false )
                    return false;
            }
            return true;
        }
        return cmd._undo.isPersistable() && cmd._redo.isPersistable();
    }

    void CommandStack::save( Archive& outArchive ) const
    {
        CommandStackInternal::kSaveFormat.write( outArchive );
        CommandStackInternal::writeList( outArchive, _listCommand, _index, &CommandStack::isPersistable );
        outArchive << _bHistorySuspended;
        CommandStackInternal::writeList( outArchive, _listSuspendedCommand, _suspendedIndex, &CommandStack::isPersistable );
    }

    bool CommandStack::load( Archive& archive )
    {
        clear();
        _listSuspendedCommand.clear();
        _suspendedIndex    = 0;
        _bHistorySuspended = SW_FALSE;
        bool       bSuspended{ false };
        const bool bLoaded = CommandStackInternal::kSaveFormat.check( archive ).has_value() && CommandStackInternal::readList( archive, _listCommand, _index ) &&
                             ( archive >> bSuspended ).isOk() && CommandStackInternal::readList( archive, _listSuspendedCommand, _suspendedIndex );
        if ( bLoaded == false )
        {
            clear();
            _listSuspendedCommand.clear();
            _suspendedIndex = 0;
            return false;
        }
        _bHistorySuspended = bSuspended;
        return true;
    }

    bool CommandStack::isExecutable( const Command& cmd )
    {
        const bool bHasBothSteps = cmd._undo.isBound() && cmd._redo.isBound();
        const bool bIsCompound   = cmd._listChild != nullptr && cmd._listChild->empty() == false;
        return bHasBothSteps || bIsCompound;
    }

    void CommandStack::clear()
    {
        _listCommand.clear();
        _listPendingTransactionCommand.clear();
        _transactionLabel.clear();
        _index            = 0;
        _transactionDepth = 0;
    }

    void CommandStack::suspendHistory()
    {
        _listSuspendedCommand = std::move( _listCommand );
        _suspendedIndex       = _index;
        _bHistorySuspended    = SW_TRUE;
        clear();
    }

    void CommandStack::resumeHistory()
    {
        clear();
        if ( _bHistorySuspended == false )
            return;
        _listCommand = std::move( _listSuspendedCommand );
        _index       = _suspendedIndex;
        _listSuspendedCommand.clear();
        _suspendedIndex    = 0;
        _bHistorySuspended = SW_FALSE;
    }

    const string& CommandStack::peekUndoLabel() const
    {
        if ( _index == 0 ) // 이름은 열린 편집과 무관하다(메뉴 글이 끌기 동안 깜빡이지 않게)
            return _empty;
        return _listCommand[_index - 1]._label;
    }

    const string& CommandStack::peekRedoLabel() const
    {
        if ( _index >= _listCommand.size() )
            return _empty;
        return _listCommand[_index]._label;
    }

    void CommandStack::jumpTo( size_t targetIndex )
    {
        // 재진입 플래그를 여기서도 본다. undo/redo 콜백 안에서 부르면 아래 `undo()` 가 그 플래그 때문에 아무것도 하지 않아 `_index` 가
        // 줄지 않고 `while` 이 끝나지 않는다.
        if ( _bIsExecuting || isEditOpen() )
            return;

        if ( targetIndex > _listCommand.size() )
            targetIndex = _listCommand.size();

        // 거절되면(단계가 편집을 열었다) 멈춘다 — `_index` 가 줄지 않으면 `while` 이 끝나지 않는다.
        while ( _index > targetIndex && undo() )
        {
        }
        while ( _index < targetIndex && redo() )
        {
        }
    }
} // namespace sw
