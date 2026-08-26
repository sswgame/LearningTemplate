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
	 * @brief 에디터의 게임오브젝트 및 애셋 다중 선택 상태를 관리하는 정적 인터페이스 클래스
	 */
	class SelectionManager
	{
	public:
		SelectionManager()	= default;
		~SelectionManager() = default;

		// ------------------------------------------------------------------------------
		// 정적(Static) 공개 API
		// ------------------------------------------------------------------------------
		static void							selectObject( GameObjectPtr pObj, SelectionMode mode = SelectionMode::Replace );
		static void							selectObjects( const vector<GameObjectPtr>& listObjs, SelectionMode mode = SelectionMode::Replace );
		static bool							hasObject( const GameObjectPtr& pObj );
		static GameObjectPtr				getPrimaryObject();
		static uint64						getPrimaryObjectId();
		static const vector<GameObjectPtr>& getSelectedObjects();
		static size_t						getSelectedObjectCount();
		static void							selectAsset( string_view assetPath, SelectionMode mode = SelectionMode::Replace );
		static void							selectAssets( const vector<string>& listAssetPaths, SelectionMode mode = SelectionMode::Replace );
		static bool							hasAsset( string_view assetPath );
		static string_view					getPrimaryAsset();
		static const vector<string>&		getSelectedAssets();
		static void							clearObjectSelection();
		static void							clearAssetSelection();
		static void							clearAll();
		static void							pruneInvalid();
		static Delegate<void()>&			onSelectionChanged();

		// ------------------------------------------------------------------------------
		// 인스턴스 구현체 메서드 (EditorContext 소유)
		// ------------------------------------------------------------------------------
		void						 selectObjectImpl( GameObjectPtr pObj, SelectionMode mode );
		void						 selectObjectsImpl( const vector<GameObjectPtr>& listObjs, SelectionMode mode );
		bool						 hasObjectImpl( const GameObjectPtr& pObj ) const;
		GameObjectPtr				 getPrimaryObjectImpl() const;
		uint64						 getPrimaryObjectIdImpl() const;
		const vector<GameObjectPtr>& getSelectedObjectsImpl() const { return _listSelectedObjects; }
		size_t						 getSelectedObjectCountImpl() const { return _listSelectedObjects.size(); }
		void						 selectAssetImpl( string_view assetPath, SelectionMode mode );
		void						 selectAssetsImpl( const vector<string>& listAssetPaths, SelectionMode mode );
		bool						 hasAssetImpl( string_view assetPath ) const;
		string_view					 getPrimaryAssetImpl() const;
		const vector<string>&		 getSelectedAssetsImpl() const { return _listSelectedAssets; }
		void						 clearObjectSelectionImpl();
		void						 clearAssetSelectionImpl();
		void						 clearAllImpl();
		void						 pruneInvalidImpl();
		Delegate<void()>&			 onSelectionChangedImpl() { return _onSelectionChanged; }

	private:
		void notifyChanged();

	private:
		vector<GameObjectPtr> _listSelectedObjects;
		vector<string>		  _listSelectedAssets;
		Delegate<void()>	  _onSelectionChanged;
	};
} // namespace sw::editor
