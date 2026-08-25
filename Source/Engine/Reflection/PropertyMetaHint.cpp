#include "pch.h"

#include "Engine/Reflection/PropertyMetaHint.h"

namespace sw
{
	PropertyWidgetType PropertyMetaHint::deduceWidgetType( const PropertyMetadata& meta, string_view typeName )
	{
		if ( meta._bHasRange )
			return PropertyWidgetType::Slider;

		if ( meta._bAssetPath || meta._assetType.empty() == false )
			return PropertyWidgetType::AssetPicker;

		if ( typeName == "Color" || typeName == "sw::Color" || typeName == "LinearColor" || meta._category == "Color" )
			return PropertyWidgetType::ColorPicker;

		if ( typeName == "bool" || ( typeName == "uint8" && meta._displayName.rfind( "b", 0 ) == 0 ) )
			return PropertyWidgetType::Checkbox;

		return PropertyWidgetType::Default;
	}

	bool PropertyMetaHint::getSliderRange( const PropertyMetadata& meta, float32& outMin, float32& outMax )
	{
		if ( meta._bHasRange == 0 )
			return false;

		outMin = meta._minRange;
		outMax = meta._maxRange;
		return true;
	}

	const char* PropertyMetaHint::getAssetFilter( const PropertyMetadata& meta )
	{
		if ( meta._assetType == "Texture" || meta._assetType == "Sprite" )
			return "Image Files (*.png;*.jpg;*.dds)\0*.png;*.jpg;*.dds\0";
		if ( meta._assetType == "Material" )
			return "Material Files (*.material;*.mat)\0*.material;*.mat\0";
		if ( meta._assetType == "Shader" )
			return "Shader Files (*.hlsl;*.glsl)\0*.hlsl;*.glsl\0";
		if ( meta._assetType == "Scene" )
			return "Scene Files (*.scene;*.scene.xml)\0*.scene;*.scene.xml\0";
		if ( meta._assetType == "Audio" )
			return "Audio Files (*.wav;*.ogg;*.mp3)\0*.wav;*.ogg;*.mp3\0";

		return "All Files (*.*)\0*.*\0";
	}
} // namespace sw
