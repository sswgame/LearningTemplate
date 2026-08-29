#pragma once
#include "Core/Common/Defines.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/String/fixed_string.h"

#include "Editor/Common/Commands/EditorToolAssetCommands.h"
#include "Editor/Common/Gui/EditorDocumentPanel.h"

namespace sw::editor
{
	/** @brief 프레임 목록과 선택적 TransformAnimation 키를 편집합니다 (AnimGraph과 별개) */
	class SpriteClipPanel : public EditorDocumentPanel
	{
	public:
		/** @brief 스프라이트 클립 도구를 생성합니다. */
		SpriteClipPanel();

		// ------------------------------------------------------------------------------
		// 1) IEditorPanel — 제목/그리기
		// ------------------------------------------------------------------------------
		/** @brief 스프라이트 클립 편집 UI를 그립니다. */
		void drawContent() override;
		bool saveDocument() override;

	private:
		string				 captureDocumentText() const override;
		void				 applyDocumentText( string_view text ) override;
		EditorSpriteClipData captureClipData() const;

	private:
		// ------------------------------------------------------------------------------
		// 2) 프레임 · 트랜스폼 키
		// ------------------------------------------------------------------------------
		using Frame		   = EditorSpriteClipFrame;
		using TransformKey = EditorSpriteClipKey;

		// ------------------------------------------------------------------------------
		// 3) SpriteClip.json 로드/저장
		// ------------------------------------------------------------------------------
		/** @brief SpriteClip.json을 불러옵니다. */
		void loadJson();
		/** @brief SpriteClip.json을 저장합니다. */
		void saveJson();

	private:
		fixed_string<constant::kMaxBuffer256> _atlasPath;
		vector<Frame>						  _listFrame;
		vector<TransformKey>				  _listKey;
		int32								  _selectedFrame;
		int32								  _selectedKey;
		string								  _status;
	};
} // namespace sw::editor
