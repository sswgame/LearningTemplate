/**
 * @file EditorNodeGraph.h
 * @brief imgui-node-editor 컨텍스트 수명주기 및 캔버스 Begin/End
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace ax::NodeEditor
{
	struct EditorContext;
} // namespace ax::NodeEditor

namespace sw::editor
{
	/**
	 * @class EditorNodeGraph
	 * @brief 노드 그래프 캔버스 호스트. 패널·팝업·섹션이 멤버로 소유합니다.
	 */
	class EditorNodeGraph
	{
	public:
		EditorNodeGraph();
		~EditorNodeGraph();

		/** @brief 노드 에디터 컨텍스트를 해제합니다. */
		void shutdown();

		/** @brief 컨텍스트를 준비하고 캔버스를 엽니다. false면 endCanvas를 호출하지 않습니다. */
		bool beginCanvas( const utf8* pCanvasId, const utf8* pSettingsFileName );
		/** @brief beginCanvas()와 짝을 이룹니다. */
		void endCanvas();

		/** @brief Begin 없이 현재 에디터만 바인딩합니다. 위치 조회용. */
		bool bind() const;
		/** @brief bind()와 짝을 이룹니다. */
		void unbind() const;

		/** @brief 컨텍스트가 만들어져 있으면 true입니다. */
		bool isReady() const { return _pEditor != nullptr; }

		/** @brief 다음 Begin에서 노드 위치를 시드하고 뷰를 맞출지 여부입니다. */
		bool needsContentFit() const { return _bNeedsContentFit; }
		/** @brief 다음 캔버스에서 콘텐츠에 맞게 줌/팬하도록 요청합니다. */
		void requestContentFit() { _bNeedsContentFit = true; }
		/** @brief 캔버스 안에서 한 번만 NavigateToContent를 호출합니다. */
		void applyContentFitIfNeeded();

	private:
		void ensureContext( const utf8* pSettingsFileName );
		void destroyContext();

		ax::NodeEditor::EditorContext* _pEditor;
		string						   _settingsPath;
		bool						   _bNeedsContentFit;
	};
} // namespace sw::editor
