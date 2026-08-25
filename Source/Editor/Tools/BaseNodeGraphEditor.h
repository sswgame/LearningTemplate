/**
 * @file BaseNodeGraphEditor.h
 * @brief imgui-node-editor 기반 노드 그래프 에디터 공통 컨텍스트 및 ID 매핑 헬퍼
 */
#pragma once
#include "Editor/Windows/IEditorWindow.h"

#include "Core/Common/Macros.h"
#include "Core/Common/Types.h"
#include "Core/Container/string.h"

namespace ax::NodeEditor
{
	struct EditorContext;
	struct NodeId;
	struct PinId;
	struct LinkId;
} // namespace ax::NodeEditor

namespace sw
{
	/**
	 * @struct NodeGraphIdHelper
	 * @brief 노드/핀/링크 정수 ID와 imgui-node-editor 식별자 간 변환 유틸리티
	 */
	struct NodeGraphIdHelper
	{
		static int32 makePinIn( int32 nodeId, int32 pinIndex = 0 )
		{
			return nodeId * 100 + pinIndex * 2 + 1;
		}

		static int32 makePinOut( int32 nodeId, int32 pinIndex = 0 )
		{
			return nodeId * 100 + pinIndex * 2 + 2;
		}

		static uintptr_t toRawId( int32 id )
		{
			return static_cast<uintptr_t>( id );
		}

		static int32 fromRawId( uintptr_t id )
		{
			return static_cast<int32>( id );
		}
	};

	/** @brief 노드 그래프 기본 노드 데이터 */
	struct GraphNodeBase
	{
		int32	_id{ 0 };
		string	_name;
		float32 _x{ 40.0f };
		float32 _y{ 40.0f };
	};

	/** @brief 노드 그래프 기본 링크 데이터 */
	struct GraphLinkBase
	{
		int32 _id{ 0 };
		int32 _fromPin{ 0 };
		int32 _toPin{ 0 };
	};

	/**
	 * @class BaseNodeGraphEditor
	 * @brief imgui-node-editor 컨텍스트 수명주기 및 캔버스 기본 동작을 관리하는 기반 클래스
	 */
	class BaseNodeGraphEditor : public IEditorWindow
	{
	public:
		BaseNodeGraphEditor( bool bDefaultOpen = false );
		virtual ~BaseNodeGraphEditor() override;

		void shutdown( IRHIDevice* pRhiDevice ) override;
		bool isToolWindow() const override { return true; }

	protected:
		void ensureEditorContext( const utf8* pSettingsFileName );
		void destroyEditorContext();

		/** @brief 노드 에디터 캔버스 시작 (컨텍스트 자동 생성 및 Begin 호출) */
		bool beginGraphCanvas( const utf8* pCanvasId, const utf8* pSettingsFileName );
		/** @brief 노드 에디터 캔버스 종료 */
		void endGraphCanvas();
		/** @brief 최초 진입 시 노드 전체를 포커스합니다. */
		void navigateToContentIfNeeded();

	protected:
		ax::NodeEditor::EditorContext* _pEditor;
		string						   _settingsPath;
		bool						   _bNavigatedToContent;
	};

} // namespace sw
