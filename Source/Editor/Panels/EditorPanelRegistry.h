#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Common/Gui/IEditorPanel.h"

namespace sw
{
	class IRHIDevice;
} // namespace sw

namespace sw::editor
{
	/** @brief 에디터 패널 카테고리 */
	enum class EditorPanelCategory : uint8
	{
		Core = 0, // Hierarchy, Inspector, GameView, Console, Profiler, ContentBrowser
		Tool,	  // Sequencer, AnimationGraph, DialogueGraph, PrefabEditor, TileMap, SpriteClip
		Custom	  // 게임/플러그인 커스텀 패널
	};

	/** @brief 등록된 에디터 패널 항목 메타데이터 */
	struct EditorPanelEntry
	{
		string					 _title;
		string					 _menuPath;
		EditorPanelCategory		 _category{ EditorPanelCategory::Core };
		unique_ptr<IEditorPanel> _pInstance;
	};

	/**
	 * @class EditorPanelRegistry
	 * @brief 에디터 패널 인스턴스를 중앙에서 등록 및 관리하는 정적 레지스트리
	 */
	class EditorPanelRegistry
	{
	public:
		EditorPanelRegistry()  = default;
		~EditorPanelRegistry() = default;

		// ------------------------------------------------------------------------------
		// 정적(Static) 공개 API
		// ------------------------------------------------------------------------------
		static void registerPanel( unique_ptr<IEditorPanel> pPanel,
								   EditorPanelCategory		category = EditorPanelCategory::Core,
								   string_view				menuPath = {} );

		template <typename TPanel, typename... TArgs>
		static TPanel* registerPanel( EditorPanelCategory category = EditorPanelCategory::Core,
									  string_view		  menuPath = {}, TArgs&&... args )
		{
			auto	pPanel = make_unique<TPanel>( std::forward<TArgs>( args )... );
			TPanel* pRaw   = pPanel.get();
			registerPanel( std::move( pPanel ), category, menuPath );
			return pRaw;
		}

		static const vector<EditorPanelEntry>& getPanels();
		static vector<EditorPanelEntry>&	   getPanelsMutable();
		static IEditorPanel*				   findPanel( string_view title );
		static bool							   setPanelOpen( string_view title, bool bOpen );
		static void							   clear();
		static void							   registerDefaultPanels();
		static void							   drawOpenPanels();
		static void							   preRenderOpenPanels( IRHIDevice* pRhiDevice );
		static void							   shutdownAllPanels( IRHIDevice* pRhiDevice );

		// ------------------------------------------------------------------------------
		// 인스턴스 메서드 (EditorContext 소유)
		// ------------------------------------------------------------------------------
		void							registerPanelImpl( unique_ptr<IEditorPanel> pPanel, EditorPanelCategory category,
														   string_view menuPath );
		const vector<EditorPanelEntry>& getPanelsImpl() const { return _listPanels; }
		vector<EditorPanelEntry>&		getPanelsImpl() { return _listPanels; }
		IEditorPanel*					findPanelImpl( string_view title ) const;
		bool							setPanelOpenImpl( string_view title, bool bOpen );
		void							clearImpl();
		void							registerDefaultPanelsImpl();

	private:
		vector<EditorPanelEntry> _listPanels;
	};
} // namespace sw::editor
