#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/RHI/IRHICommandContext.h"
#include "Engine/Graphics/RHI/IRHIResource.h"
#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
	using DrawIndexedInstancedIndirectCommand = RHIDrawIndexedIndirectCommand;

	/**
	 * @class IndirectDrawBuffer
	 * @brief GPU-Driven 렌더링을 위한 인디렉트 아규먼트 버퍼 빌더 및 디스패처
	 */
	class SW_API IndirectDrawBuffer
	{
	public:
		IndirectDrawBuffer();
		~IndirectDrawBuffer() = default;

		IndirectDrawBuffer( const IndirectDrawBuffer& )			   = delete;
		IndirectDrawBuffer& operator=( const IndirectDrawBuffer& ) = delete;

		IndirectDrawBuffer( IndirectDrawBuffer&& ) noexcept			   = default;
		IndirectDrawBuffer& operator=( IndirectDrawBuffer&& ) noexcept = default;

		void addDrawCommand( const DrawIndexedInstancedIndirectCommand& cmd );
		void addDrawCommand( uint32 indexCount, uint32 instanceCount, uint32 startIndex, int32 baseVertex, uint32 startInstance );
		void clear();

		size_t getCommandCount() const { return _listCommand.size(); }
		size_t getBufferSizeInBytes() const { return _listCommand.size() * sizeof( DrawIndexedInstancedIndirectCommand ); }

		const vector<DrawIndexedInstancedIndirectCommand>& getCommands() const { return _listCommand; }

		bool			uploadToBuffer( IRHIResource* pResource );
		void			releaseGpu( IRHIResource* pResource );
		RHIBufferHandle getGpuBufferHandle() const { return _gpuBufferHandle; }

		void drawAllIndirect( IRHICommandList* pCmdList ) const;

	private:
		vector<DrawIndexedInstancedIndirectCommand> _listCommand;
		RHIBufferHandle								_gpuBufferHandle;
	};
} // namespace sw
