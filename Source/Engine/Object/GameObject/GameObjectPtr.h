#pragma once
#include "Core/Common/Types.h"
#include "Core/Concurrency/SpinLock.h"
#include "Core/String/hashed_string.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMacros.h"
#include "Engine/ECS/Entity.h"

namespace sw
{
	namespace generated
	{
		struct sw_GameObjectPtr_Registrar;
	} // namespace generated

	class GameObject;
	class GameObjectManager;

	/**
	 * @struct GameObjectPtr
	 * @brief 핫리로드 환경에서도 깨지지 않는 안전한 GameObject 참조 (Soft Object Pointer)
	 * @details 실제 포인터 대신 고유 이름을 저장하여, 언제든 현재 유효한 인스턴스를 동적으로 룩업합니다.
	 */
	REFLECT()
	struct SW_API GameObjectPtr
	{
		friend struct ::sw::generated::sw_GameObjectPtr_Registrar;
		REFLECT_BODY();

	public:
		GameObjectPtr();
		GameObjectPtr( GameObject* pTarget );
		GameObjectPtr( const GameObjectPtr& other );
		GameObjectPtr( GameObjectPtr&& other ) noexcept;
		~GameObjectPtr();

		GameObjectPtr& operator=( GameObject* pTarget );
		GameObjectPtr& operator=( const GameObjectPtr& other );
		GameObjectPtr& operator=( GameObjectPtr&& other ) noexcept;

		/** @brief 캐싱된 빠른 메모리 주소를 반환합니다. */
		GameObject* get() const { resolveLazy(); return _pCachedPtr; }

		bool isValid() const { resolveLazy(); return _pCachedPtr != nullptr; }

		GameObject* operator->() const { resolveLazy(); return _pCachedPtr; }
		explicit	operator bool() const { return isValid(); }

		bool operator==( const GameObjectPtr& other ) const { return _targetName == other._targetName; }
		bool operator!=( const GameObjectPtr& other ) const { return _targetName != other._targetName; }

		hashed_string getTargetName() const { return _targetName; }



	private:
		void resolveLazy() const;

	private:
		PROPERTY()
		hashed_string _targetName;

		mutable GameObject* _pCachedPtr{ nullptr };
		mutable sw::Entity  _cachedEntity{ sw::kNullEntity };
		mutable sw::GameObjectManager* _pManager{ nullptr };
	};

} // namespace sw
