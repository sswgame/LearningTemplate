#include "pch.h"

#include "GameFramework/Kits/Feature/World/Housing/Component/HousingPlotComponent.h"

#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Math/MathUtil.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Engine/Foundation/WorldOrigin.h"
#include "Engine/Object/Component/SceneComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/GameObjectManager.inl"
#include "Engine/Object/GameObject/Subsystem/SceneNetLink.h"
#include "Engine/Object/Prefab/PrefabAsset.h"
#include "Engine/Reflection/EnumName.h"
#include "Engine/Reflection/ReflectionValidation.h"
#include "Engine/Scene/SceneManager.h"
#include "Engine/Serialization/Format/Archive.h"

#include "GameFramework/Kits/Feature/World/Housing/Component/HousingNetSessionComponent.h"
#include "GameFramework/Runtime/Foundation/Save/StatefulComponentRegistrar.h"
#include "GameFramework/Runtime/Gameplay/GameState/GameStateComponent.h"

namespace sw
{
    namespace
    {
        struct HousingPlotInternal
        {
            /** @brief 부지 둘이 칸 경계에서 맞닿는 것은 겹침이 아니다(m). */
            static constexpr float32 kTouchEpsilon = 1.0e-3f;
        };

        /** @brief 배치 · 지갑을 모든 게임의 상태 스냅샷에 싣고, 저장 전에 부품 뷰 · 고스트를 걷는다(게임마다 올리지 않는다). */
        const StatefulComponentRegistrar<HousingPlotComponent> s_housingPlotComponentStateRegistration{ "HousingPlotComponent" };
    } // namespace
} // namespace sw

namespace sw
{
    HousingPlotComponent::HousingPlotComponent()
        : _settings{}
        , _partCatalog{}
        , _startingFunds{ 0 }
        , _ghostPlaceablePrefab{}
        , _ghostBlockedPrefab{}
        , _pCatalog{}
        , _layout{}
        , _wallet{}
        , _listView{}
        , _ghostCandidate{}
        , _ghostCheck{}
        , _ghost{}
        , _outgoingEditBuffer{}
        , _viewRevision{ 0 }
        , _pendingRequestCount{ 0 }
        , _lastResult{ HousingPlaceResult::Ok }
        , _ownerID{ HousingOwnerID::kNone }
        , _replicatedOwner{ HousingPlotOwner::None }
        , _netRole{ HousingNetRole::Standalone }
        , _bGhostShown{ SW_FALSE }
        , _bGhostDirty{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    HousingPlotComponent::~HousingPlotComponent() = default;

    bool HousingPlotComponent::startGame()
    {
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || pOwner->getPrimarySceneComponent() == nullptr )
        {
            SW_LOG_ERROR_ID( "HousingPlot.PlotNeedsSceneComponent", "Housing plot '%#': the plot object needs a scene component - its position is the plot origin", _settings._plotID.c_str() );
            return false;
        }
        if ( _ghostPlaceablePrefab.empty() || _ghostBlockedPrefab.empty() )
        {
            SW_LOG_ERROR_ID( "HousingPlot.GhostPrefabMissing", "Housing plot '%#': both ghost prefabs are required - the plot cannot open", _settings._plotID.c_str() );
            return false;
        }
        if ( acquireRequired( _partCatalog.path(), _pCatalog ) == false )
            return false;
        // 네트워크 월드면 같은 씬의 세션이 요청 · 방송을 맡는다 — 없으면 클라이언트 편집이 어디에도 가지 않고 서버 편집이 방송되지 않는다(조용히 혼자 돌지 않는다).
        const NetLinkValue* pLink = SceneNetLink::findBoundFor( *this );
        _netRole                  = HousingNetRole::Standalone;
        if ( pLink != nullptr )
        {
            const GameObjectManager* pManager = getObjectManager();
            if ( pManager == nullptr || pManager->getComponentRegistry().getAll<HousingNetSessionComponent>().empty() )
            {
                SW_LOG_ERROR_ID( "HousingPlot.NetSessionMissing", "Housing plot '%#': this world is networked but the scene has no HousingNetSessionComponent - the plot cannot open",
                                 _settings._plotID.c_str() );
                return false;
            }
            _netRole = pLink->_role == NetLinkRole::Server ? HousingNetRole::Server : HousingNetRole::Client;
        }
        _outgoingEditBuffer.clear();
        _pendingRequestCount = 0;
        _replicatedOwner     = HousingPlotOwner::None;
        _layout.initialize( _settings, *_pCatalog, _wallet );
        // 판 상태(같은 오브젝트의 GameStateComponent)가 있으면 그 공유 땅에서 부지를 차지한다 — 밭 · 도시 · 전장 키트와 한 맵의 칸을 나눈다.
        // 판 상태가 없는 씬은 단독 부지다(땅이 없는 게임 — 선택 연동이지 빠진 필수 값이 아니다).
        GameStateComponent* pState = GameStateComponent::findOnOwner( *this );
        if ( pState != nullptr && pState->isInitialized() == false )
        {
            SW_LOG_ERROR_ID( "HousingPlot.SessionStateNotOpen", "Housing plot '%#': the session state on this object is not open yet - put the plot after it so the plot can claim the shared land",
                             _settings._plotID.c_str() );
            return false;
        }
        if ( pState != nullptr && pState->getLand().isInitialized() )
        {
            LandRegistry& land = pState->getLand();
            if ( MathUtil::nearEqual( land.getCellSize(), _pCatalog->getRoot()._cellSize ) == false )
            {
                SW_LOG_ERROR_ID( "HousingPlot.LandCellSizeMismatch", "Housing plot '%#': the shared land cell (%#) and the catalog cell (%#) differ - the plot cannot claim land", _settings._plotID.c_str(),
                                 land.getCellSize(), _pCatalog->getRoot()._cellSize );
                return false;
            }
            if ( _layout.bindLand( land, land.computeCell( AbsolutePosition{ getOwner()->getManager()->getWorldOrigin().toAbsolute( getPlotOrigin() ) } ) ) == false )
                return false;
        }
        _wallet.clear();
        _wallet.setBalance( _pCatalog->getRoot()._currency, _startingFunds );
        _lastResult = HousingPlaceResult::Ok;
        return true;
    }

    void HousingPlotComponent::onEndPlay()
    {
        _layout.releaseLand(); // 판 상태가 같은 오브젝트에서 먼저 닫혀도 땅 칸만 비운다(땅 리비전은 다음 판이 다시 칠한다)
        GameModeComponent::onEndPlay();
    }

    void HousingPlotComponent::serializeGameState( StateIO& io )
    {
        io.header( kStateFormat );
        if ( io.isReading() && _pCatalog != nullptr )
            _layout.rebindCatalog( *_pCatalog ); // 다시 읽힌 카탈로그면 새 판으로 읽는다
        uint64 owner = _ownerID;
        io.value( owner );
        io.value( _layout );
        io.value( _wallet );
        if ( io.isWriting() || io.isOk() == false )
            return;
        // 세션 주체(계정 없는 연결)는 그 세션에만 뜻이 있다 — 지금 주인이 아니면 잇지 않는다(불러온 판에서 아무도 열지 못하는 부지가 된다).
        _ownerID = HousingOwnerID::isPersistent( owner ) || owner == _ownerID ? owner : HousingOwnerID::kNone; // 레이아웃이 빌린 지갑의 주소는 그대로다
    }

    void HousingPlotComponent::onStateRestored( bool bRestored )
    {
        if ( bRestored == false )
            SW_LOG_ERROR_ID( "HousingPlot.StateNotRestored", "Housing plot '%#': the saved state could not be read - the plot keeps its current layout", _settings._plotID.c_str() );
    }

    bool HousingPlotComponent::saveToFile( const AbsolutePath& absolutePath ) const
    {
        Archive archive;
        writeState( archive );
        if ( FileUtil::writeFile( absolutePath, archive.getData(), archive.getSize() ) )
            return true;
        SW_LOG_ERROR_ID( "HousingPlot.SaveWriteFailed", "Housing plot '%#': cannot write '%#'", _settings._plotID.c_str(), absolutePath.str() );
        return false;
    }

    bool HousingPlotComponent::loadFromFile( const AbsolutePath& absolutePath )
    {
        if ( _netRole == HousingNetRole::Client )
        {
            SW_LOG_ERROR_ID( "HousingPlot.LoadOnClient", "Housing plot '%#': a network client cannot load a layout - the server owns the plot state", _settings._plotID.c_str() );
            return false;
        }
        vector<uint8> bytes;
        if ( FileUtil::readFile( absolutePath, bytes ) == false )
        {
            SW_LOG_ERROR_ID( "HousingPlot.LoadReadFailed", "Housing plot '%#': cannot read '%#'", _settings._plotID.c_str(), absolutePath.str() );
            return false;
        }
        Archive archive( bytes.data(), bytes.size() );
        if ( readStateOrRevert( archive ) == false )
        {
            onStateRestored( false );
            return false;
        }
        _lastResult = HousingPlaceResult::Ok;
        despawnViews(); // 같은 입력에서 같은 오브젝트 — 다음 틱이 지금 배치대로 모두 다시 세운다(옛 뷰가 남아 겹치지 않는다)
        return true;
    }

    int64 HousingPlotComponent::getFunds() const
    {
        return _pCatalog != nullptr ? _wallet.getBalance( _pCatalog->getRoot()._currency ) : 0;
    }

    uint32 HousingPlotComponent::getViewCount() const
    {
        const GameObjectManager* pManager = getObjectManager();
        uint32                   count    = 0;
        for ( const PartView& view : _listView )
        {
            if ( pManager != nullptr && pManager->resolveGameObject( view._object ) != nullptr )
                ++count;
        }
        return count;
    }

    float3 HousingPlotComponent::getPlotOrigin() const
    {
        const GameObject*     pOwner = getOwner();
        const SceneComponent* pRoot  = pOwner != nullptr ? pOwner->getPrimarySceneComponent() : nullptr;
        return pRoot != nullptr ? pRoot->getWorldPosition() : float3{ 0.0f, 0.0f, 0.0f }; // 열린 부지는 startGame 이 루트를 확인했다
    }

    bool HousingPlotComponent::tryGetPlotGrid( float3& outOrigin, float32& outCellSize ) const
    {
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || pOwner->getPrimarySceneComponent() == nullptr )
            return false;
        // 열기 전(에디터 그리기)에는 파일의 판이다.
        const shared_ptr<const HousingPartCatalog> pCatalog = _pCatalog != nullptr || _partCatalog.empty() ? _pCatalog.snapshot() : _partCatalog.acquire().snapshot();
        if ( pCatalog == nullptr )
            return false;
        outOrigin   = getPlotOrigin();
        outCellSize = pCatalog->getRoot()._cellSize;
        return true;
    }

    float2 HousingPlotComponent::computeLocalPosition( const float3& worldPosition ) const
    {
        const float3 origin = getPlotOrigin();
        return float2{ worldPosition._x - origin._x, worldPosition._z - origin._z };
    }

    // ------------------------------------------------------------------------------
    // 편집
    // ------------------------------------------------------------------------------
    HousingPlaceCheck HousingPlotComponent::previewPlacement( const HousingCandidate& candidate )
    {
        _ghostCandidate = candidate;
        _ghostCheck     = _layout.evaluate( candidate );
        _lastResult     = _ghostCheck._result;
        _bGhostShown    = SW_TRUE;
        _bGhostDirty    = SW_TRUE;
        scheduleFlush();
        return _ghostCheck;
    }

    void HousingPlotComponent::clearPreview()
    {
        _bGhostShown = SW_FALSE;
        _bGhostDirty = SW_TRUE;
        scheduleFlush();
    }

    HousingPlaceResult HousingPlotComponent::finishEdit( HousingPlaceResult result )
    {
        _lastResult = result;
        if ( _bGhostShown == SW_TRUE )
        {
            _ghostCheck  = _layout.evaluate( _ghostCandidate ); // 놓은 뒤 같은 자리는 이제 겹친다 — 고스트도 새 까닭을 보인다
            _bGhostDirty = SW_TRUE;
        }
        scheduleFlush();
        return result;
    }

    bool HousingPlotComponent::queueRemoteEdit( const HousingEditRequest& request, HousingPlaceResult& outResult )
    {
        if ( _netRole != HousingNetRole::Client )
            return false;
        _outgoingEditBuffer.push( request );
        ++_pendingRequestCount;
        _lastResult = HousingPlaceResult::Pending;
        outResult   = HousingPlaceResult::Pending;
        return true;
    }

    HousingPlaceResult HousingPlotComponent::place( const HousingCandidate& candidate, uint32& outPlacementID )
    {
        outPlacementID = 0;
        HousingEditRequest request;
        request._op               = HousingEditOp::Place;
        request._candidate        = candidate;
        HousingPlaceResult result = HousingPlaceResult::Ok;
        if ( queueRemoteEdit( request, result ) )
            return result; // 새 id 는 서버가 정한다 — 방송된 상태로 온다
        return finishEdit( _layout.place( candidate, outPlacementID ) );
    }

    HousingPlaceResult HousingPlotComponent::remove( uint32 placementID )
    {
        HousingEditRequest request;
        request._op               = HousingEditOp::Remove;
        request._placementID      = placementID;
        HousingPlaceResult result = HousingPlaceResult::Ok;
        if ( queueRemoteEdit( request, result ) )
            return result;
        return finishEdit( _layout.remove( placementID ) );
    }

    HousingPlaceResult HousingPlotComponent::move( uint32 placementID, const HousingTransform& rawTransform, HousingPlacementMode mode )
    {
        HousingEditRequest request;
        request._op                   = HousingEditOp::Move;
        request._placementID          = placementID;
        request._candidate._transform = rawTransform;
        request._candidate._mode      = mode;
        HousingPlaceResult result     = HousingPlaceResult::Ok;
        if ( queueRemoteEdit( request, result ) )
            return result;
        return finishEdit( _layout.move( placementID, rawTransform, mode ) );
    }

    HousingPlaceResult HousingPlotComponent::undo()
    {
        HousingEditRequest request;
        request._op               = HousingEditOp::Undo;
        HousingPlaceResult result = HousingPlaceResult::Ok;
        if ( queueRemoteEdit( request, result ) )
            return result;
        return finishEdit( _layout.undo() );
    }

    HousingPlaceResult HousingPlotComponent::redo()
    {
        HousingEditRequest request;
        request._op               = HousingEditOp::Redo;
        HousingPlaceResult result = HousingPlaceResult::Ok;
        if ( queueRemoteEdit( request, result ) )
            return result;
        return finishEdit( _layout.redo() );
    }

    HousingPlaceResult HousingPlotComponent::claim()
    {
        HousingEditRequest request;
        request._op               = HousingEditOp::Claim;
        HousingPlaceResult result = HousingPlaceResult::Ok;
        if ( queueRemoteEdit( request, result ) )
            return result;
        if ( _netRole == HousingNetRole::Standalone )
            _ownerID = HousingOwnerID::kLocal; // 로컬 주체 — 세이브에 실린다
        _lastResult = _netRole == HousingNetRole::Standalone ? HousingPlaceResult::Ok : HousingPlaceResult::NotOwner;
        return _lastResult;
    }

    bool HousingPlotComponent::isOwnedLocally() const
    {
        switch ( _netRole )
        {
            case HousingNetRole::Standalone:
                return true;
            case HousingNetRole::Client:
                return _replicatedOwner == HousingPlotOwner::Self;
            case HousingNetRole::Server:
                return false; // 서버 월드에는 플레이어가 없다 — 주인은 연결이다
        }
        return false;
    }

    // ------------------------------------------------------------------------------
    // 네트워크 세션(틱 뒤 게임 스레드)
    // ------------------------------------------------------------------------------
    HousingNetPlotRef HousingPlotComponent::makeNetPlot()
    {
        HousingNetPlotRef plot;
        plot._pLayout = &_layout;
        plot._pWallet = &_wallet;
        plot._pOwner  = &_ownerID;
        plot._origin  = AbsolutePosition{ getOwner()->getManager()->getWorldOrigin().toAbsolute( getPlotOrigin() ) };
        return plot;
    }

    void HousingPlotComponent::takeOutgoingEdits( vector<HousingEditRequest>& outListRequest )
    {
        _outgoingEditBuffer.moveAllTo( outListRequest );
    }

    void HousingPlotComponent::receiveNetReply( HousingPlaceResult result )
    {
        _pendingRequestCount = _pendingRequestCount > 0 ? _pendingRequestCount - 1 : 0;
        (void)finishEdit( result );
    }

    void HousingPlotComponent::abandonNetRequest()
    {
        _pendingRequestCount = _pendingRequestCount > 0 ? _pendingRequestCount - 1 : 0;
    }

    void HousingPlotComponent::abandonNetRequests()
    {
        _outgoingEditBuffer.clear();
        _pendingRequestCount = 0;
    }

    // ------------------------------------------------------------------------------
    // 뷰(틱 뒤 게임 스레드)
    // ------------------------------------------------------------------------------
    bool HousingPlotComponent::hasPendingSpawn() const
    {
        return _viewRevision != _layout.getRevision() || _bGhostDirty == SW_TRUE;
    }

    void HousingPlotComponent::onViewsDespawned()
    {
        // 베이스가 세운 것을 모두 지웠다 — 핸들만 비우고, 다음 플러시가 지금 배치와 미리보기대로 다시 세운다.
        _listView.clear();
        _ghost       = GameObjectHandle{};
        _bGhostDirty = _bGhostShown;
    }

    void HousingPlotComponent::onFlush( GameObjectManager& manager, bool bRespawnViews )
    {
        if ( bRespawnViews )
            onViewsDespawned();
        syncPartViews( manager );
        syncGhost( manager );
    }

    void HousingPlotComponent::placeObject( GameObject& object, const HousingTransform& transform ) const
    {
        SceneComponent* pRoot = object.getPrimarySceneComponent();
        if ( pRoot == nullptr )
            return;
        pRoot->setWorldPosition( getPlotOrigin() + _layout.computeLocalPosition( transform ) );
        pRoot->setLocalRotation( float3{ 0.0f, transform._yaw, 0.0f } );
    }

    void HousingPlotComponent::syncPartViews( GameObjectManager& manager )
    {
        // 배치와 뷰는 모두 id 오름차순이다 — 한 번 훑어 사라진 · 옮긴 뷰를 지우고 없는 뷰를 세운다(대량 배치에서도 O(n)).
        _viewRevision                                 = _layout.getRevision();
        const vector<HousingPlacement>& listPlacement = _layout.getPlacements();
        vector<PartView>                listNext;
        listNext.reserve( listPlacement.size() );
        size_t viewIndex = 0;
        for ( const HousingPlacement& placement : listPlacement )
        {
            while ( viewIndex < _listView.size() && _listView[viewIndex]._placementID < placement._placementID )
                destroySpawned( manager, _listView[viewIndex++]._object );
            if ( viewIndex < _listView.size() && _listView[viewIndex]._placementID == placement._placementID )
            {
                PartView& view = _listView[viewIndex++];
                if ( view._transform == placement._transform && manager.resolveGameObject( view._object ) != nullptr )
                {
                    listNext.push_back( view );
                    continue;
                }
                destroySpawned( manager, view._object );
            }
            const HousingPartDef*                    pPart = _pCatalog->findPart( placement._partID );
            StringBuilder<constant::buffer::kMax256> nameBuilder;
            nameBuilder.appendFormat( "%#_Part%#", getOwner()->getName().c_str(), placement._placementID );
            const string name{ nameBuilder.view() };
            GameObject*  pObject = pPart != nullptr ? spawnPrefab( manager, pPart->_prefabPath.path(), name.c_str() ) : nullptr;
            if ( pObject == nullptr )
            {
                SW_LOG_ERROR_ID( "HousingPlot.PartPrefabNotSpawned", "Housing plot '%#': placement %# of part '%#' could not spawn its prefab", _settings._plotID.c_str(), placement._placementID,
                                 placement._partID.c_str() );
                continue;
            }
            placeObject( *pObject, placement._transform );
            PartView view;
            view._placementID = placement._placementID;
            view._transform   = placement._transform;
            view._object      = pObject->getHandle();
            listNext.push_back( view );
        }
        while ( viewIndex < _listView.size() )
            destroySpawned( manager, _listView[viewIndex++]._object );
        _listView = std::move( listNext );
    }

    void HousingPlotComponent::syncGhost( GameObjectManager& manager )
    {
        if ( _bGhostDirty == SW_FALSE )
            return;
        _bGhostDirty = SW_FALSE;
        destroySpawned( manager, _ghost );
        if ( _bGhostShown == SW_FALSE )
            return;
        const bool                               bPlaceable = _ghostCheck._result == HousingPlaceResult::Ok;
        StringBuilder<constant::buffer::kMax256> nameBuilder;
        nameBuilder.appendFormat( "%#_Ghost", getOwner()->getName().c_str() );
        const string    name{ nameBuilder.view() };
        GameObject*     pGhost = spawnPrefab( manager, bPlaceable ? _ghostPlaceablePrefab.path() : _ghostBlockedPrefab.path(), name.c_str() );
        SceneComponent* pRoot  = pGhost != nullptr ? pGhost->getPrimarySceneComponent() : nullptr;
        if ( pRoot == nullptr )
        {
            SW_LOG_ERROR_ID( "HousingPlot.GhostPrefabNotSpawned", "Housing plot '%#': the ghost prefab could not spawn or has no scene component", _settings._plotID.c_str() );
            return;
        }
        _ghost = pGhost->getHandle();
        placeObject( *pGhost, _ghostCheck._transform );
        // 고스트 상자는 부품의 점유 상자 크기다(모르는 부품이면 한 칸). 요는 루트에 들어가 상자도 함께 돈다.
        const HousingPartDef* pPart    = _pCatalog->findPart( _ghostCandidate._partID );
        const float32         cellSize = _pCatalog->getRoot()._cellSize;
        const float3          size     = pPart != nullptr ? float3{ static_cast<float32>( pPart->_sizeX ) * cellSize, pPart->_height, static_cast<float32>( pPart->_sizeZ ) * cellSize }
                                                          : float3{ cellSize, cellSize, cellSize };
        pRoot->setLocalScale( size );
    }

    // ------------------------------------------------------------------------------
    // 에디터 저작 검증(맵 검사)
    // ------------------------------------------------------------------------------
    void HousingPlotComponent::validatePlot( ValidationContext& context ) const
    {
        using Internal = HousingPlotInternal;
        if ( _ghostPlaceablePrefab.empty() || _ghostBlockedPrefab.empty() )
            context.addError( "both ghost prefabs are required" );
        if ( _partCatalog.empty() )
        {
            context.addError( "no part catalog" );
            return;
        }
        const shared_ptr<const HousingPartCatalog> pCatalog = _partCatalog.acquire().snapshot();
        if ( pCatalog == nullptr )
        {
            StringBuilder<constant::buffer::kMax256> textBuilder;
            textBuilder.appendFormat( "part catalog '%#' cannot be loaded", _partCatalog.path() );
            context.addError( textBuilder.view() );
            return;
        }
        // 스냅 지점 · 판: 카탈로그 읽기와 같은 검사다(읽기를 지난 카탈로그는 비어 있다 — 핫 리로드 전 편집 사본을 보는 길이 같다).
        vector<string> listProblem;
        for ( const HousingPartDef& part : pCatalog->getParts() )
        {
            (void)pCatalog->validatePart( part, listProblem );
            const bool bFits        = part._sizeX <= _settings._sizeX && part._sizeZ <= _settings._sizeZ;
            const bool bFitsRotated = part._sizeZ <= _settings._sizeX && part._sizeX <= _settings._sizeZ;
            if ( bFits == false && bFitsRotated == false )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "part '%#' (%#x%#) never fits this %#x%# plot", part._id.c_str(), part._sizeX, part._sizeZ, _settings._sizeX, _settings._sizeZ );
                listProblem.push_back( string( textBuilder.view() ) );
            }
        }
        for ( const string& problem : listProblem )
            context.addError( problem );
        // 부지 경계: 같은 씬의 다른 부지와 겹치거나 같은 id 를 쓰면 세이브가 남의 부지에 들어간다.
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        if ( pOwner->getPrimarySceneComponent() == nullptr )
        {
            context.addError( "the plot object needs a scene component - its position is the plot origin" );
            return;
        }
        const GameObjectManager* pManager = pOwner->getManager();
        if ( pManager == nullptr )
            return;
        const float32 cellSize = pCatalog->getRoot()._cellSize;
        const float3  origin   = getPlotOrigin();
        for ( const HousingPlotComponent* pOther : pManager->getComponentRegistry().getAll<HousingPlotComponent>() )
        {
            const GameObject* pOtherOwner = pOther->getOwner();
            if ( pOther == this || pOtherOwner == nullptr || pOtherOwner->getPrimarySceneComponent() == nullptr )
                continue; // 루트가 없는 부지는 제 검증이 알린다
            if ( pOther->_settings._plotID == _settings._plotID )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder2;
                textBuilder2.appendFormat( "plot id '%#' is also used by another plot in this scene", _settings._plotID.c_str() );
                context.addError( textBuilder2.view() );
            }
            const float3 otherOrigin = pOther->getPlotOrigin();
            const bool   bSeparateX  = origin._x + static_cast<float32>( _settings._sizeX ) * cellSize <= otherOrigin._x + Internal::kTouchEpsilon ||
                                    otherOrigin._x + static_cast<float32>( pOther->_settings._sizeX ) * cellSize <= origin._x + Internal::kTouchEpsilon;
            const bool bSeparateZ = origin._z + static_cast<float32>( _settings._sizeZ ) * cellSize <= otherOrigin._z + Internal::kTouchEpsilon ||
                                    otherOrigin._z + static_cast<float32>( pOther->_settings._sizeZ ) * cellSize <= origin._z + Internal::kTouchEpsilon;
            if ( bSeparateX == false && bSeparateZ == false )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "plot '%#' overlaps plot '%#'", _settings._plotID.c_str(), pOther->_settings._plotID.c_str() );
                context.addError( textBuilder.view() );
            }
        }
    }
} // namespace sw
