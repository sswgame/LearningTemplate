#include "pch.h"

#include "Engine/EngineStartupSequence.h"

#include "Core/String/StringBuilder.h"

namespace sw
{
    namespace
    {
        struct EngineStartupSequenceInternal
        {
            /** @brief 표 그대로의 이름 · 의존 칸입니다(줄 순서 = `EngineStartupStep` 값). */
            static constexpr EngineStartupNode kArrStepNode[] = {
#define SW_ENGINE_STARTUP_STEP( Name, Dependencies ) { #Name, Dependencies },
#include "Engine/EngineStartupStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
            };
            static_assert( sizeof( kArrStepNode ) / sizeof( kArrStepNode[0] ) == static_cast<size_t>( EngineStartupStep::Count ),
                           "Startup node table must have one row per EngineStartupStep" );

            static constexpr uint32 kNotFound = 0xFFFFFFFFu;

            static uint32 findNodeIndex( const vector<EngineStartupNode>& listNode, string_view name )
            {
                for ( uint32 nodeIndex = 0; nodeIndex < static_cast<uint32>( listNode.size() ); ++nodeIndex )
                {
                    if ( string_view{ listNode[nodeIndex]._pName } == name )
                        return nodeIndex;
                }
                return kNotFound;
            }

            /** @brief 공백으로 구분한 의존 이름을 노드 자리로 풉니다. 모르는 이름이면 false 입니다. */
            static bool resolveDependencies( const vector<EngineStartupNode>& listNode, uint32 nodeIndex, vector<uint32>& outListDependency, string& outError )
            {
                const utf8* pText = listNode[nodeIndex]._pDependencyText;
                if ( pText == nullptr )
                    return true;
                const string_view text{ pText };
                size_t            cursor{ 0 };
                while ( cursor < text.size() )
                {
                    while ( cursor < text.size() && text[cursor] == ' ' )
                        ++cursor;
                    size_t tokenEnd = cursor;
                    while ( tokenEnd < text.size() && text[tokenEnd] != ' ' )
                        ++tokenEnd;
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
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "EngineStartup" );

    EngineStartupSequence::EngineStartupSequence()
        : _listOrder{}
        , _listDependency{}
        , _listInitialized{}
        , _shutdownStep{}
        , _error{}
    {
        EngineStartupGraph graph{};
        if ( computeGraph( makeStepNodes(), graph, _error ) == false )
            return;
        _listOrder.reserve( graph._listOrder.size() );
        for ( const uint32 nodeIndex : graph._listOrder )
            _listOrder.push_back( static_cast<EngineStartupStep>( nodeIndex ) );
        _listDependency = std::move( graph._listDependency );
    }

    bool EngineStartupSequence::initializeAll( const InitializeStepDelegate& initializeStep, const ShutdownStepDelegate& shutdownStep )
    {
        if ( _error.empty() == false )
        {
            SW_LOG_ERROR( "Invalid startup step table: %#", _error.c_str() );
            return false;
        }

        _shutdownStep = shutdownStep;
        _listInitialized.clear();
        // 건너뛴 단계(그리고 `SkipDependents` 를 돌려준 단계)를 표시한다. 위상 순서로 돌므로 의존을 먼저 본다.
        vector<uint8> listBlocked( static_cast<size_t>( EngineStartupStep::Count ), SW_FALSE );
        for ( const EngineStartupStep step : _listOrder )
        {
            const uint32 stepIndex = static_cast<uint32>( step );
            bool         bBlocked  = false;
            for ( const uint32 dependencyIndex : _listDependency[stepIndex] )
            {
                if ( listBlocked[dependencyIndex] == SW_TRUE )
                    bBlocked = true;
            }
            if ( bBlocked )
            {
                listBlocked[stepIndex] = SW_TRUE;
                continue;
            }

            const EngineStartupResult result = initializeStep.isBound() ? initializeStep( step ) : EngineStartupResult::Succeeded;
            if ( result == EngineStartupResult::Failed )
            {
                SW_LOG_ERROR( "Startup step '%#' failed", getStepName( step ) );
                return false;
            }
            _listInitialized.push_back( step );
            if ( result == EngineStartupResult::SkipDependents )
                listBlocked[stepIndex] = SW_TRUE;
        }
        return true;
    }

    void EngineStartupSequence::shutdownAll()
    {
        while ( _listInitialized.empty() == false )
        {
            const EngineStartupStep step = _listInitialized.back();
            _listInitialized.pop_back();
            if ( _shutdownStep.isBound() )
                _shutdownStep( step );
        }
    }

    const utf8* EngineStartupSequence::getStepName( EngineStartupStep step )
    {
        const uint32 stepIndex = static_cast<uint32>( step );
        if ( stepIndex >= static_cast<uint32>( EngineStartupStep::Count ) )
            return "Unknown";
        return EngineStartupSequenceInternal::kArrStepNode[stepIndex]._pName;
    }

    vector<EngineStartupNode> EngineStartupSequence::makeStepNodes()
    {
        vector<EngineStartupNode> listNode;
        listNode.reserve( static_cast<size_t>( EngineStartupStep::Count ) );
        for ( const EngineStartupNode& node : EngineStartupSequenceInternal::kArrStepNode )
            listNode.push_back( node );
        return listNode;
    }

    bool EngineStartupSequence::computeGraph( const vector<EngineStartupNode>& listNode, EngineStartupGraph& outGraph, string& outError )
    {
        const uint32 nodeCount = static_cast<uint32>( listNode.size() );
        outGraph._listOrder.clear();
        outGraph._listDependency.clear();
        outGraph._listDependency.resize( nodeCount );

        for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
        {
            if ( EngineStartupSequenceInternal::findNodeIndex( listNode, listNode[nodeIndex]._pName ) != nodeIndex )
            {
                StringBuilder<constant::kMaxBuffer256> sb;
                sb.append( "Startup step '" ).append( listNode[nodeIndex]._pName ).append( "' is declared twice" );
                outError = string( sb.c_str() );
                return false;
            }
            if ( EngineStartupSequenceInternal::resolveDependencies( listNode, nodeIndex, outGraph._listDependency[nodeIndex], outError ) == false )
                return false;
        }

        // Kahn: 남은 의존이 0 인 노드 중 목록 앞의 것을 고른다. 노드가 스무 개 남짓이라 매번 처음부터 훑는다.
        vector<uint32> listRemaining( nodeCount, 0 );
        for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
            listRemaining[nodeIndex] = static_cast<uint32>( outGraph._listDependency[nodeIndex].size() );
        vector<uint8> listEmitted( nodeCount, SW_FALSE );
        outGraph._listOrder.reserve( nodeCount );
        while ( outGraph._listOrder.size() < nodeCount )
        {
            uint32 readyIndex = EngineStartupSequenceInternal::kNotFound;
            for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
            {
                if ( listEmitted[nodeIndex] == SW_FALSE && listRemaining[nodeIndex] == 0 )
                {
                    readyIndex = nodeIndex;
                    break;
                }
            }
            if ( readyIndex == EngineStartupSequenceInternal::kNotFound )
            {
                // 남은 노드는 모두 서로를 기다린다 — 순환(이나 순환에 매달린 노드)이다.
                StringBuilder<constant::kMaxBuffer512> sb;
                sb.append( "Startup steps in or behind a dependency cycle:" );
                for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
                {
                    if ( listEmitted[nodeIndex] == SW_FALSE )
                        sb.append( ' ' ).append( listNode[nodeIndex]._pName );
                }
                outError = string( sb.c_str() );
                outGraph._listOrder.clear();
                return false;
            }

            listEmitted[readyIndex] = SW_TRUE;
            outGraph._listOrder.push_back( readyIndex );
            for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
            {
                for ( const uint32 dependencyIndex : outGraph._listDependency[nodeIndex] )
                {
                    if ( dependencyIndex == readyIndex )
                        --listRemaining[nodeIndex];
                }
            }
        }
        return true;
    }
} // namespace sw
