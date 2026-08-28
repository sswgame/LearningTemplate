#include "pch.h"

#include "Engine/Graphics/RenderPass/GpuScene.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Graphics/Material/Material.h"
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
		void extractTranslation( const float4x4& m, float32 out[3] )
		{
			out[0] = m._41;
			out[1] = m._42;
			out[2] = m._43;
		}

		float32 distSq( const float32 a[3], const float32 b[3] )
		{
			const float32 dx = a[0] - b[0];
			const float32 dy = a[1] - b[1];
			const float32 dz = a[2] - b[2];
			return dx * dx + dy * dy + dz * dz;
		}

		bool cameraNearlyEqual( const float32 a[3], const float32 b[3] )
		{
			constexpr float32 kEps = 1e-5f;
			return MathUtil::abs( a[0] - b[0] ) <= kEps && MathUtil::abs( a[1] - b[1] ) <= kEps &&
				   MathUtil::abs( a[2] - b[2] ) <= kEps;
		}

		void mixHash( uint64& h, uint64 v )
		{
			h ^= v + 0x9e3779b97f4a7c15ull + ( h << 6 ) + ( h >> 2 );
		}

		void mixBytes( uint64& h, const void* pData, size_t bytes )
		{
			const uint8* pBytes = static_cast<const uint8*>( pData );
			for ( size_t byteIndex = 0; byteIndex < bytes; ++byteIndex )
				mixHash( h, pBytes[byteIndex] );
		}

		template <typename TCandidate, typename TInstance>
		static void fillRangeVal( const vector<TCandidate>& scratchCandidates, vector<TInstance>& scratchRaw, uint32 begin, uint32 end )
		{
			for ( uint32 entryIndex = begin; entryIndex < end; ++entryIndex )
			{
				const auto& cand = scratchCandidates[entryIndex];
				auto&		inst = scratchRaw[entryIndex];
				Memory::copy( inst._world, cand._world, sizeof( inst._world ) );
				Memory::copy( inst._boundsCenter, cand._boundsCenter, sizeof( inst._boundsCenter ) );
				inst._boundsRadius = cand._boundsRadius;
				inst._blendMode	   = cand._blendMode;
			}
		}

		static void applyInstanceCbsVal( IRHIDevice* pDevice, vector<GpuMeshBatch>& batches )
		{
			for ( GpuMeshBatch& batch : batches )
			{
				if ( batch._pMaterialInstance == nullptr )
					continue;
				if ( batch._pMaterialInstance->applyToGpu( pDevice ) )
					batch._materialCb = batch._pMaterialInstance->getDescriptorIndex();
			}
		}

		static void uploadMeshesVal( IRHIDevice* pDevice, vector<GpuMeshBatch>& batches )
		{
			for ( GpuMeshBatch& batch : batches )
			{
				if ( batch._pMesh != nullptr && batch._pMesh->upload( pDevice ) )
					batch._vertexBuffer = batch._pMesh->getVertexBuffer();
			}
		}

	} // namespace

	void GpuScene::invalidateBuildCache()
	{
		_lastContentHash  = 0;
		_lastCameraPos[0] = _lastCameraPos[1] = _lastCameraPos[2] = 0.0f;
		_bHasBuildCache											  = 0;
		_bCpuDirty												  = 1;
	}

	void GpuScene::clear()
	{
		_listInstances.clear();
		_listOpaqueBatches.clear();
		_listTransparentBatches.clear();
		_listAllBatches.clear();
		_indirectCommandCount = 0;
		_listScratchCandidates.clear();
		_listScratchRaw.clear();
		_listScratchOpaqueEntries.clear();
		_listScratchTransparentIdx.clear();
		invalidateBuildCache();
		_materialRetire.clear();
	}

	void GpuScene::syncMaterialPins()
	{
		_materialRetire.syncFromBatches( _listOpaqueBatches, _listTransparentBatches );
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

	void GpuMaterialRetireQueue::syncFromBatches( const vector<GpuMeshBatch>& opaque, const vector<GpuMeshBatch>& transparent )
	{
		unordered_set<MaterialInstance*> uniqueLive;
		auto							 collect = [&]( const vector<GpuMeshBatch>& batches )
		{
			for ( const GpuMeshBatch& batch : batches )
			{
				if ( batch._pMaterialInstance != nullptr )
					uniqueLive.insert( batch._pMaterialInstance );
			}
		};
		collect( opaque );
		collect( transparent );

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
		mixHash( h, static_cast<uint64>( _listScratchCandidates.size() ) );
		for ( const DrawCandidate& cand : _listScratchCandidates )
		{
			mixHash( h, reinterpret_cast<uintptr_t>( cand._pMesh ) );
			mixHash( h, reinterpret_cast<uintptr_t>( cand._pMaterial ) );
			mixHash( h, reinterpret_cast<uintptr_t>( cand._pInstance ) );
			mixHash( h, cand._blendMode );
			mixBytes( h, cand._world, sizeof( cand._world ) );
			mixBytes( h, cand._boundsCenter, sizeof( cand._boundsCenter ) );
			mixBytes( h, &cand._boundsRadius, sizeof( cand._boundsRadius ) );
		}
		return h;
	}

	void GpuScene::rebuildPartitionTables()
	{
		const uint32 count = static_cast<uint32>( _listScratchCandidates.size() );
		_listScratchOpaqueEntries.clear();
		_listScratchOpaqueEntries.reserve( count );
		_listScratchTransparentIdx.clear();
		_listScratchTransparentIdx.reserve( count );

		for ( uint32 instanceIndex = 0; instanceIndex < count; ++instanceIndex )
		{
			const DrawCandidate& cand = _listScratchCandidates[instanceIndex];
			if ( static_cast<RHIBlendMode>( cand._blendMode ) == RHIBlendMode::Transparent )
			{
				_listScratchTransparentIdx.push_back( instanceIndex );
				continue;
			}
			SortKey key{ cand._pMesh, cand._pMaterial, cand._pInstance };
			_listScratchOpaqueEntries.push_back( SortEntry{ key, instanceIndex } );
		}

		if ( _listScratchOpaqueEntries.empty() == false )
		{
			std::sort( _listScratchOpaqueEntries.begin(), _listScratchOpaqueEntries.end(), []( const SortEntry& entryA, const SortEntry& entryB )
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
		fillRangeVal( _listScratchCandidates, _listScratchRaw, start, end );
	}

	void GpuScene::buildFromScene( Scene* pScene, const float32 cameraPos[3],
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

		_listScratchCandidates.clear();
		_listScratchCandidates.reserve( 1024 );

		const vector<GameObject*> listObjects = pObjects->getAllGameObjects();
		for ( GameObject* pObj : listObjects )
		{
			if ( pObj == nullptr || pObj->isPendingKill() || pObj->isActiveInHierarchy() == false )
				continue;

			for ( Component* pComp : pObj->getAllComponents() )
			{
				MeshComponent* pMeshComp = pComp != nullptr ? castTo<MeshComponent>( pComp ) : nullptr;
				if ( pMeshComp == nullptr || pMeshComp->isVisible() == false )
					continue;
				shared_ptr<Mesh> mesh = pMeshComp->getMesh();
				if ( mesh == nullptr || mesh->getVertexCount() == 0 )
					continue;

				const float4x4 world = pMeshComp->getWorldMatrix();
				DrawCandidate  cand{};
				Memory::copy( cand._world, &world._11, sizeof( cand._world ) );
				extractTranslation( world, cand._boundsCenter );
				cand._boundsRadius = pMeshComp->getBoundsRadius();
				cand._blendMode	   = static_cast<uint32>( pMeshComp->getBlendMode() );
				cand._pMesh		   = mesh.get();
				cand._pMaterial	   = pMeshComp->getMaterial();
				shared_ptr<MaterialInstance> materialInstance = pMeshComp->getMaterialInstance();
				cand._pInstance	   = materialInstance.get();
				_listScratchCandidates.push_back( cand );
			}
		}

		if ( _listScratchCandidates.empty() )
		{
			clear();
			return;
		}

		const float32 arrCam[3] = {
			cameraPos != nullptr ? cameraPos[0] : 0.0f,
			cameraPos != nullptr ? cameraPos[1] : 0.0f,
			cameraPos != nullptr ? cameraPos[2] : 0.0f };

		const uint64 contentHash = hashCandidates();
		const bool	 bContentSame =
			_bHasBuildCache != 0 && contentHash == _lastContentHash && _listInstances.empty() == false;
		const bool bCamSame = _bHasBuildCache != 0 && cameraNearlyEqual( arrCam, _lastCameraPos );

		if ( bContentSame && bCamSame )
			return;

		const uint32 count = static_cast<uint32>( _listScratchCandidates.size() );
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
				fillRangeVal( _listScratchCandidates, _listScratchRaw, 0, count );

			rebuildPartitionTables();
		}

		sortTransparent( arrCam );

		_listInstances.clear();
		_listOpaqueBatches.clear();
		_listTransparentBatches.clear();
		_listAllBatches.clear();
		buildBatches();

		_lastContentHash = contentHash;
		Memory::copy( _lastCameraPos, arrCam, sizeof( _lastCameraPos ) );
		_bHasBuildCache = 1;
		_bCpuDirty		= 1;
	}

	bool GpuScene::upload( IRHIDevice* pDevice )
	{
		if ( pDevice == nullptr || _listInstances.empty() || _listAllBatches.empty() )
			return false;

		// RT-owned context: pack MaterialInstance overrides and upload meshes in a single pass.
		applyInstanceCbsVal( pDevice, _listAllBatches );
		uploadMeshesVal( pDevice, _listAllBatches );

		const size_t opaqueCount = _listOpaqueBatches.size();
		for ( size_t batchIndex = 0; batchIndex < opaqueCount; ++batchIndex )
		{
			_listOpaqueBatches[batchIndex]._materialCb	 = _listAllBatches[batchIndex]._materialCb;
			_listOpaqueBatches[batchIndex]._vertexBuffer = _listAllBatches[batchIndex]._vertexBuffer;
		}
		for ( size_t batchIndex = 0; batchIndex < _listTransparentBatches.size(); ++batchIndex )
		{
			_listTransparentBatches[batchIndex]._materialCb	  = _listAllBatches[opaqueCount + batchIndex]._materialCb;
			_listTransparentBatches[batchIndex]._vertexBuffer = _listAllBatches[opaqueCount + batchIndex]._vertexBuffer;
		}

		// CPU 스냅샷이 그대로면 인스턴스/간접 버퍼 재업로드 생략.
		if ( _bCpuDirty == 0 && _instanceBuffer != 0 && _indirectArgsBuffer != 0 )
			return true;

		const uint32 instanceCount = static_cast<uint32>( _listInstances.size() );
		const uint32 argsCount	   = static_cast<uint32>( _listAllBatches.size() );

		if ( _instanceBuffer == 0 || _instanceCapacity < instanceCount )
		{
			if ( _instanceBuffer != 0 )
			{
				if ( _instanceSrv != kInvalidDescriptorIndex )
					pDevice->getResource()->unregisterBindlessResource( _instanceSrv );
				pDevice->getResource()->destroyBuffer( _instanceBuffer );
				_instanceBuffer = 0;
				_instanceSrv	= kInvalidDescriptorIndex;
			}
			RHIBufferDesc desc{};
			desc._elementSize  = static_cast<uint32>( sizeof( GpuInstance ) );
			desc._elementCount = instanceCount;
			desc._sizeBytes	   = desc._elementSize * desc._elementCount;
			desc._usage		   = RHIBufferUsage::Structured | RHIBufferUsage::ShaderResource;
			desc._pInitialData = _listInstances.data();
			_instanceBuffer	   = pDevice->getResource()->createBuffer( desc );
			if ( _instanceBuffer == 0 )
			{
				_instanceBuffer = pDevice->getResource()->createStructuredBuffer( desc._elementSize, desc._elementCount );
				if ( _instanceBuffer != 0 )
					pDevice->getResource()->updateStructuredBuffer( _instanceBuffer, _listInstances.data(), desc._sizeBytes );
			}
			if ( _instanceBuffer != 0 )
			{
				_instanceSrv	  = pDevice->getResource()->registerBindlessResource( _instanceBuffer );
				_instanceCapacity = instanceCount;
			}
		}
		else
		{
			pDevice->getResource()->updateStructuredBuffer( _instanceBuffer, _listInstances.data(),
															instanceCount * static_cast<uint32>( sizeof( GpuInstance ) ) );
		}

		vector<RHIDrawIndirectCommand> listCmds( argsCount );
		for ( uint32 argIndex = 0; argIndex < argsCount; ++argIndex )
		{
			listCmds[argIndex]._vertexCount			  = _listAllBatches[argIndex]._vertexCount;
			listCmds[argIndex]._instanceCount		  = _listAllBatches[argIndex]._instanceCount;
			listCmds[argIndex]._startVertexLocation	  = 0;
			listCmds[argIndex]._startInstanceLocation = _listAllBatches[argIndex]._instanceBase;
		}

		if ( _indirectArgsBuffer == 0 || _argsCapacity < argsCount )
		{
			if ( _indirectArgsBuffer != 0 )
			{
				if ( _indirectArgsUav != kInvalidDescriptorIndex )
					pDevice->getResource()->unregisterBindlessUAV( _indirectArgsUav );
				pDevice->getResource()->destroyBuffer( _indirectArgsBuffer );
				_indirectArgsBuffer = 0;
				_indirectArgsUav	= kInvalidDescriptorIndex;
			}
			RHIBufferDesc desc{};
			desc._elementSize	= static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) );
			desc._elementCount	= argsCount;
			desc._sizeBytes		= desc._elementSize * desc._elementCount;
			desc._usage			= RHIBufferUsage::UnorderedAccess | RHIBufferUsage::IndirectArgs | RHIBufferUsage::Raw | RHIBufferUsage::ShaderResource;
			desc._pInitialData	= listCmds.data();
			_indirectArgsBuffer = pDevice->getResource()->createBuffer( desc );
			if ( _indirectArgsBuffer == 0 )
			{
				_indirectArgsBuffer = pDevice->getResource()->createStructuredBuffer( desc._elementSize, desc._elementCount );
				if ( _indirectArgsBuffer != 0 )
					pDevice->getResource()->updateStructuredBuffer( _indirectArgsBuffer, listCmds.data(), desc._sizeBytes );
			}
			if ( _indirectArgsBuffer != 0 )
			{
				_indirectArgsUav = pDevice->getResource()->registerBindlessUAV( _indirectArgsBuffer );
				_argsCapacity	 = argsCount;
			}
		}
		else
		{
			pDevice->getResource()->updateStructuredBuffer( _indirectArgsBuffer, listCmds.data(),
															argsCount * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ) );
		}

		_indirectCommandCount = argsCount;
		_bCpuDirty			  = 0;

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
		_instanceBuffer		  = 0;
		_instanceSrv		  = kInvalidDescriptorIndex;
		_indirectArgsBuffer	  = 0;
		_indirectArgsUav	  = kInvalidDescriptorIndex;
		_instanceCapacity	  = 0;
		_argsCapacity		  = 0;
		_indirectCommandCount = 0;
		_bCpuDirty			  = 1;
		_materialRetire.flushAfterGpu( pDevice );
	}

	void GpuScene::sortTransparent( const float32* pCameraPos )
	{
		if ( pCameraPos == nullptr || _listScratchTransparentIdx.size() <= 1 )
			return;
		std::sort( _listScratchTransparentIdx.begin(), _listScratchTransparentIdx.end(), [&]( uint32 idxA, uint32 idxB )
		{ return distSq( _listScratchRaw[idxA]._boundsCenter, pCameraPos ) > distSq( _listScratchRaw[idxB]._boundsCenter, pCameraPos ); } );
	}

	void GpuScene::buildBatches()
	{
		_listInstances.reserve( _listScratchCandidates.size() );

		if ( _listScratchOpaqueEntries.empty() == false )
		{
			uint32 batchStart{ 0 };
			for ( uint32 entryIndex = 1; entryIndex <= _listScratchOpaqueEntries.size(); ++entryIndex )
			{
				if ( entryIndex == _listScratchOpaqueEntries.size() || ( _listScratchOpaqueEntries[entryIndex]._key == _listScratchOpaqueEntries[batchStart]._key ) == false )
				{
					const SortKey& key = _listScratchOpaqueEntries[batchStart]._key;
					GpuMeshBatch   batch{};
					batch._pMesh			 = key._pMesh;
					batch._vertexCount		 = batch._pMesh->getVertexCount();
					batch._instanceBase		 = static_cast<uint32>( _listInstances.size() );
					batch._instanceCount	 = entryIndex - batchStart;
					batch._blendMode		 = RHIBlendMode::Opaque;
					batch._pMaterialInstance = key._pInstance;
					if ( key._pInstance != nullptr )
						batch._materialCb = key._pInstance->getDescriptorIndex();
					else
						batch._materialCb = key._pMaterial ? key._pMaterial->getDescriptorIndex() : kInvalidDescriptorIndex;

					const uint32 batchIndex = static_cast<uint32>( _listAllBatches.size() );
					for ( uint32 batchEntryIndex = batchStart; batchEntryIndex < entryIndex; ++batchEntryIndex )
					{
						uint32		srcIdx	 = _listScratchOpaqueEntries[batchEntryIndex]._srcIdx;
						GpuInstance inst	 = _listScratchRaw[srcIdx];
						inst._meshBatchIndex = batchIndex;
						_listInstances.push_back( inst );
					}
					_listOpaqueBatches.push_back( batch );
					_listAllBatches.push_back( batch );

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
				bool	   bKeyChange{ false };
				if ( bEnd == false )
				{
					const DrawCandidate& a = _listScratchCandidates[_listScratchTransparentIdx[batchStart]];
					const DrawCandidate& b = _listScratchCandidates[_listScratchTransparentIdx[entryIndex]];
					bKeyChange			   = ( a._pMesh != b._pMesh ) || ( a._pMaterial != b._pMaterial ) || ( a._pInstance != b._pInstance );
				}
				if ( bEnd || bKeyChange )
				{
					const DrawCandidate& cand = _listScratchCandidates[_listScratchTransparentIdx[batchStart]];
					GpuMeshBatch		 batch{};
					batch._pMesh			 = cand._pMesh;
					batch._vertexCount		 = batch._pMesh->getVertexCount();
					batch._instanceBase		 = static_cast<uint32>( _listInstances.size() );
					batch._instanceCount	 = entryIndex - batchStart;
					batch._blendMode		 = RHIBlendMode::Transparent;
					batch._pMaterialInstance = cand._pInstance;
					if ( cand._pInstance != nullptr )
						batch._materialCb = cand._pInstance->getDescriptorIndex();
					else
						batch._materialCb = cand._pMaterial ? cand._pMaterial->getDescriptorIndex() : kInvalidDescriptorIndex;

					const uint32 batchIndex = static_cast<uint32>( _listAllBatches.size() );
					for ( uint32 batchEntryIndex = batchStart; batchEntryIndex < entryIndex; ++batchEntryIndex )
					{
						const uint32 srcIdx	 = _listScratchTransparentIdx[batchEntryIndex];
						GpuInstance	 inst	 = _listScratchRaw[srcIdx];
						inst._meshBatchIndex = batchIndex;
						_listInstances.push_back( inst );
					}
					_listTransparentBatches.push_back( batch );
					_listAllBatches.push_back( batch );
					batchStart = entryIndex;
				}
			}
		}

		syncMaterialPins();
	}
} // namespace sw
