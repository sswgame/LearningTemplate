#include "pch.h"

#include "DevTools/Diagnostics/TestCaseStateProbe.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Event/EventDispatcher.h"
#include "Core/GlobalVariable/GlobalVariableManager.h"
#include "Core/Hash/HashUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/Subsystem/Subsystem.h"
#include "Core/Task/TaskManager.h"
#include "Core/TestFramework/TestWait.h"

#include "Engine/Config/GameTimeScale.h"
#include "Engine/Console/DevCommandRegistry.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineSubsystems.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Inherit/ResourceTags.h"

namespace sw
{
    namespace
    {
        struct TestCaseStateProbeInternal
        {
            static void addGrowth( vector<string>& outListLeak, string_view name, uint64 before, uint64 after )
            {
                if ( after <= before )
                    return;
                StringBuilder<constant::buffer::kMax256> line;
                line.appendFormat( "%#: %# -> %#", name, before, after );
                outListLeak.emplace_back( line.view() );
            }

            static void addChange( vector<string>& outListLeak, string_view name, uint64 before, uint64 after )
            {
                if ( after != before )
                    outListLeak.emplace_back( name );
            }

            static uint64 hashTexts( const vector<string>& listText )
            {
                uint64 hash = 0;
                for ( const string& text : listText )
                {
                    hash = HashUtil::combine( hash, HashUtil::makeHash( text ) );
                }
                return hash;
            }
        };
    } // namespace

    TestCaseStateProbe::TestCaseStateProbe()
        : _before{}
        , _listVariable{}
        , _listVariableText{}
        , _listSubsystemScratch{}
        , _listDomainScratch{}
        , _listKnownState{}
        , _variableGeneration{ 0 }
        , _bStarted{ SW_FALSE }
    {
    }

    uint64 TestCaseStateProbe::hashVariables()
    {
        const GlobalVariableManager& manager    = engine::getSubsystem<GlobalVariableManager>();
        const uint64                 generation = manager.getRegistrationGeneration();
        if ( generation != _variableGeneration || _listVariable.empty() )
        {
            _variableGeneration = generation;
            manager.collectVariables( _listVariable );
            refreshVariableTexts( nullptr );
        }
        uint64 hash = 0;
        for ( const GlobalVariableInfo* pInfo : _listVariable )
        {
            uint64 value = 0;
            switch ( pInfo->_type )
            {
                case GlobalVariableType::Boolean:
                {
                    value = pInfo->getValueAsBool() ? 1u : 0u;
                    break;
                }
                case GlobalVariableType::Int32:
                case GlobalVariableType::Enum:
                {
                    value = static_cast<uint64>( static_cast<int64>( pInfo->getValueAsInt() ) );
                    break;
                }
                case GlobalVariableType::Float:
                {
                    value = HashUtil::makeHash( pInfo->getValueAsFloat() );
                    break;
                }
                case GlobalVariableType::String:
                case GlobalVariableType::DiskPath:
                case GlobalVariableType::Secret:
                case GlobalVariableType::Count:
                {
                    value = HashUtil::makeHash( pInfo->getValueAsString() ); // 비밀 값은 가린 글이라 바뀜을 보지 않는다
                    break;
                }
            }
            hash = HashUtil::combine( hash, value );
        }
        return hash;
    }

    void TestCaseStateProbe::refreshVariableTexts( vector<string>* pOutListLeak )
    {
        vector<string> listText;
        listText.reserve( _listVariable.size() );
        for ( size_t index = 0; index < _listVariable.size(); ++index )
        {
            listText.push_back( _listVariable[index]->getValueAsString() );
            if ( pOutListLeak == nullptr || listText.back() == _listVariableText[index] )
                continue;
            StringBuilder<constant::buffer::kMax256> line;
            line.appendFormat( "global variable %#: '%#' -> '%#'", _listVariable[index]->_name, _listVariableText[index], listText.back() );
            pOutListLeak->emplace_back( line.view() );
        }
        _listVariableText = std::move( listText );
    }

    void TestCaseStateProbe::collectNewStates( vector<string>& outListState )
    {
        _listSubsystemScratch.clear();
        engine::collectInitializedSubsystems( _listSubsystemScratch );
        for ( const Subsystem* const pSubsystem : _listSubsystemScratch )
        {
            const utf8* const pState = pSubsystem->findGameInstanceState();
            if ( pState == nullptr )
                continue;
            StringBuilder<constant::buffer::kMax256> line;
            line.appendFormat( "%#: %#", pSubsystem->getSubsystemName(), pState );
            const string text( line.view() );
            if ( VectorUtil::contains( _listKnownState, text ) )
                continue;
            _listKnownState.push_back( text );
            outListState.push_back( text );
        }
    }

    TestCaseStateProbe::Snapshot TestCaseStateProbe::capture()
    {
        using Internal             = TestCaseStateProbeInternal;
        const SceneManager& scenes = engine::getSubsystem<SceneManager>();
        Snapshot            snapshot;
        snapshot._subscriberCount   = engine::getSubsystem<EventDispatcher>().getSubscriberCount();
        snapshot._commandCount      = engine::getSubsystem<DevCommandRegistry>().getCount();
        snapshot._pauseRequestCount = engine::getSubsystem<GameTimeScale>().getPauseRequestCount();
        snapshot._sceneCount        = scenes.getLoadedScenes().size() + scenes.getPreviewScenes().size();
        _listDomainScratch.clear();
        engine::getSubsystem<AssetPaths>().collectMountedDiskDomains( _listDomainScratch );
        snapshot._domainHash                 = Internal::hashTexts( _listDomainScratch );
        snapshot._tagHash                    = Internal::hashTexts( engine::getSubsystem<ResourceTags>().capture().getTags() );
        const DeterministicRunPolicy& policy = engine::getSubsystem<DeterministicRun>().getPolicy();
        const uint64                  flags  = uint64{ policy._bFinishAssetJobsEachFrame } | ( uint64{ policy._bWaitForAnimationClips } << 1 ) | ( uint64{ policy._bFreezeEnvironmentTime } << 2 ) |
                             ( uint64{ policy._bSkipLoadingScreenMinimum } << 3 ) | ( static_cast<uint64>( static_cast<uint32>( policy._realTimeFrameRate ) ) << 8 );
        snapshot._policyHash   = HashUtil::combine( flags, HashUtil::makeHash( policy._fixedFrameDelta ) );
        snapshot._variableHash = hashVariables();
        snapshot._taskCount    = engine::getSubsystem<TaskManager>().getActiveTaskCount();
        return snapshot;
    }

    void TestCaseStateProbe::beginCase()
    {
        _before = capture();
        if ( _bStarted == SW_TRUE )
            return;
        _bStarted = SW_TRUE;
        vector<string> listState; // 첫 케이스 앞에 이미 있는 것은 엔진의 것이다
        collectNewStates( listState );
    }

    void TestCaseStateProbe::endCase( vector<string>& outListLeak )
    {
        using Internal     = TestCaseStateProbeInternal;
        TaskManager& tasks = engine::getSubsystem<TaskManager>();
        // 결과를 돌려준 태스크도 잠깐은 남은 수에 있다 — 빠지기를 기다린다. 케이스가 흘린 비동기 일도 여기서 끝나 그 로그가 이 케이스의 것이 된다.
        const bool bTasksDone = tasks.getActiveTaskCount() <= _before._taskCount || test::waitUntil( [&tasks, this]
        { return tasks.getActiveTaskCount() <= _before._taskCount; } );
        if ( bTasksDone == false )
        {
            StringBuilder<constant::buffer::kMax256> line;
            line.appendFormat( "tasks still running: %# -> %# (%#)", _before._taskCount, tasks.getActiveTaskCount(), tasks.describeUnfinishedTask() );
            outListLeak.emplace_back( line.view() );
        }
        const Snapshot after = capture();
        Internal::addGrowth( outListLeak, "event subscribers", _before._subscriberCount, after._subscriberCount );
        Internal::addGrowth( outListLeak, "dev commands", _before._commandCount, after._commandCount );
        Internal::addGrowth( outListLeak, "pause requests", _before._pauseRequestCount, after._pauseRequestCount );
        Internal::addGrowth( outListLeak, "scenes of the engine scene manager", _before._sceneCount, after._sceneCount );
        Internal::addChange( outListLeak, "mounted disk domains changed", _before._domainHash, after._domainHash );
        Internal::addChange( outListLeak, "resource tags changed", _before._tagHash, after._tagHash );
        Internal::addChange( outListLeak, "deterministic run policy changed", _before._policyHash, after._policyHash );
        if ( after._variableHash != _before._variableHash )
            refreshVariableTexts( &outListLeak );
        collectNewStates( outListLeak );
    }
} // namespace sw
