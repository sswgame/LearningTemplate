#pragma once
/**
 * @file IEditorPanel.h
 * @brief Editor ImGui 패널 공통 인터페이스
 */
#include "Runtime/EditorUIContext.h"

namespace sw
{
	class IRHIDevice;

	class IEditorPanel
	{
	public:
		virtual ~IEditorPanel() = default;

		virtual const char* getWindowTitle() const = 0;
		virtual void		draw( const EditorUIContext& ctx ) = 0;
		virtual void		preRender( IRHIDevice* /*rhiDevice*/ ) {}
		virtual void		shutdown( IRHIDevice* /*rhiDevice*/ ) {}
	};
}
