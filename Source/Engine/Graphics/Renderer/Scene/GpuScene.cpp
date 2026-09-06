#include "pch.h"

#include "Engine/Graphics/Renderer/Scene/GpuScene.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInstance.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
    namespace
    {
        struct GpuSceneInternal
        {
            static void mixHash( uint64& h, uint64 v )
            {
                h ^= v + 0x9e3779b97f4a7c15ull + ( h << 6 ) + ( h >> 2 );
            }

            static void mixBytes( uint64& h, const void* pData, size_t bytes )
            {
                const uint8* pBytes    = static_cast<const uint8*>( pData );
                const size_t wordCount = bytes / sizeof( uint64 );
                const size_t remainder = bytes % sizeof( uint64 );

                for ( size_t wordIndex = 0; wordIndex < wordCount; ++wordIndex )
                {
                    uint64 word = 0;
                    Memory::copy( &word, pBytes + wordIndex * sizeof( uint64 ), sizeof( uint64 ) );
                    mixHash( h, word );
                }

                if ( remainder > 0 )
                {
                    uint64 tailWord = 0;
                    Memory::copy( &tailWord, pBytes + wordCount * sizeof( uint64 ), remainder );
                    mixHash( h, tailWord );
                }
            }

            template <typename TCandidate, typename TInstance>
            static void fillRangeVal( const vector<TCandidate>& listScratchCandidate, vector<TInstance>& listScratchRaw, uint32 begin, uint32 end )
            {
                for ( uint32 entryIndex = begin; entryIndex < end; ++entryIndex )
                {
                    const auto& cand   = listScratchCandidate[entryIndex];
                    auto&       inst   = listScratchRaw[entryIndex];
                    inst._world        = cand._world;
                    inst._boundsCenter = cand._boundsCenter;
                    inst._boundsRadius = cand._boundsRadius;
                    inst._blendMode    = cand._blendMode;
                }
            }

            static void applyInstanceCbsVal( IRHIDevice* pDevice, vector<GpuMeshBatch>& listBatch )
            {
                for ( GpuMeshBatch& batch : listBatch )
                {
                    if ( batch._pMaterialInstance == nullptr )
                        continue;
                    if ( batch._pMaterialInstance->applyToGpu( pDevice ) )
                        batch._materialCb = batch._pMaterialInstance->getDescriptorIndex();
                }
            }

            static void uploadMeshesVal( IRHIDevice* pDevice, vector<GpuMeshBatch>& listBatch )
            {
                for ( GpuMeshBatch& batch : listBatch )
                {
                    if ( batch._pMesh != nullptr && batch._pMesh->upload( pDevice ) )
                        batch._vertexBuffer = batch._pMesh->getVertexBuffer();
                }
            }
        };
    } // namespace
} // namespace sw

namespace sw
{
    void GpuScene::invalidateBuildCache()
    {
        _lastContentHash = 0;
        _lastCameraPos   = float3{};
        _bHasBuildCache  = 0;
        _bCpuDirty       = 1;
    }

    void GpuScene::clear()
    {
        _listInstance.clear();
        _listOpaqueBatch.clear();
        _listTransparentBatch.clear();
        _listAllBatch.clear();
        _indirectCommandCount = 0;
        _listScratchCandidate.clear();
        _listScratchRaw.clear();
        _listScratchOpaqueEntry.clear();
        _listScratchTransparentIdx.clear();
        invalidateBuildCache();
        _materialRetire.clear();
    }

    void GpuScene::syncMaterialPins()
    {
        _materialRetire.syncFromBatches( _listOpaqueBatch, _listTransparentBatch );
    }

    void GpuMaterialRetireQueue::clear()
    {
        _uniquePinned.clear();
        _listRetiring.clear();
    }

    bool GpuMaterialRetireQueue::isPinned( const MaterialInstance* pInstance ) const
    {
        if ( pInstance == nullptr )
            return false;
        if ( _uniquePinned.find( const_cast<MaterialInstance*>( pInstance ) ) != _uniquePinned.end() )
            return true;
        for ( const RetireEntry& retireEntry : _listRetiring )
        {
            if ( retireEntry._pInstance == pInstance )
                return true;
        }
        return false;
    }

    void GpuMaterialRetireQueue::syncFromBatches( const vector<GpuMeshBatch>& listOpaque, const vector<GpuMeshBatch>& listTransparent )
    {
        unordered_set<MaterialInstance*> uniqueLive;
        auto                             collect = [&]( const vector<GpuMeshBatch>& listBatch )
        {
            for ( const GpuMeshBatch& batch : listBatch )
            {
                if ( batch._pMaterialInstance != nullptr )
                    uniqueLive.insert( batch._pMaterialInstance );
            }
        };
        collect( listOpaque );
        collect( listTransparent );

        for ( MaterialInstance* pInst : _uniquePinned )
        {
            if ( uniqueLive.find( pInst ) == uniqueLive.end() )
            {
                RetireEntry entry{};
                entry._pInstance  = pInst;
                entry._framesLeft = kRetireFrameDelay;
                _listRetiring.push_back( entry );
            }
        }

        _uniquePinned = std::move( uniqueLive );
    }

    void GpuMaterialRetireQueue::advanceFrame()
    {
        uint32 write{ 0 };
        for ( uint32 index = 0; index < _listRetiring.size(); ++index )
        {
            RetireEntry& entry = _listRetiring[index];
            if ( entry._framesLeft > 0 )
                --entry._framesLeft;
            if ( entry._framesLeft > 0 )
            {
                _listRetiring[write++] = entry;
            }
        }
        _listRetiring.resize( write );
    }

    void GpuMaterialRetireQueue::flushAfterGpu( IRHIDevice* pDevice )
    {
        if ( pDevice != nullptr )
            pDevice->waitIdle();
        clear();
    }

    uint64 GpuScene::hashCandidates() const
    {
        uint64 h = 14695981039346656037ull;
        GpuSceneInternal::mixHash( h, static_cast<uint64>( _listScratchCandidate.size() ) );
        for ( const DrawCandidate& cand : _listScratchCandidate )
        {
            GpuSceneInternal::mixHash( h, reinterpret_cast<uintptr_t>( cand._pMesh ) );
            GpuSceneInternal::mixHash( h, reinterpret_cast<uintptr_t>( cand._pMaterial ) );
            GpuSceneInternal::mixHash( h, reinterpret_cast<uintptr_t>( cand._pInstance ) );
            GpuSceneInternal::mixHash( h, cand._blendMode );
            GpuSceneInternal::mixBytes( h, &cand._world, sizeof( cand._world ) );
            GpuSceneInternal::mixBytes( h, &cand._boundsCenter, sizeof( cand._boundsCenter ) );
            GpuSceneInternal::mixBytes( h, &cand._boundsRadius, sizeof( cand._boundsRadius ) );
        }
        return h;
    }

    void GpuScene::rebuildPartitionTables()
    {
        const uint32 count = static_cast<uint32>( _listScratchCandidate.size() );
        _listScratchOpaqueEntry.clear();
        _listScratchOpaqueEntry.reserve( count );
        _listScratchTransparentIdx.clear();
        _listScratchTransparentIdx.reserve( count );

        for ( uint32 instanceIndex = 0; instanceIndex < count; ++instanceIndex )
        {
            const DrawCandidate& cand = _listScratchCandidate[instanceIndex];
            if ( static_cast<RHIBlendMode>( cand._blendMode ) == RHIBlendMode::Transparent )
            {
                _listScratchTransparentIdx.push_back( instanceIndex );
                continue;
            }
            SortKey key{ cand._pMesh, cand._pMaterial, cand._pInstance };
            _listScratchOpaqueEntry.push_back( SortEntry{ key, instanceIndex } );
        }

        if ( _listScratchOpaqueEntry.empty() == false )
        {
            std::sort( _listScratchOpaqueEntry.begin(), _listScratchOpaqueEntry.end(), []( const SortEntry& entryA, const SortEntry& entryB )
            {
                if ( entryA._key._pMesh != entryB._key._pMesh )
                    return entryA._key._pMesh < entryB._key._pMesh;
                if ( entryA._key._pMaterial != entryB._key._pMaterial )
                    return entryA._key._pMaterial < entryB._key._pMaterial;
                return entryA._key._pInstance < entryB._key._pInstance;
            } );
        }
    }

    void GpuScene::fillScratchRange( uint32 start, uint32 end )
    {
        GpuSceneInternal::fillRangeVal( _listScratchCandidate, _listScratchRaw, start, end );
    }

    void GpuScene::buildFromScene( Scene* pScene, const float3& cameraPos,
                                   TaskManager* pTaskManager )
    {
        if ( pScene == nullptr )
        {
            clear();
            return;
        }

        GameObjectManager* pObjects = pScene->getObjectManager();
        if ( pObjects == nullptr )
        {
            clear();
            return;
        }

        pObjects->flushSceneTransforms();

        _listScratchCandidate.clear();
        _listScratchCandidate.reserve( 1024 );

        pObjects->forEachGameObject( [this]( GameObject* pObj )
        {
            if ( pObj == nullptr || pObj->isActiveInHierarchy() == false )
                return;

            pObj->forEachComponent( [this]( Component* pComp )
            {
                MeshComponent* pMeshComp = castTo<MeshComponent>( pComp );
                if ( pMeshComp == nullptr || pMeshComp->isVisible() == false )
                    return;
                Mesh* pMesh = pMeshComp->getRawMesh();
                if ( pMesh == nullptr || pMesh->getVertexCount() == 0 )
                    return;

                const float4x4 world = pMeshComp->getWorldMatrix();
                DrawCandidate  cand{};
                cand._world        = world;
                cand._boundsCenter = world.getTranslation();
                cand._boundsRadius = pMeshComp->getBoundsRadius();
                cand._blendMode    = static_cast<uint32>( pMeshComp->getBlendMode() );
                cand._pMesh        = pMesh;
                cand._pMaterial    = pMeshComp->getMaterial();
                cand._pInstance    = pMeshComp->getRawMaterialInstance();
                _listScratchCandidate.push_back( cand );
            } );
        } );

        if ( _listScratchCandidate.empty() )
        {
            clear();
            return;
        }

        const uint64 contentHash = hashCandidates();
        const bool   bContentSame =
            _bHasBuildCache != 0 && contentHash == _lastContentHash && _listInstance.empty() == false;
        const bool bCamSame = _bHasBuildCache != 0 && ( float3::getDistanceSquared( cameraPos, _lastCameraPos ) <= MathUtil::Epsilon );

        if ( bContentSame && bCamSame )
            return;

        const uint32 count = static_cast<uint32>( _listScratchCandidate.size() );
        _listScratchRaw.resize( count );

        if ( bContentSame == false )
        {
            if ( pTaskManager != nullptr && count >= 8 && pTaskManager->getWorkerCount() > 0 )
            {
                if ( _snapshotStage.isValid() == false )
                    _snapshotStage = pTaskManager->createAnonymousStage( "GpuSceneSnapshot" );

                TaskHandle handle = pTaskManager->emplaceParallelBlock(
                    0, count, SW_DELEGATE_METHOD( ParallelBlockDelegate, &GpuScene::fillScratchRange, this ) );
                _snapshotStage.addTask( handle );
                handle.submit();
                pTaskManager->waitStage( _snapshotStage );
            }
            else
                GpuSceneInternal::fillRangeVal( _listScratchCandidate, _listScratchRaw, 0, count );

            rebuildPartitionTables();
        }

        sortTransparent( &cameraPos._x );

        _listInstance.clear();
        _listOpaqueBatch.clear();
        _listTransparentBatch.clear();
        _listAllBatch.clear();
        buildBatches();

        _lastContentHash = contentHash;
        _lastCameraPos   = cameraPos;
        _bHasBuildCache  = 1;
        _bCpuDirty       = 1;
    }

    bool GpuScene::upload( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr || _listInstance.empty() || _listAllBatch.empty() )
            return false;

        // RT-owned context: pack MaterialInstance overrides and upload meshes in a single pass.
        GpuSceneInternal::applyInstanceCbsVal( pDevice, _listAllBatch );
        GpuSceneInternal::uploadMeshesVal( pDevice, _listAllBatch );

        const size_t opaqueCount = _listOpaqueBatch.size();
        for ( size_t batchIndex = 0; batchIndex < opaqueCount; ++batchIndex )
        {
            _listOpaqueBatch[batchIndex]._materialCb   = _listAllBatch[batchIndex]._materialCb;
            _listOpaqueBatch[batchIndex]._vertexBuffer = _listAllBatch[batchIndex]._vertexBuffer;
        }
        for ( size_t batchIndex = 0; batchIndex < _listTransparentBatch.size(); ++batchIndex )
        {
            _listTransparentBatch[batchIndex]._materialCb   = _listAllBatch[opaqueCount + batchIndex]._materialCb;
            _listTransparentBatch[batchIndex]._vertexBuffer = _listAllBatch[opaqueCount + batchIndex]._vertexBuffer;
        }

        // CPU 스냅샷이 그대로면 인스턴스/간접 버퍼 재업로드 생략.
        if ( _bCpuDirty == 0 && _instanceBuffer != 0 && _indirectArgsBuffer != 0 )
            return true;

        const uint32 instanceCount = static_cast<uint32>( _listInstance.size() );
        const uint32 argsCount     = static_cast<uint32>( _listAllBatch.size() );

        if ( _instanceBuffer == 0 || _instanceCapacity < instanceCount )
        {
            if ( _instanceBuffer != 0 )
            {
                if ( _instanceSrv != kInvalidDescriptorIndex )
                    pDevice->getResource()->unregisterBindlessResource( _instanceSrv );
                pDevice->getResource()->destroyBuffer( _instanceBuffer );
                _instanceBuffer = 0;
                _instanceSrv    = kInvalidDescriptorIndex;
            }
            RHIBufferDesc desc{};
            desc._elementSize  = static_cast<uint32>( sizeof( GpuInstance ) );
            desc._elementCount = instanceCount;
            desc._sizeBytes    = desc._elementSize * desc._elementCount;
            desc._usage        = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
            desc._pInitialData = _listInstance.data();
            _instanceBuffer    = pDevice->getResource()->createBuffer( desc );
            if ( _instanceBuffer == 0 )
            {
                _instanceBuffer = pDevice->getResource()->createStructuredBuffer( desc._elementSize, desc._elementCount );
                if ( _instanceBuffer != 0 )
                    pDevice->getResource()->updateStructuredBuffer( _instanceBuffer, _listInstance.data(), desc._sizeBytes );
            }
            if ( _instanceBuffer != 0 )
            {
                _instanceSrv      = pDevice->getResource()->registerBindlessResource( _instanceBuffer );
                _instanceCapacity = instanceCount;
            }
        }
        else
        {
            pDevice->getResource()->updateStructuredBuffer( _instanceBuffer, _listInstance.data(),
                                                            instanceCount * static_cast<uint32>( sizeof( GpuInstance ) ) );
        }

        _listScratchIndirectCmd.resize( argsCount );
        for ( uint32 argIndex = 0; argIndex < argsCount; ++argIndex )
        {
            _listScratchIndirectCmd[argIndex]._vertexCount           = _listAllBatch[argIndex]._vertexCount;
            _listScratchIndirectCmd[argIndex]._instanceCount         = _listAllBatch[argIndex]._instanceCount;
            _listScratchIndirectCmd[argIndex]._startVertexLocation   = 0;
            _listScratchIndirectCmd[argIndex]._startInstanceLocation = _listAllBatch[argIndex]._instanceBase;
        }

        if ( _indirectArgsBuffer == 0 || _argsCapacity < argsCount )
        {
            if ( _indirectArgsBuffer != 0 )
            {
                if ( _indirectArgsUav != kInvalidDescriptorIndex )
                    pDevice->getResource()->unregisterBindlessUAV( _indirectArgsUav );
                pDevice->getResource()->destroyBuffer( _indirectArgsBuffer );
                _indirectArgsBuffer = 0;
                _indirectArgsUav    = kInvalidDescriptorIndex;
            }
            RHIBufferDesc desc{};
            desc._elementSize   = static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) );
            desc._elementCount  = argsCount;
            desc._sizeBytes     = desc._elementSize * desc._elementCount;
            desc._usage         = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs | RHIBufferUsage::Raw | RHIBufferUsage::ShaderResource;
            desc._pInitialData  = _listScratchIndirectCmd.data();
            _indirectArgsBuffer = pDevice->getResource()->createBuffer( desc );
            if ( _indirectArgsBuffer == 0 )
            {
                _indirectArgsBuffer = pDevice->getResource()->createStructuredBuffer( desc._elementSize, desc._elementCount );
                if ( _indirectArgsBuffer != 0 )
                    pDevice->getResource()->updateStructuredBuffer( _indirectArgsBuffer, _listScratchIndirectCmd.data(), desc._sizeBytes );
            }
            if ( _indirectArgsBuffer != 0 )
            {
                _indirectArgsUav = pDevice->getResource()->registerBindlessUAV( _indirectArgsBuffer );
                _argsCapacity    = argsCount;
            }
        }
        else
        {
            pDevice->getResource()->updateStructuredBuffer( _indirectArgsBuffer, _listScratchIndirectCmd.data(),
                                                            argsCount * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ) );
        }

        _indirectCommandCount = argsCount;
        _bCpuDirty            = 0;

        return _instanceBuffer != 0 && _indirectArgsBuffer != 0;
    }

    void GpuScene::releaseGpu( IRHIDevice* pDevice )
    {
        if ( pDevice == nullptr )
            return;
        if ( _instanceSrv != kInvalidDescriptorIndex )
            pDevice->getResource()->unregisterBindlessResource( _instanceSrv );
        if ( _indirectArgsUav != kInvalidDescriptorIndex )
            pDevice->getResource()->unregisterBindlessUAV( _indirectArgsUav );
        if ( _instanceBuffer != 0 )
            pDevice->getResource()->destroyBuffer( _instanceBuffer );
        if ( _indirectArgsBuffer != 0 )
            pDevice->getResource()->destroyBuffer( _indirectArgsBuffer );
        _instanceBuffer       = 0;
        _instanceSrv          = kInvalidDescriptorIndex;
        _indirectArgsBuffer   = 0;
        _indirectArgsUav      = kInvalidDescriptorIndex;
        _instanceCapacity     = 0;
        _argsCapacity         = 0;
        _indirectCommandCount = 0;
        _bCpuDirty            = 1;
        _materialRetire.flushAfterGpu( pDevice );
    }

    void GpuScene::exportCpuSnapshot( GpuScene& outSnapshot )
    {
        outSnapshot._listInstance         = _listInstance;
        outSnapshot._listOpaqueBatch      = _listOpaqueBatch;
        outSnapshot._listTransparentBatch = _listTransparentBatch;
        outSnapshot._listAllBatch         = _listAllBatch;
        outSnapshot._indirectCommandCount = _indirectCommandCount;
        outSnapshot._bCpuDirty            = _bCpuDirty;
        _bCpuDirty                        = 0;
    }

    void GpuScene::adoptCpuSnapshot( GpuScene&& snapshot )
    {
        _listInstance         = std::move( snapshot._listInstance );
        _listOpaqueBatch      = std::move( snapshot._listOpaqueBatch );
        _listTransparentBatch = std::move( snapshot._listTransparentBatch );
        _listAllBatch         = std::move( snapshot._listAllBatch );
        _indirectCommandCount = snapshot._indirectCommandCount;
        _bCpuDirty            = snapshot._bCpuDirty;
    }

    void GpuScene::sortTransparent( const float32* pCameraPos )
    {
        if ( pCameraPos == nullptr || _listScratchTransparentIdx.size() <= 1 )
            return;
        const float3 camPos{ pCameraPos[0], pCameraPos[1], pCameraPos[2] };
        std::sort( _listScratchTransparentIdx.begin(), _listScratchTransparentIdx.end(), [&]( uint32 idxA, uint32 idxB )
        { return float3::getDistanceSquared( _listScratchRaw[idxA]._boundsCenter, camPos ) > float3::getDistanceSquared( _listScratchRaw[idxB]._boundsCenter, camPos ); } );
    }

    void GpuScene::buildBatches()
    {
        _listInstance.reserve( _listScratchCandidate.size() );

        if ( _listScratchOpaqueEntry.empty() == false )
        {
            uint32 batchStart{ 0 };
            for ( uint32 entryIndex = 1; entryIndex <= _listScratchOpaqueEntry.size(); ++entryIndex )
            {
                if ( entryIndex == _listScratchOpaqueEntry.size() || ( _listScratchOpaqueEntry[entryIndex]._key == _listScratchOpaqueEntry[batchStart]._key ) == false )
                {
                    const SortKey& key = _listScratchOpaqueEntry[batchStart]._key;
                    GpuMeshBatch   batch{};
                    batch._pMesh             = key._pMesh;
                    batch._vertexCount       = batch._pMesh->getVertexCount();
                    batch._instanceBase      = static_cast<uint32>( _listInstance.size() );
                    batch._instanceCount     = entryIndex - batchStart;
                    batch._blendMode         = RHIBlendMode::Opaque;
                    batch._pMaterialInstance = key._pInstance;
                    if ( key._pInstance != nullptr )
                        batch._materialCb = key._pInstance->getDescriptorIndex();
                    else
                        batch._materialCb = key._pMaterial ? key._pMaterial->getDescriptorIndex() : kInvalidDescriptorIndex;

                    const uint32 batchIndex = static_cast<uint32>( _listAllBatch.size() );
                    for ( uint32 batchEntryIndex = batchStart; batchEntryIndex < entryIndex; ++batchEntryIndex )
                    {
                        uint32      srcIdx   = _listScratchOpaqueEntry[batchEntryIndex]._srcIdx;
                        GpuInstance inst     = _listScratchRaw[srcIdx];
                        inst._meshBatchIndex = batchIndex;
                        _listInstance.push_back( inst );
                    }
                    _listOpaqueBatch.push_back( batch );
                    _listAllBatch.push_back( batch );

                    batchStart = entryIndex;
                }
            }
        }

        // back-to-front 정렬된 transparent 인덱스에서 연속 동일 mesh/mat/instance 만 머지.
        if ( _listScratchTransparentIdx.empty() == false )
        {
            uint32 batchStart{ 0 };
            for ( uint32 entryIndex = 1; entryIndex <= _listScratchTransparentIdx.size(); ++entryIndex )
            {
                const bool bEnd = entryIndex == _listScratchTransparentIdx.size();
                bool       bKeyChange{ false };
                if ( bEnd == false )
                {
                    const DrawCandidate& a = _listScratchCandidate[_listScratchTransparentIdx[batchStart]];
                    const DrawCandidate& b = _listScratchCandidate[_listScratchTransparentIdx[entryIndex]];
                    bKeyChange             = ( a._pMesh != b._pMesh ) || ( a._pMaterial != b._pMaterial ) || ( a._pInstance != b._pInstance );
                }
                if ( bEnd || bKeyChange )
                {
                    const DrawCandidate& cand = _listScratchCandidate[_listScratchTransparentIdx[batchStart]];
                    GpuMeshBatch         batch{};
                    batch._pMesh             = cand._pMesh;
                    batch._vertexCount       = batch._pMesh->getVertexCount();
                    batch._instanceBase      = static_cast<uint32>( _listInstance.size() );
                    batch._instanceCount     = entryIndex - batchStart;
                    batch._blendMode         = RHIBlendMode::Transparent;
                    batch._pMaterialInstance = cand._pInstance;
                    if ( cand._pInstance != nullptr )
                        batch._materialCb = cand._pInstance->getDescriptorIndex();
                    else
                        batch._materialCb = cand._pMaterial ? cand._pMaterial->getDescriptorIndex() : kInvalidDescriptorIndex;

                    const uint32 batchIndex = static_cast<uint32>( _listAllBatch.size() );
                    for ( uint32 batchEntryIndex = batchStart; batchEntryIndex < entryIndex; ++batchEntryIndex )
                    {
                        const uint32 srcIdx  = _listScratchTransparentIdx[batchEntryIndex];
                        GpuInstance  inst    = _listScratchRaw[srcIdx];
                        inst._meshBatchIndex = batchIndex;
                        _listInstance.push_back( inst );
                    }
                    _listTransparentBatch.push_back( batch );
                    _listAllBatch.push_back( batch );
                    batchStart = entryIndex;
                }
            }
        }

        syncMaterialPins();
    }
} // namespace sw
