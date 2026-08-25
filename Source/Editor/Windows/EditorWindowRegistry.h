#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Memory/Memory.h"

#include "Editor/Windows/IEditorWindow.h"

namespace sw
{
	/** @brief 에디터 윈도우 카테고리 */
	enum class EditorWindowCategory : uint8
	{
		Core = 0, // Hierarchy, Inspector, GameView, Console, Profiler, ContentBrowser
		Tool,	  // Sequencer, AnimationGraph, DialogueGraph, PrefabEditor, TileMap, SpriteClip
		Custom	  // 게임/플러그인 커스텀 도구
	};

	/** @brief 등록된 에디터 윈도우 항목 메타데이터 */
	struct EditorWindowEntry
	{
		string					  _title;
		string					  _menuPath;
		EditorWindowCategory	  _category{ EditorWindowCategory::Core };
		unique_ptr<IEditorWindow> _pInstance;
	};

	/**
	 * @class EditorWindowRegistry
	 * @brief 에디터 윈도우 및 도구 인스턴스를 중앙에서 등록 및 관리하는 정적 레지스트리
	 */
	class EditorWindowRegistry
	{
	public:
		EditorWindowRegistry()	= default;
		~EditorWindowRegistry() = default;

		// ------------------------------------------------------------------------------
		// 정적(Static) 공개 API
		// ------------------------------------------------------------------------------
		static void registerWindow( unique_ptr<IEditorWindow> pWindow,
									EditorWindowCategory	  category = EditorWindowCategory::Core,
									string_view				  menuPath = {} );

		template <typename TWindow, typename... TArgs>
		static TWindow* registerWindow( EditorWindowCategory category = EditorWindowCategory::Core,
										string_view			 menuPath = {}, TArgs&&... args )
		{
			auto	 pWindow = make_unique<TWindow>( std::forward<TArgs>( args )... );
			TWindow* pRaw	 = pWindow.get();
			registerWindow( std::move( pWindow ), category, menuPath );
			return pRaw;
		}

		static const vector<EditorWindowEntry>& getWindows();
		static vector<EditorWindowEntry>&		getWindowsMutable();
		static IEditorWindow*					findWindow( string_view title );
		static bool								setWindowOpen( string_view title, bool bOpen );
		static void								clear();
		static void								registerDefaultWindows();

		// ------------------------------------------------------------------------------
		// 인스턴스 메서드 (EditorContext 소유)
		// ------------------------------------------------------------------------------
		void							 registerWindowImpl( unique_ptr<IEditorWindow> pWindow, EditorWindowCategory category,
															 string_view menuPath );
		const vector<EditorWindowEntry>& getWindowsImpl() const { return _listWindows; }
		vector<EditorWindowEntry>&		 getWindowsImpl() { return _listWindows; }
		IEditorWindow*					 findWindowImpl( string_view title ) const;
		bool							 setWindowOpenImpl( string_view title, bool bOpen );
		void							 clearImpl();
		void							 registerDefaultWindowsImpl();

	private:
		vector<EditorWindowEntry> _listWindows;
	};
} // namespace sw
