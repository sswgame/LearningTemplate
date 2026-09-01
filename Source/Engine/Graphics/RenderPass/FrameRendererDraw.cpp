#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RenderPass/FrameRenderer.h"
#include "Engine/Graphics/RenderPass/FrameRendererUtil.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Reflection/ReflectionCast.h"

namespace sw
{
	void FrameRenderer::drawSceneMeshes( RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass )
	{
		if ( _pDevice == nullptr || _pCmd == nullptr )
			return;

		if ( _bUseGpuDriven != 0 && _gpuScene.isUploaded() )
		{
			drawGpuBatches( pso, cbIndex, bTransparentPass );
			return;
		}

		if ( _gpuScene.getInstances().empty() == false )
		{
			drawGpuSceneMeshes( pso, cbIndex, bTransparentPass );
			return;
		}

		if ( pso != 0 )
			_pCmd->setPipelineState( pso );

		GameObjectManager* pObjects = ( _pScene != nullptr ) ? _pScene->getObjectManager() : nullptr;
		if ( pObjects == nullptr )
			return;

		if ( _bSceneTransformsFlushed == 0 )
		{
			pObjects->flushSceneTransforms();
			_bSceneTransformsFlushed = 1;
		}

		const utf8* pPassTypeForMat = bTransparentPass
										? FrameRendererUtil::PassType::kTransparent
										: FrameRendererUtil::PassType::kForwardOpaque;
		const utf8* pDefaultShader	= engine::getEngineData()._shaderForwardLit.c_str();

		uint32 drawn{ 0 };
		if ( pObjects != nullptr )
		{
			struct SceneMeshDrawItem
			{
				uint64				   _sortKey{ 0 };
				shared_ptr<Mesh>	   _mesh;
				MaterialInstance*	   _pMaterialInstance{ nullptr };
				RHIPipelineStateHandle _pso{ 0 };
				RHIDescriptorIndex	   _cbIndex{ 0 };
				float4x4			   _world;
			};

			vector<SceneMeshDrawItem> listDrawItem;
			pObjects->forEachGameObject( [&]( GameObject* pObj )
			{
				if ( pObj == nullptr || pObj->isActiveInHierarchy() == false )
					return;

				pObj->forEachComponent( [&]( Component* pComp )
				{
					MeshComponent* pMeshComp = castTo<MeshComponent>( pComp );
					if ( pMeshComp == nullptr || pMeshComp->isVisible() == false )
						return;

					const bool bTransparent = pMeshComp->getBlendMode() == RHIBlendMode::Transparent;
					if ( bTransparent != bTransparentPass )
						return;

					shared_ptr<Mesh> mesh = pMeshComp->getMesh();
					if ( mesh == nullptr || mesh->getVertexCount() == 0 )
						return;
					if ( mesh->upload( _pDevice ) == false )
						return;

					RHIPipelineStateHandle		 drawPso		  = pso;
					Material*					 pMaterial		  = pMeshComp->getMaterial();
					shared_ptr<MaterialInstance> materialInstance = pMeshComp->getMaterialInstance();
					if ( pMaterial != nullptr || materialInstance != nullptr )
					{
						const RHIPipelineStateHandle matPso = getOrCreateMaterialPassPso(
							pPassTypeForMat, pDefaultShader, true, pMaterial, materialInstance.get(),
							1, nullptr, bTransparentPass, bTransparentPass == false );
						if ( matPso != 0 )
							drawPso = matPso;
					}

					RHIDescriptorIndex drawCb = cbIndex;
					if ( materialInstance != nullptr )
						drawCb = materialInstance->getDescriptorIndex();
					else if ( pMaterial != nullptr )
						drawCb = pMaterial->getDescriptorIndex();

					const uint64 vbId	 = static_cast<uint64>( mesh->getVertexBuffer() ) & 0xFFFF;
					const uint64 sortKey = ( static_cast<uint64>( drawPso ) << 32 ) | ( static_cast<uint64>( drawCb ) << 16 ) | vbId;

					SceneMeshDrawItem item{};
					item._sortKey			= sortKey;
					item._mesh				= mesh;
					item._pMaterialInstance = materialInstance.get();
					item._pso				= drawPso;
					item._cbIndex			= drawCb;
					item._world				= pMeshComp->getWorldMatrix();
					listDrawItem.push_back( std::move( item ) );
				} );
			} );

			if ( listDrawItem.empty() == false )
			{
				std::sort( listDrawItem.begin(), listDrawItem.end(),
						   []( const SceneMeshDrawItem& lhs, const SceneMeshDrawItem& rhs )
				{ return lhs._sortKey < rhs._sortKey; } );

				RHIPipelineStateHandle lastPso	  = 0;
				RHIBufferHandle		   lastVb	  = 0;
				bool				   bFirstItem = true;

				for ( const auto& item : listDrawItem )
				{
					if ( item._pso != lastPso )
					{
						if ( item._pso != 0 )
							_pCmd->setPipelineState( item._pso );
						lastPso = item._pso;
					}

					if ( bFirstItem || _passConstants._world != item._world )
					{
						_passConstants._world = item._world;
						commitBindlessTextureBindings();
						bFirstItem = false;
					}

					if ( item._pMaterialInstance != nullptr )
						item._pMaterialInstance->applyToGpu( _pDevice );

					const RHIBufferHandle vb = item._mesh->getVertexBuffer();
					if ( vb != lastVb )
					{
						_pCmd->setVertexBuffer( 0, vb, sizeof( RHIVertex ), 0 );
						lastVb = vb;
					}

					_pCmd->draw( item._mesh->getVertexCount(), 0, item._cbIndex );
					++drawn;
				}
			}
		}

		if ( drawn == 0 )
		{
			setIdentityWorld();
			commitBindlessTextureBindings();
		}
	}

	void FrameRenderer::drawGpuSceneMeshes( RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass )
	{
		if ( _pDevice == nullptr || _pCmd == nullptr )
			return;
		if ( _gpuScene.getInstances().empty() )
			return;

		const vector<GpuMeshBatch>& batches =
			bTransparentPass ? _gpuScene.getTransparentBatches() : _gpuScene.getOpaqueBatches();
		const vector<GpuInstance>& listInstances = _gpuScene.getInstances();

		if ( pso != 0 )
			_pCmd->setPipelineState( pso );

		uint32 drawn{ 0 };
		bool   bFirstItem = true;
		for ( const GpuMeshBatch& batch : batches )
		{
			Mesh* pMesh = batch._pMesh;
			if ( pMesh == nullptr || pMesh->getVertexCount() == 0 )
				continue;
			if ( pMesh->upload( _pDevice ) == false )
				continue;

			const RHIBufferHandle vb = pMesh->getVertexBuffer();
			if ( vb == 0 )
				continue;
			_pCmd->setVertexBuffer( 0, vb, sizeof( RHIVertex ), 0 );

			const RHIDescriptorIndex drawCb =
				batch._materialCb != kInvalidDescriptorIndex ? batch._materialCb : cbIndex;
			if ( batch._pMaterialInstance != nullptr )
				batch._pMaterialInstance->applyToGpu( _pDevice );

			for ( uint32 instanceIndex = 0; instanceIndex < batch._instanceCount; ++instanceIndex )
			{
				const uint32 globalIndex = batch._instanceBase + instanceIndex;
				if ( globalIndex >= listInstances.size() )
					break;
				const GpuInstance& inst = listInstances[globalIndex];
				if ( bFirstItem || _passConstants._world != inst._world )
				{
					_passConstants._world = inst._world;
					commitBindlessTextureBindings();
					bFirstItem = false;
				}
				_pCmd->draw( pMesh->getVertexCount(), 0, drawCb );
				++drawn;
			}
		}

		if ( drawn == 0 )
		{
			setIdentityWorld();
			commitBindlessTextureBindings();
		}
	}

	void FrameRenderer::drawGpuBatches( RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex, bool bTransparentPass )
	{
		if ( _pDevice == nullptr || _pCmd == nullptr || _gpuScene.isUploaded() == false )
			return;

		if ( pso != 0 )
			_pCmd->setPipelineState( pso );

		setIdentityWorld();
		commitBindlessTextureBindings();

		const vector<GpuMeshBatch>& batches =
			bTransparentPass ? _gpuScene.getTransparentBatches() : _gpuScene.getOpaqueBatches();

		// Indirect slots are laid out opaque then transparent in upload order.
		uint32 batchOffset{ 0 };
		if ( bTransparentPass )
			batchOffset = static_cast<uint32>( _gpuScene.getOpaqueBatches().size() );

		for ( uint32 batchIndex = 0; batchIndex < batches.size(); ++batchIndex )
		{
			const GpuMeshBatch& batch = batches[batchIndex];
			if ( batch._vertexBuffer == 0 || batch._instanceCount == 0 )
				continue;
			_pCmd->setVertexBuffer( 0, batch._vertexBuffer, sizeof( RHIVertex ), 0 );
			const RHIDescriptorIndex drawCb =
				batch._materialCb != kInvalidDescriptorIndex ? batch._materialCb : cbIndex;
			_pCmd->drawIndirect( _gpuScene.getIndirectArgsBuffer(),
								 ( batchOffset + batchIndex ) * static_cast<uint32>( sizeof( RHIDrawIndirectCommand ) ), drawCb );
		}
	}

	void FrameRenderer::drawFullscreen( RHIPipelineStateHandle pso, RHIDescriptorIndex cbIndex )
	{
		if ( _pCmd == nullptr )
			return;
		setIdentityWorld();
		commitBindlessTextureBindings();
		_pCmd->setVertexBuffer( 0, 0, 0, 0 );
		if ( pso != 0 )
			_pCmd->setPipelineState( pso );
		_pCmd->draw( 3, 0, cbIndex != kInvalidDescriptorIndex ? cbIndex : 0 );
	}
} // namespace sw
