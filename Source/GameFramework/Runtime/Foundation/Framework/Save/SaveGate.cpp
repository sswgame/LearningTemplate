#include "pch.h"

#include "GameFramework/Runtime/Foundation/Framework/Save/SaveGate.h"

namespace sw
{
    SaveGate::SaveGate()
        : _listBlock{}
        , _nextID{ 1 }
    {
    }

    SaveGate::~SaveGate() = default; // 남은 구독은 기반 소멸자가 무효화한다(해제를 부르지 않으니 이 객체의 목록을 보지 않는다)

    Subscription SaveGate::block( const hashed_string& reason )
    {
        const uint64 id = _nextID++;
        _listBlock.push_back( Block{ id, reason } );
        return issueSubscription( id );
    }

    const hashed_string* SaveGate::findBlockReason() const
    {
        return _listBlock.empty() ? nullptr : &_listBlock.front()._reason;
    }

    void SaveGate::releaseSubscription( uint64 id )
    {
        for ( size_t index = 0; index < _listBlock.size(); ++index )
        {
            if ( _listBlock[index]._id == id )
            {
                _listBlock.erase( _listBlock.begin() + static_cast<ptrdiff_t>( index ) );
                return;
            }
        }
    }
} // namespace sw
