/**
 * @file EditorDocumentPanel.h
 * @brief 포커스 애셋 경로와 연동되는 온디맨드 도구 패널
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/MathUtil.h"

#include "Editor/Common/Gui/IEditorPanel.h"
#include "Editor/Common/Workspace/EditorAssetType.h"

namespace sw::editor
{
	/**
	 * @class EditorDocumentPanel
	 * @brief 워크스페이스 포커스 경로가 지정 애셋 종류와 맞으면 문서를 바꿉니다.
	 */
	class EditorDocumentPanel : public IEditorPanel
	{
	public:
		bool		isToolPanel() const override { return true; }
		const utf8* getPanelTitle() const override;

	protected:
		/**
		 * @param kind 이 패널이 다루는 애셋 종류
		 * @param bLoadOnOpen true면 포커스가 없어도 첫 draw에서 로드를 요청합니다 (기본 문서).
		 */
		explicit EditorDocumentPanel( EditorAssetKind kind, bool bLoadOnOpen );

		/** @brief 포커스가 이 종류이고 현재 로드 경로와 다르면 true입니다. */
		bool hasNewFocusedDocument() const;
		/** @brief 매칭된 포커스 경로입니다. 매칭이 없으면 empty입니다. */
		string_view getMatchingFocusedPath() const;
		/** @brief 포커스 경로를 로드 경로로 확정하고 로드가 필요함을 표시합니다. */
		void acceptFocusedDocument();

		const string& getLoadedAssetPath() const { return _loadedAssetPath; }
		void		  markDocumentLoaded();
		bool		  isDocumentLoaded() const;

		template <typename TItem>
		static int32 nextItemId( const vector<TItem>& list )
		{
			int32 maxId{ 0 };
			for ( const TItem& item : list )
				maxId = MathUtil::max( maxId, item._id );
			return maxId + 1;
		}

	private:
		EditorAssetKind		   _kind;
		string				   _loadedAssetPath;
		uint8				   _bLoaded	 : 1;
		[[maybe_unused]] uint8 _reserved : 7;
	};
} // namespace sw::editor
