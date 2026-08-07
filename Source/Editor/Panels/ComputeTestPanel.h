#pragma once
/**
 * @file ComputeTestPanel.h
 * @brief Compute 셰이더 디스패치/간접 드로우 테스트 패널
 */
#include "Panels/IEditorPanel.h"
#include "Core/Graphics/RHI/RHITypes.h"

namespace sw
{
	/** @brief Compute UAV 디스패치와 간접 드로우를 검증하는 테스트 패널 */
	class ComputeTestPanel : public IEditorPanel
	{
	public:
		ComputeTestPanel() noexcept;

		const char* getWindowTitle() const override { return "Compute Test"; }
		/** @brief 테스트 컨트롤 UI를 그립니다. */
		void draw( const EditorUIContext& ctx ) override;
		/** @brief 요청된 compute 디스패치/드로우를 GPU에 기록합니다. */
		void preRender( IRHIDevice* rhiDevice ) override;
		/** @brief 테스트용 PSO·버퍼·디스크립터를 해제합니다. */
		void shutdown( IRHIDevice* rhiDevice ) override;

	private:
		/** @brief Compute 셰이더를 디스패치합니다. */
		void executeComputeDispatch( IRHIDevice* rhiDevice );
		/** @brief 간접 드로우 경로를 실행합니다. */
		void executeComputeDraw( IRHIDevice* rhiDevice );

		RHIPipelineStateHandle _csPso			  = 0;
		RHIPipelineStateHandle _indirectPso		  = 0;
		RHIBufferHandle		   _uavBuffer		  = 0;
		RHIBufferHandle		   _dispatchUavBuffer = 0;
		RHIDescriptorIndex	   _uavIndex		  = kInvalidDescriptorIndex;
		RHIDescriptorIndex	   _dispatchUavIndex  = kInvalidDescriptorIndex;

		uint8				   _bRequestComputeDispatch : 1;
		uint8				   _bComputeTestInitialized : 1;
		uint8				   _bComputeTestDispatched	: 1;
		[[maybe_unused]] uint8 _reservedFlags			: 5;
	};
} // namespace sw
