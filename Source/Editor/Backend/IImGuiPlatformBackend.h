#pragma once
/**
 * @file IImGuiPlatformBackend.h
 * @brief Auto-generated documentation header
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

		virtual bool initialize( IWindow* window, RHIBackend backendType ) = 0;
		virtual void shutdown()											   = 0;
		virtual void newFrame()											   = 0;

		/** @brief 플랫폼 네이티브 이벤트를 ImGui로 전달 */
		virtual bool processEvent( const NativeWindowEvent& event ) = 0;

		static std::unique_ptr<IImGuiPlatformBackend> createPlatformBackend();
	};
}
