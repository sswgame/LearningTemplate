/**
 * @file TickBatchRegistry.cpp
 * @brief 타입별 일괄 틱 레지스트리 구현입니다(서비스 범위의 등록 목록 · 세대, 모듈을 올리고 내리는 길이 붙이고 떼는 등록자).
 */
#include "pch.h"

#include "Engine/Object/GameObject/Tick/TickBatchRegistry.h"

#include "Engine/Foundation/EngineServices.h"
#include "Engine/Foundation/EngineSubsystemRegistration.h"

namespace sw
{
    bool TickBatchRegistry::registerEntry( Entry& entry )
    {
        if ( entry._pStaticType == nullptr || entry._pFunction == nullptr )
            return false;
        {
            ScopedLock<mutex> lock{ _mutex };
            (void)_listEntry.addLogged( &entry, "Tick batch entry" ); // 같은 줄을 다시 올리면 `AlreadyPresent` 라 할 일이 없다
        }
        _generation.fetch_add( 1, std::memory_order_acq_rel );
        return true;
    }

    void TickBatchRegistry::unregisterEntry( Entry& entry )
    {
        bool bRemoved = false;
        {
            ScopedLock<mutex> lock{ _mutex };
            bRemoved = _listEntry.remove( &entry );
        }
        if ( bRemoved )
            _generation.fetch_add( 1, std::memory_order_acq_rel );
    }

    TickBatchFunction TickBatchRegistry::findFunction( const TypeInfo* pType )
    {
        if ( pType == nullptr )
            return nullptr;
        ScopedLock<mutex>     lock{ _mutex };
        const vector<Entry*>& listEntry = _listEntry.getItems();
        for ( size_t index = listEntry.size(); index > 0; --index )
        {
            // 주소가 아니라 지금의 `StaticType` 과 비교한다 — 모듈을 다시 올리면 타입 정보가 새로 만들어질 수 있다.
            const Entry* const pEntry = listEntry[index - 1];
            if ( pEntry->_pStaticType() == pType )
                return pEntry->_pFunction;
        }
        return nullptr;
    }

    uint32 TickBatchRegistry::getRegisteredCount()
    {
        ScopedLock<mutex> lock{ _mutex };
        return _listEntry.getCount();
    }
} // namespace sw

namespace sw
{
    // 일괄 틱 함수 표 — 데이터 종류와 같이 모듈을 올리는 길이 정적 등록자를 붙인다.
    SW_ENGINE_SERVICE_SUBSYSTEM( TickBatchRegistry, HostOnly, Services, (), SW_ENGINE_DEFAULT_FACTORY( TickBatchRegistry ) );
} // namespace sw
