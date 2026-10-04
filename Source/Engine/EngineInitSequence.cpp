#include "pch.h"

#include "Engine/EngineInitSequence.h"

#include "Core/Common/TopologicalSortUtil.h"
#include "Core/Memory/MemoryProfiler.h"
#include "Core/String/StringBuilder.h"

namespace sw
{
    namespace
    {
        struct EngineInitSequenceInternal
        {
            /** @brief 표 그대로의 이름 · 의존 칸입니다(줄 순서 = `EngineInitStep` 값). 의존 칸은 `{ A, B }` 를 글로 든다. */
            static constexpr EngineInitNode kArrStepNode[] = {
#define SW_ENGINE_STARTUP_STEP( Name, Tag, ... ) { #Name, #__VA_ARGS__ },
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
            };
            static_assert( sizeof( kArrStepNode ) / sizeof( kArrStepNode[0] ) == static_cast<size_t>( EngineInitStep::Count ),
                           "Startup node table must have one row per EngineInitStep" );

            /** @brief 표의 메모리 태그 칸입니다(줄 순서 = `EngineInitStep` 값). 단계 초기화를 부르는 동안 건다. */
            static constexpr MemoryTag kArrStepMemoryTag[] = {
#define SW_ENGINE_STARTUP_STEP( Name, Tag, ... ) MemoryTag::Tag,
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
            };
            static_assert( sizeof( kArrStepMemoryTag ) / sizeof( kArrStepMemoryTag[0] ) == static_cast<size_t>( EngineInitStep::Count ),
                           "Startup memory tag table must have one row per EngineInitStep" );

            static constexpr uint32 kNotFound = 0xFFFFFFFFu;

            /** @brief 의존 글의 구분자입니다 — 표는 `{ A, B }`, 시험은 공백으로 이은 이름을 넘긴다. */
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
                        ++cursor;
                    size_t tokenEnd = cursor;
                    while ( tokenEnd < text.size() && isDependencySeparator( text[tokenEnd] ) == false )
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
    // 표의 의존이 모두 자기보다 위 줄에 있는지 컴파일 때 본다 — 표를 기동 순서로 읽을 수 있게(오타도 여기서 컴파일 오류).
    namespace EngineInitTableCheck
    {
        enum Index : uint32
        {
#define SW_ENGINE_STARTUP_STEP( Name, ... ) Name,
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
        };

        /** @brief @p listDependency 가 모두 @p self 보다 앞 줄이면 true 입니다. */
        constexpr bool areAllAbove( uint32 self, std::initializer_list<uint32> listDependency )
        {
            for ( const uint32 dependency : listDependency )
            {
                if ( dependency >= self )
                    return false;
            }
            return true;
        }

// `{ A, B }` 의 쉼표는 매크로 인자를 가르므로 가변 인자로 받아 다시 붙인다(`__VA_ARGS__` = `{ A, B }`).
#define SW_ENGINE_STARTUP_STEP( Name, Tag, ... )                                   \
    static_assert( areAllAbove( Name, std::initializer_list<uint32> __VA_ARGS__ ), \
                   "EngineInitStepList.xxx: step " #Name " must be listed below every step it depends on" );
#include "Engine/EngineInitStepList.xxx"
#undef SW_ENGINE_STARTUP_STEP
    } // namespace EngineInitTableCheck

    SW_LOG_CALLER( "EngineInit" );

    EngineInitSequence::EngineInitSequence()
        : _listOrder{}
        , _listDependency{}
        , _listInitialized{}
        , _listStopped{}
        , _pArrEntry{ nullptr }
        , _pHost{ nullptr }
        , _error{}
    {
        EngineInitGraph graph{};
        if ( computeGraph( makeStepNodes(), graph, _error ) == false )
            return;
        _listOrder.reserve( graph._listOrder.size() );
        for ( const uint32 nodeIndex : graph._listOrder )
            _listOrder.push_back( static_cast<EngineInitStep>( nodeIndex ) );
        _listDependency = std::move( graph._listDependency );
    }

    bool EngineInitSequence::initializeAllInternal( const EngineInitStepEntry* pArrEntry, void* pHost )
    {
        // 표가 틀렸어도 호스트는 기억한다 — 부트스트랩이 이미 만든 단계 객체를 `destroyAll` 이 해제해야 한다.
        _pArrEntry = pArrEntry;
        _pHost     = pHost;
        _listInitialized.clear();
        _listStopped.clear();
        if ( _error.empty() == false )
        {
            SW_LOG_ERROR( "Invalid startup step table: %#", _error.c_str() );
            return false;
        }

        // 건너뛴 단계(그리고 `SkipDependents` 를 돌려준 단계)를 표시한다. 위상 순서로 돌므로 의존을 먼저 본다.
        vector<uint8> listBlocked( static_cast<size_t>( EngineInitStep::Count ), SW_FALSE );
        for ( const EngineInitStep step : _listOrder )
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

            const ScopedMemoryTag  stepMemoryTag{ getStepMemoryTag( step ) };
            const EngineInitResult result = pArrEntry[stepIndex]._pInitialize( pHost );
            if ( result == EngineInitResult::Failed )
            {
                SW_LOG_ERROR( "Startup step '%#' failed", getStepName( step ) );
                return false;
            }
            _listInitialized.push_back( step );
            if ( result == EngineInitResult::SkipDependents )
                listBlocked[stepIndex] = SW_TRUE;
        }
        return true;
    }

    void EngineInitSequence::shutdownAll()
    {
        while ( _listInitialized.empty() == false )
        {
            const EngineInitStep step = _listInitialized.back();
            _listInitialized.pop_back();
            _pArrEntry[static_cast<uint32>( step )]._pShutdown( _pHost );
        }
    }

    void EngineInitSequence::destroyAll()
    {
        // 해제는 늘 종료 뒤다. 종료하지 않은 단계가 남았으면 여기서 먼저 내린다.
        shutdownAll();
        if ( _pArrEntry == nullptr )
            return;
        // 표의 줄 순서는 의존을 지키는 기동 순서이므로(`EngineInitTableCheck`) 줄의 역순이 곧 해제 순서다. 정렬 결과(`_listOrder`)를
        // 쓰지 않는 것은 표 오류로 정렬이 비었어도 부트스트랩이 만든 객체를 해제해야 하기 때문이다.
        for ( uint32 stepIndex = static_cast<uint32>( EngineInitStep::Count ); stepIndex > 0; --stepIndex )
            _pArrEntry[stepIndex - 1]._pDestroy( _pHost );
    }

    void EngineInitSequence::shutdownDependentsOf( EngineInitStep step )
    {
        // 위상 순서로 훑으며 의존을 따라 표시한다 — 의존이 늘 먼저 오므로 한 번에 닫힌다(간접 의존 포함).
        vector<uint8> listDependent( static_cast<size_t>( EngineInitStep::Count ), SW_FALSE );
        for ( const EngineInitStep orderStep : _listOrder )
        {
            const uint32 stepIndex = static_cast<uint32>( orderStep );
            for ( const uint32 dependencyIndex : _listDependency[stepIndex] )
            {
                if ( dependencyIndex == static_cast<uint32>( step ) || listDependent[dependencyIndex] == SW_TRUE )
                    listDependent[stepIndex] = SW_TRUE;
            }
        }

        // 초기화한 순서의 역순으로 내리고, 내린 것은 초기화 순서로 기억한다.
        vector<EngineInitStep> listStopped;
        for ( size_t order = _listInitialized.size(); order > 0; --order )
        {
            const EngineInitStep initializedStep = _listInitialized[order - 1];
            if ( listDependent[static_cast<uint32>( initializedStep )] == SW_FALSE )
                continue;
            _pArrEntry[static_cast<uint32>( initializedStep )]._pShutdown( _pHost );
            listStopped.insert( listStopped.begin(), initializedStep );
            _listInitialized.erase( _listInitialized.begin() + static_cast<ptrdiff_t>( order - 1 ) );
        }
        _listStopped = std::move( listStopped );
    }

    bool EngineInitSequence::restartStoppedSteps()
    {
        vector<EngineInitStep> listStopped = std::move( _listStopped );
        _listStopped.clear();
        bool bRestarted = true;
        for ( const EngineInitStep step : listStopped )
        {
            const ScopedMemoryTag stepMemoryTag{ getStepMemoryTag( step ) };
            if ( _pArrEntry[static_cast<uint32>( step )]._pInitialize( _pHost ) == EngineInitResult::Failed )
            {
                SW_LOG_ERROR( "Startup step '%#' failed to restart", getStepName( step ) );
                bRestarted = false;
                break;
            }
            _listInitialized.push_back( step );
        }
        // 종료가 역순으로 돌도록 초기화한 단계를 표의 순서로 되돌린다(다시 세운 단계는 끝에 붙었다).
        vector<EngineInitStep> listOrdered;
        listOrdered.reserve( _listInitialized.size() );
        for ( const EngineInitStep orderStep : _listOrder )
        {
            for ( const EngineInitStep initializedStep : _listInitialized )
            {
                if ( initializedStep == orderStep )
                {
                    listOrdered.push_back( orderStep );
                    break;
                }
            }
        }
        _listInitialized = std::move( listOrdered );
        return bRestarted;
    }

    const utf8* EngineInitSequence::getStepName( EngineInitStep step )
    {
        const uint32 stepIndex = static_cast<uint32>( step );
        if ( stepIndex >= static_cast<uint32>( EngineInitStep::Count ) )
            return "Unknown";
        return EngineInitSequenceInternal::kArrStepNode[stepIndex]._pName;
    }

    MemoryTag EngineInitSequence::getStepMemoryTag( EngineInitStep step )
    {
        const uint32 stepIndex = static_cast<uint32>( step );
        if ( stepIndex >= static_cast<uint32>( EngineInitStep::Count ) )
            return MemoryTag::Unknown;
        return EngineInitSequenceInternal::kArrStepMemoryTag[stepIndex];
    }

    vector<EngineInitNode> EngineInitSequence::makeStepNodes()
    {
        vector<EngineInitNode> listNode;
        listNode.reserve( static_cast<size_t>( EngineInitStep::Count ) );
        for ( const EngineInitNode& node : EngineInitSequenceInternal::kArrStepNode )
            listNode.push_back( node );
        return listNode;
    }

    bool EngineInitSequence::computeGraph( const vector<EngineInitNode>& listNode, EngineInitGraph& outGraph, string& outError )
    {
        const uint32 nodeCount = static_cast<uint32>( listNode.size() );
        outGraph._listOrder.clear();
        outGraph._listDependency.clear();
        outGraph._listDependency.resize( nodeCount );

        for ( uint32 nodeIndex = 0; nodeIndex < nodeCount; ++nodeIndex )
        {
            if ( EngineInitSequenceInternal::findNodeIndex( listNode, listNode[nodeIndex]._pName ) != nodeIndex )
            {
                StringBuilder<constant::kMaxBuffer256> sb;
                sb.append( "Startup step '" ).append( listNode[nodeIndex]._pName ).append( "' is declared twice" );
                outError = string( sb.c_str() );
                return false;
            }
            if ( EngineInitSequenceInternal::resolveDependencies( listNode, nodeIndex, outGraph._listDependency[nodeIndex], outError ) == false )
                return false;
        }

        // 남은 의존이 0 인 노드 중 **이름이 가장 앞인** 것을 고른다(`TopologicalSortUtil` — 모듈 적재 순서와 같은 규칙). 목록 순서를 보지 않으므로
        // 순서는 의존 칸만이 정한다(표를 기동 순서로 적고, 그것이 이 결과와 같은지 시험이 본다).
        vector<string_view> listName;
        listName.reserve( nodeCount );
        for ( const EngineInitNode& node : listNode )
            listName.push_back( node._pName );
        vector<uint32> listUnsorted;
        if ( TopologicalSortUtil::sortByDependency( listName, outGraph._listDependency, outGraph._listOrder, listUnsorted ) == false )
        {
            // 남은 노드는 모두 서로를 기다린다 — 순환(이나 순환에 매달린 노드)이다.
            StringBuilder<constant::kMaxBuffer512> sb;
            sb.append( "Startup steps in or behind a dependency cycle:" );
            for ( const uint32 nodeIndex : listUnsorted )
                sb.append( ' ' ).append( listNode[nodeIndex]._pName );
            outError = string( sb.c_str() );
            return false;
        }
        return true;
    }
} // namespace sw
