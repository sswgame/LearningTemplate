#include "pch.h"

#include "Engine/Visual/Component/MeshComponent.h"

#include "Core/Math/AABB.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Asset/AssetManager.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Foundation/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/Mesh/MeshUtil.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/Registry/CameraRegistry.h"
#include "Engine/Object/GameObject/Registry/PrimitiveRegistry.h"
#include "Engine/Visual/Component/MeshSectionComponent.h"

namespace sw
{
    MeshComponent::MeshComponent()
        : _mesh{}
        , _meshLoad{}
        , _pMaterial{ nullptr }
        , _materialInstance{}
        , _meshID{}
        , _materialPath{}
        , _acquiredMaterialPath{}
        , _requestedMaterialPath{}
        , _resolvedMeshID{}
        , _boundsRadius{ 0.866f }
        , _gpuSpinSeed{ 0 }
        , _sectionName{}
        , _spriteFrame{}
        , _customData0{}
        , _customData1{}
        , _vertexAnimationPhase{ 0.0f }
        , _sortKey{ 0 }
        , _highlight{}
        , _appearSeconds{ kDefaultAppearSeconds }
        , _appearance{ 1.0f }
        , _appearanceRate{ 0.0f }
        , _bTeleportAppearPending{ false }
        , _motionTeleportSerial{ 0 }
        , _lodIndex{ 0 }
        , _pPrimitiveRegistry{ nullptr }
        , _bVisible{ SW_TRUE }
        , _bFirstPersonView{ SW_FALSE }
        , _bAwaitingAppearance{ SW_FALSE }
        , _bAppearOnTeleport{ SW_FALSE }
        , _bStatic{ SW_FALSE }
        , _reserved{ 0 }
    {
        // 월드가 바뀌면 트랜스폼 칸의 프리미티브 번호로 렌더 더티가 찍힌다(`ScenePrimitive::setPrimitiveIndex`). 훅은 받지 않는다.
        setWorldTransformNotify( false );
    }

    void MeshComponent::onBeginPlay()
    {
        ScenePrimitive::onBeginPlay();
        resolveRenderAssets();
    }

    void MeshComponent::resolveRenderAssets()
    {
        resolveRuntimeMesh();
        resolveMaterialAsset();
    }

    void MeshComponent::setMaterialPath( string_view path )
    {
        _materialPath = path;
        resolveRenderAssets();
    }

    void MeshComponent::resolveMaterialAsset()
    {
        const hashed_string path = _materialPath.empty() ? getDefaultMaterialPath() : hashed_string( _materialPath.c_str() );
        if ( path == _requestedMaterialPath || engine::areEngineServicesBound() == false )
            return;
        _requestedMaterialPath = path;

        MaterialCache& cache     = engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>();
        Material*      pMaterial = nullptr;
        hashed_string  borrowedPath{};
        if ( path.empty() == false )
        {
            // 디바이스 없이 잡는 acquire 는 파일을 읽지 않아 늘 성공한다 — 없는 파일은 여기서 가린다.
            pMaterial    = AssetIO::hasAsset( AssetID{ path.viewOrEmpty() } ) ? cache.acquire( path.viewOrEmpty() ) : nullptr;
            borrowedPath = path;
            if ( pMaterial == nullptr )
            {
                // 못 읽은 머티리얼은 마젠타 체커로 보인다 — 씬 기본(흰색)이면 화면에서 빠진 것을 알 수 없다. 대체를 정하는 곳은 여기 한 곳이다.
                // 파일은 있는데 내용이 틀린 머티리얼은 GPU 에 올릴 때(`Material::initialize`) 실패해 그려지지 않고 오류가 남는다.
                const string& missingPath = engine::getSubsystem<EngineDefaultAssets>()._missingMaterial.get();
                SW_LOG_ERROR_ID( "MeshComponent.MaterialCouldNotAcquired", "Material '%#' could not be acquired - the mesh uses the missing-material checker '%#'", path.c_str(), missingPath.c_str() );
                borrowedPath = hashed_string( missingPath.c_str() );
                pMaterial    = cache.acquire( borrowedPath.viewOrEmpty() );
            }
            if ( pMaterial != nullptr )
                cache.requestInitialize( borrowedPath.viewOrEmpty() );
        }

        const hashed_string previousPath = _acquiredMaterialPath;
        _acquiredMaterialPath            = ( pMaterial != nullptr ) ? borrowedPath : hashed_string{};
        setMaterial( pMaterial );
        if ( previousPath.empty() == false )
            cache.release( previousPath.viewOrEmpty() );
    }

    void MeshComponent::resolveRuntimeMesh()
    {
        const string_view   meshID = _meshID.empty() ? getDefaultMeshID() : string_view{ _meshID };
        const hashed_string hashedMeshID( string{ meshID }.c_str() );
        const bool          bSameID = _resolvedMeshID == hashedMeshID;
        if ( bSameID && ( _mesh != nullptr || _meshLoad.isLoading() ) )
            return;
        _resolvedMeshID = hashedMeshID;
        // **공유되는** 메시를 받는다(`.mesh` 경로는 메시 캐시, 그 밖은 내장 도형). 컴포넌트마다 제 메시를 만들면 배치가 그만큼 갈린다.
        if ( MeshAssetFormat::isMeshAssetPath( meshID ) == false )
        {
            _meshLoad.reset();
            _mesh = MeshUtil::acquirePrimitive( meshID );
            onMeshAssigned();
            notifyMeshFollowers();
            return;
        }
        // 에셋은 워커에서 읽는다. 다 읽을 때까지 그리지 않고, 도착하면 `AssetArrival` 단계가 넣는다. 읽지 못한 에셋은 메시 없이 남는다(그리지 않는다) —
        // 캐시가 경로마다 한 번 경고하고, 핫 리로드 알림이 다시 해석한다.
        _meshLoad = MeshCache::request( meshID );
        if ( _meshLoad.isLoading() == false )
        {
            applyMeshLoad();
            return;
        }
        _mesh = nullptr;
        onMeshAssigned();
        notifyMeshFollowers();
        if ( _pPrimitiveRegistry != nullptr )
            _pPrimitiveRegistry->addAssetWaiter( this, &MeshComponent::updateArrival );
    }

    bool MeshComponent::updateArrival( Component& component, float32 deltaSeconds )
    {
        // 이 함수를 거는 것은 메시 컴포넌트 자신뿐이다(`addAssetWaiter( this, &MeshComponent::updateArrival )`).
        MeshComponent& self = static_cast<MeshComponent&>( component );
        if ( self._bTeleportAppearPending.exchange( false, std::memory_order_acq_rel ) )
            self.beginAppearance();
        if ( self._meshLoad.isLoading() )
            return true;
        if ( self._meshLoad.isValid() )
        {
            self.applyMeshLoad();
            // 도착한 메시로 렌더 에셋을 다시 푼다 — 파생 타입(스켈레탈)이 스킨 사본을 이때 만든다. 메시 id 는 그대로라 메시를 다시 요청하지 않는다.
            // 실패한 로드는 다시 풀지 않는다 — 메시가 없으면 다시 요청하므로 프레임마다 디스크를 읽게 된다(다음 해석 · 핫 리로드가 다시 시도한다).
            if ( self._mesh != nullptr )
            {
                self.resolveRenderAssets();
                // 로드 뒤에 나타나는 메시는 한 프레임에 튀지 않고 서서히 나타난다(디더 등장). 준비가 더 필요하면(캐릭터의 애니메이션) 그때까지 숨는다.
                self.holdAppearanceUntilReady();
            }
            if ( self._meshLoad.isLoading() )
                return true;
        }
        return self.advanceAppearance( deltaSeconds );
    }

    uint64 MeshComponent::getMotionHistoryKey() const
    {
        constexpr uint64 kSerialMask = 0xFFFFu; // 차례는 감겨도 된다 — 열쇠는 지난 한 프레임과만 견준다
        return ( getComponentID() << 16 ) | ( static_cast<uint64>( _motionTeleportSerial.load( std::memory_order_relaxed ) ) & kSerialMask );
    }

    void MeshComponent::onTeleported()
    {
        ScenePrimitive::onTeleported();
        // 화면 속도의 기록을 버린다 — 열쇠가 바뀌어 렌더 쪽이 지난 행렬을 찾지 못한다. 트랜스폼만 바뀐 프레임은 후보를 다시 채우지 않으므로 렌더 상태 더티로 열쇠를 다시 읽게 한다.
        _motionTeleportSerial.fetch_add( 1u, std::memory_order_relaxed );
        markRenderStateDirty();
        if ( _bAppearOnTeleport == SW_FALSE || _pPrimitiveRegistry == nullptr )
            return;
        // 틱 중(워커)에도 불린다 — 표시만 하고(원자값) 등장은 씬 프레임의 도착 단계가 시작한다. 거는 것은 레지스트리가 잠근다.
        _bTeleportAppearPending.store( true, std::memory_order_release );
        _pPrimitiveRegistry->addAssetWaiter( this, &MeshComponent::updateArrival );
    }

    void MeshComponent::beginAppearance()
    {
        _bAwaitingAppearance = SW_FALSE;
        startAppearanceRamp( 1.0f );
    }

    void MeshComponent::beginDisappearance()
    {
        _bAwaitingAppearance = SW_FALSE;
        startAppearanceRamp( -1.0f );
    }

    void MeshComponent::holdAppearanceUntilReady()
    {
        _appearance          = 0.0f;
        _appearanceRate      = 0.0f;
        _bAwaitingAppearance = SW_TRUE;
        markRenderStateDirty();
        if ( _pPrimitiveRegistry != nullptr )
            _pPrimitiveRegistry->addAssetWaiter( this, &MeshComponent::updateArrival );
    }

    void MeshComponent::startAppearanceRamp( float32 direction )
    {
        // 시간이 0 이면 바로 끝 값이다(등장을 끈 물체 — 데이터 `_appearSeconds`). 플레이 전 씬(에디터 편집 · 로드 중)도 그렇다 — 씬 시간이 흐르지 않아(편집 모드는
        // 경과 0) 램프가 끝나지 않고 메시가 0 에 머문다(유니티 편집 모드 · 언리얼 에디터 월드도 등장 페이드를 하지 않는다).
        const GameObject* const pOwner   = getOwner();
        const bool              bPlaying = pOwner != nullptr && pOwner->getManager() != nullptr && pOwner->getManager()->hasBegunPlay();
        if ( _appearSeconds <= 0.0f || bPlaying == false )
        {
            _appearance     = direction > 0.0f ? 1.0f : 0.0f;
            _appearanceRate = 0.0f;
            markRenderStateDirty();
            return;
        }
        if ( direction > 0.0f && _appearance >= 1.0f )
            _appearance = 0.0f; // 다 보이던 것을 다시 나타나게 한다(텔레포트)
        _appearanceRate = direction / _appearSeconds;
        markRenderStateDirty();
        if ( _pPrimitiveRegistry != nullptr )
            _pPrimitiveRegistry->addAssetWaiter( this, &MeshComponent::updateArrival );
    }

    bool MeshComponent::advanceAppearance( float32 deltaSeconds )
    {
        if ( _bAwaitingAppearance == SW_TRUE )
        {
            if ( pollReadyToAppear() == false )
                return true;
            beginAppearance();
        }
        if ( _appearanceRate == 0.0f )
            return false;
        markRenderStateDirty();
        return stepAppearanceRamp( _appearance, _appearanceRate, deltaSeconds );
    }

    bool MeshComponent::stepAppearanceRamp( float32& inoutAppearance, float32& inoutRate, float32 deltaSeconds )
    {
        if ( inoutRate == 0.0f )
            return false;
        inoutAppearance = MathUtil::clamp( inoutAppearance + inoutRate * deltaSeconds, 0.0f, 1.0f );
        // 끝은 램프 방향의 끝 값이다. 방향 없이 "0 이하 또는 1 이상" 으로 보면 0 에서 오르기 시작한 램프가 시간 0 인 걸음(텔레포트 직후 · 멈춘 프레임)에서
        // 바로 끝나 등장 비율 0 으로 굳었다 — 플레이어 몸이 그림자만 남고 보이지 않았다.
        const bool bRampDone = inoutRate > 0.0f ? inoutAppearance >= 1.0f : inoutAppearance <= 0.0f;
        if ( bRampDone )
            inoutRate = 0.0f;
        return bRampDone == false;
    }

    void MeshComponent::applyMeshLoad()
    {
        _mesh = _meshLoad.get();
        _meshLoad.reset();
        onMeshAssigned();
        notifyMeshFollowers();
    }

    void MeshComponent::onMeshAssigned()
    {
        validateSectionName();
        _lodIndex           = 0;
        const bool bHasLODs = _mesh != nullptr && _mesh->getLODCount() > 1;
        if ( _pPrimitiveRegistry != nullptr )
        {
            if ( bHasLODs )
                _pPrimitiveRegistry->addLODPrimitive( this );
            else
                _pPrimitiveRegistry->removeLODPrimitive( this );
        }
        markRenderStateDirty();
    }

    void MeshComponent::setLODIndex( uint32 lodIndex )
    {
        const uint32 lastIndex = _mesh != nullptr ? _mesh->getLODCount() - 1 : 0u;
        const uint8  clamped   = static_cast<uint8>( MathUtil::min( lodIndex, lastIndex ) );
        if ( _lodIndex == clamped )
            return;
        _lodIndex = clamped;
        markRenderStateDirty();
        // 구간 컴포넌트는 주 메시와 같은 단계를 그린다 — 스킨 결과는 칸마다 고른 단계 하나만 변형하므로 단계가 갈리면 구간이 변형되지 않은 정점을 읽는다.
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr || castTo<MeshSectionComponent>( this ) != nullptr )
            return;
        pOwner->forEachComponentOfType<MeshSectionComponent>( [clamped]( MeshSectionComponent* pSection )
        { pSection->setLODIndex( clamped ); } );
    }

    void MeshComponent::setSectionName( hashed_string sectionName )
    {
        _sectionName = sectionName;
        validateSectionName();
        markRenderStateDirty();
    }

    bool MeshComponent::findDrawSection( uint32& outFirstIndex, uint32& outIndexCount ) const
    {
        outFirstIndex         = 0;
        outIndexCount         = 0;
        const size_t lodCount = _mesh != nullptr ? _mesh->getLODs().size() : 0u;
        const size_t lodSlot  = MathUtil::min( static_cast<size_t>( _lodIndex ), lodCount ); // 메시가 제자리에서 다시 읽혀 단계가 줄었으면 가장 낮은 단계를 그린다
        if ( _sectionName.empty() )
        {
            // 스킨드 메시의 GPU 스키닝(모프 풀의 스킨 구간)은 LOD 0 뒤의 낮은 단계 정점까지 변형하므로(`GPUMeshMorphPool::rebuildSkinSources`) 같은 범위로 그린다.
            if ( lodSlot > 0 )
            {
                const MeshLOD& lod = _mesh->getLODs()[lodSlot - 1];
                outFirstIndex      = lod._firstIndex;
                outIndexCount      = lod._indexCount;
            }
            return true;
        }
        const optional<uint32> sectionIndex = ( _mesh != nullptr ) ? _mesh->findSectionIndex( _sectionName ) : nullopt;
        if ( sectionIndex.has_value() == false )
            return false;
        // 낮은 단계면 그 단계의 구간 범위(`Mesh::setLODs` 가 구간마다 범위를 받았다).
        const MeshSection&         section = _mesh->getSections()[*sectionIndex];
        const MeshLODSectionRange* pLower  = lodSlot > 0 ? &_mesh->getLODs()[lodSlot - 1]._listSectionRange[*sectionIndex] : nullptr;
        outFirstIndex                      = pLower != nullptr ? pLower->_firstIndex : section._firstIndex;
        outIndexCount                      = pLower != nullptr ? pLower->_indexCount : section._indexCount;
        return outIndexCount > 0; // 빈 구간(조립이 덮인 삼각형을 모두 뺐다)은 그리지 않는다 — (0, 0) 은 "메시 전체" 라 넘기면 안 된다
    }

    void MeshComponent::validateSectionName() const
    {
        const bool bResolvable = _sectionName.empty() || _mesh == nullptr || _mesh->findSectionIndex( _sectionName ).has_value();
        if ( bResolvable )
            return;
        SW_LOG_ERROR_ID( "MeshComponent.MeshNoSectionComponent", "Mesh '%#' has no section '%#' - the component draws nothing", _meshID.c_str(), _sectionName.c_str() );
    }

    void MeshComponent::setFirstPersonView( bool bFirstPerson )
    {
        const uint8 firstPersonFlag = bFirstPerson ? SW_TRUE : SW_FALSE;
        if ( _bFirstPersonView == firstPersonFlag )
            return;
        _bFirstPersonView = firstPersonFlag;
        markRenderStateDirty();
    }

    void MeshComponent::setFirstPersonViewOnObject( const GameObject& owner, bool bFirstPerson )
    {
        owner.forEachComponentOfType<MeshComponent>( [bFirstPerson]( MeshComponent* pMesh )
        { pMesh->setFirstPersonView( bFirstPerson ); } );
    }

    bool MeshComponent::isVisibleInCurrentPerson() const
    {
        const optional<uint32> sectionIndex = ( _mesh != nullptr && _sectionName.empty() == false ) ? _mesh->findSectionIndex( _sectionName ) : nullopt;
        if ( sectionIndex.has_value() == false )
            return true;
        const MeshPersonVisibility visibility = _mesh->getSections()[*sectionIndex]._personVisibility;
        if ( visibility == MeshPersonVisibility::ThirdPersonOnly )
            return _bFirstPersonView == SW_FALSE;
        if ( visibility == MeshPersonVisibility::FirstPersonOnly )
            return _bFirstPersonView == SW_TRUE;
        return true;
    }

    void MeshComponent::notifyMeshFollowers()
    {
        const GameObject* pOwner = getOwner();
        if ( pOwner == nullptr )
            return;
        // 구간 컴포넌트는 이 오브젝트의 주 메시 컴포넌트의 메시 객체를 그린다(MeshSectionComponent::resolveRenderAssets) — 메시가 바뀌면 다시 따라간다.
        pOwner->forEachComponentOfType<MeshSectionComponent>( [this]( MeshSectionComponent* pSection )
        {
            if ( pSection != this )
                pSection->followSourceMesh();
        } );
    }

    void MeshComponent::onRegister( GameObjectManager& manager )
    {
        ScenePrimitive::onRegister( manager );
        _pPrimitiveRegistry = &manager.getPrimitiveRegistry();
        _pPrimitiveRegistry->add( this );
        // 등록 전에 받은 메시가 LOD 를 가졌으면 이제부터 LOD 고르기가 본다.
        if ( _mesh != nullptr && _mesh->getLODCount() > 1 )
            _pPrimitiveRegistry->addLODPrimitive( this );
        // 등록 전에 요청한 메시 로드가 아직 끝나지 않았으면 이제부터 씬 프레임이 기다린다.
        if ( _meshLoad.isLoading() )
            _pPrimitiveRegistry->addAssetWaiter( this, &MeshComponent::updateArrival );
        else if ( _bAwaitingAppearance == SW_TRUE || _appearanceRate != 0.0f )
            _pPrimitiveRegistry->addAssetWaiter( this, &MeshComponent::updateArrival ); // 등록 전에 시작한 등장 · 대기도 이제부터 씬 프레임이 진행한다
        // 픽셀 퍼펙트 카메라가 이미 정한 스냅 단위를 이어받는다(정한 뒤에 생긴 스프라이트도 격자에 붙는다).
        setPixelSnapUnit( manager.getCameraRegistry().getPixelSnapUnit() );
    }

    void MeshComponent::onUnregister( GameObjectManager& manager )
    {
        manager.getPrimitiveRegistry().remove( this );
        _pPrimitiveRegistry = nullptr;
        if ( _acquiredMaterialPath.empty() == false )
        {
            _pMaterial = nullptr;
            if ( engine::areEngineServicesBound() )
                engine::getSubsystem<AssetManager>().getAssetCache<MaterialCache>().release( _acquiredMaterialPath.viewOrEmpty() );
            _acquiredMaterialPath = hashed_string{};
        }
        _requestedMaterialPath = hashed_string{};
        ScenePrimitive::onUnregister( manager );
    }

    void MeshComponent::markRenderStateDirty()
    {
        if ( _pPrimitiveRegistry != nullptr )
            _pPrimitiveRegistry->markDirty( this );
    }

    void MeshComponent::onPropertyChanged( hashed_string propertyName )
    {
        // 트랜스폼 PROPERTY 는 여기서 markTransformDirty 로 이어지고, 그 결과 월드 행렬이 다시
        // 계산될 때 트랜스폼 칸의 프리미티브 번호로 렌더 더티가 찍힌다. 그래서 여기서는 렌더 관련
        // PROPERTY 만 보면 된다. 어느 쪽이든 빠지는 경로가 없다.
        ScenePrimitive::onPropertyChanged( propertyName );
        static const hashed_string s_materialPathName( "_materialPath" );
        static const hashed_string s_meshIDName( "_meshID" );
        static const hashed_string s_sectionName( "_sectionName" );
        if ( propertyName == s_materialPathName || propertyName == s_meshIDName )
            resolveRenderAssets();
        static const hashed_string s_staticName( "_bStatic" );
        if ( propertyName == s_sectionName )
            validateSectionName();
        if ( propertyName == s_staticName && _pPrimitiveRegistry != nullptr )
            _pPrimitiveRegistry->markSetDirty(); // setStatic 과 같은 까닭
        markRenderStateDirty();
    }

    void MeshComponent::onOwnerActiveInHierarchyChanged()
    {
        // 부분 수집은 더티 칸의 포함 여부가 바뀐 것을 보고 전체 수집으로 넘어간다(`setVisible` 과 같은 길).
        markRenderStateDirty();
    }

    void MeshComponent::setMesh( shared_ptr<Mesh> mesh )
    {
        _mesh = std::move( mesh );
        _meshLoad.reset(); // 런타임 지정이 이긴다 — 읽던 에셋이 도착해도 덮지 않는다
        // 지금 id 의 메시로 삼는다 — 시작 · 로드 뒤 해석이 런타임 지정을 덮지 않고, id 가 바뀌면 그 id 의 것으로 바뀐다.
        _resolvedMeshID = hashed_string( string{ _meshID.empty() ? getDefaultMeshID() : string_view{ _meshID } }.c_str() );
        onMeshAssigned();
        notifyMeshFollowers();
    }

    void MeshComponent::assignMeshWithoutFollowers( shared_ptr<Mesh> mesh )
    {
        if ( _mesh == mesh )
            return;
        _mesh = std::move( mesh );
        onMeshAssigned();
    }

    void MeshComponent::setMeshID( string_view meshID )
    {
        _meshID = meshID;
        resolveRuntimeMesh();
    }

    void MeshComponent::setMaterial( Material* pMaterial )
    {
        _pMaterial = pMaterial;
        markRenderStateDirty();
    }

    void MeshComponent::setMaterialInstance( shared_ptr<MaterialInstance> instance )
    {
        _materialInstance = std::move( instance );
        markRenderStateDirty();
    }

    void MeshComponent::setSpriteFrame( const float4& uvRect, const float4& tint )
    {
        // 프레임 · 색을 바꾸는 쪽은 스냅 단위를 모른다 — 지금 값을 지킨다.
        InstanceSpriteFrame merged = _spriteFrame;
        merged._uvRect             = uvRect;
        merged._tint               = tint;
        if ( _spriteFrame == merged )
            return;
        _spriteFrame = merged;
        markRenderStateDirty();
    }

    void MeshComponent::setCustomData( uint32 index, const float4& value )
    {
        if ( index >= InstanceCustomData::kCount )
        {
            SW_LOG_ERROR_ID( "MeshComponent.CustomDataIndex", "Custom data index %# is out of range (%# slots)", index, InstanceCustomData::kCount );
            return;
        }
        float4& slot = index == 0u ? _customData0 : _customData1;
        if ( Memory::compare( &slot, &value, sizeof( float4 ) ) == 0 )
            return;
        slot = value;
        markRenderStateDirty();
    }

    InstanceCustomData MeshComponent::getInstanceCustomData() const
    {
        InstanceCustomData data{};
        data._arrValue[0] = _customData0;
        data._arrValue[1] = _customData1;
        return data;
    }

    void MeshComponent::setPixelSnapUnit( float32 unit )
    {
        InstanceSpriteFrame snapped = _spriteFrame;
        snapped._pixelSnap          = MathUtil::max( unit, 0.0f );
        if ( _spriteFrame == snapped )
            return;
        _spriteFrame = snapped;
        markRenderStateDirty();
    }

    void MeshComponent::setHighlight( InstanceHighlightStyle style, const float3& color )
    {
        const InstanceHighlight highlight{ style, color };
        if ( _highlight == highlight )
            return;
        _highlight = highlight;
        markRenderStateDirty();
    }

    void MeshComponent::setSortKey( uint32 sortKey )
    {
        if ( _sortKey == sortKey )
            return;
        _sortKey = sortKey;
        markRenderStateDirty();
    }

    void MeshComponent::setStatic( bool bStatic )
    {
        const uint8 value = bStatic ? SW_TRUE : SW_FALSE;
        if ( _bStatic == value )
            return;
        _bStatic = value;
        markRenderStateDirty();
        // 정적 집합이 바뀌었다 — 빌더가 정적 그림자 세대를 다시 잰다(움직이는 쪽으로 바뀐 메시는 더티 슬롯만으로는 보이지 않는다).
        if ( _pPrimitiveRegistry != nullptr )
            _pPrimitiveRegistry->markSetDirty();
    }

    bool MeshComponent::getWorldBounds( float3& outCenter, float32& outRadius ) const
    {
        const float4x4 world = getWorldMatrix();
        outCenter            = float3::transform( getLocalBoundsCenter(), world ); // 구의 중심은 메시 상자 중심이다(`Mesh::getBoundingRadius`)
        outRadius            = getBoundsRadius() * world.getMaximumAxisScale();    // GPU 컬링(`GPUSceneBuilder`)과 같은 구
        return true;
    }

    bool MeshComponent::getWorldBox( AABB& outBox ) const
    {
        const AABB local = ( _mesh != nullptr && _mesh->getVertexCount() > 0 ) ? AABB{
                                                                                     _mesh->getLocalBoundsMin(), _mesh->getLocalBoundsMax()
        }
                                                                               : AABB{ float3{ -0.5f, -0.5f, -0.5f }, float3{ 0.5f, 0.5f, 0.5f } };
        outBox           = local.transformedBy( getWorldMatrix() );
        return true;
    }

    float3 MeshComponent::getLocalBoundsCenter() const
    {
        return ( _mesh != nullptr ) ? _mesh->getLocalBoundsCenter() : float3{};
    }

    float32 MeshComponent::getBoundsRadius() const
    {
        // 메시가 아는 경계를 덮는다 — 적어 둔 값은 키우기만 한다(정점 애니메이션 · 셰이더 변형의 여유). 적어 둔 값(기본 단위 상자 0.866)
        // 만 쓰면 그보다 큰 도형(캡슐 끝 · 평면)이 보이는데도 컬링에 잘린다.
        const float32 meshRadius = ( _mesh != nullptr ) ? _mesh->getBoundingRadius() : 0.0f;
        return MathUtil::max( _boundsRadius, meshRadius );
    }

    void MeshComponent::setBoundsRadius( float32 radius )
    {
        _boundsRadius = radius;
        markRenderStateDirty();
    }

    void MeshComponent::setGPUSpinSeed( uint32 seed )
    {
        _gpuSpinSeed = seed;
        markRenderStateDirty();
    }

    void MeshComponent::setVertexAnimationPhase( float32 phaseSeconds )
    {
        if ( Memory::compare( &_vertexAnimationPhase, &phaseSeconds, sizeof( phaseSeconds ) ) == 0 )
            return;
        _vertexAnimationPhase = phaseSeconds;
        markRenderStateDirty();
    }

    void MeshComponent::setVisible( bool bVisible )
    {
        _bVisible = bVisible ? SW_TRUE : SW_FALSE;
        markRenderStateDirty();
    }

} // namespace sw
