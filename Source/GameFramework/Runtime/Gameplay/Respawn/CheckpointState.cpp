#include "pch.h"

#include "GameFramework/Runtime/Gameplay/Respawn/CheckpointState.h"

#include "Core/Foundation/Constants.h"

#include "Engine/Serialization/Format/Archive.h"
#include "Engine/Serialization/Format/StateIO.h"

namespace sw
{
    CheckpointState::CheckpointState()
        : _listActivated{}
        , _currentIndex{ constant::invalid_index::kInt32 }
        , _bForwardOnly{ SW_FALSE }
    {
    }

    CheckpointActivation CheckpointState::activate( const CheckpointDef& def, const float3& position, float32 yaw )
    {
        if ( def._id.empty() )
            return CheckpointActivation::Invalid;
        const CheckpointRecord* pCurrent = getCurrent();
        if ( pCurrent != nullptr && pCurrent->_id == def._id )
            return CheckpointActivation::AlreadyCurrent;
        // 앞으로만 가는 상태는 지금보다 앞쪽 체크포인트로 되돌아가지 않는다(플랫포머 — 건너뛴 것은 인정한다).
        if ( _bForwardOnly == SW_TRUE && pCurrent != nullptr && def._order <= pCurrent->_order )
            return CheckpointActivation::Behind;
        for ( size_t index = 0; index < _listActivated.size(); ++index )
        {
            if ( _listActivated[index]._id != def._id )
                continue;
            if ( def._bReusable == false )
                return CheckpointActivation::Spent;
            _currentIndex = static_cast<int32>( index );
            return CheckpointActivation::Reused;
        }
        CheckpointRecord record;
        record._id       = def._id;
        record._position = position;
        record._yaw      = yaw;
        record._order    = def._order;
        _listActivated.push_back( record );
        _currentIndex = static_cast<int32>( _listActivated.size() ) - 1;
        return CheckpointActivation::Activated;
    }

    void CheckpointState::reset()
    {
        _listActivated.clear();
        _currentIndex = constant::invalid_index::kInt32;
    }

    const CheckpointRecord* CheckpointState::getCurrent() const
    {
        return _currentIndex >= 0 && _currentIndex < static_cast<int32>( _listActivated.size() ) ? &_listActivated[static_cast<size_t>( _currentIndex )] : nullptr;
    }

    const CheckpointRecord* CheckpointState::find( const hashed_string& id ) const
    {
        for ( const CheckpointRecord& record : _listActivated )
        {
            if ( record._id == id )
                return &record;
        }
        return nullptr;
    }

    bool CheckpointState::isConsistent() const
    {
        const int32 count = static_cast<int32>( _listActivated.size() );
        // 켜면 늘 부활 지점이 생기므로 목록이 있는데 지금 자리가 없는 상태는 만들어지지 않는다.
        if ( _currentIndex < constant::invalid_index::kInt32 || _currentIndex >= count || ( count > 0 ) != ( _currentIndex >= 0 ) )
            return false;
        for ( int32 index = 0; index < count; ++index )
        {
            const CheckpointRecord& record = _listActivated[static_cast<size_t>( index )];
            if ( record._id.empty() )
                return false;
            for ( int32 previous = 0; previous < index; ++previous )
            {
                const CheckpointRecord& earlier = _listActivated[static_cast<size_t>( previous )];
                if ( earlier._id == record._id || ( _bForwardOnly == SW_TRUE && earlier._order >= record._order ) )
                    return false;
            }
        }
        return _bForwardOnly == SW_FALSE || _currentIndex == count - 1;
    }

    void CheckpointState::serializeState( StateIO& io )
    {
        io.reflected( *this ); // 켠 목록 · 지금 자리 — 칸 바닥은 `PROPERTY( Min )`
        if ( io.isReading() && io.isOk() )
            io.check( isConsistent() );
    }
} // namespace sw
