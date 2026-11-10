#include "pch.h"

#include "DevTools/ModuleReload/LiveReload/ModuleDependencyGraph.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/DependencyGraph.h"
#include "Core/Container/unordered_set.h"
#include "Core/Foundation/StdHeaders.h"

namespace sw
{
    ModuleDependencyGraph::ModuleDependencyGraph()
        : _mapDependsOn{}
    {
    }

    void ModuleDependencyGraph::ensureModule( string_view moduleName )
    {
        (void)_mapDependsOn[string( moduleName )];
    }

    void ModuleDependencyGraph::setDependsOn( string_view moduleName, const vector<string>& listDependsOn )
    {
        _mapDependsOn[string( moduleName )] = listDependsOn;
    }

    void ModuleDependencyGraph::removeModule( string_view moduleName )
    {
        _mapDependsOn.erase( string( moduleName ) );
    }

    void ModuleDependencyGraph::clear()
    {
        _mapDependsOn = unordered_map<string, vector<string>>{}; // `clear()` 는 용량을 남긴다 — 종료 때 비우므로 용량까지 놓는다
    }

    bool ModuleDependencyGraph::hasModule( string_view moduleName ) const
    {
        return _mapDependsOn.find( string( moduleName ) ) != _mapDependsOn.end();
    }

    const vector<string>& ModuleDependencyGraph::getDependsOn( string_view moduleName ) const
    {
        static const vector<string> s_listEmpty{};
        const auto                  found = _mapDependsOn.find( string( moduleName ) );
        return found != _mapDependsOn.end() ? found->second : s_listEmpty;
    }

    void ModuleDependencyGraph::getModuleNames( vector<string>& outListModuleName ) const
    {
        outListModuleName.reserve( outListModuleName.size() + _mapDependsOn.size() );
        for ( const auto& [moduleName, listDependsOn] : _mapDependsOn )
        {
            outListModuleName.push_back( moduleName );
        }
    }

    void ModuleDependencyGraph::collectDependentClosure( string_view root, vector<string>& outListUnique ) const
    {
        unordered_set<string> uniqueVisited;
        for ( const string& existing : outListUnique )
        {
            uniqueVisited.insert( existing );
        }

        vector<string> listStack;
        listStack.push_back( string( root ) );
        while ( listStack.empty() == false )
        {
            const string cur = listStack.back();
            listStack.pop_back();
            if ( uniqueVisited.insert( cur ).second == false )
                continue;
            if ( _mapDependsOn.find( cur ) == _mapDependsOn.end() )
                continue;
            outListUnique.push_back( cur );
            for ( const auto& [moduleName, listDependsOn] : _mapDependsOn )
            {
                for ( const string& dep : listDependsOn )
                {
                    if ( dep == cur )
                        listStack.push_back( moduleName );
                }
            }
        }
    }

    bool ModuleDependencyGraph::sortByDependency( const vector<string>& listModuleName, vector<string>& outListOrdered ) const
    {
        outListOrdered.clear();
        // 고른 이름 가운데 그래프에 든 것만 중복 없이 노드가 된다. 정렬은 이름으로 동점을 깨므로 이 목록의 차례는 결과에 뜻이 없다.
        vector<string_view> listSubgraph;
        listSubgraph.reserve( listModuleName.size() );
        for ( const string& name : listModuleName )
        {
            if ( _mapDependsOn.contains( name ) && VectorUtil::contains( listSubgraph, string_view{ name } ) == false )
                listSubgraph.push_back( name );
        }
        if ( listSubgraph.empty() )
            return true;

        DependencyGraph graph{ static_cast<uint32>( listSubgraph.size() ) };
        for ( uint32 nodeIndex = 0; nodeIndex < static_cast<uint32>( listSubgraph.size() ); ++nodeIndex )
        {
            for ( const string& dependency : _mapDependsOn.find( string{ listSubgraph[nodeIndex] } )->second )
            {
                const optional<size_t> dependencyIndex = VectorUtil::findIndex( listSubgraph, string_view{ dependency } );
                if ( dependencyIndex.has_value() )
                    graph.addDependency( nodeIndex, static_cast<uint32>( *dependencyIndex ) );
            }
        }

        const DependencyOrder order = graph.sortByName( listSubgraph );
        if ( order.isSorted() == false )
        {
            string cycleModules;
            for ( const uint32 nodeIndex : order._listCycle )
            {
                if ( cycleModules.empty() == false )
                    cycleModules += " -> ";
                cycleModules += listSubgraph[nodeIndex];
            }
            SW_LOG_ERROR_ID( "LiveReloadManager.DependencyCycleModuleGraph", "Dependency cycle in module graph (%# of %# modules cannot be ordered): %#",
                             static_cast<uint32>( order._listUnsorted.size() ), static_cast<uint32>( listSubgraph.size() ), cycleModules.c_str() );
            return false;
        }

        outListOrdered.reserve( order._listOrder.size() );
        for ( const uint32 nodeIndex : order._listOrder )
        {
            outListOrdered.push_back( string{ listSubgraph[nodeIndex] } );
        }
        return true;
    }
} // namespace sw
