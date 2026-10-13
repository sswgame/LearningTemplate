#include "pch.h"

#include "Engine/Common/SubsystemCollection.h"

#include "Core/Common/Defines.h"
#include "Core/Container/TopologicalSortUtil.h"
#include "Core/String/StringBuilder.h"

namespace sw
{
    namespace
    {
        struct SubsystemCollectionInternal
        {
            static constexpr uint32 kNotFound = invalid_index::kUint32;

            /** @brief 의존 글의 구분자입니다 — 기동 표는 `{ A, B }`, 서브시스템 · 테스트는 공백 · 쉼표로 이은 이름을 넘긴다. */
            static constexpr bool isDependencySeparator( utf8 ch ) { return ch == ' ' || ch == ',' || ch == '{' || ch == '}'; }

            static uint32 findNodeIndex( const vector<EngineInitNode>& listNode, string_view name )
            {
                for ( uint32 nodeIndex = 0; nodeIndex < static_cast<uint32>( listNode.size() ); ++nodeIndex )
                {
                    if ( string_view{ listNode[nodeIndex]._pName } == name )
                        return nodeIndex;
                }
                return kNotFound;
            }

            /** @brief 의존 이름(쉼표 · 중괄호 · 공백으로 구분)을 노드 자리로 풉니다. 모르는 이름이면 false 입니다. */
            static bool resolveDependencies( const vector<EngineInitNode>& listNode, uint32 nodeIndex, vector<uint32>& outListDependency, string& outError )
            {
                const utf8* pText = listNode[nodeIndex]._pDependencyText;
                if ( pText == nullptr )
                    return true;
                const string_view text{ pText };
                size_t            cursor{ 0 };
                while ( cursor < text.size() )
                {
                    while ( cursor < text.size() && isDependencySeparator( text[cursor] ) )
                    {
                        ++cursor;
                    }
                    size_t tokenEnd = cursor;
                    while ( tokenEnd < text.size() && isDependencySeparator( text[tokenEnd] ) == false )
                    {
                        ++tokenEnd;
                    }
                    if ( tokenEnd > cursor )
                    {
                        const string_view dependencyName  = text.substr( cursor, tokenEnd - cursor );
                        const uint32      dependencyIndex = findNodeIndex( listNode, dependencyName );
                        if ( dependencyIndex == kNotFound )
                        {
                            StringBuilder<constant::kMaxBuffer256> sb;
                            sb.append( "Startup step '" ).append( listNode[nodeIndex]._pName ).append( "' depends on unknown step '" ).append( dependencyName ).append( "'" );
                            outError = string( sb.c_str() );
                            return false;
                        }
                        outListDependency.push_back( dependencyIndex );
                    }
                    cursor = tokenEnd;
                }
                return true;
            }

            /** @brief @p selfTarget 의 대상마다 @p dependencyTarget 도 도는가 — 서버에서 도는 것이 클라이언트 전용에 의존하면 false 입니다. */
            static bool isTargetCovered( EngineInitTarget selfTarget, EngineInitTarget dependencyTarget )
            {
                return ( static_cast<uint8>( selfTarget ) & static_cast<uint8>( ~static_cast<uint8>( dependencyTarget ) ) ) == 0;
            }

            static bool isTargetIncluded( EngineInitTarget subsystemTarget, EngineInitTarget hostTarget )
            {
                return ( static_cast<uint8>( subsystemTarget ) & static_cast<uint8>( hostTarget ) ) != 0;
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "EngineInit" );

    SubsystemCollection::SubsystemCollection()
        : _listSubsystem{}
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

    void SubsystemCollection::addInternal( unique_ptr<ISubsystem> subsystem )
    {
        _listSubsystem.push_back( std::move( subsystem ) );
        _bSorted = false;
    }

    bool SubsystemCollection::sort()
    {
        _bSorted = true;
        _error.clear();
        _listOrder.clear();
        _listDependency.clear();

        vector<EngineInitNode> listNode;
        listNode.reserve( _listSubsystem.size() );
        for ( const unique_ptr<ISubsystem>& subsystem : _listSubsystem )
        {
            listNode.push_back( { subsystem->getSubsystemName(), subsystem->getSubsystemDependencies() } );
        }
        EngineInitGraph graph{};
        if ( computeGraph( listNode, graph, _error ) == false )
            return false;

        // 서브시스템이 도는 대상마다 그 의존도 돌아야 한다(서버에서 도는 것이 클라이언트 전용에 기대면 서버 기동이 빈 자리를 본다).
        for ( uint32 index = 0; index < static_cast<uint32>( _listSubsystem.size() ); ++index )
        {
            const EngineInitTarget selfTarget = _listSubsystem[index]->getSubsystemTarget();
            for ( const uint32 dependencyIndex : graph._listDependency[index] )
            {
                if ( SubsystemCollectionInternal::isTargetCovered( selfTarget, _listSubsystem[dependencyIndex]->getSubsystemTarget() ) )
                    continue;
                StringBuilder<constant::kMaxBuffer256> sb;
                sb.append( "Startup step '" ).append( listNode[index]._pName ).append( "' runs on a target where its dependency '" ).append( listNode[dependencyIndex]._pName ).append( "' does not" );
                _error = string( sb.c_str() );
                return false;
            }
        }
        _listOrder      = std::move( graph._listOrder );
        _listDependency = std::move( graph._listDependency );
        return true;
    }

    EngineInitResult SubsystemCollection::initializeAt( uint32 index )
    {
        ISubsystem&           subsystem = *_listSubsystem[index];
        const ScopedMemoryTag subsystemMemoryTag{ subsystem.getSubsystemMemoryTag() };
        return subsystem.initializeSubsystem();
    }

    bool SubsystemCollection::initializeAll( EngineInitTarget target )
    {
        _listInitialized.clear();
        _listStopped.clear();
        if ( _bSorted == false )
            (void)sort(); // 오류는 바로 아래에서 알린다
        if ( _error.empty() == false )
        {
            SW_LOG_ERROR( "Invalid startup step table: %#", _error.c_str() );
            return false;
        }

        // 건너뛴 것(그리고 `SkipDependents` 를 돌려준 것)을 표시한다. 위상 순서로 돌므로 의존을 먼저 본다.
        vector<uint8> listBlocked( _listSubsystem.size(), SW_FALSE );
        for ( const uint32 index : _listOrder )
        {
            // 이 호스트의 대상이 아닌 것은 돌리지 않는다. 그에 의존하는 것도 같은 대상뿐이다(정렬의 대상 검사).
            if ( SubsystemCollectionInternal::isTargetIncluded( _listSubsystem[index]->getSubsystemTarget(), target ) == false )
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

            const EngineInitResult result = initializeAt( index );
            if ( result == EngineInitResult::Failed )
            {
                SW_LOG_ERROR( "Startup step '%#' failed", _listSubsystem[index]->getSubsystemName() );
                return false;
            }
            _listInitialized.push_back( index );
            if ( result == EngineInitResult::SkipDependents )
                listBlocked[index] = SW_TRUE;
        }
        return true;
    }

    void SubsystemCollection::shutdownAll()
    {
        while ( _listInitialized.empty() == false )
        {
            const uint32 index = _listInitialized.back();
            _listInitialized.pop_back();
            _listSubsystem[index]->shutdownSubsystem();
        }
    }

    void SubsystemCollection::destroyAll()
    {
        // 소멸은 늘 종료 뒤다. 종료하지 않은 것이 남았으면 여기서 먼저 내린다.
        shutdownAll();
        _listStopped.clear();
        // 초기화 순서의 역순으로 모두 지운다. 정렬이 안 됐으면(오류) 추가의 역순으로라도 지운다 — 소유한 객체는 기동이 어디서 멈췄든 놓아야 한다.
        const bool bOrderComputed = _bSorted && _listOrder.size() == _listSubsystem.size();
        for ( size_t order = _listSubsystem.size(); order > 0; --order )
        {
            const uint32 index = bOrderComputed ? _listOrder[order - 1] : static_cast<uint32>( order - 1 );
            _listSubsystem[index].reset();
        }
        _listSubsystem.clear();
        _listOrder.clear();
        _listDependency.clear();
        _bSorted = false;
    }

    void SubsystemCollection::shutdownDependentsOf( const ISubsystem& subsystem )
    {
        // 위상 순서로 훑으며 의존을 따라 표시한다 — 의존이 늘 먼저 오므로 한 번에 닫힌다(간접 의존 포함).
        vector<uint8> listDependent( _listSubsystem.size(), SW_FALSE );
        for ( const uint32 index : _listOrder )
        {
            for ( const uint32 dependencyIndex : _listDependency[index] )
            {
                if ( _listSubsystem[dependencyIndex].get() == &subsystem || listDependent[dependencyIndex] == SW_TRUE )
                    listDependent[index] = SW_TRUE;
            }
        }

        // 초기화한 순서의 역순으로 내리고, 내린 것은 초기화 순서로 기억한다.
        vector<uint32> listStopped;
        for ( size_t order = _listInitialized.size(); order > 0; --order )
        {
            const uint32 index = _listInitialized[order - 1];
            if ( listDependent[index] == SW_FALSE )
                continue;
            _listSubsystem[index]->shutdownSubsystem();
            listStopped.insert( listStopped.begin(), index );
            _listInitialized.erase( _listInitialized.begin() + static_cast<ptrdiff_t>( order - 1 ) );
        }
        _listStopped = std::move( listStopped );
    }

    bool SubsystemCollection::restartStopped()
    {
        vector<uint32> listStopped = std::move( _listStopped );
        _listStopped.clear();
        bool bRestarted = true;
        for ( const uint32 index : listStopped )
        {
            if ( initializeAt( index ) == EngineInitResult::Failed )
            {
                SW_LOG_ERROR( "Startup step '%#' failed to restart", _listSubsystem[index]->getSubsystemName() );
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

    bool SubsystemCollection::computeGraph( const vector<EngineInitNode>& listNode, EngineInitGraph& outGraph, string& outError )
    {
        const uint32 nodeCount = static_cast<uint32>( listNode.size() );
        outGraph._listOrder.clear();
        outGraph._listDependency.clear();
        outGraph._listDependency.resize( nodeCount );

        for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
        {
            if ( SubsystemCollectionInternal::findNodeIndex( listNode, listNode[nodeIndex]._pName ) != nodeIndex )
            {
                StringBuilder<constant::kMaxBuffer256> sb;
                sb.append( "Startup step '" ).append( listNode[nodeIndex]._pName ).append( "' is declared twice" );
                outError = string( sb.c_str() );
                return false;
            }
            if ( SubsystemCollectionInternal::resolveDependencies( listNode, nodeIndex, outGraph._listDependency[nodeIndex], outError ) == false )
                return false;
        }

        // 남은 의존이 0 인 노드 중 **이름이 가장 앞인** 것을 고른다(`TopologicalSortUtil` — 모듈 적재 순서와 같은 규칙). 목록 순서를 보지 않으므로
        // 순서는 의존만이 정한다 — 추가한 순서 · 표의 줄 위치는 뜻이 없다.
        vector<string_view> listName;
        listName.reserve( nodeCount );
        for ( const EngineInitNode& node : listNode )
        {
            listName.push_back( node._pName );
        }
        vector<uint32> listUnsorted;
        if ( TopologicalSortUtil::sortByDependency( listName, outGraph._listDependency, outGraph._listOrder, listUnsorted ) == false )
        {
            // 남은 노드는 모두 서로를 기다린다 — 순환(이나 순환에 매달린 노드)이다.
            StringBuilder<constant::kMaxBuffer512> sb;
            sb.append( "Startup steps in or behind a dependency cycle:" );
            for ( const uint32 nodeIndex : listUnsorted )
            {
                sb.append( ' ' ).append( listNode[nodeIndex]._pName );
            }
            outError = string( sb.c_str() );
            return false;
        }
        return true;
    }
} // namespace sw
