#pragma once
/**
 * @file IImGuiPlatformBackend.h
 * @brief ImGui 플랫폼(윈도우/입력) 백엔드 추상 인터페이스
 */

#include "Core/Common/Common.h"
#include "Core/Graphics/RHI/RHITypes.h"
#include "Core/Window/NativeWindowEvent.h"

namespace sw
{
	class IWindow;

	class IImGuiPlatformBackend
	{
	public:
		virtual ~IImGuiPlatformBackend() = default;

		/** @brief 네이티브 윈도우와 RHI 백엔드에 맞춰 ImGui 플랫폼 레이어를 초기화합니다. */
		virtual bool initialize( IWindow* window, RHIBackend backendType ) = 0;
		/** @brief ImGui 플랫폼 리소스를 해제합니다. */
		virtual void shutdown()											   = 0;
		/** @brief 프레임 시작 시 디스플레이 크기·입력 상태를 갱신합니다. */
		virtual void newFrame()											   = 0;

		/** @brief 플랫폼 네이티브 이벤트를 ImGui로 전달합니다. */
		virtual bool processEvent( const NativeWindowEvent& event ) = 0;

		/** @brief 현재 플랫폼용 ImGui 플랫폼 백엔드를 생성합니다. */
		static std::unique_ptr<IImGuiPlatformBackend> createPlatformBackend();
	};
}
