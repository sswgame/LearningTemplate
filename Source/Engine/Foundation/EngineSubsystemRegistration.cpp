#include "pch.h"

#include "Engine/Foundation/EngineSubsystemRegistration.h"

#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Subsystem/Subsystem.h"
#include "Core/Subsystem/SubsystemCollection.h"

namespace sw::engine
{
    namespace
    {
        struct EngineRegistrationListInternal
        {
            /** @brief @p pScope 부터 상위로 올라가며 @p typeHash 를 찾습니다(`findSubsystemFrom` 의 해시 판). */
            static void* locate( const ISubsystemLocator* pScope, uint64 typeHash )
            {
                for ( ; pScope != nullptr; pScope = pScope->getParentScope() )
                {
                    void* const pFound = pScope->findInScope( typeHash );
                    if ( pFound != nullptr )
                        return pFound;
                }
                return nullptr;
            }

            /** @brief @p pScope 부터 상위로 올라가며 @p typeHash 를 처음 찾은 범위의 소유 서브시스템입니다(`locate` 와 같은 범위 — 빌려 건 칸이면 nullptr). */
            static const Subsystem* locateOwner( const ISubsystemLocator* pScope, uint64 typeHash )
            {
                for ( ; pScope != nullptr; pScope = pScope->getParentScope() )
                {
                    if ( pScope->findInScope( typeHash ) != nullptr )
                        return pScope->findOwnerInScope( typeHash );
                }
                return nullptr;
            }

            /** @brief 이름 순으로 앞인지입니다(링크 순서와 무관하게 결정적인 보고 · 동점 순서). */
            static bool isNameBefore( const utf8* pLeft, const utf8* pRight ) { return string_view{ pLeft } < string_view{ pRight }; }

            /** @brief @p from 에서 간선(@p listEdge)을 따라 @p to 에 닿는지입니다. 넓이 우선으로 걷는다(기동 때 한 번, 수십 개). */
            static bool isReachable( const vector<vector<uint32>>& listEdge, uint32 from, uint32 to )
            {
                vector<uint8>  listVisited( listEdge.size(), SW_FALSE );
                vector<uint32> listQueue{ from };
                for ( size_t head = 0; head < listQueue.size(); ++head )
                {
                    for ( const uint32 next : listEdge[listQueue[head]] )
                    {
                        if ( next == to )
                            return true;
                        if ( listVisited[next] == SW_TRUE )
                            continue;
                        listVisited[next] = SW_TRUE;
                        listQueue.push_back( next );
                    }
                }
                return false;
            }

            /** @brief 목록에서 @p typeHash 의 공장 등록입니다. 없으면 nullptr 입니다. */
            static const EngineSubsystemRecord* findSubsystem( const EngineSubsystemRecord* pHead, uint64 typeHash )
            {
                for ( const EngineSubsystemRecord* pRecord = pHead; pRecord != nullptr; pRecord = pRecord->_pNext )
                {
                    if ( pRecord->_typeHash == typeHash )
                        return pRecord;
                }
                return nullptr;
            }
        };
    } // namespace

    EngineSubsystemSlotRegistration::EngineSubsystemSlotRegistration( EngineRegistrationList& list, const utf8* pTypeName, uint64 typeHash, SubsystemSlot& slot, bool ( *pfnBind )( SubsystemScope&, void* ),
                                                                      SubsystemRequirement requirement, EngineSubsystemVisibility visibility )
        : _pTypeName{ pTypeName }
        , _typeHash{ typeHash }
        , _pSlot{ &slot }
        , _pfnBind{ pfnBind }
        , _requirement{ requirement }
        , _visibility{ visibility }
        , _pNext{ list._pSlotHead }
    {
        list._pSlotHead = this;
    }

    EngineSubsystemRecord::EngineSubsystemRecord( EngineRegistrationList& list, const utf8* pTypeName, uint64 typeHash, EngineSubsystemStage stage, span<const uint64> listAfterHash,
                                                  const utf8* pAfterNames, EngineSubsystemFactory pfnFactory, MemoryTag memoryTag )
        : _pTypeName{ pTypeName }
        , _typeHash{ typeHash }
        , _stage{ stage }
        , _listAfterHash{ listAfterHash }
        , _pAfterNames{ pAfterNames }
        , _pfnFactory{ pfnFactory }
        , _memoryTag{ memoryTag }
        , _pNext{ list._pSubsystemHead }
    {
        list._pSubsystemHead = this;
    }

    EngineSubsystemFactoryScope::EngineSubsystemFactoryScope( SubsystemScope& scope, const EngineSubsystemRecord& record )
        : _scope{ scope }
        , _record{ record }
    {
    }

    bool EngineSubsystemFactoryScope::isDeclared( uint64 typeHash ) const
    {
        for ( const uint64 afterHash : _record._listAfterHash )
        {
            if ( afterHash == typeHash )
                return true;
        }
        return false;
    }

    void EngineSubsystemFactoryScope::requireDeclared( uint64 typeHash, const utf8* pLookup ) const
    {
        if ( isDeclared( typeHash ) )
            return;
        reportFatalError( "Engine subsystem factory '%#' looks up a type it does not list in its after list %# (%#) - add the type to the parentheses of its SW_ENGINE_SUBSYSTEM line",
                          _record._pTypeName, _record._pAfterNames, pLookup );
    }

    EngineRegistrationList::EngineRegistrationList()
        : _pSlotHead{ nullptr }
        , _pSubsystemHead{ nullptr }
        , _pBoundScope{ nullptr }
    {
    }

    EngineRegistrationList& EngineRegistrationList::getGlobal()
    {
        static EngineRegistrationList s_list;
        return s_list;
    }

    EngineSubsystemBindResult EngineRegistrationList::bindSubsystemSlots( const ISubsystemLocator& subsystems )
    {
        // 칸은 타입마다 프로세스에 하나다 — 두 번째 범위가 덮으면 앞 엔진의 조회가 소리 없이 다른 객체로 간다. 덮지 않고 알린다.
        if ( _pBoundScope != nullptr )
            return EngineSubsystemBindResult{ EngineSubsystemBindKind::AlreadyBound, nullptr };
        for ( const EngineSubsystemSlotRegistration* pRecord = _pSlotHead; pRecord != nullptr; pRecord = pRecord->_pNext )
        {
            pRecord->_pSlot->_pBound = EngineRegistrationListInternal::locate( &subsystems, pRecord->_typeHash );
            pRecord->_pSlot->_pOwner = EngineRegistrationListInternal::locateOwner( &subsystems, pRecord->_typeHash );
        }
        _pBoundScope               = &subsystems;
        const utf8* const pMissing = findMissingRequiredSubsystem();
        if ( pMissing != nullptr )
            return EngineSubsystemBindResult{ EngineSubsystemBindKind::MissingRequired, pMissing };
        return EngineSubsystemBindResult{ EngineSubsystemBindKind::Bound, nullptr };
    }

    void EngineRegistrationList::unbindSubsystemSlots( const ISubsystemLocator& subsystems )
    {
        if ( _pBoundScope != &subsystems )
            return; // 묶지 못한 범위(두 번째 엔진)는 앞 엔진의 칸을 비우지 않는다
        for ( const EngineSubsystemSlotRegistration* pRecord = _pSlotHead; pRecord != nullptr; pRecord = pRecord->_pNext )
        {
            pRecord->_pSlot->_pBound = nullptr;
            pRecord->_pSlot->_pOwner = nullptr;
        }
        _pBoundScope = nullptr;
    }

    bool EngineRegistrationList::isHostOnlySubsystem( uint64 typeHash ) const
    {
        for ( const EngineSubsystemSlotRegistration* pRecord = _pSlotHead; pRecord != nullptr; pRecord = pRecord->_pNext )
        {
            if ( pRecord->_typeHash == typeHash )
                return pRecord->_visibility == EngineSubsystemVisibility::HostOnly;
        }
        return false;
    }

    const utf8* EngineRegistrationList::findMissingRequiredSubsystem() const
    {
        const utf8* pMissing = nullptr;
        for ( const EngineSubsystemSlotRegistration* pRecord = _pSlotHead; pRecord != nullptr; pRecord = pRecord->_pNext )
        {
            const bool bMissing = pRecord->_requirement == SubsystemRequirement::Required && pRecord->_pSlot->_pBound == nullptr;
            if ( bMissing && ( pMissing == nullptr || EngineRegistrationListInternal::isNameBefore( pRecord->_pTypeName, pMissing ) ) )
                pMissing = pRecord->_pTypeName;
        }
        return pMissing;
    }

    void EngineRegistrationList::bindModuleSubsystems( SubsystemScope& outScope, bool bGameModuleOnly, bool bSkipAlreadyBound ) const
    {
        for ( const EngineSubsystemSlotRegistration* pRecord = _pSlotHead; pRecord != nullptr; pRecord = pRecord->_pNext )
        {
            void* const pService  = pRecord->_pSlot->_pBound;
            const bool  bVisible  = bGameModuleOnly == false || pRecord->_visibility == EngineSubsystemVisibility::GameVisible;
            const bool  bFresh    = bSkipAlreadyBound == false || outScope.findInScope( pRecord->_typeHash ) == nullptr;
            const bool  bBindable = pService != nullptr && bVisible;
            if ( bBindable && bFresh )
                SW_LOW_LEVEL_ASSERT( pRecord->_pfnBind( outScope, pService ), "Engine service %s is bound twice - each registration is a distinct type and the caller passes a fresh scope",
                                     pRecord->_pTypeName );
        }
    }

    expected<void> EngineRegistrationList::orderSubsystems( EngineSubsystemStage stage, vector<const EngineSubsystemRecord*>& outListOrder ) const
    {
        outListOrder.clear();
        vector<const EngineSubsystemRecord*> listRecord;
        for ( const EngineSubsystemRecord* pRecord = _pSubsystemHead; pRecord != nullptr; pRecord = pRecord->_pNext )
        {
            if ( pRecord->_stage == stage )
                listRecord.push_back( pRecord );
        }
        // 의존 해시를 이름으로 푼다 — 정렬 · 같은 이름 · 순환 판정은 서브시스템 목록과 같은 함수(`SubsystemCollection::computeGraph`)다.
        vector<vector<const utf8*>> listDependencyName( listRecord.size() );
        for ( size_t recordIndex = 0; recordIndex < listRecord.size(); ++recordIndex )
        {
            const EngineSubsystemRecord& record = *listRecord[recordIndex];
            for ( const uint64 afterHash : record._listAfterHash )
            {
                const EngineSubsystemRecord* const pAfter = EngineRegistrationListInternal::findSubsystem( _pSubsystemHead, afterHash );
                if ( pAfter == nullptr )
                    return unexpected( Error{ ErrorKind::NotFound, "an engine subsystem is built after a type that has no registration (SW_ENGINE_SUBSYSTEM)", record._pTypeName }
                                           .appendDetail( " after " )
                                           .appendDetail( record._pAfterNames ) );
                if ( pAfter->_stage != stage )
                    return unexpected( Error{ ErrorKind::InvalidArgument, "an engine subsystem is built after a type built in another stage - list only same-stage types", record._pTypeName }
                                           .appendDetail( " after " )
                                           .appendDetail( pAfter->_pTypeName ) );
                listDependencyName[recordIndex].push_back( pAfter->_pTypeName );
            }
        }
        vector<SubsystemNode> listNode;
        listNode.reserve( listRecord.size() );
        for ( size_t recordIndex = 0; recordIndex < listRecord.size(); ++recordIndex )
        {
            listNode.push_back( { listRecord[recordIndex]->_pTypeName, listDependencyName[recordIndex] } );
        }
        const expected<SubsystemGraph> graph = SubsystemCollection::computeGraph( listNode );
        if ( graph.has_value() == false )
            return unexpected( graph.error() ); // 정렬 · 같은 이름 · 순환의 문장은 서브시스템 목록과 같다
        outListOrder.reserve( graph->_listOrder.size() );
        for ( const uint32 nodeIndex : graph->_listOrder )
        {
            outListOrder.push_back( listRecord[nodeIndex] );
        }
        return {};
    }

    expected<void> EngineRegistrationList::findLifetimeMismatch( const vector<const EngineSubsystemRecord*>& listOrder, const vector<vector<uint32>>& listBuilt, const SubsystemCollection& collection )
    {
        // 수명 의존 그래프 — 이름으로 잇는다(서브시스템 목록의 정렬과 같은 규칙: 내 의존 칸, 그리고 상대가 나를 적은 의존자 칸).
        const uint32           subsystemCount = collection.getSubsystemCount();
        vector<vector<uint32>> listEdge( subsystemCount );
        for ( uint32 index = 0; index < subsystemCount; ++index )
        {
            const Subsystem& subsystem = collection.getSubsystem( index );
            for ( uint32 other = 0; other < subsystemCount; ++other )
            {
                const Subsystem& candidate = collection.getSubsystem( other );
                for ( const utf8* const pName : subsystem.getSubsystemDependencies() )
                {
                    if ( string_view{ pName } == candidate.getSubsystemName() )
                        listEdge[index].push_back( other );
                }
                for ( const utf8* const pName : candidate.getSubsystemDependents() )
                {
                    if ( string_view{ pName } == subsystem.getSubsystemName() )
                        listEdge[index].push_back( other );
                }
            }
        }
        for ( size_t recordIndex = 0; recordIndex < listOrder.size(); ++recordIndex )
        {
            const EngineSubsystemRecord& record = *listOrder[recordIndex];
            for ( const uint64 afterHash : record._listAfterHash )
            {
                size_t afterIndex = listOrder.size();
                for ( size_t candidate = 0; candidate < recordIndex; ++candidate )
                {
                    if ( listOrder[candidate]->_typeHash == afterHash )
                        afterIndex = candidate;
                }
                if ( afterIndex == listOrder.size() )
                    continue; // 순서 정렬이 이미 거절했다(모르는 타입 · 다른 단계)
                for ( const uint32 builtIndex : listBuilt[recordIndex] )
                {
                    for ( const uint32 requiredIndex : listBuilt[afterIndex] )
                    {
                        if ( EngineRegistrationListInternal::isReachable( listEdge, builtIndex, requiredIndex ) )
                            continue;
                        return unexpected( Error{ ErrorKind::InvalidArgument,
                                                  "an engine subsystem is built from another but does not depend on it - list it in getSubsystemDependencies so it starts first and shuts down last",
                                                  collection.getSubsystem( builtIndex ).getSubsystemName() }
                                               .appendDetail( " from " )
                                               .appendDetail( collection.getSubsystem( requiredIndex ).getSubsystemName() )
                                               .appendDetail( " (after list of " )
                                               .appendDetail( record._pTypeName )
                                               .appendDetail( ")" ) );
                    }
                }
            }
        }
        return {};
    }

    void EngineRegistrationList::buildSubsystems( EngineSubsystemStage stage, SubsystemScope& scope, EngineSubsystemBuild& build ) const
    {
        vector<const EngineSubsystemRecord*> listOrder;
        if ( const expected<void> ordered = orderSubsystems( stage, listOrder ); ordered.has_value() == false )
            reportFatalError( "Engine subsystem build order: %# (%#)", ordered.error().message(), ordered.error().detail() );
        // 공장마다 그 공장이 더한 서브시스템 번호 — 수명 의존 대조(`findLifetimeMismatch`)가 쓴다.
        vector<vector<uint32>> listBuilt( listOrder.size() );
        for ( size_t recordIndex = 0; recordIndex < listOrder.size(); ++recordIndex )
        {
            const EngineSubsystemRecord& record     = *listOrder[recordIndex];
            const uint32                 countStart = scope.getCollection().getSubsystemCount();
            EngineSubsystemFactoryScope  factoryScope{ scope, record };
            {
                const ScopedMemoryTag factoryMemoryTag{ record._memoryTag }; // 공장 본문(형식 밖 객체 · 노출 칸)도 그 서브시스템의 용도로 센다 — 공장이 태그를 적지 않는다
                record._pfnFactory( factoryScope, build );
            }
            for ( uint32 index = countStart; index < scope.getCollection().getSubsystemCount(); ++index )
            {
                listBuilt[recordIndex].push_back( index );
            }
        }
        if ( const expected<void> lifetime = findLifetimeMismatch( listOrder, listBuilt, scope.getCollection() ); lifetime.has_value() == false )
            reportFatalError( "Engine subsystem lifetime: %# (%#)", lifetime.error().message(), lifetime.error().detail() );
    }
} // namespace sw::engine
