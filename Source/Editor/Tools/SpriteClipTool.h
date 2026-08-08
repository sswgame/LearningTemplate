#pragma once
/**
 * @file SpriteClipTool.h
 * @brief Atlas sprite frame / transform key clip editor (Config/Editor/SpriteClip.json)
 */
#include "Windows/IEditorWindow.h"
#include "Core/Common/Types.h"
#include "Core/Common/CommonHeaders.h"

namespace sw
{
	/** @brief Frame list + optional TransformAnimation keys ??separate from AnimGraph */
	class SpriteClipTool : public IEditorWindow
	{
	public:
		SpriteClipTool();
		bool isToolWindow() const override { return true; }

		const char* getWindowTitle() const override { return "Sprite Clip"; }
		void		draw( const EditorUIContext& ctx ) override;

	private:
		struct Frame
		{
			float32 u			= 0.0f;
			float32 v			= 0.0f;
			float32 w			= 1.0f;
			float32 h			= 1.0f;
			int32	durationMs	= 100;
		};

		struct TransformKey
		{
			float32 time	 = 0.0f;
			float32 x		 = 0.0f;
			float32 y		 = 0.0f;
			float32 angleDeg = 0.0f;
		};

		void loadJson();
		void saveJson() const;

		char					   _atlasPath[256]{};
		std::vector<Frame>		   _frames;
		std::vector<TransformKey>  _keys;
		int32					   _selectedFrame = -1;
		int32					   _selectedKey	  = -1;
		std::string				   _status;
	};
} // namespace sw
