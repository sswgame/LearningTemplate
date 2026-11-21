/**
 * @file FixedStepTickList.cpp
 * @brief 고정 스텝 그룹의 구성원 · 일괄 구간 구현입니다(파일 머리말은 헤더).
 */
#include "pch.h"

#include "Engine/Object/GameObject/Tick/FixedStepTickList.h"

#include "Engine/Object/Component/Component.h"

namespace sw
{
    FixedStepTickList::FixedStepTickList()
        : _arrListMember{}
        , _mapLocation{}
        , _arrListComponent{}
        , _arrListRun{}
        , _bStale{ SW_TRUE }
    {
    }

    bool FixedStepTickList::addFunction( Component& component, FixedStepGroup group, TickPhase phase, FixedStepBatchFunction pFunction )
    {
        // 단계는 64 간격이라 칸 하나가 버킷 하나다(`TickPhase`).
        const uint32 bucket = static_cast<uint32>( phase ) / kPhaseBucketWidth;
        SW_CHECK_INDEX( bucket, kPhaseBucketCount );
        if ( pFunction == nullptr )
            return false;
        auto found = _mapLocation.find( &component );
        if ( found == _mapLocation.end() )
        {
            vector<Member>& listMember = _arrListMember[bucket];
            found                      = _mapLocation.insert_or_assign( &component, Location{ bucket, static_cast<uint32>( listMember.size() ) } ).first;
            listMember.push_back( Member{ &component, {}, SW_TRUE } );
        }
        else if ( found->second._bucket != bucket )
        {
            // 한 구성원은 한 단계다 — 그룹마다 단계가 다르면 차례(단계 버킷의 넣은 차례)를 하나로 정할 수 없다.
            SW_LOG_ERROR_ID( "FixedStepTickList.PhaseMismatch", "Fixed step: a component registered groups in two phases (%# and %#) - the later group is ignored",
                             found->second._bucket, bucket );
            return false;
        }
        FixedStepBatchFunction& slot = _arrListMember[found->second._bucket][found->second._index]._arrFunction[group];
        if ( slot != nullptr )
            return false;
        slot    = pFunction;
        _bStale = SW_TRUE;
        return true;
    }

    bool FixedStepTickList::isRegistered( const Component& component, FixedStepGroup group ) const
    {
        const auto found = _mapLocation.find( &component );
        return found != _mapLocation.end() && _arrListMember[found->second._bucket][found->second._index]._arrFunction[group] != nullptr;
    }

    bool FixedStepTickList::remove( const Component& component )
    {
        const auto found = _mapLocation.find( &component );
        if ( found == _mapLocation.end() )
            return false;
        const Location  location   = found->second;
        vector<Member>& listMember = _arrListMember[location._bucket];
        _mapLocation.erase( found );
        // 끝 구성원을 그 자리로 옮긴다(물리 단계 목록과 같은 차례 규칙 — 파일 머리말).
        if ( location._index + 1 != listMember.size() )
        {
            listMember[location._index]                                  = listMember.back();
            _mapLocation[listMember[location._index]._pComponent]._index = location._index;
        }
        listMember.pop_back();
        _bStale = SW_TRUE; // 구간이 지운 구성원을 가리키지 않게 다음 그룹 전에 다시 짓는다
        return true;
    }

    bool FixedStepTickList::setEnabled( const Component& component, bool bEnabled )
    {
        const auto found = _mapLocation.find( &component );
        if ( found == _mapLocation.end() )
            return false;
        // 지금 값과 견주어 일찍 돌아가지 않는다 — 구간을 지을 때의 값만 본다(늦은 비교 결함이 없다).
        _arrListMember[found->second._bucket][found->second._index]._bEnabled = bEnabled ? SW_TRUE : SW_FALSE;
        _bStale                                                               = SW_TRUE;
        return true;
    }

    bool FixedStepTickList::isEnabled( const Component& component ) const
    {
        const auto found = _mapLocation.find( &component );
        return found != _mapLocation.end() && _arrListMember[found->second._bucket][found->second._index]._bEnabled == SW_TRUE;
    }

    void FixedStepTickList::refresh()
    {
        if ( _bStale == SW_FALSE )
            return;
        _bStale = SW_FALSE;
        for ( const FixedStepGroup group : EnumRange<FixedStepGroup>{} )
        {
            vector<Component*>& listComponent = _arrListComponent[group];
            vector<Run>&        listRun       = _arrListRun[group];
            listComponent.clear();
            listRun.clear();
            for ( const vector<Member>& listMember : _arrListMember )
            {
                for ( const Member& member : listMember )
                {
                    const FixedStepBatchFunction pFunction = member._arrFunction[group];
                    if ( member._bEnabled == SW_FALSE || pFunction == nullptr )
                        continue;
                    // 차례 그대로 이어진 같은 함수(같은 메서드)만 한 구간이다(차례를 지킨다 — 파일 머리말).
                    const bool bExtend = listRun.empty() == false && listRun.back()._pFunction == pFunction &&
                                         listRun.back()._start + listRun.back()._count == static_cast<uint32>( listComponent.size() );
                    if ( bExtend )
                        ++listRun.back()._count;
                    else
                        listRun.push_back( Run{ pFunction, static_cast<uint32>( listComponent.size() ), 1 } );
                    listComponent.push_back( member._pComponent );
                }
            }
        }
    }

    uint32 FixedStepTickList::run( FixedStepGroup group, const FixedStepContext& context )
    {
        refresh();
        // 구간은 이 그룹을 도는 동안 다시 짓지 않는다 — 훅 안의 켜고 끄기 · 빼기는 다음 그룹부터 맞다(파일 머리말).
        const vector<Component*>& listComponent = _arrListComponent[group];
        for ( const Run& run : _arrListRun[group] )
        {
            run._pFunction( context, listComponent.data() + run._start, run._count );
        }
        return static_cast<uint32>( listComponent.size() );
    }
} // namespace sw
