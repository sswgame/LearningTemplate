#include "pch.h"

#include "Core/Math/MatrixMath.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Config/EngineData.h"
#include "Engine/ECS/Registry.h"
#include "Engine/ECS/View.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RenderPass/FrameRenderer.h"
#include "Engine/Graphics/RenderPass/FrameRendererInternal.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObjectManagerInternal.h"
#include "Engine/Reflection/ReflectionTypes.h"

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

		if ( pso != 0 )
			_pCmd->setPipelineState( pso );

		GameObjectManager* pObjects = ( _pScene != nullptr ) ? _pScene->getObjectManager() : nullptr;
		if ( pObjects == nullptr )
		{
			// Packet path without Scene*: draw from GpuScene CPU fallback counts via indirect
			if ( _gpuScene.isUploaded() )
				drawGpuBatches( pso, cbIndex, bTransparentPass );
			return;
		}

		if ( _bSceneTransformsFlushed == 0 )
		{
			pObjects->flushSceneTransforms();
			_bSceneTransformsFlushed = 1;
		}

		const utf8* pPassTypeForMat = bTransparentPass
										? PassType::kTransparent
										: PassType::kForwardOpaque;
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

			vector<SceneMeshDrawItem>					   listDrawItems;
			Registry&									   reg = GameObjectManagerAccess::get( *pObjects );
			View<MeshData, TransformData, EntityStateData> view( reg );

			for ( auto [e, mdata, tdata, state] : view )
			{
				if ( mdata._bVisible == 0 )
					continue;

				const bool bTransparent = mdata._blendMode == RHIBlendMode::Transparent;
				if ( bTransparent != bTransparentPass )
					continue;

				if ( state.bIsActiveInHierarchy == 0 || state.bIsPendingKill != 0 )
					continue;

				shared_ptr<Mesh> mesh = mdata._mesh;
				if ( mesh == nullptr || mesh->getVertexCount() == 0 )
					continue;
				if ( mesh->upload( _pDevice ) == false )
					continue;

				RHIPipelineStateHandle drawPso = pso;
				if ( mdata._pMaterial != nullptr || mdata._materialInstance != nullptr )
				{
					const RHIPipelineStateHandle matPso = getOrCreateMaterialPassPso(
						pPassTypeForMat, pDefaultShader, true, mdata._pMaterial, mdata._materialInstance.get(),
						1, nullptr, bTransparentPass, bTransparentPass == false );
					if ( matPso != 0 )
						drawPso = matPso;
				}

				RHIDescriptorIndex drawCb = cbIndex;
				if ( mdata._materialInstance != nullptr )
					drawCb = mdata._materialInstance->getDescriptorIndex();
				else if ( mdata._pMaterial != nullptr )
					drawCb = mdata._pMaterial->getDescriptorIndex();

				const uint64 vbId	 = static_cast<uint64>( mesh->getVertexBuffer() ) & 0xFFFF;
				const uint64 sortKey = ( static_cast<uint64>( drawPso ) << 32 ) | ( static_cast<uint64>( drawCb ) << 16 ) | vbId;

				SceneMeshDrawItem item{};
				item._sortKey			= sortKey;
				item._mesh				= mesh;
				item._pMaterialInstance = mdata._materialInstance.get();
				item._pso				= drawPso;
				item._cbIndex			= drawCb;
				item._world				= tdata.cachedWorldMatrix;
				listDrawItems.push_back( std::move( item ) );
			}

			if ( listDrawItems.empty() == false )
			{
				std::sort( listDrawItems.begin(), listDrawItems.end(),
						   []( const SceneMeshDrawItem& lhs, const SceneMeshDrawItem& rhs )
				{ return lhs._sortKey < rhs._sortKey; } );

				RHIPipelineStateHandle lastPso = 0;
				RHIBufferHandle		   lastVb  = 0;

				for ( const auto& item : listDrawItems )
				{
					if ( item._pso != lastPso )
					{
						if ( item._pso != 0 )
							_pCmd->setPipelineState( item._pso );
						lastPso = item._pso;
					}

					if ( Memory::compare( _passConstants._world, &item._world._11, sizeof( _passConstants._world ) ) != 0 )
					{
						Memory::copy( _passConstants._world, &item._world._11, sizeof( _passConstants._world ) );
						commitBindlessTextureBindings();
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
