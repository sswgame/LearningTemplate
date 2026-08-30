/**
 * @file SelectionManager.h
 * @brief 에디터의 게임오브젝트 및 애셋 다중 선택 상태 관리자 (EditorContext 소유)
 */
#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"

#include "Engine/Object/GameObject/GameObjectPtr.h"

namespace sw::editor
{
	/** @brief 선택 모드 */
	enum class SelectionMode : uint8
	{
		Replace = 0, ///< 기존 선택을 지우고 새로 선택
		Add,		 ///< 기존 선택에 추가
		Remove,		 ///< 기존 선택에서 제거
		Toggle		 ///< 이미 선택되어 있으면 제거, 아니면 추가
	};

	/**
	 * @class SelectionManager
	 * @brief 에디터의 게임오브젝트 및 애셋 다중 선택 상태를 관리하는 멤버 클래스
	 */
	class SelectionManager
	{
	public:
		SelectionManager()	= default;
		~SelectionManager() = default;

		// ------------------------------------------------------------------------------
		// 멤버 메서드
		// ------------------------------------------------------------------------------
		void						 selectObject( GameObjectPtr pObj, SelectionMode mode = SelectionMode::Replace );
		void						 selectObjects( const vector<GameObjectPtr>& listObj, SelectionMode mode = SelectionMode::Replace );
		bool						 hasObject( const GameObjectPtr& pObj ) const;
		GameObjectPtr				 getPrimaryObject() const;
		uint64						 getPrimaryObjectId() const;
		const vector<GameObjectPtr>& getSelectedObjects() const { return _listSelectedObject; }
		size_t						 getSelectedObjectCount() const { return _listSelectedObject.size(); }

		void				  selectAsset( string_view assetPath, SelectionMode mode = SelectionMode::Replace );
		void				  selectAssets( const vector<string>& listAssetPath, SelectionMode mode = SelectionMode::Replace );
		bool				  hasAsset( string_view assetPath ) const;
		string_view			  getPrimaryAsset() const;
		const vector<string>& getSelectedAssets() const { return _listSelectedAsset; }

		void			  clearObjectSelection();
		void			  clearAssetSelection();
		void			  clearAll();
		void			  pruneInvalid();
		Delegate<void()>& onSelectionChanged() { return _onSelectionChanged; }

	private:
		void notifyChanged();

	private:
		vector<GameObjectPtr> _listSelectedObject;
		vector<string>		  _listSelectedAsset;
		Delegate<void()>	  _onSelectionChanged;
	};
} // namespace sw::editor
