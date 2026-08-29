#include "pch.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialUtil.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
	namespace
	{
		struct MaterialPackingInternal
		{
			struct PropertyTypeDesc
			{
				const utf8*			 _pName;
				MaterialPropertyType _type;
				uint32				 _size; ///< Packed size when used as shader/CB type (0 = non-CB)
			};

			inline static const PropertyTypeDesc s_PropertyTypes[] = {
				{		  "Float",		   MaterialPropertyType::Float,	4},
				{		  "Float2",			MaterialPropertyType::Float2,  8},
				{		  "Float3",			MaterialPropertyType::Float3, 12},
				{		  "Float4",			MaterialPropertyType::Float4, 16},
				{	  "Float4x4",		  MaterialPropertyType::Float4x4, 64},
				{		  "Uint",			  MaterialPropertyType::Uint,  4},
				{		  "Uint2",		   MaterialPropertyType::Uint2,	8},
				{		  "Uint3",		   MaterialPropertyType::Uint3, 12},
				{		  "Uint4",		   MaterialPropertyType::Uint4, 16},
				{			  "Int",			 MaterialPropertyType::Int,	4},
				{		  "Int2",			  MaterialPropertyType::Int2,  8},
				{		  "Int3",			  MaterialPropertyType::Int3, 12},
				{		  "Int4",			  MaterialPropertyType::Int4, 16},
				{		  "Bool",			  MaterialPropertyType::Bool,  4},
				{		  "Range",		   MaterialPropertyType::Range,	4},
				{		  "Color",		   MaterialPropertyType::Color, 16},
				{		  "Enum",			  MaterialPropertyType::Enum,  4},
				{		  "BitFlag",		 MaterialPropertyType::BitFlag,	4},
				{	  "ChannelMask",	 MaterialPropertyType::ChannelMask,	4},
				{	  "Texture2D",	   MaterialPropertyType::Texture2D,	4},
				{	  "TextureCube",	 MaterialPropertyType::TextureCube,	4},
				{	  "Texture3D",	   MaterialPropertyType::Texture3D,	4},
				{"Texture2DArray", MaterialPropertyType::Texture2DArray,  4},
				{		  "Keyword",		 MaterialPropertyType::Keyword,	0},
				// Aliases (Unity / Unreal naming)
				{		  "Scalar",			MaterialPropertyType::Float,	 4},
				{		  "Vector",			MaterialPropertyType::Float4, 16},
				{		  "Vector2",		 MaterialPropertyType::Float2,  8},
				{		  "Vector3",		 MaterialPropertyType::Float3, 12},
				{		  "Vector4",		 MaterialPropertyType::Float4, 16},
				{		  "Matrix",		MaterialPropertyType::Float4x4, 64},
				{		  "Integer",			 MaterialPropertyType::Int,	4},
				{		  "Toggle",			MaterialPropertyType::Bool,	4},
				{  "StaticSwitch",		  MaterialPropertyType::Keyword,	 0},
				{		  "Cubemap",	 MaterialPropertyType::TextureCube,	4},
				{		  "Volume",		MaterialPropertyType::Texture3D,	 4},
			};

			static bool iequals( string_view a, const utf8* pB )
			{
				if ( pB == nullptr )
					return false;
				return StringUtil::equalsIgnoreCase( a, string_view( pB ) );
			}

			static int64 resolveNamedValue( const MaterialProperty& prop, string_view token, bool bitFlagMode )
			{
				const string tokenNt( token );
				const string name = StringUtil::trim( tokenNt.c_str() );
				if ( name.empty() )
					return 0;

				// Numeric literal
				{
					utf8*		end{ nullptr };
					const int64 v = StringUtil::strtoll( name.c_str(), &end, 0 );
					if ( end != nullptr && *end == '\0' )
						return v;
				}

				for ( const MaterialEnumEntry& enumEntry : prop._listEnumEntry )
				{
					if ( iequals( enumEntry._name, name.c_str() ) )
						return enumEntry._value;
				}

				if ( prop._enumType.empty() == false )
				{
					const EnumInfo* pInfo = engine::getTypeRegistry().findEnum( hashed_string( prop._enumType.c_str() ) );
					if ( pInfo != nullptr )
					{
						if ( bitFlagMode || pInfo->_bIsBitFlag )
							return pInfo->stringFlagsToValue( name );
						hashed_string key( name.c_str() );
						const auto	  it = pInfo->_mapNameToValue.find( key );
						if ( it != pInfo->_mapNameToValue.end() )
							return it->second;
					}
				}
				return 0;
			}

			static int64 parseEnumOrFlags( const MaterialProperty& prop, string_view value, bool bitFlagMode )
			{
				if ( bitFlagMode || prop._type == MaterialPropertyType::BitFlag )
				{
					if ( prop._enumType.empty() == false )
					{
						const EnumInfo* pInfo = engine::getTypeRegistry().findEnum( hashed_string( prop._enumType.c_str() ) );
						if ( pInfo != nullptr && pInfo->_bIsBitFlag )
							return pInfo->stringFlagsToValue( value );
					}
					int64			result{ 0 };
					string_splitter splitter( value, { "|", "," } );
					for ( string_view part : splitter.getSplitList() )
					{
						result |= resolveNamedValue( prop, string( part ), true );
					}
					return result;
				}
				return resolveNamedValue( prop, value, false );
			}

			static uint32 parseChannelMask( string_view value )
			{
				const string valueNt( value );
				const string v = StringUtil::trim( valueNt.c_str() );
				if ( v.empty() )
					return 0xFu;

				// Numeric
				utf8*		 end{ nullptr };
				const uint64 n = StringUtil::strtoull( v.c_str(), &end, 0 );
				if ( end != nullptr && *end == '\0' )
					return static_cast<uint32>( n );

				uint32 mask{ 0 };
				for ( utf8 charByte : v )
				{
					switch ( StringUtil::toUpperChar( charByte ) )
					{
						case 'R':
						case 'X':
							mask |= 1u << 0;
							break;
						case 'G':
						case 'Y':
							mask |= 1u << 1;
							break;
						case 'B':
						case 'Z':
							mask |= 1u << 2;
							break;
						case 'A':
						case 'W':
							mask |= 1u << 3;
							break;
						default:
							break;
					}
				}
				return mask;
			}

			static bool writeNumericValue( uint8* pDst, uint32 capacity, MaterialPropertyType shaderType, string_view value )
			{
				const uint32 need = MaterialUtil::packedSizeOf( shaderType );
				if ( need == 0 || capacity < need || pDst == nullptr )
					return false;

				string_splitter ss( value, { " ", "	", "," } );
				const auto&		tokens = ss.getSplitList();
				if ( shaderType == MaterialPropertyType::Float || shaderType == MaterialPropertyType::Float2 || shaderType == MaterialPropertyType::Float3 || shaderType == MaterialPropertyType::Float4 || shaderType == MaterialPropertyType::Float4x4 || shaderType == MaterialPropertyType::Range || shaderType == MaterialPropertyType::Color )
				{
					float32* pPtr  = reinterpret_cast<float32*>( pDst );
					uint32	 count = need / 4;
					for ( uint32 propIndex = 0; propIndex < count; ++propIndex )
					{
						float32 f{ 0.0f };
						if ( propIndex < tokens.size() )
						{
							string tokenStr( tokens[propIndex] );
							f = static_cast<float32>( StringUtil::atof( tokenStr.c_str() ) );
						}
						else
							f = ( propIndex == 3 && shaderType == MaterialPropertyType::Color ) ? 1.0f : 0.0f;
						pPtr[propIndex] = f;
					}
					return true;
				}
				if ( shaderType == MaterialPropertyType::Uint || shaderType == MaterialPropertyType::Uint2 || shaderType == MaterialPropertyType::Uint3 || shaderType == MaterialPropertyType::Uint4 )
				{
					uint32* pPtr  = reinterpret_cast<uint32*>( pDst );
					uint32	count = need / 4;
					for ( uint32 propIndex = 0; propIndex < count; ++propIndex )
					{
						uint32 u{ 0 };
						if ( propIndex < tokens.size() )
						{
							string tokenStr( tokens[propIndex] );
							u = static_cast<uint32>( StringUtil::atoll( tokenStr.c_str() ) );
						}
						pPtr[propIndex] = u;
					}
					return true;
				}
				if ( shaderType == MaterialPropertyType::Int || shaderType == MaterialPropertyType::Int2 || shaderType == MaterialPropertyType::Int3 || shaderType == MaterialPropertyType::Int4 )
				{
					int32* pPtr	 = reinterpret_cast<int32*>( pDst );
					uint32 count = need / 4;
					for ( uint32 propIndex = 0; propIndex < count; ++propIndex )
					{
						int32 n{ 0 };
						if ( propIndex < tokens.size() )
						{
							string tokenStr( tokens[propIndex] );
							n = StringUtil::atoi( tokenStr.c_str() );
						}
						pPtr[propIndex] = n;
					}
					return true;
				}
				return false;
			}

			static string nodeText( XmlNode node )
			{
				if ( node.isValid() == false || node.text() == nullptr )
					return {};
				return StringUtil::trim( node.text() );
			}

			static void parseEnumEntries( XmlNode parent, vector<MaterialEnumEntry>& out )
			{
				out.clear();
				if ( parent.isValid() == false )
					return;
				XmlNode list = parent.child( "_enumEntries" );
				if ( list.isValid() == false )
					list = parent.child( "enumEntries" );
				if ( list.isValid() == false )
					return;
				for ( XmlNode item = list.child( "item" ); item; item = item.next( "item" ) )
				{
					MaterialEnumEntry e{};
					e._name		   = MaterialUtil::fieldText( item, "name" );
					const string v = MaterialUtil::fieldText( item, "value" );
					if ( v.empty() == false )
					{
						utf8* end{ nullptr };
						e._value = static_cast<uint32>( StringUtil::strtoull( v.c_str(), &end, 0 ) );
					}
					if ( e._name.empty() == false )
						out.push_back( std::move( e ) );
				}
			}

			static MaterialUsageFlags parseUsageFlags( string_view s )
			{
				uint32			mask{ 0 };
				string_splitter splitter( s, { "|", ",", " " } );
				for ( string_view part : splitter.getSplitList() )
				{
					const string partNt( part );
					const string token = StringUtil::trim( partNt.c_str() );
					if ( token.empty() )
						continue;
					if ( iequals( token, "StaticMesh" ) )
						mask |= static_cast<uint32>( MaterialUsageFlags::StaticMesh );
					else if ( iequals( token, "SkeletalMesh" ) || iequals( token, "Skinned" ) )
						mask |= static_cast<uint32>( MaterialUsageFlags::SkeletalMesh );
					else if ( iequals( token, "Instanced" ) || iequals( token, "Instancing" ) )
						mask |= static_cast<uint32>( MaterialUsageFlags::Instanced );
					else if ( iequals( token, "Particles" ) || iequals( token, "Particle" ) )
						mask |= static_cast<uint32>( MaterialUsageFlags::Particles );
					else if ( iequals( token, "Decal" ) )
						mask |= static_cast<uint32>( MaterialUsageFlags::Decal );
					else if ( iequals( token, "UI" ) )
						mask |= static_cast<uint32>( MaterialUsageFlags::UI );
					else if ( iequals( token, "PostProcess" ) )
						mask |= static_cast<uint32>( MaterialUsageFlags::PostProcess );
					else if ( iequals( token, "LightFunction" ) )
						mask |= static_cast<uint32>( MaterialUsageFlags::LightFunction );
					else if ( iequals( token, "MorphTargets" ) )
						mask |= static_cast<uint32>( MaterialUsageFlags::MorphTargets );
					else if ( iequals( token, "SplineMesh" ) )
						mask |= static_cast<uint32>( MaterialUsageFlags::SplineMesh );
				}
				return static_cast<MaterialUsageFlags>( mask );
			}

			static string usageFlagsToString( MaterialUsageFlags flags )
			{
				string outStr;
				auto   append = [&]( MaterialUsageFlags flag, const utf8* pName )
				{
					if ( hasFlag( flags, flag ) == false )
						return;
					if ( outStr.empty() == false )
						outStr += "|";
					outStr += pName;
				};
				append( MaterialUsageFlags::StaticMesh, "StaticMesh" );
				append( MaterialUsageFlags::SkeletalMesh, "SkeletalMesh" );
				append( MaterialUsageFlags::Instanced, "Instanced" );
				append( MaterialUsageFlags::Particles, "Particles" );
				append( MaterialUsageFlags::Decal, "Decal" );
				append( MaterialUsageFlags::UI, "UI" );
				append( MaterialUsageFlags::PostProcess, "PostProcess" );
				append( MaterialUsageFlags::LightFunction, "LightFunction" );
				append( MaterialUsageFlags::MorphTargets, "MorphTargets" );
				append( MaterialUsageFlags::SplineMesh, "SplineMesh" );
				return outStr.empty() ? "None" : outStr;
			}

			static void parseStringListItems( XmlNode list, vector<string>& out )
			{
				out.clear();
				if ( list.isValid() == false )
					return;
				for ( XmlNode item = list.child( "item" ); item; item = item.next( "item" ) )
				{
					string v = nodeText( item );
					if ( v.empty() )
						v = MaterialUtil::fieldText( item, "value" );
					if ( v.empty() )
						v = MaterialUtil::fieldText( item, "name" );
					if ( v.empty() == false )
						out.push_back( std::move( v ) );
				}
			}

			static void appendMaterialStringList( XmlNode parent, const utf8* pTag, const vector<string>& values )
			{
				if ( values.empty() )
					return;
				XmlNode list = parent.appendChild( pTag );
				for ( const string& valueStr : values )
				{
					XmlNode item = list.appendChild( "item" );
					item.setValue( valueStr );
				}
			}
		};
	} // namespace
} // namespace sw

namespace sw
{
	MaterialPropertyType MaterialUtil::stringToType( string_view str, uint32& outSize )
	{
		for ( const MaterialPackingInternal::PropertyTypeDesc& desc : MaterialPackingInternal::s_PropertyTypes )
		{
			if ( MaterialPackingInternal::iequals( str, desc._pName ) )
			{
				outSize = desc._size;
				return desc._type;
			}
		}
		outSize = 0;
		return MaterialPropertyType::Unknown;
	}

	const utf8* MaterialUtil::typeToString( MaterialPropertyType type )
	{
		for ( const MaterialPackingInternal::PropertyTypeDesc& desc : MaterialPackingInternal::s_PropertyTypes )
		{
			if ( desc._type == type )
				return desc._pName;
		}
		return "Unknown";
	}

	uint32 MaterialUtil::packedSizeOf( MaterialPropertyType type )
	{
		uint32 size{ 0 };
		MaterialUtil::stringToType( MaterialUtil::typeToString( type ), size );
		// Prefer first matching entry size for known enum values
		for ( const MaterialPackingInternal::PropertyTypeDesc& desc : MaterialPackingInternal::s_PropertyTypes )
		{
			if ( desc._type == type )
				return desc._size;
		}
		return size;
	}

	bool MaterialUtil::isTextureType( MaterialPropertyType type )
	{
		return type == MaterialPropertyType::Texture2D || type == MaterialPropertyType::TextureCube || type == MaterialPropertyType::Texture3D || type == MaterialPropertyType::Texture2DArray;
	}

	bool MaterialUtil::isNonBufferType( MaterialPropertyType type )
	{
		return type == MaterialPropertyType::Keyword;
	}

	MaterialPropertyType MaterialUtil::defaultShaderTypeFor( MaterialPropertyType cpuType )
	{
		switch ( cpuType )
		{
			case MaterialPropertyType::Bool:
			case MaterialPropertyType::Enum:
			case MaterialPropertyType::BitFlag:
			case MaterialPropertyType::ChannelMask:
			case MaterialPropertyType::Texture2D:
			case MaterialPropertyType::TextureCube:
			case MaterialPropertyType::Texture3D:
			case MaterialPropertyType::Texture2DArray:
				return MaterialPropertyType::Uint;
			case MaterialPropertyType::Range:
				return MaterialPropertyType::Float;
			case MaterialPropertyType::Color:
				return MaterialPropertyType::Float4;
			case MaterialPropertyType::Keyword:
			case MaterialPropertyType::Unknown:
				return MaterialPropertyType::Unknown;
			case MaterialPropertyType::Float:
			case MaterialPropertyType::Float2:
			case MaterialPropertyType::Float3:
			case MaterialPropertyType::Float4:
			case MaterialPropertyType::Float4x4:
			case MaterialPropertyType::Uint:
			case MaterialPropertyType::Uint2:
			case MaterialPropertyType::Uint3:
			case MaterialPropertyType::Uint4:
			case MaterialPropertyType::Int:
			case MaterialPropertyType::Int2:
			case MaterialPropertyType::Int3:
			case MaterialPropertyType::Int4:
				return cpuType;
		}
		return cpuType;
	}

	MaterialPropertyType MaterialUtil::shaderTypeFromReflectionName( string_view typeName, uint32 byteSize )
	{
		if ( typeName.empty() == false )
		{
			uint32					   ignored{ 0 };
			const MaterialPropertyType t = MaterialUtil::stringToType( typeName, ignored );
			if ( t != MaterialPropertyType::Unknown && MaterialUtil::isNonBufferType( t ) == false && MaterialUtil::isTextureType( t ) == false && t != MaterialPropertyType::Enum && t != MaterialPropertyType::BitFlag && t != MaterialPropertyType::Range && t != MaterialPropertyType::Color && t != MaterialPropertyType::ChannelMask && t != MaterialPropertyType::Bool )
				return t;
			// Bool from HLSL often reported as Bool
			if ( MaterialPackingInternal::iequals( typeName, "Bool" ) )
				return MaterialPropertyType::Uint;
		}
		if ( byteSize == 4 )
			return MaterialPropertyType::Float;
		if ( byteSize == 8 )
			return MaterialPropertyType::Float2;
		if ( byteSize == 12 )
			return MaterialPropertyType::Float3;
		if ( byteSize == 16 )
			return MaterialPropertyType::Float4;
		if ( byteSize == 64 )
			return MaterialPropertyType::Float4x4;
		return MaterialPropertyType::Unknown;
	}

	uint32 MaterialUtil::alignOffset( uint32 offset, uint32 typeSize )
	{
		uint32 align = 4;
		if ( typeSize > 4 && typeSize <= 16 )
			align = 16;
		if ( typeSize == 64 )
			align = 16;
		return MathUtil::align( offset, align );
	}

	bool MaterialUtil::parseBoolToken( string_view token )
	{
		return StringUtil::parseBool( token, false );
	}

	bool MaterialUtil::packPropertyIntoBuffer( MaterialProperty& prop, vector<uint8>& buffer )
	{
		if ( MaterialUtil::isNonBufferType( prop._type ) )
		{
			prop._size = 0;
			return true;
		}

		MaterialPropertyType shaderType = prop._shaderType;
		if ( shaderType == MaterialPropertyType::Unknown )
			shaderType = MaterialUtil::defaultShaderTypeFor( prop._type );
		prop._shaderType = shaderType;

		uint32 packSize = MaterialUtil::packedSizeOf( shaderType );
		if ( packSize == 0 )
			packSize = 4;
		if ( prop._size == 0 )
			prop._size = packSize;
		else
			packSize = prop._size;

		if ( buffer.size() < prop._offset + packSize )
			buffer.resize( prop._offset + packSize, 0 );

		uint8* pDst = buffer.data() + prop._offset;

		switch ( prop._type )
		{
			case MaterialPropertyType::Bool:
			{
				const uint32 boolVal = MaterialUtil::parseBoolToken( prop._value ) ? 1u : 0u;
				if ( shaderType == MaterialPropertyType::Float || shaderType == MaterialPropertyType::Range )
				{
					const float32 floatVal = boolVal != 0 ? 1.0f : 0.0f;
					Memory::copy( pDst, &floatVal, sizeof( floatVal ) );
				}
				else
					Memory::copy( pDst, &boolVal, sizeof( boolVal ) );
				return true;
			}
			case MaterialPropertyType::Enum:
			{
				const int64	 enumVal  = MaterialPackingInternal::parseEnumOrFlags( prop, prop._value, false );
				const uint32 uEnumVal = static_cast<uint32>( enumVal );
				if ( shaderType == MaterialPropertyType::Int )
				{
					const int32 intVal = static_cast<int32>( enumVal );
					Memory::copy( pDst, &intVal, sizeof( intVal ) );
				}
				else if ( shaderType == MaterialPropertyType::Float )
				{
					const float32 floatVal = static_cast<float32>( enumVal );
					Memory::copy( pDst, &floatVal, sizeof( floatVal ) );
				}
				else
					Memory::copy( pDst, &uEnumVal, sizeof( uEnumVal ) );
				return true;
			}
			case MaterialPropertyType::BitFlag:
			{
				const uint32 uEnumVal = static_cast<uint32>( MaterialPackingInternal::parseEnumOrFlags( prop, prop._value, true ) );
				Memory::copy( pDst, &uEnumVal, sizeof( uEnumVal ) );
				return true;
			}
			case MaterialPropertyType::ChannelMask:
			{
				const uint32 mask = MaterialPackingInternal::parseChannelMask( prop._value );
				if ( shaderType == MaterialPropertyType::Float4 )
				{
					float32 arrComps[4] = {
						( mask & 1 ) != 0 ? 1.0f : 0.0f,
						( mask & 2 ) != 0 ? 1.0f : 0.0f,
						( mask & 4 ) != 0 ? 1.0f : 0.0f,
						( mask & 8 ) != 0 ? 1.0f : 0.0f,
					};
					Memory::copy( pDst, arrComps, sizeof( arrComps ) );
				}
				else
					Memory::copy( pDst, &mask, sizeof( mask ) );
				return true;
			}
			case MaterialPropertyType::Texture2D:
			case MaterialPropertyType::TextureCube:
			case MaterialPropertyType::Texture3D:
			case MaterialPropertyType::Texture2DArray:
			{
				uint32 textureIndex = prop._textureIndex;
				if ( textureIndex == kInvalidDescriptorIndex )
				{
					// Allow numeric override in _value
					if ( prop._value.empty() == false )
					{
						utf8*		 end{ nullptr };
						const uint64 numericVal = StringUtil::strtoull( prop._value.c_str(), &end, 0 );
						if ( end != nullptr && *end == '\0' )
							textureIndex = static_cast<uint32>( numericVal );
					}
					else
						textureIndex = 0;
				}
				Memory::copy( pDst, &textureIndex, sizeof( textureIndex ) );
				return true;
			}
			case MaterialPropertyType::Range:
			{
				float32 floatVal = static_cast<float32>( StringUtil::atof( prop._value.c_str() ) );
				if ( prop._min < prop._max )
					floatVal = (MathUtil::max)( prop._min, (MathUtil::min)( prop._max, floatVal ) );
				if ( shaderType == MaterialPropertyType::Float || packSize >= 4 )
					Memory::copy( pDst, &floatVal, sizeof( floatVal ) );
				return true;
			}
			case MaterialPropertyType::Color:
			case MaterialPropertyType::Float:
			case MaterialPropertyType::Float2:
			case MaterialPropertyType::Float3:
			case MaterialPropertyType::Float4:
			case MaterialPropertyType::Float4x4:
			case MaterialPropertyType::Uint:
			case MaterialPropertyType::Uint2:
			case MaterialPropertyType::Uint3:
			case MaterialPropertyType::Uint4:
			case MaterialPropertyType::Int:
			case MaterialPropertyType::Int2:
			case MaterialPropertyType::Int3:
			case MaterialPropertyType::Int4:
				return MaterialPackingInternal::writeNumericValue( pDst, packSize, shaderType, prop._value );
			case MaterialPropertyType::Keyword:
			case MaterialPropertyType::Unknown:
				return false;
		}
		return MaterialPackingInternal::writeNumericValue( pDst, packSize, shaderType, prop._value );
	}

	/** @brief Attribute first, then same-name child element. */
	string MaterialUtil::fieldText( XmlNode node, const utf8* pName )
	{
		if ( node.isValid() == false || pName == nullptr )
			return {};

		const utf8* pAttr = node.attr( pName, false );
		if ( pAttr != nullptr )
			return StringUtil::trim( pAttr );
		const utf8* pText = node.childText( pName, false );
		if ( pText != nullptr )
			return StringUtil::trim( pText );
		return {};
	}

	bool MaterialUtil::parseBoolField( XmlNode node, const utf8* pName, bool defaultValue )
	{
		const string text = MaterialUtil::fieldText( node, pName );
		if ( text.empty() )
			return defaultValue;
		return MaterialUtil::parseBoolToken( text );
	}

	MaterialProperty MaterialUtil::parsePropertyNode( XmlNode item )
	{
		MaterialProperty prop{};
		prop._name = MaterialUtil::fieldText( item, "name" );
		uint32 size{ 0 };
		string typeStr = MaterialUtil::fieldText( item, "type" );
		if ( typeStr.empty() )
			typeStr = MaterialUtil::fieldText( item, "cpuType" );
		if ( typeStr.empty() == false )
			prop._type = MaterialUtil::stringToType( typeStr, size );
		const string shaderTypeStr = MaterialUtil::fieldText( item, "shaderType" );
		if ( shaderTypeStr.empty() == false )
			prop._shaderType = MaterialUtil::stringToType( shaderTypeStr, size );
		prop._defaultValue = MaterialUtil::fieldText( item, "defaultValue" );
		prop._value		   = MaterialUtil::fieldText( item, "value" );
		if ( prop._defaultValue.empty() && prop._value.empty() == false )
			prop._defaultValue = prop._value;
		if ( prop._value.empty() && prop._defaultValue.empty() == false )
			prop._value = prop._defaultValue;
		prop._assetPath		= MaterialUtil::fieldText( item, "assetPath" );
		prop._enumType		= MaterialUtil::fieldText( item, "enumType" );
		prop._displayName	= MaterialUtil::fieldText( item, "displayName" );
		prop._group			= MaterialUtil::fieldText( item, "group" );
		prop._tooltip		= MaterialUtil::fieldText( item, "tooltip" );
		prop._shaderKeyword = MaterialUtil::fieldText( item, "shaderKeyword" );
		const string minStr = MaterialUtil::fieldText( item, "min" );
		const string maxStr = MaterialUtil::fieldText( item, "max" );
		if ( minStr.empty() == false )
			prop._min = static_cast<float32>( StringUtil::atof( minStr.c_str() ) );
		if ( maxStr.empty() == false )
			prop._max = static_cast<float32>( StringUtil::atof( maxStr.c_str() ) );
		prop._bHdr		= MaterialUtil::parseBoolField( item, "bHdr", false );
		prop._bSrgb		= MaterialUtil::parseBoolField( item, "bSrgb", true );
		prop._bHidden	= MaterialUtil::parseBoolField( item, "bHidden", false );
		prop._bAdvanced = MaterialUtil::parseBoolField( item, "bAdvanced", false );
		MaterialPackingInternal::parseEnumEntries( item, prop._listEnumEntry );
		return prop;
	}

	void MaterialUtil::appendAttr( XmlNode parent, const utf8* pName, string_view value )
	{
		if ( parent.isValid() == false || pName == nullptr || value.empty() )
			return;
		parent.appendAttr( pName, value );
	}

	void MaterialUtil::appendBoolAttr( XmlNode parent, const utf8* pName, bool value )
	{
		parent.appendAttr( pName, value );
	}

	RHIBlendMode MaterialUtil::parseBlendMode( string_view s )
	{
		if ( MaterialPackingInternal::iequals( s, "Transparent" ) || MaterialPackingInternal::iequals( s, "AlphaBlend" ) )
			return RHIBlendMode::Transparent;
		return RHIBlendMode::Opaque;
	}

	const utf8* MaterialUtil::blendModeToString( RHIBlendMode mode )
	{
		return mode == RHIBlendMode::Transparent ? "Transparent" : "Opaque";
	}

	MaterialQualityLevel MaterialUtil::parseQuality( string_view s )
	{
		if ( MaterialPackingInternal::iequals( s, "Low" ) )
			return MaterialQualityLevel::Low;
		if ( MaterialPackingInternal::iequals( s, "Medium" ) || MaterialPackingInternal::iequals( s, "Med" ) )
			return MaterialQualityLevel::Medium;
		if ( MaterialPackingInternal::iequals( s, "Epic" ) )
			return MaterialQualityLevel::Epic;
		return MaterialQualityLevel::High;
	}

	const utf8* MaterialUtil::qualityToString( MaterialQualityLevel q )
	{
		switch ( q )
		{
			case MaterialQualityLevel::Low:
				return "Low";
			case MaterialQualityLevel::Medium:
				return "Medium";
			case MaterialQualityLevel::High:
				return "High";
			case MaterialQualityLevel::Epic:
				return "Epic";
			case MaterialQualityLevel::Count:
				return "High";
		}
		return "High";
	}

	void MaterialUtil::parsePermutationNode( XmlNode root, MaterialPermutationDesc& out )
	{
		out = MaterialPermutationDesc{};
		if ( root.isValid() == false )
			return;
		XmlNode perm = root.child( "_permutations" );
		if ( perm.isValid() == false )
			return;

		const string quality = MaterialUtil::fieldText( perm, "quality" );
		if ( quality.empty() == false )
			out._quality = MaterialUtil::parseQuality( quality );
		const string lod = MaterialUtil::fieldText( perm, "shaderLOD" );
		if ( lod.empty() == false )
			out._shaderLOD = static_cast<uint32>( StringUtil::strtoull( lod.c_str(), nullptr, 10 ) );
		const string usage = MaterialUtil::fieldText( perm, "usage" );
		if ( usage.empty() == false )
			out._usage = MaterialPackingInternal::parseUsageFlags( usage );

		XmlNode always = perm.child( "_alwaysDefines" );
		MaterialPackingInternal::parseStringListItems( always, out._listAlwaysDefine );

		XmlNode switches = perm.child( "_staticSwitches" );
		if ( switches.isValid() )
		{
			for ( XmlNode item = switches.child( "item" ); item; item = item.next( "item" ) )
			{
				MaterialStaticSwitch entry{};
				entry._name			  = MaterialUtil::fieldText( item, "name" );
				entry._keyword		  = MaterialUtil::fieldText( item, "keyword" );
				entry._keywordOff	  = MaterialUtil::fieldText( item, "keywordOff" );
				entry._bEnabled		  = MaterialUtil::parseBoolField( item, "bEnabled", false );
				entry._bShaderFeature = MaterialUtil::parseBoolField( item, "bShaderFeature", true );
				if ( entry._name.empty() && entry._keyword.empty() == false )
					entry._name = entry._keyword;
				if ( entry._keyword.empty() == false || entry._name.empty() == false )
					out._listStaticSwitch.push_back( std::move( entry ) );
			}
		}

		XmlNode mcs = perm.child( "_multiCompiles" );
		if ( mcs.isValid() )
		{
			for ( XmlNode item = mcs.child( "item" ); item; item = item.next( "item" ) )
			{
				MaterialMultiCompile mc{};
				mc._name	 = MaterialUtil::fieldText( item, "name" );
				mc._selected = MaterialUtil::fieldText( item, "selected" );
				XmlNode opts = item.child( "_options" );
				MaterialPackingInternal::parseStringListItems( opts, mc._listOption );
				if ( mc._selected.empty() == false || mc._listOption.empty() == false )
					out._listMultiCompile.push_back( std::move( mc ) );
			}
		}
	}

	void MaterialUtil::appendPermutationNode( XmlNode root, const MaterialPermutationDesc& perm )
	{
		XmlNode node = root.appendChild( "_permutations" );
		MaterialUtil::appendAttr( node, "quality", MaterialUtil::qualityToString( perm._quality ) );
		node.appendAttr( "shaderLOD", perm._shaderLOD );
		MaterialUtil::appendAttr( node, "usage", MaterialPackingInternal::usageFlagsToString( perm._usage ) );
		MaterialPackingInternal::appendMaterialStringList( node, "_alwaysDefines", perm._listAlwaysDefine );

		if ( perm._listStaticSwitch.empty() == false )
		{
			XmlNode list = node.appendChild( "_staticSwitches" );
			for ( const MaterialStaticSwitch& entry : perm._listStaticSwitch )
			{
				XmlNode item = list.appendChild( "item" );
				MaterialUtil::appendAttr( item, "name", entry._name );
				MaterialUtil::appendAttr( item, "keyword", entry._keyword );
				if ( entry._keywordOff.empty() == false )
					MaterialUtil::appendAttr( item, "keywordOff", entry._keywordOff );
				MaterialUtil::appendBoolAttr( item, "bEnabled", entry._bEnabled );
				MaterialUtil::appendBoolAttr( item, "bShaderFeature", entry._bShaderFeature );
			}
		}

		if ( perm._listMultiCompile.empty() == false )
		{
			XmlNode list = node.appendChild( "_multiCompiles" );
			for ( const MaterialMultiCompile& mc : perm._listMultiCompile )
			{
				XmlNode item = list.appendChild( "item" );
				MaterialUtil::appendAttr( item, "name", mc._name );
				MaterialUtil::appendAttr( item, "selected", mc._selected );
				MaterialPackingInternal::appendMaterialStringList( item, "_options", mc._listOption );
			}
		}
	}

	void MaterialUtil::appendUniqueDefine( vector<string>& out, string_view def )
	{
		if ( def.empty() )
			return;
		for ( const string& existing : out )
		{
			if ( existing == def )
				return;
		}
		out.push_back( string( def ) );
	}

	void MaterialUtil::appendUsageDefines( MaterialUsageFlags usage, vector<string>& out )
	{
		if ( hasFlag( usage, MaterialUsageFlags::StaticMesh ) )
			MaterialUtil::appendUniqueDefine( out, "MATERIAL_USAGE_STATIC_MESH" );
		if ( hasFlag( usage, MaterialUsageFlags::SkeletalMesh ) )
			MaterialUtil::appendUniqueDefine( out, "MATERIAL_USAGE_SKELETAL_MESH" );
		if ( hasFlag( usage, MaterialUsageFlags::Instanced ) )
			MaterialUtil::appendUniqueDefine( out, "MATERIAL_USAGE_INSTANCED" );
		if ( hasFlag( usage, MaterialUsageFlags::Particles ) )
			MaterialUtil::appendUniqueDefine( out, "MATERIAL_USAGE_PARTICLES" );
		if ( hasFlag( usage, MaterialUsageFlags::Decal ) )
			MaterialUtil::appendUniqueDefine( out, "MATERIAL_USAGE_DECAL" );
		if ( hasFlag( usage, MaterialUsageFlags::UI ) )
			MaterialUtil::appendUniqueDefine( out, "MATERIAL_USAGE_UI" );
		if ( hasFlag( usage, MaterialUsageFlags::PostProcess ) )
			MaterialUtil::appendUniqueDefine( out, "MATERIAL_USAGE_POSTPROCESS" );
		if ( hasFlag( usage, MaterialUsageFlags::LightFunction ) )
			MaterialUtil::appendUniqueDefine( out, "MATERIAL_USAGE_LIGHTFUNCTION" );
		if ( hasFlag( usage, MaterialUsageFlags::MorphTargets ) )
			MaterialUtil::appendUniqueDefine( out, "MATERIAL_USAGE_MORPHTARGETS" );
		if ( hasFlag( usage, MaterialUsageFlags::SplineMesh ) )
			MaterialUtil::appendUniqueDefine( out, "MATERIAL_USAGE_SPLINEMESH" );
	}

	void MaterialUtil::appendQualityDefines( MaterialQualityLevel q, vector<string>& out )
	{
		MaterialUtil::appendUniqueDefine( out, string( "MATERIAL_QUALITY=" ) + to_string( static_cast<uint32>( q ) ) );
		switch ( q )
		{
			case MaterialQualityLevel::Low:
				MaterialUtil::appendUniqueDefine( out, "MATERIAL_QUALITY_LOW" );
				break;
			case MaterialQualityLevel::Medium:
				MaterialUtil::appendUniqueDefine( out, "MATERIAL_QUALITY_MEDIUM" );
				break;
			case MaterialQualityLevel::High:
				MaterialUtil::appendUniqueDefine( out, "MATERIAL_QUALITY_HIGH" );
				break;
			case MaterialQualityLevel::Epic:
				MaterialUtil::appendUniqueDefine( out, "MATERIAL_QUALITY_EPIC" );
				break;
			case MaterialQualityLevel::Count:
				MaterialUtil::appendUniqueDefine( out, "MATERIAL_QUALITY_HIGH" );
				break;
		}
	}

	uint64 MaterialUtil::hashDefines( const vector<string>& defs )
	{
		uint64 h = 14695981039346656037ull;
		for ( const string& defineStr : defs )
		{
			for ( const utf8 ch : defineStr )
			{
				h ^= static_cast<uint64>( static_cast<uint8>( ch ) );
				h *= 1099511628211ull;
			}
			h ^= 0xFFull;
			h *= 1099511628211ull;
		}
		return h;
	}
} // namespace sw
