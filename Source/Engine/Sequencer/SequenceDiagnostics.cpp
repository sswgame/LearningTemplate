#include "pch.h"

#include "Engine/Sequencer/SequenceDiagnostics.h"

#include "Core/Foundation/BuildInfo.h"
#include "Core/Log/Logger.h"
#include "Core/Registry/RegistrationList.h"

namespace sw
{
    namespace
    {
        struct SequenceDiagnosticsInternal
        {
            static SW_DEFINE_REGISTRATION_LIST( getRegistrations, const SequenceDiagnosticRegistration, "SequenceDiagnosticRegistry", RegistrationOrder::Insertion, true )
        };
    } // namespace

    SequencePlaybackHistory::SequencePlaybackHistory()
        : _arrEntry{}
        , _recordedCount{ 0 }
        , _head{ 0 }
        , _count{ 0 }
    {
    }

    void SequencePlaybackHistory::record( SequenceHistoryKind kind, int32 frame, uint32 bindingID, string_view name, uint8 detail )
    {
        if constexpr ( build::kShipping == false )
        {
            SequenceHistoryEntry& entry = _arrEntry[_head];
            entry._name                 = name.substr( 0, entry._name.capacity() - 1 );
            entry._binding              = bindingID;
            entry._frame                = frame;
            entry._kind                 = kind;
            entry._detail               = detail;
            _head                       = ( _head + 1 ) % kCapacity;
            _count                      = _count < kCapacity ? _count + 1 : kCapacity;
            ++_recordedCount;
        }
        else
        {
            (void)kind;
            (void)frame;
            (void)bindingID;
            (void)name;
            (void)detail;
        }
    }

    void SequencePlaybackHistory::clear()
    {
        _head  = 0;
        _count = 0;
    }

    const SequenceHistoryEntry& SequencePlaybackHistory::getEntry( uint32 index ) const
    {
        if ( index >= _count )
            SW_FATAL( "Sequence history entry %# is outside %# entries", index, _count );
        const uint32 oldest = ( _head + kCapacity - _count ) % kCapacity;
        return _arrEntry[( oldest + index ) % kCapacity];
    }

    const SequenceHistoryEntry* SequencePlaybackHistory::findLatest() const
    {
        return _count > 0 ? &getEntry( _count - 1 ) : nullptr;
    }

    uint32 SequencePlaybackHistory::countKind( SequenceHistoryKind kind ) const
    {
        uint32 kindCount = 0;
        for ( uint32 index = 0; index < _count; ++index )
        {
            kindCount += getEntry( index )._kind == kind ? 1u : 0u;
        }
        return kindCount;
    }

    bool SequenceDiagnosticRegistry::registerProvider( const SequenceDiagnosticRegistration* pRegistration )
    {
        if ( pRegistration == nullptr || pRegistration->_pName == nullptr || pRegistration->_pDescribe == nullptr )
            return false;
        return SequenceDiagnosticsInternal::getRegistrations().addLogged( pRegistration, "Sequence diagnostic provider", pRegistration->_pName ) == RegistrationResult::Added;
    }

    void SequenceDiagnosticRegistry::unregisterProvider( const SequenceDiagnosticRegistration* pRegistration )
    {
        (void)SequenceDiagnosticsInternal::getRegistrations().remove( pRegistration ); // 올라 있지 않은 제공자를 빼는 것은 할 일이 없다
    }

    void SequenceDiagnosticRegistry::collectProviders( vector<const SequenceDiagnosticRegistration*>& outListProvider )
    {
        const vector<const SequenceDiagnosticRegistration*>& listItem = SequenceDiagnosticsInternal::getRegistrations().getItems();
        outListProvider.assign( listItem.begin(), listItem.end() );
    }
} // namespace sw
