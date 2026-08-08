#pragma once
/**
 * @file IEditorWindow.h
 * @brief Dockable editor window interface (not shell chrome)
 */
#include "Core/Common/Types.h"

namespace sw
{
	class IRHIDevice;
	struct EditorUIContext;

	/** @brief Dockable ImGui window (Hierarchy, Inspector, Tools, ...) */
	class IEditorWindow
	{
	public:
		virtual ~IEditorWindow() = default;

		virtual const char* getWindowTitle() const = 0;
		virtual void		draw( const EditorUIContext& ctx ) = 0;
		virtual void		preRender( IRHIDevice* /*rhiDevice*/ ) {}
		virtual void		shutdown( IRHIDevice* /*rhiDevice*/ ) {}

		/** @brief On-demand tools start closed; core windows start open. */
		virtual bool isToolWindow() const { return false; }

		bool  isOpen() const { return _bOpen; }
		void  setOpen( bool open ) { _bOpen = open; }
		bool* getOpenPtr() { return &_bOpen; }

	protected:
		explicit IEditorWindow( bool bOpenByDefault = true )
			: _bOpen( bOpenByDefault )
		{
		}

	private:
		bool _bOpen = true;
	};
} // namespace sw
