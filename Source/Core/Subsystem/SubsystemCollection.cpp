#include "pch.h"

#include "Core/Subsystem/SubsystemCollection.h"

#include "Core/Common/Defines.h"
#include "Core/Container/TopologicalSortUtil.h"

namespace sw
{
    namespace
    {
        struct SubsystemCollectionInternal
        {
            static constexpr uint32 kNotFound = invalid_index::kUint32;

            static uint32 findNodeIndex( const vector<SubsystemNode>& listNode, string_view name )
            {
                for ( uint32 nodeIndex = 0; nodeIndex < static_cast<uint32>( listNode.size() ); ++nodeIndex )
                {
                    if ( string_view{ listNode[nodeIndex]._pName } == name )
                        return nodeIndex;
                }
                return kNotFound;
            }

            /** @brief 의존 이름을 노드 자리로 풉니다. 모르는 이름이면 false 입니다. */
            static bool resolveDependencies( const vector<SubsystemNode>& listNode, uint32 nodeIndex, vector<uint32>& outListDependency, string& outError )
            {
                for ( const utf8* const pDependencyName : listNode[nodeIndex]._listDependency )
                {
                    const uint32 dependencyIndex = findNodeIndex( listNode, pDependencyName );
                    if ( dependencyIndex == kNotFound )
                    {
                        outError = string( "Subsystem '" ) + listNode[nodeIndex]._pName + "' depends on unknown subsystem '" + pDependencyName + "'";
                        return false;
                    }
                    outListDependency.push_back( dependencyIndex );
                }
                return true;
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
        , _error{}
        , _bSorted{ false }
    {
    }

    SubsystemCollection::~SubsystemCollection()
    {
        destroyAll();
    }

    void SubsystemCollection::addInternal( const Entry& entry )
    {
        _listEntry.push_back( entry );
        _bSorted = false;
    }

    bool SubsystemCollection::sort()
    {
        _bSorted = true;
        _error.clear();
        _listOrder.clear();
        _listDependency.clear();

        vector<SubsystemNode> listNode;
        listNode.reserve( _listEntry.size() );
        for ( const Entry& entry : _listEntry )
        {
            listNode.push_back( { entry._pSubsystem->getSubsystemName(), entry._pSubsystem->getSubsystemDependencies() } );
        }
        SubsystemGraph graph{};
        if ( computeGraph( listNode, graph, _error ) == false )
            return false;

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
        ISubsystem&           subsystem = *_listEntry[index]._pSubsystem;
        const ScopedMemoryTag subsystemMemoryTag{ subsystem.getSubsystemMemoryTag() };
        return subsystem.initializeSubsystem();
    }

    bool SubsystemCollection::initializeAll( SubsystemHostMask host )
    {
        _listInitialized.clear();
        _listStopped.clear();
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

            const SubsystemInitResult result = initializeAt( index );
            if ( result == SubsystemInitResult::Failed )
            {
                _error = string( "Subsystem '" ) + _listEntry[index]._pSubsystem->getSubsystemName() + "' failed to initialize";
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
        for ( size_t order = _listInitialized.size(); order > 0; --order )
        {
            _listEntry[_listInitialized[order - 1]]._pSubsystem->stopSubsystem();
        }
        while ( _listInitialized.empty() == false )
        {
            const uint32 index = _listInitialized.back();
            _listInitialized.pop_back();
            _listEntry[index]._pSubsystem->shutdownSubsystem();
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
        _listEntry.clear();
        _listOrder.clear();
        _listDependency.clear();
        _bSorted = false;
    }

    void SubsystemCollection::shutdownDependentsOf( const ISubsystem& subsystem )
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
                _error     = string( "Subsystem '" ) + _listEntry[index]._pSubsystem->getSubsystemName() + "' failed to restart";
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

    bool SubsystemCollection::computeGraph( const vector<SubsystemNode>& listNode, SubsystemGraph& outGraph, string& outError )
    {
        const uint32 nodeCount = static_cast<uint32>( listNode.size() );
        outGraph._listOrder.clear();
        outGraph._listDependency.clear();
        outGraph._listDependency.resize( nodeCount );

        for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
        {
            if ( SubsystemCollectionInternal::findNodeIndex( listNode, listNode[nodeIndex]._pName ) != nodeIndex )
            {
                outError = string( "Subsystem '" ) + listNode[nodeIndex]._pName + "' is declared twice";
                return false;
            }
            if ( SubsystemCollectionInternal::resolveDependencies( listNode, nodeIndex, outGraph._listDependency[nodeIndex], outError ) == false )
                return false;
        }

        // 남은 의존이 0 인 노드 중 **이름이 가장 앞인** 것을 고른다(`TopologicalSortUtil` — 모듈 적재 순서와 같은 규칙). 목록 순서를 보지 않으므로
        // 순서는 의존만이 정한다 — 추가한 순서는 뜻이 없다.
        vector<string_view> listName;
        listName.reserve( nodeCount );
        for ( const SubsystemNode& node : listNode )
        {
            listName.push_back( node._pName );
        }
        vector<uint32> listUnsorted;
        if ( TopologicalSortUtil::sortByDependency( listName, outGraph._listDependency, outGraph._listOrder, listUnsorted ) == false )
        {
            // 남은 노드는 모두 서로를 기다린다 — 순환(이나 순환에 매달린 노드)이다.
            outError = "Subsystems in or behind a dependency cycle:";
            for ( const uint32 nodeIndex : listUnsorted )
            {
                outError += string( " " ) + listNode[nodeIndex]._pName;
            }
            return false;
        }
        return true;
    }
} // namespace sw
