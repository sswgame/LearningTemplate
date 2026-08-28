#include "pch.h"

#include "Engine/Reflection/PropertyMetaHint.h"

#include "Engine/Reflection/ReflectionConstants.h"

namespace sw
{
	namespace
	{
		static bool isColorProperty( string_view typeName, const string& category )
		{
			for ( const utf8* colorType : constants::propertyHint::kArrColorTypes )
			{
				if ( typeName == colorType || category == colorType )
					return true;
			}
			return false;
		}
	} // namespace

	PropertyWidgetType PropertyMetaHint::deduceWidgetType( const PropertyMetadata& meta, string_view typeName )
	{
		if ( meta._bHasRange == SW_TRUE )
			return PropertyWidgetType::Slider;

		if ( meta._bAssetPath == SW_TRUE || meta._assetType.empty() == false )
			return PropertyWidgetType::AssetPicker;

		if ( isColorProperty( typeName, meta._category ) )
			return PropertyWidgetType::ColorPicker;

		if ( typeName == constants::propertyHint::kBool ||
			 ( typeName == constants::propertyHint::kUint8 &&
			   meta._displayName.rfind( constants::propertyHint::kBoolPrefix, 0 ) == 0 ) )
			return PropertyWidgetType::Checkbox;

		return PropertyWidgetType::Default;
	}

	bool PropertyMetaHint::getSliderRange( const PropertyMetadata& meta, float32& outMin, float32& outMax )
	{
		if ( meta._bHasRange == SW_FALSE )
			return false;

		outMin = meta._minRange;
		outMax = meta._maxRange;
		return true;
	}

	const utf8* PropertyMetaHint::getAssetFilter( const PropertyMetadata& meta )
	{
		for ( const constants::propertyHint::AssetFilterDef& mapping : constants::propertyHint::kArrAssetFilters )
		{
			if ( meta._assetType == mapping._assetType )
				return mapping._filter;
		}

		return constants::propertyHint::kFilterAll;
	}
} // namespace sw
