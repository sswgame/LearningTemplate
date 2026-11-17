#include "pch.h"

#include "Core/Subsystem/SubsystemCollection.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/DependencyGraph.h"

namespace sw
{
    namespace
    {
        /** @brief 이 스레드에서 초기화 중인 서브시스템 하나입니다(`SubsystemCollection::initializeAt` 의 스택 객체 — 안쪽 목록의 기동이 바깥 것을 가린다). */
        struct SubsystemInitializeFrame
        {
            SubsystemCollection*      _pCollection; ///< 초기화를 부른 목록
            uint32                    _index;       ///< 초기화 중인 자리
            const ISubsystem*         _pUndeclared; ///< 의존 선언 없이 얻은 첫 서브시스템(없으면 nullptr)
            SubsystemInitializeFrame* _pOuter;      ///< 바깥 프레임(없으면 nullptr)
        };

        /** @brief 이 스레드에서 초기화 중인 서브시스템(가장 안쪽)입니다. `initializeAt` 만 걸고 푼다. */
        thread_local SubsystemInitializeFrame* t_pInitializeFrame = nullptr;

        struct SubsystemCollectionInternal
        {
            /** @brief 이름의 노드 자리입니다. 없으면 비어 있습니다. */
            [[nodiscard]] static optional<uint32> findNodeIndex( const vector<SubsystemNode>& listNode, string_view name )
            {
                for ( uint32 nodeIndex = 0; nodeIndex < static_cast<uint32>( listNode.size() ); ++nodeIndex )
                {
                    if ( string_view{ listNode[nodeIndex]._pName } == name )
                        return nodeIndex;
                }
                return nullopt;
            }

            /** @brief 의존 이름을 노드 자리로 풉니다. 모르는 이름이면 `ErrorKind::NotFound`(대상 `A -> B`)입니다. */
            [[nodiscard]] static expected<void> resolveDependencies( const vector<SubsystemNode>& listNode, uint32 nodeIndex, vector<uint32>& outListDependency )
            {
                for ( const utf8* const pDependencyName : listNode[nodeIndex]._listDependency )
                {
                    const optional<uint32> dependencyIndex = findNodeIndex( listNode, pDependencyName );
                    if ( dependencyIndex.has_value() == false )
                    {
                        Error error{ ErrorKind::NotFound, "Subsystem depends on an unknown subsystem", listNode[nodeIndex]._pName };
                        error.appendDetail( " -> " ).appendDetail( pDependencyName );
                        return unexpected( error );
                    }
                    outListDependency.push_back( *dependencyIndex );
                }
                return {};
            }

            /** @brief @p selfHost 의 호스트마다 @p dependencyHost 도 도는가 — 어느 호스트에서 도는 것이 그 호스트에 없는 것에 의존하면 false 입니다. */
            static bool isHostCovered( SubsystemHostMask selfHost, SubsystemHostMask dependencyHost )
            {
                return ( static_cast<uint8>( selfHost ) & static_cast<uint8>( ~static_cast<uint8>( dependencyHost ) ) ) == 0;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SubsystemCollection::SubsystemCollection()
        : _listEntry{}
        , _listOrder{}
        , _listDependency{}
        , _listInitialized{}
        , _listStopped{}
        , _listInitTiming{}
        , _listShutdownTiming{}
        , _error{}
        , _bSorted{ SW_FALSE }
        , _pClockNanos{ nullptr }
    {
    }

    SubsystemCollection::~SubsystemCollection()
    {
        destroyAll();
    }

    void SubsystemCollection::addInternal( const Entry& entry )
    {
        _listEntry.push_back( entry );
        _bSorted = SW_FALSE;
    }

    bool SubsystemCollection::sort()
    {
        _bSorted = SW_TRUE;
        _error.clear();
        _listOrder.clear();
        _listDependency.clear();

        vector<SubsystemNode> listNode;
        listNode.reserve( _listEntry.size() );
        for ( const Entry& entry : _listEntry )
        {
            listNode.push_back( { entry._pSubsystem->getSubsystemName(), entry._pSubsystem->getSubsystemDependencies(), entry._pSubsystem->getSubsystemDependents() } );
        }
        expected<SubsystemGraph> result = computeGraph( listNode );
        if ( result.has_value() == false )
        {
            _error = string( result.error().message() ) + ": " + string( result.error().detail() );
            return false;
        }
        SubsystemGraph graph = std::move( result ).value();

        // 서브시스템이 도는 호스트마다 그 의존도 돌아야 한다(서버에서 도는 것이 클라이언트 전용에 기대면 서버 기동이 빈 자리를 본다).
        for ( uint32 index = 0; index < static_cast<uint32>( _listEntry.size() ); ++index )
        {
            const SubsystemHostMask selfHost = _listEntry[index]._pSubsystem->getSubsystemHost();
            for ( const uint32 dependencyIndex : graph._listDependency[index] )
            {
                if ( SubsystemCollectionInternal::isHostCovered( selfHost, _listEntry[dependencyIndex]._pSubsystem->getSubsystemHost() ) )
                    continue;
                _error = string( "Subsystem '" ) + listNode[index]._pName + "' runs on a host where its dependency '" + listNode[dependencyIndex]._pName + "' does not";
                return false;
            }
        }
        _listOrder      = std::move( graph._listOrder );
        _listDependency = std::move( graph._listDependency );
        return true;
    }

    SubsystemInitResult SubsystemCollection::initializeAt( uint32 index )
    {
        ISubsystem&              subsystem = *_listEntry[index]._pSubsystem;
        const ScopedMemoryTag    subsystemMemoryTag{ subsystem.getSubsystemMemoryTag() };
        SubsystemInitializeFrame frame{ this, index, nullptr, t_pInitializeFrame };
        t_pInitializeFrame               = &frame;
        const SubsystemInitResult result = subsystem.initializeSubsystem();
        t_pInitializeFrame               = frame._pOuter;
        if ( frame._pUndeclared == nullptr )
            return result;
        // 의존 선언 없이 같은 목록의 서브시스템을 얻었다 — 선 것은 내리고 기동을 멈춘다(부른 쪽이 앞에 "failed to initialize" 를 붙인다).
        if ( result != SubsystemInitResult::Failed )
        {
            subsystem.stopSubsystem();
            subsystem.shutdownSubsystem();
        }
        _error = string( "looked up '" ) + frame._pUndeclared->getSubsystemName() + "' without declaring it as a dependency";
        return SubsystemInitResult::Failed;
    }

    bool SubsystemCollection::isDependencyOf( uint32 index, uint32 dependencyIndex ) const
    {
        vector<uint8>  listVisited( _listEntry.size(), SW_FALSE );
        vector<uint32> listPending{ index };
        while ( listPending.empty() == false )
        {
            const uint32 current = listPending.back();
            listPending.pop_back();
            for ( const uint32 next : _listDependency[current] )
            {
                if ( next == dependencyIndex )
                    return true;
                if ( listVisited[next] == SW_TRUE )
                    continue;
                listVisited[next] = SW_TRUE;
                listPending.push_back( next );
            }
        }
        return false;
    }

    SubsystemCollection::HostedTaskScope::HostedTaskScope()
        : _pSaved{ t_pInitializeFrame }
    {
        t_pInitializeFrame = nullptr;
    }

    SubsystemCollection::HostedTaskScope::~HostedTaskScope()
    {
        t_pInitializeFrame = static_cast<SubsystemInitializeFrame*>( _pSaved );
    }

    void SubsystemCollection::recordInitializeLookup( const ISubsystem& target )
    {
        SubsystemInitializeFrame* const pFrame = t_pInitializeFrame;
        if ( pFrame == nullptr || pFrame->_pUndeclared != nullptr )
            return;
        const SubsystemCollection& collection = *pFrame->_pCollection;
        for ( uint32 targetIndex = 0; targetIndex < static_cast<uint32>( collection._listEntry.size() ); ++targetIndex )
        {
            if ( collection._listEntry[targetIndex]._pSubsystem != &target )
                continue;
            if ( targetIndex != pFrame->_index && collection.isDependencyOf( pFrame->_index, targetIndex ) == false )
                pFrame->_pUndeclared = &target;
            return;
        }
    }

    bool SubsystemCollection::initializeAll( SubsystemHostMask host )
    {
        _listInitialized.clear();
        _listStopped.clear();
        _listInitTiming.clear();
        if ( _bSorted == false )
            (void)sort(); // 오류는 바로 아래에서 돌려준다
        if ( _error.empty() == false )
            return false;

        // 건너뛴 것(그리고 `SkipDependents` 를 돌려준 것)을 표시한다. 위상 순서로 돌므로 의존을 먼저 본다.
        vector<uint8> listBlocked( _listEntry.size(), SW_FALSE );
        for ( const uint32 index : _listOrder )
        {
            // 이 호스트의 것이 아니면 돌리지 않는다. 그에 의존하는 것도 같은 호스트뿐이다(정렬의 호스트 검사).
            if ( overlapsHostMask( _listEntry[index]._pSubsystem->getSubsystemHost(), host ) == false )
            {
                listBlocked[index] = SW_TRUE;
                continue;
            }
            bool bBlocked = false;
            for ( const uint32 dependencyIndex : _listDependency[index] )
            {
                if ( listBlocked[dependencyIndex] == SW_TRUE )
                    bBlocked = true;
            }
            if ( bBlocked )
            {
                listBlocked[index] = SW_TRUE;
                continue;
            }

            const int64               startNanos = _pClockNanos != nullptr ? _pClockNanos() : 0;
            const SubsystemInitResult result     = initializeAt( index );
            if ( _pClockNanos != nullptr )
                _listInitTiming.push_back( { _listEntry[index]._pSubsystem->getSubsystemName(), ( _pClockNanos() - startNanos ) / 1000 } );
            if ( result == SubsystemInitResult::Failed )
            {
                _error = string( "Subsystem '" ) + _listEntry[index]._pSubsystem->getSubsystemName() + "' failed to initialize" + ( _error.empty() ? string{} : string( ": " ) + _error );
                return false;
            }
            _listInitialized.push_back( index );
            if ( result == SubsystemInitResult::SkipDependents )
                listBlocked[index] = SW_TRUE;
        }
        return true;
    }

    void SubsystemCollection::shutdownAll()
    {
        // 모두를 멈춘 뒤에 놓는다 — 한 서브시스템이 비우는 남은 일(완료 콜백)이 먼저 놓인 다른 서브시스템을 부르지 않게.
        // 시계가 없는 범위(코어)는 재지 않고 목록도 잡지 않는다 — 코어 범위의 소멸이 종료 누수 보고를 하므로 그때 산 목록이 보고에 찍힌다.
        _listShutdownTiming.clear();
        if ( _pClockNanos != nullptr )
            _listShutdownTiming.resize( _listInitialized.size() );
        for ( size_t order = _listInitialized.size(); order > 0; --order )
        {
            ISubsystem* const pSubsystem = _listEntry[_listInitialized[order - 1]]._pSubsystem;
            const int64       startNanos = _pClockNanos != nullptr ? _pClockNanos() : 0;
            pSubsystem->stopSubsystem();
            if ( _pClockNanos != nullptr )
                _listShutdownTiming[order - 1] = { pSubsystem->getSubsystemName(), ( _pClockNanos() - startNanos ) / 1000 };
        }
        while ( _listInitialized.empty() == false )
        {
            const uint32 index = _listInitialized.back();
            _listInitialized.pop_back();
            const int64 startNanos = _pClockNanos != nullptr ? _pClockNanos() : 0;
            _listEntry[index]._pSubsystem->shutdownSubsystem();
            if ( _pClockNanos != nullptr )
                _listShutdownTiming[_listInitialized.size()]._micros += ( _pClockNanos() - startNanos ) / 1000;
        }
    }

    void SubsystemCollection::destroyAll()
    {
        // 소멸은 늘 종료 뒤다. 종료하지 않은 것이 남았으면 여기서 먼저 내린다.
        shutdownAll();
        _listStopped.clear();
        // 초기화 순서의 역순으로 모두 지운다. 정렬이 안 됐으면(오류) 추가의 역순으로라도 지운다 — 소유한 객체는 기동이 어디서 멈췄든 놓아야 한다.
        const bool bOrderComputed = _bSorted && _listOrder.size() == _listEntry.size();
        for ( size_t order = _listEntry.size(); order > 0; --order )
        {
            Entry& entry = _listEntry[bOrderComputed ? _listOrder[order - 1] : static_cast<uint32>( order - 1 )];
            entry._pDestroy( entry._pSubsystem );
            entry._pSubsystem = nullptr;
        }
        // 저장소까지 놓는다. 범위 객체는 호스트 멤버라 소멸이 엔진의 종료 누수 보고보다 늦을 수 있다 — 기동 뒤에 자란 목록(부품 · 다시 세운 순서)이
        // 그때까지 살아 있으면 보고에 "늘었다" 로 남는다.
        _listEntry.clear();
        _listEntry.shrink_to_fit();
        _listOrder.clear();
        _listOrder.shrink_to_fit();
        _listDependency.clear();
        _listDependency.shrink_to_fit();
        _listInitialized.shrink_to_fit(); // `shutdownAll` 이 비웠다
        _listStopped.shrink_to_fit();
        _listInitTiming.clear();
        _listInitTiming.shrink_to_fit();
        _listShutdownTiming.clear();
        _listShutdownTiming.shrink_to_fit();
        _bSorted = SW_FALSE;
    }

    void SubsystemCollection::shutdownDependents( const ISubsystem& subsystem )
    {
        // 위상 순서로 훑으며 의존을 따라 표시한다 — 의존이 늘 먼저 오므로 한 번에 닫힌다(간접 의존 포함).
        vector<uint8> listDependent( _listEntry.size(), SW_FALSE );
        for ( const uint32 index : _listOrder )
        {
            for ( const uint32 dependencyIndex : _listDependency[index] )
            {
                if ( _listEntry[dependencyIndex]._pSubsystem == &subsystem || listDependent[dependencyIndex] == SW_TRUE )
                    listDependent[index] = SW_TRUE;
            }
        }

        // 내릴 것을 초기화 순서로 나누고, `shutdownAll` 과 같이 역순으로 모두 멈춘 뒤 역순으로 모두 종료한다.
        vector<uint32> listStopped;
        vector<uint32> listRemaining;
        for ( const uint32 index : _listInitialized )
        {
            ( listDependent[index] == SW_TRUE ? listStopped : listRemaining ).push_back( index );
        }
        for ( size_t order = listStopped.size(); order > 0; --order )
        {
            _listEntry[listStopped[order - 1]]._pSubsystem->stopSubsystem();
        }
        for ( size_t order = listStopped.size(); order > 0; --order )
        {
            _listEntry[listStopped[order - 1]]._pSubsystem->shutdownSubsystem();
        }
        _listInitialized = std::move( listRemaining );
        _listStopped     = std::move( listStopped );
    }

    bool SubsystemCollection::restartStopped()
    {
        vector<uint32> listStopped = std::move( _listStopped );
        _listStopped.clear();
        bool bRestarted = true;
        for ( const uint32 index : listStopped )
        {
            if ( initializeAt( index ) == SubsystemInitResult::Failed )
            {
                _error     = string( "Subsystem '" ) + _listEntry[index]._pSubsystem->getSubsystemName() + "' failed to restart" + ( _error.empty() ? string{} : string( ": " ) + _error );
                bRestarted = false;
                break;
            }
            _listInitialized.push_back( index );
        }
        // 종료가 역순으로 돌도록 초기화한 것을 초기화 순서로 되돌린다(다시 세운 것은 끝에 붙었다).
        vector<uint32> listOrdered;
        listOrdered.reserve( _listInitialized.size() );
        for ( const uint32 orderIndex : _listOrder )
        {
            for ( const uint32 initializedIndex : _listInitialized )
            {
                if ( initializedIndex == orderIndex )
                {
                    listOrdered.push_back( orderIndex );
                    break;
                }
            }
        }
        _listInitialized = std::move( listOrdered );
        return bRestarted;
    }

    expected<SubsystemGraph> SubsystemCollection::computeGraph( const vector<SubsystemNode>& listNode )
    {
        const uint32   nodeCount = static_cast<uint32>( listNode.size() );
        SubsystemGraph subsystemGraph{};
        subsystemGraph._listDependency.resize( nodeCount );

        for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
        {
            if ( SubsystemCollectionInternal::findNodeIndex( listNode, listNode[nodeIndex]._pName ) != nodeIndex )
            {
                return unexpected( Error{ ErrorKind::AlreadyExists, "Subsystem is declared twice", listNode[nodeIndex]._pName } );
            }
            expected<void> resolved = SubsystemCollectionInternal::resolveDependencies( listNode, nodeIndex, subsystemGraph._listDependency[nodeIndex] );
            if ( resolved.has_value() == false )
                return unexpected( std::move( resolved ).error() );
        }
        // 역방향 의존: 노드가 적은 뒤에 설 이름은 그 이름의 의존이 된다. 이 목록에 없는 이름은 건너뛴다(그 호스트에 없는 서브시스템).
        for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
        {
            for ( const utf8* const pDependentName : listNode[nodeIndex]._listDependent )
            {
                const optional<uint32> dependentIndex = SubsystemCollectionInternal::findNodeIndex( listNode, pDependentName );
                if ( dependentIndex.has_value() == false )
                    continue;
                vector<uint32>& listDependency = subsystemGraph._listDependency[*dependentIndex];
                if ( VectorUtil::contains( listDependency, nodeIndex ) == false )
                    listDependency.push_back( nodeIndex );
            }
        }

        // 남은 의존이 0 인 노드 중 **이름이 가장 앞인** 것을 고른다(`DependencyGraph::sortByName` — 모듈 적재 순서와 같은 규칙). 목록 순서를 보지 않으므로
        // 순서는 의존만이 정한다 — 추가한 순서는 뜻이 없다.
        vector<string_view> listName;
        listName.reserve( nodeCount );
        DependencyGraph graph{ nodeCount };
        for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
        {
            listName.push_back( listNode[nodeIndex]._pName );
            for ( const uint32 dependency : subsystemGraph._listDependency[nodeIndex] )
            {
                graph.addDependency( nodeIndex, dependency );
            }
        }
        DependencyOrder order = graph.sortByName( listName );
        if ( order.isSorted() == false )
        {
            // 남은 노드는 모두 서로를 기다린다 — 순환(이나 순환에 매달린 노드)이다.
            Error error{ ErrorKind::Cycle, "Subsystems in or behind a dependency cycle" };
            for ( const uint32 nodeIndex : order._listUnsorted )
            {
                error.appendDetail( error.detail().empty() ? "" : " " ).appendDetail( listNode[nodeIndex]._pName );
            }
            return unexpected( error );
        }
        subsystemGraph._listOrder = std::move( order._listOrder );
        return subsystemGraph;
    }
} // namespace sw
