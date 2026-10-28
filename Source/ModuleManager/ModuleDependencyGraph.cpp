#include "pch.h"

#include "ModuleManager/ModuleDependencyGraph.h"

#include "Core/Common/StdHeaders.h"
#include "Core/Container/VectorUtil.h"
#include "Core/Container/unordered_set.h"

namespace sw
{
    SW_LOG_CALLER( "LiveReloadManager" );

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
        unordered_set<string> uniqueSubgraph;
        uniqueSubgraph.reserve( listModuleName.size() );
        for ( const string& name : listModuleName )
        {
            if ( _mapDependsOn.find( name ) != _mapDependsOn.end() )
                uniqueSubgraph.insert( name );
        }
        if ( uniqueSubgraph.empty() )
            return true;

        unordered_map<string, uint32> mapIndegree;
        for ( const string& name : uniqueSubgraph )
        {
            mapIndegree[name] = 0;
        }

        for ( const string& name : uniqueSubgraph )
        {
            const auto found = _mapDependsOn.find( name );
            for ( const string& dep : found->second )
            {
                if ( uniqueSubgraph.find( dep ) != uniqueSubgraph.end() )
                    mapIndegree[name] += 1;
            }
        }

        vector<string> listReady;
        for ( const string& name : uniqueSubgraph )
        {
            if ( mapIndegree[name] == 0 )
                listReady.push_back( name );
        }
        std::sort( listReady.begin(), listReady.end() );

        vector<string> listOrder;
        listOrder.reserve( uniqueSubgraph.size() );
        size_t cursor{ 0 };
        while ( cursor < listReady.size() )
        {
            const size_t levelBegin = cursor;
            const size_t levelEnd   = listReady.size();
            for ( size_t levelIndex = levelBegin; levelIndex < levelEnd; ++levelIndex )
            {
                const string readyModuleName = listReady[levelIndex];
                listOrder.push_back( readyModuleName );
                for ( const string& name : uniqueSubgraph )
                {
                    const auto found = _mapDependsOn.find( name );
                    for ( const string& dep : found->second )
                    {
                        if ( dep != readyModuleName )
                            continue;

                        uint32& deg = mapIndegree[name];
                        if ( deg == 0 )
                            continue;
                        deg -= 1;
                        if ( deg == 0 )
                            listReady.push_back( name );
                    }
                }
            }
            cursor = levelEnd;
            if ( listReady.size() > levelEnd )
                std::sort( listReady.begin() + static_cast<std::ptrdiff_t>( levelEnd ), listReady.end() );
        }

        if ( listOrder.size() != uniqueSubgraph.size() )
        {
            string cycleModules;
            for ( const string& name : uniqueSubgraph )
            {
                if ( VectorUtil::contains( listOrder, name ) == false )
                {
                    if ( cycleModules.empty() == false )
                        cycleModules += ", ";
                    cycleModules += name;
                }
            }
            SW_LOG_ERROR( "Dependency cycle in module graph (sorted %# / %#). Cyclic modules: %s",
                          static_cast<uint32>( listOrder.size() ), static_cast<uint32>( uniqueSubgraph.size() ), cycleModules.c_str() );
            return false;
        }

        outListOrdered = std::move( listOrder );
        return true;
    }
} // namespace sw
