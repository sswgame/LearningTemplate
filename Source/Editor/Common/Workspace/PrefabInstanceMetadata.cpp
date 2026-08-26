#include "pch.h"

#include "Editor/Common/Workspace/PrefabInstanceMetadata.h"

namespace sw::editor
{
	bool PrefabInstanceMetadata::isOverridden( string_view componentName, string_view propertyName ) const
	{
		for ( const PrefabPropertyOverride& item : _listOverrides )
		{
			if ( item._componentName == componentName && item._propertyName == propertyName )
				return true;
		}
		return false;
	}

	void PrefabInstanceMetadata::setOverride( string_view componentName, string_view propertyName, string_view value )
	{
		for ( PrefabPropertyOverride& item : _listOverrides )
		{
			if ( item._componentName == componentName && item._propertyName == propertyName )
			{
				item._overrideValue = string{ value };
				return;
			}
		}

		PrefabPropertyOverride newOverride{};
		newOverride._componentName = string{ componentName };
		newOverride._propertyName  = string{ propertyName };
		newOverride._overrideValue = string{ value };
		_listOverrides.push_back( std::move( newOverride ) );
	}

	void PrefabInstanceMetadata::removeOverride( string_view componentName, string_view propertyName )
	{
		for ( size_t overrideIndex = 0; overrideIndex < _listOverrides.size(); )
		{
			const PrefabPropertyOverride& item = _listOverrides[overrideIndex];
			if ( item._componentName == componentName && item._propertyName == propertyName )
				_listOverrides.erase( _listOverrides.begin() + static_cast<ptrdiff_t>( overrideIndex ) );
			else
				++overrideIndex;
		}
	}
} // namespace sw::editor
