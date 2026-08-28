#include "pch.h"

#include "Core/Task/TaskManager.h"

#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RenderPass/FrameRenderer.h"
#include "Engine/Graphics/RenderPass/FrameRendererInternal.h"

namespace sw
{
	RHIPipelineStateHandle FrameRenderer::createEnginePso( string_view shaderPath, bool bDepthTest, uint32 numRenderTargets,
														   const RHIFormat* pRtvFormats, bool bBlend, bool bDepthWrite )
	{
		if ( _pDevice == nullptr )
			return 0;
		RHIPipelineStateDesc desc{};
		desc._vertexShaderPath	= shaderPath;
		desc._pixelShaderPath	= shaderPath;
		desc._bEnableDepthTest	= bDepthTest ? 1 : 0;
		desc._bEnableDepthWrite = bDepthWrite ? 1 : 0;
		desc._bEnableBlend		= bBlend ? 1 : 0;
		desc._cullMode			= bDepthTest ? RHICullMode::Back : RHICullMode::None;
		desc._numRenderTargets	= numRenderTargets > 0 ? numRenderTargets : 1;
		if ( desc._numRenderTargets > kMaxColorAttachments )
			desc._numRenderTargets = kMaxColorAttachments;
		for ( uint32 rtIndex = 0; rtIndex < desc._numRenderTargets; ++rtIndex )
		{
			desc._arrRtvFormats[rtIndex] = ( pRtvFormats != nullptr ) ? pRtvFormats[rtIndex] : RHIFormat::R8G8B8A8_UNORM;
		}
		return _pDevice->getResource()->createPipelineState( desc );
	}

	RHIPipelineStateHandle FrameRenderer::createPsoForPassType( string_view passType, string_view defaultShader,
																bool bDepthTest, uint32 numRenderTargets, const RHIFormat* pRtvFormats,
																bool bDefaultBlend, bool bDefaultDepthWrite,
																const vector<string>* pExtraDefines )
	{
		if ( _pDevice == nullptr )
			return 0;

		const RenderGraphPassDesc* pPassDesc = findPassDescByType( passType );
		RHIPipelineStateDesc	   desc{};
		desc._vertexShaderPath = ( pPassDesc != nullptr && pPassDesc->_shaderPath.empty() == false ) ? pPassDesc->_shaderPath : defaultShader;
		desc._pixelShaderPath  = desc._vertexShaderPath;
		desc._vertexEntryPoint = ( pPassDesc != nullptr && pPassDesc->_vertexEntryPoint.empty() == false )
								   ? pPassDesc->_vertexEntryPoint
								   : Entry::kVSMain;
		desc._pixelEntryPoint  = ( pPassDesc != nullptr && pPassDesc->_pixelEntryPoint.empty() == false )
								   ? pPassDesc->_pixelEntryPoint
								   : Entry::kPSMain;
		desc._bEnableDepthTest =
			pPassDesc != nullptr ? ( pPassDesc->_bEnableDepthTest != 0 ? 1 : 0 ) : ( bDepthTest ? 1 : 0 );
		desc._bEnableDepthWrite =
			pPassDesc != nullptr ? ( pPassDesc->_bEnableDepthWrite != 0 ? 1 : 0 ) : ( bDefaultDepthWrite ? 1 : 0 );
		desc._bEnableBlend = pPassDesc != nullptr ? ( pPassDesc->_bEnableBlend != 0 ? 1 : 0 ) : ( bDefaultBlend ? 1 : 0 );

		desc._cullMode = RHICullMode::Back;
		if ( pPassDesc != nullptr )
		{
			const string& cull = pPassDesc->_cullMode;
			if ( cull == "None" || cull == "none" )
				desc._cullMode = RHICullMode::None;
			else if ( cull == "Front" || cull == "front" )
				desc._cullMode = RHICullMode::Front;
			if ( pPassDesc->_listPermutation.empty() == false )
				desc._listShaderDefine = pPassDesc->_listPermutation;
		}
		else if ( bDepthTest == false )
			desc._cullMode = RHICullMode::None;

		if ( pExtraDefines != nullptr )
		{
			for ( const string& defineStr : *pExtraDefines )
			{
				bool found{ false };
				for ( const string& existing : desc._listShaderDefine )
				{
					if ( existing == defineStr )
					{
						found = true;
						break;
					}
				}
				if ( found == false )
					desc._listShaderDefine.push_back( defineStr );
			}
		}

		desc._numRenderTargets = numRenderTargets;
		if ( desc._numRenderTargets > kMaxColorAttachments )
			desc._numRenderTargets = kMaxColorAttachments;
		if ( pRtvFormats != nullptr )
		{
			for ( uint32 rtIndex = 0; rtIndex < desc._numRenderTargets; ++rtIndex )
			{
				desc._arrRtvFormats[rtIndex] = pRtvFormats[rtIndex];
			}
		}
		else if ( pPassDesc != nullptr && pPassDesc->_listOutput.empty() == false )
		{
			uint32 colorCount{ 0 };
			bool   bHasDepthOutput{ false };
			for ( const string& outName : pPassDesc->_listOutput )
			{
				if ( colorCount >= kMaxColorAttachments )
					break;
				for ( const RenderPassAttachment& att : _pipelineResource.getDesc()._listAttachment )
				{
					if ( att._name == outName )
					{
						const RHIFormat fmt = parseAttachmentFormat( att._format );
						if ( isDepthFormat( fmt ) == false )
						{
							desc._arrRtvFormats[colorCount++] = fmt;
						}
						else
						{
							bHasDepthOutput = true;
						}
						break;
					}
				}
			}
			desc._numRenderTargets = ( colorCount > 0 ) ? colorCount : ( bHasDepthOutput ? 0 : 1 );
			if ( desc._numRenderTargets == 1 && colorCount == 0 )
				desc._arrRtvFormats[0] = RHIFormat::R8G8B8A8_UNORM;
		}
		else
		{
			for ( uint32 rtIndex = 0; rtIndex < desc._numRenderTargets; ++rtIndex )
			{
				desc._arrRtvFormats[rtIndex] = RHIFormat::R8G8B8A8_UNORM;
			}
		}
		return _pDevice->getResource()->createPipelineState( desc );
	}

	void FrameRenderer::compileMaterialPsoTask( const TaskArgs& args )
	{
		const string			passTypeStr		   = args.get<string>( 0 );
		const string			defaultShaderStr   = args.get<string>( 1 );
		const bool				bDepthTest		   = args.get<bool>( 2 );
		const uint32			numRenderTargets   = args.get<uint32>( 3 );
		const vector<RHIFormat> rtvFormatsCopy	   = args.get<vector<RHIFormat>>( 4 );
		const bool				bDefaultBlend	   = args.get<bool>( 5 );
		const bool				bDefaultDepthWrite = args.get<bool>( 6 );
		const vector<string>	definesCopy		   = args.get<vector<string>>( 7 );
		const uint64			cacheKey		   = args.get<uint64>( 8 );

		const RHIPipelineStateHandle pso = createPsoForPassType(
			passTypeStr, defaultShaderStr, bDepthTest, numRenderTargets,
			rtvFormatsCopy.empty() ? nullptr : rtvFormatsCopy.data(),
			bDefaultBlend, bDefaultDepthWrite, &definesCopy );

		if ( pso != 0 )
		{
			std::scoped_lock<mutex> lock{ _psoMutex };
			_mapMaterialPassPso[cacheKey] = pso;
		}
	}

	RHIPipelineStateHandle FrameRenderer::getOrCreateMaterialPassPso( string_view passType, string_view defaultShader,
																	  bool bDepthTest, Material* pMaterial, MaterialInstance* pMaterialInstance,
																	  uint32 numRenderTargets, const RHIFormat* pRtvFormats,
																	  bool bDefaultBlend, bool bDefaultDepthWrite )
	{
		const vector<string>* pMatDefines = nullptr;
		uint64				  permHash{ 0 };
		if ( pMaterialInstance != nullptr )
		{
			pMatDefines = &pMaterialInstance->getCachedShaderDefines();
			permHash	= pMaterialInstance->getPermutationHash();
		}
		else if ( pMaterial != nullptr )
		{
			pMatDefines = &pMaterial->getCachedShaderDefines();
			permHash	= pMaterial->getPermutationHash();
		}

		if ( pMatDefines == nullptr || pMatDefines->empty() )
			return createPsoForPassType( passType, defaultShader, bDepthTest, numRenderTargets, pRtvFormats, bDefaultBlend, bDefaultDepthWrite );

		const uint64 passHash = std::hash<string_view>{}( passType );
		const uint64 cacheKey = passHash ^ ( permHash << 1 );

		{
			std::scoped_lock<mutex> lock{ _psoMutex };
			auto					it = _mapMaterialPassPso.find( cacheKey );
			if ( it != _mapMaterialPassPso.end() )
				return it->second;

			// Mark as pending (0) so we don't dispatch multiple compilation tasks.
			_mapMaterialPassPso.insert_or_assign( cacheKey, 0 );
		}

		if ( _pTaskManager != nullptr )
		{
			string			  passTypeStr( passType );
			string			  defaultShaderStr( defaultShader );
			vector<string>	  definesCopy = *pMatDefines;
			vector<RHIFormat> rtvFormatsCopy;
			if ( pRtvFormats != nullptr && numRenderTargets > 0 )
				rtvFormatsCopy.assign( pRtvFormats, pRtvFormats + numRenderTargets );

			TaskHandle handle = _pTaskManager->emplaceTask(
				"CompileMaterialPso",
				SW_DELEGATE_METHOD( TaskArgsDelegate, &FrameRenderer::compileMaterialPsoTask, this ),
				MakeTaskArgs( passTypeStr, defaultShaderStr, bDepthTest, numRenderTargets, rtvFormatsCopy, bDefaultBlend,
							  bDefaultDepthWrite, definesCopy, cacheKey ) );
			_pTaskManager->submit( handle );

			return 0; // Return pending
		}
		else
		{
			const RHIPipelineStateHandle pso =
				createPsoForPassType( passType, defaultShader, bDepthTest, numRenderTargets, pRtvFormats, bDefaultBlend, bDefaultDepthWrite, pMatDefines );
			if ( pso != 0 )
			{
				std::scoped_lock<mutex> lock{ _psoMutex };
				_mapMaterialPassPso.insert_or_assign( cacheKey, pso );
			}
			return pso;
		}
	}
} // namespace sw
