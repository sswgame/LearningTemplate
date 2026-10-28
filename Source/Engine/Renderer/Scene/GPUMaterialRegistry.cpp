#include "pch.h"

#include "Engine/Renderer/Scene/GPUMaterialRegistry.h"

#include "Core/Container/IndexFreeList.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Pipeline/RenderPassTypeInfo.h"

namespace sw
{
    uint64 GPUMaterialRegistry::permutationHashFor( const Material* pMaterial, const MaterialInstance* pInstance, bool bAppearanceDither )
    {
        if ( pMaterial == nullptr )
            return 0;
        // 인스턴스가 있으면 그 해시를 쓴다. 키워드 오버라이드가 퍼뮤테이션을 바꾸고, 그 해시는 부모의 것을 이미 포함한다.
        const uint64 defineHash = ( pInstance != nullptr ) ? pInstance->getPermutationHash() : pMaterial->getPermutationHash();
        uint64       hash       = pMaterial->getShaderPathHash();
        hash                    = HashUtil::combine( hash, defineHash );
        if ( bAppearanceDither )
            hash = HashUtil::combine( hash, hashed_string( kAppearanceDitherDefine ).getHash() );
        return hash;
    }

    GPUShaderPermutation GPUMaterialRegistry::makeShaderPermutation( const Material& material, const MaterialInstance* pInstance, bool bAppearanceDither )
    {
        GPUShaderPermutation permutation{};
        permutation._shaderPath = material.getShaderPath();
        permutation._listDefine = ( pInstance != nullptr ) ? pInstance->getCachedShaderDefines() : material.getCachedShaderDefines();
        if ( bAppearanceDither )
            permutation._listDefine.push_back( string( kAppearanceDitherDefine ) ); // 쿠커도 머티리얼 define 끝에 붙인다(`ShaderCookRequest`)
        permutation._hash = permutationHashFor( &material, pInstance, bAppearanceDither );
        return permutation;
    }

    void GPUMaterialRegistry::reset( GPUSceneSnapshot& inoutSnapshot )
    {
        inoutSnapshot._listMaterialGroup.clear();
        _listMaterialGroupState.clear();
        _mapShaderPathToGroup.clear();
        inoutSnapshot._pListShaderPermutation.reset();
        _mapPermutationToIndex.clear();
    }

    uint32 GPUMaterialRegistry::materialGroupFor( const Material* pMaterial, GPUSceneSnapshot& inoutSnapshot )
    {
        if ( pMaterial == nullptr )
            return kInvalidMaterialGroup;
        const string& shaderPath = pMaterial->getShaderPath();
        const auto    it         = _mapShaderPathToGroup.find( shaderPath );
        if ( it != _mapShaderPathToGroup.end() )
            return it->second;

        GPUMaterialGroup group{};
        group._shaderPath = shaderPath;
        inoutSnapshot._listMaterialGroup.push_back( std::move( group ) );
        _listMaterialGroupState.emplace_back();
        const uint32 groupIndex = static_cast<uint32>( inoutSnapshot._listMaterialGroup.size() - 1 );
        _mapShaderPathToGroup.emplace( shaderPath, groupIndex );
        return groupIndex;
    }

    uint32 GPUMaterialRegistry::shaderPermutationFor( const Material* pMaterial, const MaterialInstance* pInstance, bool bAppearanceDither, GPUSceneSnapshot& inoutSnapshot )
    {
        if ( pMaterial == nullptr )
            return kInvalidShaderPermutation;
        const uint64 hash = permutationHashFor( pMaterial, pInstance, bAppearanceDither );
        const auto   it   = _mapPermutationToIndex.find( hash );
        if ( it != _mapPermutationToIndex.end() )
            return it->second;

        GPUShaderPermutation permutation = makeShaderPermutation( *pMaterial, pInstance, bAppearanceDither );

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
        const bool              bInvalidMaterialGroup = pMaterial == nullptr || groupIndex >= inoutSnapshot._listMaterialGroup.size() || groupIndex >= _listMaterialGroupState.size();
        if ( bInvalidMaterialGroup )
            return 0;
        GPUMaterialGroup&           group = inoutSnapshot._listMaterialGroup[groupIndex];
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
            // 회수된 슬롯을 먼저 재사용한다. 새 슬롯만 늘리면 버퍼가 단조 증가한다.
            elementIndex = allocateFreeListIndex( group._listEntry, state._listFreeEntry, GPUMaterialElement{ material, instance } );
            if ( elementIndex >= state._listEntryLastUsedBuild.size() )
                state._listEntryLastUsedBuild.resize( elementIndex + 1 );
            state._mapEntryToIndex.emplace( key, elementIndex );
        }
        state._listEntryLastUsedBuild[elementIndex] = _buildCounter;
        state._lastKey                              = key;
        state._lastIndex                            = elementIndex;
        state._bHasLast                             = SW_TRUE;
        return elementIndex;
    }

    void GPUMaterialRegistry::stampBatchElements( const vector<GPUMeshBatch>& listBatch, const vector<GPUInstanceRun>& listBatchElementRange,
                                                  const vector<uint32>& listBatchElementIndex )
    {
        ++_buildCounter;
        const size_t batchCount = MathUtil::min( listBatch.size(), listBatchElementRange.size() );
        for ( size_t batchIndex = 0; batchIndex < batchCount; ++batchIndex )
        {
            const uint32 groupIndex = listBatch[batchIndex]._materialGroup;
            if ( groupIndex >= _listMaterialGroupState.size() )
                continue;
            MaterialGroupState&   state = _listMaterialGroupState[groupIndex];
            const GPUInstanceRun& range = listBatchElementRange[batchIndex];
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
        // 패킷 링 깊이(constant::kRenderFrameQueueDepth)와 같은 지연 기준을 쓴다. 큐에 있는 패킷이 아직 원소를 읽을 수 있다.
        if ( _buildCounter <= constant::kRenderFrameQueueDepth )
            return;
        const uint64 staleBefore = _buildCounter - constant::kRenderFrameQueueDepth;

        const size_t groupCount = MathUtil::min( inoutSnapshot._listMaterialGroup.size(), _listMaterialGroupState.size() );
        for ( size_t groupIndex = 0; groupIndex < groupCount; ++groupIndex )
        {
            GPUMaterialGroup&   group = inoutSnapshot._listMaterialGroup[groupIndex];
            MaterialGroupState& state = _listMaterialGroupState[groupIndex];
            for ( uint32 index = 0; index < group._listEntry.size(); ++index )
            {
                if ( group._listEntry[index]._material == nullptr )
                    continue;
                if ( index < state._listEntryLastUsedBuild.size() && state._listEntryLastUsedBuild[index] >= staleBefore )
                    continue;

                const GPUMaterialElementKey key{ group._listEntry[index]._material.get(), group._listEntry[index]._instance.get() };
                state._mapEntryToIndex.erase( key );
                // 슬롯은 비워 두고 프리리스트로 돌린다. 뒤 원소를 당겨오면 그들의 인덱스가 바뀌어
                // 이미 인스턴스에 기록된 materialIndex 가 엉뚱한 머티리얼을 가리킨다.
                group._listEntry[index] = GPUMaterialElement{};
                state._listFreeEntry.push_back( index );
                state._bHasLast = SW_FALSE;
            }
        }
    }
} // namespace sw
