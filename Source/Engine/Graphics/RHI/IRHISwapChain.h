#pragma once
#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"

#include "Engine/Graphics/RHI/RHITypes.h"

namespace sw
{
	/**
	 * @class IRHISwapChain
	 * @brief 디스플레이 출력을 위한 스왑체인 관리 인터페이스
	 */
	class SW_API IRHISwapChain
	{
	public:
		IRHISwapChain()									 = default;
		virtual ~IRHISwapChain()						 = default;
		IRHISwapChain( const IRHISwapChain& )			 = delete;
		IRHISwapChain& operator=( const IRHISwapChain& ) = delete;

		/** @brief 스왑체인과 백버퍼 크기를 바꿉니다. */
		virtual void resize( uint32 width, uint32 height ) = 0;

		/** @brief 프레임 렌더를 시작합니다 (백버퍼 클리어). */
		virtual void beginFrame( const float4& clearColor ) = 0;

		/** @brief 프레임 렌더를 끝냅니다. bPresent=false면 GPU submit만 하고 Present는 생략합니다. */
		virtual void endFrame( bool vsync = true, bool bPresent = true ) = 0;

		/**
		 * @brief 오프스크린 컬러 타깃에 렌더를 시작합니다.
		 * @note 기본 구현은 스왑체인 beginFrame으로 떨어집니다. 백엔드는 반드시 override 하세요.
		 */
		virtual void beginOffscreenPass( RHITextureHandle colorTarget, const float4& clearColor )
		{
			(void)colorTarget;
			beginFrame( clearColor );
		}

		/** @brief 오프스크린 패스를 끝내고 셰이더 샘플링 가능 상태로 전환합니다. */
		virtual void endOffscreenPass( RHITextureHandle colorTarget ) { (void)colorTarget; }

		/** @brief 네이티브 스왑체인 포인터. */
		virtual void* getNativeSwapChain() const = 0;
	};
} // namespace sw
