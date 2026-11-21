/**
 * @file FixedStepTickList.cpp
 * @brief 고정 스텝 그룹의 구성원 · 일괄 구간 구현입니다(파일 머리말은 헤더).
 */
#include "pch.h"

#include "Engine/Object/GameObject/Tick/FixedStepTickList.h"

#include "Engine/Object/Component/Component.h"
#include "Engine/Object/GameObject/Tick/TickBatchRegistry.h"
#include "Engine/Profiling/FrameProfiler.h"

namespace sw
{
    FixedStepTickList::FixedStepTickList()
        : _arrListMember{}
        , _mapLocation{}
        , _arrListComponent{}
        , _arrListRun{}
        , _mapFunctions{}
        , _registryGeneration{ 0 }
        , _bStale{ SW_TRUE }
    {
    }

    bool FixedStepTickList::add( Component& component, TickPhase phase )
    {
        if ( _mapLocation.find( &component ) != _mapLocation.end() )
            return false;
        // 단계는 64 간격이라 칸 하나가 버킷 하나다(`TickPhase`).
        const uint32 bucket = static_cast<uint32>( phase ) / kPhaseBucketWidth;
        SW_CHECK_INDEX( bucket, kPhaseBucketCount );
        vector<Member>& listMember = _arrListMember[bucket];
        _mapLocation.insert_or_assign( &component, Location{ bucket, static_cast<uint32>( listMember.size() ) } );
        listMember.push_back( Member{ &component, SW_TRUE } );
        _bStale = SW_TRUE;
        return true;
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

    const FixedStepTickList::FunctionTable& FixedStepTickList::findFunctions( TickBatchRegistry& registry, const TypeInfo* pType )
    {
        const auto found = _mapFunctions.find( pType );
        if ( found != _mapFunctions.end() )
            return found->second;
        FunctionTable                            table{};
        const TickBatchRegistry::FixedStepEntry* pEntry = pType != nullptr ? registry.findFixedStepEntry( pType ) : nullptr;
        if ( pEntry != nullptr )
            table = pEntry->_arrFunction;
        return _mapFunctions.insert_or_assign( pType, table ).first->second;
    }

    void FixedStepTickList::refresh( TickBatchRegistry* pRegistry )
    {
        // 표가 바뀌었으면(모듈 적재 · 해제) 기억한 함수를 버린다 — 내려간 모듈의 함수를 들고 있으면 안 된다.
        const uint32 generation = pRegistry != nullptr ? pRegistry->getGeneration() : 0;
        if ( generation != _registryGeneration )
        {
            _registryGeneration = generation;
            _mapFunctions.clear();
            _bStale = SW_TRUE;
        }
        if ( _bStale == SW_FALSE )
            return;
        _bStale = SW_FALSE;
        if ( pRegistry == nullptr )
        {
            // 표 없이 구성원이 있으면 훅을 부를 길이 없다 — 조용히 건너뛰지 않는다(가상 호출 물러나기는 지웠다).
            if ( _mapLocation.empty() == false )
                SW_FATAL( "FixedStepTickList: %# fixed step members but the scene has no TickBatchRegistry", static_cast<uint32>( _mapLocation.size() ) );
            for ( const FixedStepGroup group : EnumRange<FixedStepGroup>{} )
            {
                _arrListComponent[group].clear();
                _arrListRun[group].clear();
            }
            return;
        }
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
                    if ( member._bEnabled == SW_FALSE )
                        continue;
                    const FixedStepBatchFunction pFunction = findFunctions( *pRegistry, member._pComponent->findCachedTypeInfo() )[group];
                    if ( pFunction == nullptr )
                        continue; // 그 타입은 이 그룹의 훅을 재정의하지 않았다(또는 줄이 없다)
                    // 차례 그대로 이어진 같은 함수만 한 구간이다(차례를 지킨다 — 파일 머리말). 같은 함수면 같은 타입의 한정 호출이다.
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

    uint32 FixedStepTickList::run( FixedStepGroup group, const FixedStepContext& context, TickBatchRegistry* pRegistry )
    {
        refresh( pRegistry );
        // 구간은 이 그룹을 도는 동안 다시 짓지 않는다 — 훅 안의 켜고 끄기 · 빼기는 다음 그룹부터 맞다(파일 머리말).
        const vector<Component*>& listComponent = _arrListComponent[group];
        for ( const Run& run : _arrListRun[group] )
        {
            run._pFunction( context, listComponent.data() + run._start, run._count );
        }
        return static_cast<uint32>( listComponent.size() );
    }
} // namespace sw
