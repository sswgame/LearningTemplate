#pragma once
#include "Core/Common/Types.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"

namespace sw
{
	/** @brief 개별 프로퍼티 오버라이드 기록 */
	struct PrefabPropertyOverride
	{
		string _componentName;
		string _propertyName;
		string _overrideValue;
	};

	/**
	 * @class PrefabInstanceMetadata
	 * @brief 씬에 배치된 프리팹 인스턴스의 원본 경로와 프로퍼티 오버라이드
	 */
	class PrefabInstanceMetadata
	{
	public:
		PrefabInstanceMetadata()  = default;
		~PrefabInstanceMetadata() = default;

		void		  setPrefabPath( string_view path ) { _prefabPath = string{ path }; }
		const string& getPrefabPath() const { return _prefabPath; }

		/** @brief 특정 컴포넌트의 특정 프로퍼티가 오버라이드되었는지 확인합니다. */
		bool isOverridden( string_view componentName, string_view propertyName ) const;
		/** @brief 프로퍼티 오버라이드를 등록하거나 갱신합니다. */
		void setOverride( string_view componentName, string_view propertyName, string_view value );
		/** @brief 프로퍼티 오버라이드를 해제합니다. */
		void removeOverride( string_view componentName, string_view propertyName );
		/** @brief 모든 오버라이드를 지웁니다. */
		void clearOverrides() { _listOverrides.clear(); }

		const vector<PrefabPropertyOverride>& getOverrides() const { return _listOverrides; }
		size_t								  getOverrideCount() const { return _listOverrides.size(); }

	private:
		string						   _prefabPath;
		vector<PrefabPropertyOverride> _listOverrides;
	};
} // namespace sw
