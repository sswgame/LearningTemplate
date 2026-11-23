#include "pch.h"

#include "Engine/Scene/SceneSubLevelSubsystem.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Content/AssetID.h"
#include "Engine/Foundation/DeterministicRun.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Service/SceneServiceRegistry.h"
#include "Engine/Serialization/Format/Archive.h"

namespace sw
{
    // 짓는 일은 파괴를 처리한 뒤(`DestroyPost`)다 — 스트리밍 셀 · 데이터 레이어와 같은 자리다. 만든 오브젝트는 다음 프레임 병합에서 틱에 든다.
    SW_SCENE_SERVICE( SceneSubLevelSubsystem, DestroyPost, makeSceneFrameAccess( SceneDataKindSet{ SceneDataKind::Structure }, SceneDataKindSet{}, SceneDataKindSet{} ) )

    namespace
    {
        struct SceneSubLevelInternal
        {
            /** @brief 멤버 이름의 앞머리를 가르는 글자입니다(씬 인스턴스 `SceneDocument::expandSceneInstances` 와 같다). */
            static constexpr const utf8* kNameSeparator = "/";
        };
    } // namespace

    SceneSubLevelSubsystem::SceneSubLevelSubsystem( GameObjectManager& manager )
        : _pScene{ nullptr }
        , _pManager{ &manager }
        , _listSubLevel{}
        , _mapObjectSubLevel{}
    {
    }

    uint64 SceneSubLevelSubsystem::computeAnchorFileID( string_view name )
    {
        // 씬 인스턴스 범위와 같은 섞기 — 작은 파일 id 와 겹치지 않는 넓은 값이다. 대소문자를 가린다(이름이 곧 정체다).
        return SceneDocument::computeInstanceIDScope( HashUtil::computeStringHash64( name.data(), name.size(), false ) );
    }

    void SceneSubLevelSubsystem::makeMemberNodes( const SceneDocument& doc, string_view name, const uint64 anchorFileID, vector<SceneDocument::SceneObjectNode>& outListNode )
    {
        outListNode                                    = doc._listSceneObjectNode;
        const uint64                             scope = SceneDocument::computeInstanceIDScope( anchorFileID );
        StringBuilder<constant::buffer::kMax256> memberName;
        for ( SceneDocument::SceneObjectNode& member : outListNode )
        {
            member._instanceAnchorFileID = member._instanceAnchorFileID == 0 ? anchorFileID : ( member._instanceAnchorFileID ^ scope );
            member._fileID ^= scope;
            member._idScope ^= scope;            // 글 상태의 참조(내부 id → 평평한 id)
            member._cookedStateIDScope ^= scope; // 쿠킹된 바이너리 상태의 참조(쿠킹본 안에서 이미 평평한 id → 이 하위 레벨의 id)
            memberName.clear();
            memberName.appendFormat( "%#%#%#", name, SceneSubLevelInternal::kNameSeparator, member._name );
            member._name = string( memberName.view() );
        }
    }

    bool SceneSubLevelSubsystem::addSubLevel( Scene& scene, string_view name, string_view scenePath, const Placement& placement )
    {
        if ( scene.getObjectManager() != _pManager )
        {
            SW_LOG_ERROR_ID( "SceneSubLevels.WrongScene", "Sub-level '%#' was added to a scene this service does not belong to - it is not added", name );
            return false;
        }
        if ( _pScene != nullptr && _pScene != &scene )
        {
            SW_LOG_ERROR_ID( "SceneSubLevels.SceneChanged", "Sub-level '%#' was added to scene '%#' while sub-levels of another scene remain - it is not added", name, scene.getName() );
            return false;
        }
        if ( name.empty() || findSubLevel( name ) != nullptr )
        {
            SW_LOG_ERROR_ID( "SceneSubLevels.NameTaken", "Sub-level name '%#' is empty or already used - it is not added", name );
            return false;
        }
        SceneDocument doc{};
        if ( doc.load( AssetID{ scenePath } ) == false )
        {
            SW_LOG_ERROR_ID( "SceneSubLevels.SceneNotLoaded", "Sub-level '%#' could not load scene '%#' - it is not added", name, scenePath );
            return false;
        }
        // 하위 레벨은 한 묶음으로 오르내린다 — 안을 셀 · 레이어로 다시 나누는 씬은 받지 않는다(그 상태를 들 곳이 없다).
        bool bLayered = doc._sceneSettings._listDataLayer.empty() == false;
        for ( const SceneDocument::SceneObjectNode& node : doc._listSceneObjectNode )
        {
            bLayered = bLayered || node._listDataLayer.empty() == false;
        }
        if ( doc._sceneSettings._bStreaming || doc._listStreamingCell.empty() == false || bLayered )
        {
            SW_LOG_ERROR_ID( "SceneSubLevels.SceneNotAddable", "Sub-level '%#': scene '%#' streams cells or uses data layers - add it as a scene of its own instead", name, scenePath );
            return false;
        }
        vector<SceneDocument::SceneObjectNode> listNode;
        makeMemberNodes( doc, name, computeAnchorFileID( name ), listNode );
        _pScene = &scene;
        pushSubLevel( name, scenePath, placement, std::move( listNode ) );
        return true;
    }

    bool SceneSubLevelSubsystem::removeSubLevel( string_view name )
    {
        SubLevel* pSubLevel = findSubLevel( name );
        if ( pSubLevel == nullptr || pSubLevel->_state == SubLevelState::Removing )
        {
            SW_LOG_ERROR_ID( "SceneSubLevels.NotFound", "There is no sub-level named '%#' to remove", name );
            return false;
        }
        pSubLevel->_state = SubLevelState::Removing;
        return true;
    }

    void SceneSubLevelSubsystem::endScene()
    {
        // 오브젝트는 씬이 모두 지운다 — 짓기 진행만 놓는다.
        _listSubLevel.clear();
        _mapObjectSubLevel.clear();
        _pScene = nullptr;
    }

    void SceneSubLevelSubsystem::onSceneCleared()
    {
        // 매니저가 오브젝트를 비웠다(세이브 복원) — 남길 오브젝트가 없다. 세이브에 실린 하위 레벨은 복원이 다시 세운다(`applySaveState`).
        _listSubLevel.clear();
        _mapObjectSubLevel.clear();
    }

    SceneSubLevelSubsystem::SubLevel* SceneSubLevelSubsystem::findSubLevel( string_view name )
    {
        for ( const unique_ptr<SubLevel>& pSubLevel : _listSubLevel )
        {
            if ( pSubLevel->_name == name )
                return pSubLevel.get();
        }
        return nullptr;
    }

    void SceneSubLevelSubsystem::pushSubLevel( string_view name, string_view scenePath, const Placement& placement, vector<SceneDocument::SceneObjectNode> listNode )
    {
        unique_ptr<SubLevel> pSubLevel = make_unique<SubLevel>();
        pSubLevel->_name               = name;
        pSubLevel->_scenePath          = scenePath;
        pSubLevel->_placement          = placement;
        pSubLevel->_anchorFileID       = computeAnchorFileID( name );
        pSubLevel->_listNode           = std::move( listNode );
        _listSubLevel.push_back( std::move( pSubLevel ) );
    }

    void SceneSubLevelSubsystem::update( float32 deltaTime )
    {
        (void)deltaTime;
        if ( _pScene == nullptr )
            return;

        // 1. 빼기 — 목록에서 지운다(짓는 중이면 짓기 진행도 함께 놓는다).
        for ( size_t index = _listSubLevel.size(); index > 0; --index )
        {
            SubLevel& subLevel = *_listSubLevel[index - 1];
            if ( subLevel._state != SubLevelState::Removing )
                continue;
            subLevel._pBuild.reset();
            unloadSubLevel( subLevel );
            _listSubLevel.erase( _listSubLevel.begin() + static_cast<ptrdiff_t>( index - 1 ) );
        }

        // 2. 짓기 — 더한 순서대로, 셀과 같은 시간 예산. 결정적 실행은 예산 없이 그 프레임에 다 짓는다(도착이 프레임 시간에 달리지 않게).
        const bool      bFinishEachFrame  = engine::getSubsystem<DeterministicRun>().getPolicy()._bFinishAssetJobsEachFrame == SW_TRUE;
        const int64     budgetNanoseconds = bFinishEachFrame ? 0 : static_cast<int64>( _pScene->getSceneSettings()._addToWorldBudgetMicroseconds ) * constant::time::kNanosecondsPerMicrosecond;
        const Stopwatch stopwatch;
        bool            bProgressed{ false };
        for ( const unique_ptr<SubLevel>& pSubLevel : _listSubLevel )
        {
            SubLevel& subLevel = *pSubLevel;
            if ( subLevel._state == SubLevelState::Queued )
                beginBuild( subLevel );
            if ( subLevel._state != SubLevelState::Building )
                continue;
            if ( bProgressed && budgetNanoseconds > 0 && stopwatch.getElapsedNanoseconds() >= budgetNanoseconds )
                break;
            const Scene::CellBuildResult result = _pScene->continueCellBuild( *subLevel._pBuild, stopwatch, budgetNanoseconds, bProgressed == false, subLevel._listObjectID );
            bProgressed                         = true;
            if ( result == Scene::CellBuildResult::InProgress )
                break;
            subLevel._pBuild.reset();
            includeAnchorObject( subLevel );
            if ( result == Scene::CellBuildResult::Done )
            {
                subLevel._state = SubLevelState::Loaded;
                if ( subLevel._anchorObjectID == 0 )
                    subLevel._anchorObjectID = _pScene->findObjectIDByFileID( subLevel._anchorFileID ); // 떠 둔 상태로 지었다 — 앵커가 그 안에 있었다
                rebuildObjectMap();
                SW_LOG_INFO_ID( "SceneSubLevels.Loaded", "Sub-level '%#' (%#) is loaded with %# objects", subLevel._name, subLevel._scenePath, static_cast<uint32>( subLevel._listObjectID.size() ) );
                continue;
            }
            SW_LOG_ERROR_ID( "SceneSubLevels.BuildFailed", "Sub-level '%#' could not build scene '%#' - it stays unloaded", subLevel._name, subLevel._scenePath );
            unloadSubLevel( subLevel ); // 반쯤 지은 것을 남기지 않는다
            subLevel._state = SubLevelState::Failed;
        }
    }

    void SceneSubLevelSubsystem::beginBuild( SubLevel& subLevel )
    {
        // 떠 둔 상태(세이브)에는 앵커가 엔티티로 들어 있다 — 그때는 짓기가 앵커를 함께 세운다.
        _pScene->recordWideFileID( subLevel._anchorFileID ); // 떠 둔 상태의 앵커는 인스턴스 멤버가 아니라 짓기가 넓은 id 인 줄 모른다
        bool bAnchorInNodes{ false };
        for ( const SceneDocument::SceneObjectNode& node : subLevel._listNode )
        {
            bAnchorInNodes = bAnchorInNodes || node._fileID == subLevel._anchorFileID;
        }
        if ( bAnchorInNodes == false )
        {
            GameObject*     pAnchor = _pManager->createGameObject( hashed_string( subLevel._name.c_str() ) );
            SceneComponent* pRoot   = pAnchor != nullptr ? pAnchor->addComponent<SceneComponent>() : nullptr;
            if ( pRoot == nullptr || _pScene->assignFileID( pAnchor->getObjectID(), subLevel._anchorFileID ) == false )
            {
                SW_LOG_ERROR_ID( "SceneSubLevels.AnchorNotMade", "Sub-level '%#' could not place its anchor object (its file id is taken) - it stays unloaded", subLevel._name );
                if ( pAnchor != nullptr )
                    _pManager->destroyGameObject( pAnchor );
                subLevel._state = SubLevelState::Failed;
                return;
            }
            // 자리는 절대 좌표다 — 원점을 옮긴 월드에서는 그 원점 기준으로 놓는다(세이브 · 서버 · 다른 프로세스와 같은 자리).
            const double3& origin = _pScene->getWorldOrigin();
            pRoot->setLocalPosition( float3{ static_cast<float32>( static_cast<float64>( subLevel._placement._position._x ) - origin._x ),
                                             static_cast<float32>( static_cast<float64>( subLevel._placement._position._y ) - origin._y ),
                                             static_cast<float32>( static_cast<float64>( subLevel._placement._position._z ) - origin._z ) } );
            pRoot->setLocalRotation( subLevel._placement._rotation );
            pAnchor->setAlwaysLoaded( true );                  // 스트리밍 셀이 런타임 스폰으로 삼키지 않는다 — 하위 레벨은 자기 단위로 오르내린다
            subLevel._anchorObjectID = pAnchor->getObjectID(); // 목록에는 짓기 뒤에 든다 — 셀 짓기는 목록을 "이 셀이 지은 것" 으로 보고 참조 대상에서 빼, 멤버가 앵커를 찾지 못한다
        }
        subLevel._pBuild = _pScene->beginCellBuild( subLevel._listNode );
        subLevel._state  = SubLevelState::Building;
    }

    void SceneSubLevelSubsystem::includeAnchorObject( SubLevel& subLevel )
    {
        const bool bListed = std::find( subLevel._listObjectID.begin(), subLevel._listObjectID.end(), subLevel._anchorObjectID ) != subLevel._listObjectID.end();
        if ( subLevel._anchorObjectID != 0 && bListed == false )
            subLevel._listObjectID.insert( subLevel._listObjectID.begin(), subLevel._anchorObjectID );
    }

    void SceneSubLevelSubsystem::unloadSubLevel( SubLevel& subLevel )
    {
        includeAnchorObject( subLevel ); // 짓는 중에 빼도 앵커가 남지 않게
        for ( const uint64 objectID : subLevel._listObjectID )
        {
            _mapObjectSubLevel.erase( objectID );
        }
        if ( _pScene != nullptr )
            _pScene->unloadCell( subLevel._listObjectID );
        subLevel._listObjectID.clear();
        subLevel._anchorObjectID = 0;
    }

    void SceneSubLevelSubsystem::rebuildObjectMap()
    {
        _mapObjectSubLevel.clear();
        for ( const unique_ptr<SubLevel>& pSubLevel : _listSubLevel )
        {
            for ( const uint64 objectID : pSubLevel->_listObjectID )
            {
                _mapObjectSubLevel[objectID] = pSubLevel->_anchorObjectID;
            }
        }
    }

    uint32 SceneSubLevelSubsystem::getLoadedSubLevelCount() const
    {
        uint32 count{ 0 };
        for ( const unique_ptr<SubLevel>& pSubLevel : _listSubLevel )
        {
            count += pSubLevel->_state == SubLevelState::Loaded ? 1u : 0u;
        }
        return count;
    }

    bool SceneSubLevelSubsystem::findSubLevelState( string_view name, SubLevelState& outState ) const
    {
        for ( const unique_ptr<SubLevel>& pSubLevel : _listSubLevel )
        {
            if ( pSubLevel->_name != name )
                continue;
            outState = pSubLevel->_state;
            return true;
        }
        return false;
    }

    void SceneSubLevelSubsystem::collectSubLevelNames( vector<string>& outListName ) const
    {
        outListName.clear();
        for ( const unique_ptr<SubLevel>& pSubLevel : _listSubLevel )
        {
            outListName.push_back( pSubLevel->_name );
        }
    }

    bool SceneSubLevelSubsystem::isSubLevelObject( const uint64 objectID ) const
    {
        if ( _pScene == nullptr || _mapObjectSubLevel.empty() )
            return false;
        // 멤버에 붙은 자식(중첩 프리팹 부품 · 게임이 붙인 것)은 하위 레벨과 함께 선다.
        for ( const GameObject* pObject = _pManager->findGameObjectByID( objectID ); pObject != nullptr; pObject = pObject->getParent() )
        {
            if ( _mapObjectSubLevel.find( pObject->getObjectID() ) != _mapObjectSubLevel.end() )
                return true;
        }
        return false;
    }

    void SceneSubLevelSubsystem::writeSaveState( vector<uint8>& outBytes ) const
    {
        Archive                 arch;
        vector<const SubLevel*> listSaved;
        for ( const unique_ptr<SubLevel>& pSubLevel : _listSubLevel )
        {
            const bool bSaved = pSubLevel->_state != SubLevelState::Failed && pSubLevel->_state != SubLevelState::Removing;
            if ( bSaved )
                listSaved.push_back( pSubLevel.get() );
        }
        arch << static_cast<uint32>( listSaved.size() );
        for ( const SubLevel* pSubLevel : listSaved )
        {
            arch << string_view( pSubLevel->_name ) << string_view( pSubLevel->_scenePath ) << pSubLevel->_placement._position << pSubLevel->_placement._rotation;
            // 다 지었으면 지금 떠서(앵커 포함 — 앵커가 옮겨졌을 수 있다), 짓는 중이면 아직 지을 내용 그대로다(앵커는 다시 세울 때 만든다).
            vector<SceneDocument::SceneObjectNode> listCaptured;
            const bool                             bLoaded = pSubLevel->_state == SubLevelState::Loaded && _pScene != nullptr;
            if ( bLoaded )
                _pScene->captureCellNodes( pSubLevel->_listObjectID, listCaptured );
            vector<uint8> bytes;
            SceneDocument::writeSavedNodes( bLoaded ? listCaptured : pSubLevel->_listNode, bytes );
            arch.writeSection( bytes.data(), static_cast<uint32>( bytes.size() ) );
        }
        outBytes.assign( arch.getData(), arch.getData() + arch.getSize() );
    }

    bool SceneSubLevelSubsystem::readSaveState( const uint8* pData, const size_t size, SaveState& outState )
    {
        outState = SaveState{};
        if ( pData == nullptr )
            return false;
        Archive arch( pData, size );
        uint32  count{ 0 };
        count = arch.readCount( sizeof( uint32 ) * 3 );
        if ( arch.isError() )
            return false;
        outState._listSubLevel.resize( count );
        for ( SavedSubLevel& saved : outState._listSubLevel )
        {
            arch >> saved._name >> saved._scenePath >> saved._placement._position >> saved._placement._rotation;
            vector<uint8> bytes;
            if ( arch.readSection( bytes ) == false || arch.isError() || saved._name.empty() ||
                 SceneDocument::readSavedNodes( bytes.data(), bytes.size(), saved._listNode, "save game (sub-levels)" ) == false )
            {
                SW_LOG_ERROR_ID( "SceneSubLevels.SaveCorrupted", "Save game sub-levels could not be read - the save is not used" );
                outState = SaveState{};
                return false;
            }
        }
        return arch.isError() == false;
    }

    void SceneSubLevelSubsystem::applySaveState( Scene& scene, SaveState state )
    {
        if ( scene.getObjectManager() != _pManager )
        {
            SW_LOG_ERROR_ID( "SceneSubLevels.SaveWrongScene", "The save's sub-levels were applied to a scene this service does not belong to - they are not restored" );
            return;
        }
        for ( const unique_ptr<SubLevel>& pSubLevel : _listSubLevel )
        {
            unloadSubLevel( *pSubLevel ); // 복원은 비운 매니저 위다 — 남은 오브젝트가 있으면 세이브의 상태로 갈아 끼운다
        }
        _listSubLevel.clear();
        _mapObjectSubLevel.clear();
        _pScene = &scene;
        for ( SavedSubLevel& saved : state._listSubLevel )
        {
            if ( findSubLevel( saved._name ) != nullptr )
            {
                SW_LOG_ERROR_ID( "SceneSubLevels.SaveNameTwice", "The save carries sub-level '%#' twice - the later one is not restored", saved._name );
                continue;
            }
            pushSubLevel( saved._name, saved._scenePath, saved._placement, std::move( saved._listNode ) );
        }
    }

    SceneSubLevelSubsystem::State SceneSubLevelSubsystem::captureState() const
    {
        State state{};
        for ( const unique_ptr<SubLevel>& pSubLevel : _listSubLevel )
        {
            if ( pSubLevel->_state != SubLevelState::Loaded )
                continue;
            state._listSubLevel.push_back( CapturedSubLevel{ pSubLevel->_name, pSubLevel->_scenePath, pSubLevel->_placement, pSubLevel->_anchorObjectID, pSubLevel->_listObjectID } );
        }
        return state;
    }

    void SceneSubLevelSubsystem::restoreState( Scene& scene, State state )
    {
        _listSubLevel.clear();
        _pScene = state._listSubLevel.empty() ? nullptr : &scene;
        for ( CapturedSubLevel& captured : state._listSubLevel )
        {
            pushSubLevel( captured._name, captured._scenePath, captured._placement, {} );
            SubLevel& subLevel       = *_listSubLevel.back();
            subLevel._anchorObjectID = captured._anchorObjectID;
            subLevel._listObjectID   = std::move( captured._listObjectID );
            subLevel._state          = SubLevelState::Loaded;
        }
        rebuildObjectMap();
    }
} // namespace sw
