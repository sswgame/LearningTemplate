#include "pch.h"

#include "Engine/Renderer/Scene/GPUMaterialRegistry.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/IndexFreeList.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialCache.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Profiling/FrameProfiler.h"
#include "Engine/Renderer/Pipeline/RenderPassKindInfo.h"

namespace sw
{
    uint64 GPUMaterialRegistry::computePermutationHashFor( const Material* pMaterial, const MaterialInstance* pInstance, bool bAppearanceDither )
    {
        if ( pMaterial == nullptr )
            return 0;
        // 인스턴스가 있으면 그 해시를 쓴다. 키워드 오버라이드가 퍼뮤테이션을 바꾸고, 그 해시는 부모의 것을 이미 포함한다.
        const uint64 defineHash = ( pInstance != nullptr ) ? pInstance->getPermutationHash() : pMaterial->getPermutationHash();
        return combinePermutationHash( pMaterial->getShaderPathHash(), defineHash, bAppearanceDither );
    }

    uint64 GPUMaterialRegistry::combinePermutationHash( uint64 shaderPathHash, uint64 defineHash, bool bAppearanceDither )
    {
        uint64 hash = HashUtil::combine( shaderPathHash, defineHash );
        if ( bAppearanceDither )
            hash = HashUtil::combine( hash, hashed_string( kAppearanceDitherDefine ).getHash() );
        return hash;
    }

    GPUShaderPermutation GPUMaterialRegistry::makeShaderPermutation( const MaterialPSOWarmEntry& entry, bool bAppearanceDither )
    {
        GPUShaderPermutation permutation{};
        permutation._shaderPath          = entry._shaderPath;
        permutation._listDefine          = entry._listDefine;
        permutation._renderStateOverride = entry._renderStateOverride;
        if ( bAppearanceDither )
            permutation._listDefine.push_back( string( kAppearanceDitherDefine ) ); // 머티리얼에서 만드는 길(아래)과 같은 자리에 붙인다
        permutation._hash = combinePermutationHash( entry._shaderPathHash, entry._permutationHash, bAppearanceDither );
        return permutation;
    }

    GPUShaderPermutation GPUMaterialRegistry::makeShaderPermutation( const Material& material, const MaterialInstance* pInstance, bool bAppearanceDither )
    {
        GPUShaderPermutation permutation{};
        permutation._shaderPath          = material.getShaderPath();
        permutation._listDefine          = ( pInstance != nullptr ) ? pInstance->getCachedShaderDefines() : material.getCachedShaderDefines();
        permutation._renderStateOverride = material.getRenderStateOverride(); // 인스턴스는 렌더 상태를 덮지 않는다(`MaterialInstance` 설명)
        if ( bAppearanceDither )
            permutation._listDefine.push_back( string( kAppearanceDitherDefine ) ); // 쿠커도 머티리얼 define 끝에 붙인다(`ShaderCookRequest`)
        permutation._hash = computePermutationHashFor( &material, pInstance, bAppearanceDither );
        return permutation;
    }

    void GPUMaterialRegistry::reset( GPUSceneSnapshot& inoutSnapshot )
    {
        if ( inoutSnapshot.getMaterialGroups().empty() == false )
            inoutSnapshot.editMaterialGroups().clear();
        _listMaterialGroupState.clear();
        _mapShaderPathToGroup.clear();
        inoutSnapshot._pListShaderPermutation.reset();
        _mapPermutationToIndex.clear();
        _mapOwnerToElement.clear();
        _listNewElement.clear();
        _bAllElementsStale = SW_TRUE;
    }

    uint32 GPUMaterialRegistry::getOrCreateMaterialGroup( const Material* pMaterial, GPUSceneSnapshot& inoutSnapshot )
    {
        if ( pMaterial == nullptr )
            return kInvalidMaterialGroup;
        const string& shaderPath = pMaterial->getShaderPath();
        const auto    it         = _mapShaderPathToGroup.find( shaderPath );
        if ( it != _mapShaderPathToGroup.end() )
            return it->second;

        GPUMaterialGroup group{};
        group._shaderPath                   = shaderPath;
        vector<GPUMaterialGroup>& listGroup = inoutSnapshot.editMaterialGroups();
        listGroup.push_back( std::move( group ) );
        _listMaterialGroupState.emplace_back();
        const uint32 groupIndex = static_cast<uint32>( listGroup.size() - 1 );
        _mapShaderPathToGroup.emplace( shaderPath, groupIndex );
        return groupIndex;
    }

    uint32 GPUMaterialRegistry::getShaderPermutationFor( const Material* pMaterial, const MaterialInstance* pInstance, bool bAppearanceDither, GPUSceneSnapshot& inoutSnapshot )
    {
        if ( pMaterial == nullptr )
            return kInvalidShaderPermutation;
        const uint64 hash = computePermutationHashFor( pMaterial, pInstance, bAppearanceDither );
        const auto   it   = _mapPermutationToIndex.find( hash );
        if ( it != _mapPermutationToIndex.end() )
            return it->second;

        GPUShaderPermutation permutation        = makeShaderPermutation( *pMaterial, pInstance, bAppearanceDither );
        const uint64         previousDefineHash = ( pInstance != nullptr ) ? pInstance->getPreviousPermutationHash() : pMaterial->getPreviousPermutationHash();
        if ( previousDefineHash != 0 )
        {
            const uint64 fallbackHash = combinePermutationHash( pMaterial->getShaderPathHash(), previousDefineHash, bAppearanceDither );
            if ( fallbackHash != hash )
                permutation._fallbackHash = fallbackHash;
        }

        // copy-on-write: 목록은 RT 와 공유하므로 제자리에 추가하지 않는다. 새 퍼뮤테이션은 드물어(머티리얼 종류만큼) 복사가 싸다.
        shared_ptr<vector<GPUShaderPermutation>> pList = ( inoutSnapshot._pListShaderPermutation != nullptr )
                                                           ? make_shared<vector<GPUShaderPermutation>>( *inoutSnapshot._pListShaderPermutation )
                                                           : make_shared<vector<GPUShaderPermutation>>();
        pList->push_back( std::move( permutation ) );
        inoutSnapshot._pListShaderPermutation = pList;

        const uint32 index = static_cast<uint32>( pList->size() - 1 );
        _mapPermutationToIndex.emplace( hash, index );
        return index;
    }

    void GPUMaterialRegistry::beginBuild()
    {
        ++_buildCounter;
        for ( MaterialGroupState& state : _listMaterialGroupState )
        {
            state._bHasLast = SW_FALSE;
        }
    }

    uint32 GPUMaterialRegistry::assignMaterialElement( const shared_ptr<Material>& material, const shared_ptr<MaterialInstance>& instance, uint32 groupIndex,
                                                       GPUSceneSnapshot& inoutSnapshot )
    {
        Material* const         pMaterial             = material.get();
        MaterialInstance* const pInstance             = instance.get();
        const bool              bInvalidMaterialGroup = pMaterial == nullptr || groupIndex >= inoutSnapshot.getMaterialGroups().size() || groupIndex >= _listMaterialGroupState.size();
        if ( bInvalidMaterialGroup )
            return 0;
        MaterialGroupState&         state = _listMaterialGroupState[groupIndex];
        const GPUMaterialElementKey key{ pMaterial, pInstance };
        // 배치 안의 인스턴스는 같은 원소를 연속으로 조회한다. 포인터 비교 한 번으로 끝낸다.
        if ( state._bHasLast != SW_FALSE && state._lastKey == key )
            return state._lastIndex;
        const auto it = state._mapEntryToIndex.find( key );
        uint32     elementIndex{ 0 };
        if ( it != state._mapEntryToIndex.end() )
        {
            elementIndex = it->second;
        }
        else
        {
            // 회수된 슬롯을 먼저 재사용한다. 새 슬롯만 늘리면 버퍼가 단조 증가한다. 사본은 이번 빌드의 `refreshMaterialBindings` 가 채운다.
            // 그룹 목록은 새 원소가 생길 때만 고친다(공유 중이면 이때 베낀다 — `GPUSceneSnapshot::editMaterialGroups`).
            GPUMaterialGroup& group = inoutSnapshot.editMaterialGroups()[groupIndex];
            elementIndex            = state._freeEntry.allocate( group._listEntry, GPUMaterialElement{ key, MaterialRenderBinding{} } );
            if ( elementIndex >= state._listEntryLastUsedBuild.size() )
                state._listEntryLastUsedBuild.resize( elementIndex + 1 );
            if ( elementIndex >= state._listEntryOwner.size() )
                state._listEntryOwner.resize( elementIndex + 1 );
            state._listEntryOwner[elementIndex] = MaterialElementOwner{ material, instance }; // 소유가 키(생포인터)를 살려 둔다 — 회수 전에는 키가 가리키는 것이 사라지지 않는다
            state._mapEntryToIndex.emplace( key, elementIndex );
            // 새 원소의 사본은 비었다 — 이번 맞추기가 이 원소를 맞춘다. 주인 → 원소 표에도 올려 값이 바뀌면 이 원소만 다시 맞춘다.
            const ElementRef ref{ groupIndex, elementIndex };
            _listNewElement.push_back( ref );
            _mapOwnerToElement[static_cast<const void*>( pMaterial )].push_back( ref );
            if ( pInstance != nullptr )
                _mapOwnerToElement[static_cast<const void*>( pInstance )].push_back( ref );
        }
        state._listEntryLastUsedBuild[elementIndex] = _buildCounter;
        state._lastKey                              = key;
        state._lastIndex                            = elementIndex;
        state._bHasLast                             = SW_TRUE;
        return elementIndex;
    }

    void GPUMaterialRegistry::markBatchElements( const vector<GPUMeshBatch>& listBatch, const vector<GPUInstanceDirtyRange>& listBatchElementRange,
                                                 const vector<uint32>& listBatchElementIndex )
    {
        ++_buildCounter;
        const size_t batchCount = MathUtil::min( listBatch.size(), listBatchElementRange.size() );
        for ( size_t batchIndex = 0; batchIndex < batchCount; ++batchIndex )
        {
            const uint32 groupIndex = listBatch[batchIndex]._materialGroup;
            if ( groupIndex >= _listMaterialGroupState.size() )
                continue;
            MaterialGroupState&          state = _listMaterialGroupState[groupIndex];
            const GPUInstanceDirtyRange& range = listBatchElementRange[batchIndex];
            for ( uint32 entry = range._start; entry < range._start + range._count && entry < listBatchElementIndex.size(); ++entry )
            {
                const uint32 elementIndex = listBatchElementIndex[entry];
                if ( elementIndex < state._listEntryLastUsedBuild.size() )
                    state._listEntryLastUsedBuild[elementIndex] = _buildCounter;
            }
        }
    }

    void GPUMaterialRegistry::freeUnusedMaterialElements( GPUSceneSnapshot& inoutSnapshot )
    {
        // 이번 빌드에서 쓰이지 않은 원소는 바로 지우지 않는다. 아직 GPU 가 읽는 중인 프레임이 있을 수 있다.
        // 패킷 링 깊이(constant::render::kRenderFrameQueueDepth)와 같은 지연 기준을 쓴다. 큐에 있는 패킷이 아직 원소를 읽을 수 있다.
        if ( _buildCounter <= constant::render::kRenderFrameQueueDepth )
            return;
        const uint64 staleBefore = _buildCounter - constant::render::kRenderFrameQueueDepth;

        // 그룹 목록은 회수할 원소가 있을 때만 고친다(공유 중이면 그때 베낀다). 베끼면 옛 목록은 다른 쪽이 놓을 수 있으므로 참조를 들고 있지 않고 매번 다시 읽는다.
        const size_t groupCount = MathUtil::min( inoutSnapshot.getMaterialGroups().size(), _listMaterialGroupState.size() );
        for ( size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            MaterialGroupState& state      = _listMaterialGroupState[groupIndex];
            const uint32        entryCount = static_cast<uint32>( inoutSnapshot.getMaterialGroups()[groupIndex]._listEntry.size() );
            for ( uint32 index = 0; index < entryCount; ++index )
            {
                const GPUMaterialElementKey key = inoutSnapshot.getMaterialGroups()[groupIndex]._listEntry[index]._key;
                if ( key._pMaterial == nullptr )
                    continue;
                if ( index < state._listEntryLastUsedBuild.size() && state._listEntryLastUsedBuild[index] >= staleBefore )
                    continue;

                state._mapEntryToIndex.erase( key );
                const ElementRef ref{ static_cast<uint32>( groupIndex ), index };
                unlinkOwner( key._pMaterial, ref );
                if ( key._pInstance != nullptr )
                    unlinkOwner( key._pInstance, ref );
                // 슬롯은 비워 두고 프리리스트로 돌린다. 뒤 원소를 당겨오면 그들의 인덱스가 바뀌어
                // 이미 인스턴스에 기록된 materialIndex 가 엉뚱한 머티리얼을 가리킨다.
                inoutSnapshot.editMaterialGroups()[groupIndex]._listEntry[index] = GPUMaterialElement{};
                if ( index < state._listEntryOwner.size() )
                    state._listEntryOwner[index] = MaterialElementOwner{};
                state._freeEntry.push( index );
                state._bHasLast = SW_FALSE;
            }
        }
    }

    void GPUMaterialRegistry::unlinkOwner( const void* pOwner, const ElementRef& ref )
    {
        const auto it = _mapOwnerToElement.find( pOwner );
        if ( it == _mapOwnerToElement.end() )
            return;
        VectorUtil::removeIf( it->second, [&ref]( const ElementRef& item )
        { return item._group == ref._group && item._index == ref._index; } );
        if ( it->second.empty() )
            _mapOwnerToElement.erase( it );
    }

    bool GPUMaterialRegistry::refreshElement( GPUSceneSnapshot& inoutSnapshot, const ElementRef& ref, const MaterialRenderInputs& inputs ) const
    {
        if ( ref._group >= _listMaterialGroupState.size() || ref._group >= inoutSnapshot.getMaterialGroups().size() )
            return false;
        const MaterialGroupState& state = _listMaterialGroupState[ref._group];
        if ( ref._index >= state._listEntryOwner.size() || ref._index >= inoutSnapshot.getMaterialGroups()[ref._group]._listEntry.size() )
            return false;
        const MaterialElementOwner& owner = state._listEntryOwner[ref._index];
        if ( owner._material == nullptr )
            return false; // 회수된 칸이다
        MaterialRenderBinding        binding = ( owner._instance != nullptr ) ? owner._instance->getRenderBinding( inputs ) : owner._material->getRenderBinding( inputs );
        const MaterialRenderBinding& current = inoutSnapshot.getMaterialGroups()[ref._group]._listEntry[ref._index]._binding;
        if ( binding._proxy == current._proxy && binding._data == current._data )
            return false;
        // 사본이 바뀐 원소가 있을 때만 그룹 목록을 고친다(공유 중이면 그때 베낀다) — 맞추기가 돌아도 값이 그대로면 패킷은 같은 목록을 받는다(렌더 스레드는 목록이 같으면 올리지 않는다).
        inoutSnapshot.editMaterialGroups()[ref._group]._listEntry[ref._index]._binding = std::move( binding );
        return true;
    }

    void GPUMaterialRegistry::refreshMaterialBindings( GPUSceneSnapshot& inoutSnapshot )
    {
        // 입력은 맞추기 **전에** 읽는다 — 맞추는 중에 오른 번호(인스턴스가 부모의 레이아웃을 맞춤 · 묶음 값을 다시 패킹)는 다음 빌드가 한 번 더 맞춰 수렴한다.
        // 뒤에 읽으면 앞 원소(부모)가 옛 사본을 든 채 건너뛸 수 있다.
        const MaterialRenderInputs inputs      = MaterialRenderInputs::capture();
        const bool                 bInputsSame = inputs.isSame( _lastRefreshInputs );
        if ( _bAllElementsStale == SW_FALSE && bInputsSame && _listNewElement.empty() )
            return;
        // 바뀐 주인만 맞출 수 있는가 — 레이아웃이 그대로이고 변경 기록이 지난 맞추기 뒤를 모두 덮으며 주인을 모르는 변경이 없어야 한다.
        bool bTargeted = _bAllElementsStale == SW_FALSE && inputs._bKnown == SW_TRUE && _lastRefreshInputs._bKnown == SW_TRUE &&
                         inputs._shaderReflectionGeneration == _lastRefreshInputs._shaderReflectionGeneration;
        _listChangedOwnerScratch.clear();
        if ( bTargeted && bInputsSame == false )
        {
            const MaterialCache* const pCache = MaterialCache::findBound();
            bTargeted                         = pCache != nullptr &&
                        pCache->getRenderValueLog().collectChangedOwners( _lastRefreshInputs._materialRevision, inputs._materialRevision, _listChangedOwnerScratch );
        }
        _bAllElementsStale = SW_FALSE;
        _lastRefreshInputs = inputs;
        uint32 visitCount  = 0;
        uint32 changeCount = 0;
        if ( bTargeted )
        {
            for ( const ElementRef& ref : _listNewElement )
            {
                ++visitCount;
                changeCount += refreshElement( inoutSnapshot, ref, inputs ) ? 1u : 0u;
            }
            for ( const void* pOwner : _listChangedOwnerScratch )
            {
                const auto it = _mapOwnerToElement.find( pOwner );
                if ( it == _mapOwnerToElement.end() )
                    continue; // 이 장면에 없는 머티리얼이다
                for ( const ElementRef& ref : it->second )
                {
                    ++visitCount;
                    changeCount += refreshElement( inoutSnapshot, ref, inputs ) ? 1u : 0u;
                }
            }
        }
        else
        {
            SW_PROFILE_COUNT( "GT.Material.refreshBindingsAll", 1 );
            for ( uint32 groupIndex = 0; groupIndex < _listMaterialGroupState.size(); ++groupIndex )
            {
                const uint32 entryCount = static_cast<uint32>( _listMaterialGroupState[groupIndex]._listEntryOwner.size() );
                for ( uint32 index = 0; index < entryCount; ++index )
                {
                    ++visitCount;
                    changeCount += refreshElement( inoutSnapshot, ElementRef{ groupIndex, index }, inputs ) ? 1u : 0u;
                }
            }
        }
        _listNewElement.clear();
        SW_PROFILE_COUNT( "GT.Material.refreshBindings", 1 );
        SW_PROFILE_COUNT( "GT.Material.refreshVisited", visitCount );
        SW_PROFILE_COUNT( "GT.Material.refreshChanged", changeCount );
    }
} // namespace sw
