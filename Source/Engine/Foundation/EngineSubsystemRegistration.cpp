#include "pch.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

#include "Core/Container/array.h"

namespace sw::engine
{
    namespace
    {
        struct SlotInfo
        {
            const utf8*          _pName;
            EngineSubsystemStage _stage;
            bool                 _bClientOnly; ///< NOLINT(sw-bool-flag): 집합체다(중괄호 · 지정 초기화로 만든다) — C++17 비트필드는 기본값을 둘 수 없어 `T x;` 가 값을 남기지 않는다
        };

        constexpr SlotInfo kArrSlotInfo[] = {
#define SW_ENGINE_SUBSYSTEM_SLOT_INFO( Name, Stage, bClientOnly ) SlotInfo{ #Name, EngineSubsystemStage::Stage, bClientOnly },
            SW_ENGINE_SUBSYSTEM_SLOTS( SW_ENGINE_SUBSYSTEM_SLOT_INFO )
#undef SW_ENGINE_SUBSYSTEM_SLOT_INFO
        };
        static_assert( sizeof( kArrSlotInfo ) / sizeof( kArrSlotInfo[0] ) == static_cast<size_t>( EngineSubsystemSlot::Count ), "자리 표와 열거형의 개수가 다릅니다" );

        /** @brief 이 빌드가 그 자리를 등록해야 하는지입니다 — 클라이언트 전용 자리는 클라이언트 코드 빌드에서만 있다. */
        constexpr bool isSlotBuilt( const SlotInfo& info )
        {
#if defined( SW_WITH_CLIENT_CODE )
            (void)info;
            return true;
#else
            return info._bClientOnly == false;
#endif
        }
    } // namespace

    const EngineSubsystemRegistrar*& EngineSubsystemRegistrar::getHead()
    {
        static const EngineSubsystemRegistrar* s_pHead = nullptr;
        return s_pHead;
    }

    EngineSubsystemRegistrar::EngineSubsystemRegistrar( EngineSubsystemSlot slot, EngineSubsystemFactory pfnFactory )
        : _slot{ slot }
        , _pfnFactory{ pfnFactory }
        , _pNext{ getHead() }
    {
        getHead() = this;
    }

    const utf8* getEngineSubsystemSlotName( EngineSubsystemSlot slot )
    {
        return kArrSlotInfo[static_cast<size_t>( slot )]._pName;
    }

    EngineSubsystemStage getEngineSubsystemSlotStage( EngineSubsystemSlot slot )
    {
        return kArrSlotInfo[static_cast<size_t>( slot )]._stage;
    }

    bool isEngineSubsystemSlotClientOnly( EngineSubsystemSlot slot )
    {
        return kArrSlotInfo[static_cast<size_t>( slot )]._bClientOnly;
    }

    void buildEngineSubsystems( EngineSubsystemStage stage, SubsystemScope& scope, EngineSubsystemBuild& build )
    {
        // 자리 → 공장. 링크 순서(목록의 순서)는 쓰지 않는다 — 만드는 순서는 자리 표의 줄 순서다.
        array<EngineSubsystemFactory, static_cast<size_t>( EngineSubsystemSlot::Count )> arrFactory{};
        for ( const EngineSubsystemRegistrar* pRegistrar = EngineSubsystemRegistrar::getHead(); pRegistrar != nullptr; pRegistrar = pRegistrar->_pNext )
        {
            const size_t index = static_cast<size_t>( pRegistrar->_slot );
            if ( arrFactory[index] != nullptr )
                SW_FATAL( "Engine subsystem slot '%#' is registered twice (SW_ENGINE_SUBSYSTEM) - one registration per slot", kArrSlotInfo[index]._pName );
            arrFactory[index] = pRegistrar->_pfnFactory;
        }
        for ( size_t index = 0; index < arrFactory.size(); ++index )
        {
            const SlotInfo& info = kArrSlotInfo[index];
            if ( info._stage != stage )
                continue;
            const bool bBuilt = isSlotBuilt( info );
            if ( bBuilt && arrFactory[index] == nullptr )
                SW_FATAL( "Engine subsystem slot '%#' has no registration (SW_ENGINE_SUBSYSTEM in its .cpp) - the object file was dropped or the line was removed", info._pName );
            if ( bBuilt == false && arrFactory[index] != nullptr )
                SW_FATAL( "Engine subsystem slot '%#' is client-only but this build registered it", info._pName );
            if ( bBuilt )
                arrFactory[index]( scope, build );
        }
    }
} // namespace sw::engine
