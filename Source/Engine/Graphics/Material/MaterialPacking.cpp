#include "pch.h"

#include "Engine/Common/EngineServices.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialInternal.h"
#include "Engine/Reflection/ReflectionTypes.h"
#include "Engine/Reflection/TypeRegistry.h"

namespace sw
{
	namespace
	{
		struct PropertyTypeDesc
		{
			const utf8*			 _pName;
			MaterialPropertyType _type;
			uint32				 _size; ///< Packed size when used as shader/CB type (0 = non-CB)
		};

		const PropertyTypeDesc s_PropertyTypes[] = {
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

		bool iequals( string_view a, const utf8* pB )
		{
			if ( pB == nullptr )
				return false;
			return StringUtil::equalsIgnoreCase( a, string_view( pB ) );
		}

		int64 resolveNamedValue( const MaterialProperty& prop, string_view token, bool bitFlagMode )
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

			for ( const MaterialEnumEntry& enumEntry : prop._listEnumEntries )
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

		int64 parseEnumOrFlags( const MaterialProperty& prop, string_view value, bool bitFlagMode )
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

		uint32 parseChannelMask( string_view value )
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

		bool writeNumericValue( uint8* pDst, uint32 capacity, MaterialPropertyType shaderType, string_view value )
		{
			const uint32 need = packedSizeOf( shaderType );
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

		string nodeText( XmlNode node )
		{
			if ( node.isValid() == false || node.text() == nullptr )
				return {};
			return StringUtil::trim( node.text() );
		}

		void parseEnumEntries( XmlNode parent, vector<MaterialEnumEntry>& out )
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
				e._name		   = fieldText( item, "name" );
				const string v = fieldText( item, "value" );
				if ( v.empty() == false )
				{
					utf8* end{ nullptr };
					e._value = static_cast<uint32>( StringUtil::strtoull( v.c_str(), &end, 0 ) );
				}
				if ( e._name.empty() == false )
					out.push_back( std::move( e ) );
			}
		}

		MaterialUsageFlags parseUsageFlags( string_view s )
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

		string usageFlagsToString( MaterialUsageFlags flags )
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

		void parseStringListItems( XmlNode list, vector<string>& out )
		{
			out.clear();
			if ( list.isValid() == false )
				return;
			for ( XmlNode item = list.child( "item" ); item; item = item.next( "item" ) )
			{
				string v = nodeText( item );
				if ( v.empty() )
					v = fieldText( item, "value" );
				if ( v.empty() )
					v = fieldText( item, "name" );
				if ( v.empty() == false )
					out.push_back( std::move( v ) );
			}
		}

		void appendMaterialStringList( XmlNode parent, const utf8* pTag, const vector<string>& values )
		{
			if ( values.empty() )
				return;
			XmlNode list = parent.appendChild( pTag );
			for ( const string& valueStr : values )
			{
				XmlNode item = list.appendChild( "item" );
				item.setValue( valueStr.c_str() );
			}
		}

	} // namespace

	MaterialPropertyType stringToType( string_view str, uint32& outSize )
	{
		for ( const PropertyTypeDesc& desc : s_PropertyTypes )
		{
			if ( iequals( str, desc._pName ) )
			{
				outSize = desc._size;
				return desc._type;
			}
		}
		outSize = 0;
		return MaterialPropertyType::Unknown;
	}

	const utf8* typeToString( MaterialPropertyType type )
	{
		for ( const PropertyTypeDesc& desc : s_PropertyTypes )
		{
			if ( desc._type == type )
				return desc._pName;
		}
		return "Unknown";
	}

	uint32 packedSizeOf( MaterialPropertyType type )
	{
		uint32 size{ 0 };
		stringToType( typeToString( type ), size );
		// Prefer first matching entry size for known enum values
		for ( const PropertyTypeDesc& desc : s_PropertyTypes )
		{
			if ( desc._type == type )
				return desc._size;
		}
		return size;
	}

	bool isTextureType( MaterialPropertyType type )
	{
		return type == MaterialPropertyType::Texture2D || type == MaterialPropertyType::TextureCube || type == MaterialPropertyType::Texture3D || type == MaterialPropertyType::Texture2DArray;
	}

	bool isNonBufferType( MaterialPropertyType type )
	{
		return type == MaterialPropertyType::Keyword;
	}

	MaterialPropertyType defaultShaderTypeFor( MaterialPropertyType cpuType )
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

	MaterialPropertyType shaderTypeFromReflectionName( string_view typeName, uint32 byteSize )
	{
		if ( typeName.empty() == false )
		{
			uint32					   ignored{ 0 };
			const MaterialPropertyType t = stringToType( typeName, ignored );
			if ( t != MaterialPropertyType::Unknown && isNonBufferType( t ) == false && isTextureType( t ) == false && t != MaterialPropertyType::Enum && t != MaterialPropertyType::BitFlag && t != MaterialPropertyType::Range && t != MaterialPropertyType::Color && t != MaterialPropertyType::ChannelMask && t != MaterialPropertyType::Bool )
				return t;
			// Bool from HLSL often reported as Bool
			if ( iequals( typeName, "Bool" ) )
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

	uint32 alignOffset( uint32 offset, uint32 typeSize )
	{
		uint32 align = 4;
		if ( typeSize > 4 && typeSize <= 16 )
			align = 16;
		if ( typeSize == 64 )
			align = 16;
		return ( offset + align - 1 ) & ~( align - 1 );
	}

	bool parseBoolToken( string_view token )
	{
		const string tokenNt( token );
		const string t = StringUtil::trim( tokenNt.c_str() );
		if ( t == "1" || iequals( t, "true" ) || iequals( t, "on" ) || iequals( t, "yes" ) )
			return true;
		return false;
	}

	bool packPropertyIntoBuffer( MaterialProperty& prop, vector<uint8>& buffer )
	{
		if ( isNonBufferType( prop._type ) )
		{
			prop._size = 0;
			return true;
		}

		MaterialPropertyType shaderType = prop._shaderType;
		if ( shaderType == MaterialPropertyType::Unknown )
			shaderType = defaultShaderTypeFor( prop._type );
		prop._shaderType = shaderType;

		uint32 packSize = packedSizeOf( shaderType );
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
				const uint32 boolVal = parseBoolToken( prop._value ) ? 1u : 0u;
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
				const int64	 enumVal  = parseEnumOrFlags( prop, prop._value, false );
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
				const uint32 uEnumVal = static_cast<uint32>( parseEnumOrFlags( prop, prop._value, true ) );
				Memory::copy( pDst, &uEnumVal, sizeof( uEnumVal ) );
				return true;
			}
			case MaterialPropertyType::ChannelMask:
			{
				const uint32 mask = parseChannelMask( prop._value );
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
				return writeNumericValue( pDst, packSize, shaderType, prop._value );
			case MaterialPropertyType::Keyword:
			case MaterialPropertyType::Unknown:
				return false;
		}
		return writeNumericValue( pDst, packSize, shaderType, prop._value );
	}

	/** @brief Attribute first, then same-name child element. */
	string fieldText( XmlNode node, const utf8* pName )
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

	bool parseBoolField( XmlNode node, const utf8* pName, bool defaultValue )
	{
		const string text = fieldText( node, pName );
		if ( text.empty() )
			return defaultValue;
		return parseBoolToken( text );
	}

	MaterialProperty parsePropertyNode( XmlNode item )
	{
		MaterialProperty prop{};
		prop._name = fieldText( item, "name" );
		uint32 size{ 0 };
		string typeStr = fieldText( item, "type" );
		if ( typeStr.empty() )
			typeStr = fieldText( item, "cpuType" );
		if ( typeStr.empty() == false )
			prop._type = stringToType( typeStr, size );
		const string shaderTypeStr = fieldText( item, "shaderType" );
		if ( shaderTypeStr.empty() == false )
			prop._shaderType = stringToType( shaderTypeStr, size );
		prop._defaultValue = fieldText( item, "defaultValue" );
		prop._value		   = fieldText( item, "value" );
		if ( prop._defaultValue.empty() && prop._value.empty() == false )
			prop._defaultValue = prop._value;
		if ( prop._value.empty() && prop._defaultValue.empty() == false )
			prop._value = prop._defaultValue;
		prop._assetPath		= fieldText( item, "assetPath" );
		prop._enumType		= fieldText( item, "enumType" );
		prop._displayName	= fieldText( item, "displayName" );
		prop._group			= fieldText( item, "group" );
		prop._tooltip		= fieldText( item, "tooltip" );
		prop._shaderKeyword = fieldText( item, "shaderKeyword" );
		const string minStr = fieldText( item, "min" );
		const string maxStr = fieldText( item, "max" );
		if ( minStr.empty() == false )
			prop._min = static_cast<float32>( StringUtil::atof( minStr.c_str() ) );
		if ( maxStr.empty() == false )
			prop._max = static_cast<float32>( StringUtil::atof( maxStr.c_str() ) );
		prop._bHdr		= parseBoolField( item, "bHdr", false );
		prop._bSrgb		= parseBoolField( item, "bSrgb", true );
		prop._bHidden	= parseBoolField( item, "bHidden", false );
		prop._bAdvanced = parseBoolField( item, "bAdvanced", false );
		parseEnumEntries( item, prop._listEnumEntries );
		return prop;
	}

	void appendAttr( XmlNode parent, const utf8* pName, string_view value )
	{
		if ( parent.isValid() == false || pName == nullptr || value.empty() )
			return;
		parent.appendAttr( pName, string( value ).c_str() );
	}

	void appendBoolAttr( XmlNode parent, const utf8* pName, bool value )
	{
		appendAttr( parent, pName, value ? "1" : "0" );
	}

	RHIBlendMode parseBlendMode( string_view s )
	{
		if ( iequals( s, "Transparent" ) || iequals( s, "AlphaBlend" ) )
			return RHIBlendMode::Transparent;
		return RHIBlendMode::Opaque;
	}

	const utf8* blendModeToString( RHIBlendMode mode )
	{
		return mode == RHIBlendMode::Transparent ? "Transparent" : "Opaque";
	}

	MaterialQualityLevel parseQuality( string_view s )
	{
		if ( iequals( s, "Low" ) )
			return MaterialQualityLevel::Low;
		if ( iequals( s, "Medium" ) || iequals( s, "Med" ) )
			return MaterialQualityLevel::Medium;
		if ( iequals( s, "Epic" ) )
			return MaterialQualityLevel::Epic;
		return MaterialQualityLevel::High;
	}

	const utf8* qualityToString( MaterialQualityLevel q )
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

	void parsePermutationNode( XmlNode root, MaterialPermutationDesc& out )
	{
		out = MaterialPermutationDesc{};
		if ( root.isValid() == false )
			return;
		XmlNode perm = root.child( "_permutations" );
		if ( perm.isValid() == false )
			return;

		const string quality = fieldText( perm, "quality" );
		if ( quality.empty() == false )
			out._quality = parseQuality( quality );
		const string lod = fieldText( perm, "shaderLOD" );
		if ( lod.empty() == false )
			out._shaderLOD = static_cast<uint32>( StringUtil::strtoull( lod.c_str(), nullptr, 10 ) );
		const string usage = fieldText( perm, "usage" );
		if ( usage.empty() == false )
			out._usage = parseUsageFlags( usage );

		XmlNode always = perm.child( "_alwaysDefines" );
		parseStringListItems( always, out._listAlwaysDefines );

		XmlNode switches = perm.child( "_staticSwitches" );
		if ( switches.isValid() )
		{
			for ( XmlNode item = switches.child( "item" ); item; item = item.next( "item" ) )
			{
				MaterialStaticSwitch entry{};
				entry._name			  = fieldText( item, "name" );
				entry._keyword		  = fieldText( item, "keyword" );
				entry._keywordOff	  = fieldText( item, "keywordOff" );
				entry._bEnabled		  = parseBoolField( item, "bEnabled", false );
				entry._bShaderFeature = parseBoolField( item, "bShaderFeature", true );
				if ( entry._name.empty() && entry._keyword.empty() == false )
					entry._name = entry._keyword;
				if ( entry._keyword.empty() == false || entry._name.empty() == false )
					out._listStaticSwitches.push_back( std::move( entry ) );
			}
		}

		XmlNode mcs = perm.child( "_multiCompiles" );
		if ( mcs.isValid() )
		{
			for ( XmlNode item = mcs.child( "item" ); item; item = item.next( "item" ) )
			{
				MaterialMultiCompile mc{};
				mc._name	 = fieldText( item, "name" );
				mc._selected = fieldText( item, "selected" );
				XmlNode opts = item.child( "_options" );
				parseStringListItems( opts, mc._listOptions );
				if ( mc._selected.empty() == false || mc._listOptions.empty() == false )
					out._listMultiCompiles.push_back( std::move( mc ) );
			}
		}
	}

	void appendPermutationNode( XmlNode root, const MaterialPermutationDesc& perm )
	{
		XmlNode node = root.appendChild( "_permutations" );
		appendAttr( node, "quality", qualityToString( perm._quality ) );
		appendAttr( node, "shaderLOD", to_string( perm._shaderLOD ) );
		appendAttr( node, "usage", usageFlagsToString( perm._usage ) );
		appendMaterialStringList( node, "_alwaysDefines", perm._listAlwaysDefines );

		if ( perm._listStaticSwitches.empty() == false )
		{
			XmlNode list = node.appendChild( "_staticSwitches" );
			for ( const MaterialStaticSwitch& entry : perm._listStaticSwitches )
			{
				XmlNode item = list.appendChild( "item" );
				appendAttr( item, "name", entry._name );
				appendAttr( item, "keyword", entry._keyword );
				if ( entry._keywordOff.empty() == false )
					appendAttr( item, "keywordOff", entry._keywordOff );
				appendBoolAttr( item, "bEnabled", entry._bEnabled );
				appendBoolAttr( item, "bShaderFeature", entry._bShaderFeature );
			}
		}

		if ( perm._listMultiCompiles.empty() == false )
		{
			XmlNode list = node.appendChild( "_multiCompiles" );
			for ( const MaterialMultiCompile& mc : perm._listMultiCompiles )
			{
				XmlNode item = list.appendChild( "item" );
				appendAttr( item, "name", mc._name );
				appendAttr( item, "selected", mc._selected );
				appendMaterialStringList( item, "_options", mc._listOptions );
			}
		}
	}

	void appendUniqueDefine( vector<string>& out, string_view def )
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

	void appendUsageDefines( MaterialUsageFlags usage, vector<string>& out )
	{
		if ( hasFlag( usage, MaterialUsageFlags::StaticMesh ) )
			appendUniqueDefine( out, "MATERIAL_USAGE_STATIC_MESH" );
		if ( hasFlag( usage, MaterialUsageFlags::SkeletalMesh ) )
			appendUniqueDefine( out, "MATERIAL_USAGE_SKELETAL_MESH" );
		if ( hasFlag( usage, MaterialUsageFlags::Instanced ) )
			appendUniqueDefine( out, "MATERIAL_USAGE_INSTANCED" );
		if ( hasFlag( usage, MaterialUsageFlags::Particles ) )
			appendUniqueDefine( out, "MATERIAL_USAGE_PARTICLES" );
		if ( hasFlag( usage, MaterialUsageFlags::Decal ) )
			appendUniqueDefine( out, "MATERIAL_USAGE_DECAL" );
		if ( hasFlag( usage, MaterialUsageFlags::UI ) )
			appendUniqueDefine( out, "MATERIAL_USAGE_UI" );
		if ( hasFlag( usage, MaterialUsageFlags::PostProcess ) )
			appendUniqueDefine( out, "MATERIAL_USAGE_POSTPROCESS" );
		if ( hasFlag( usage, MaterialUsageFlags::LightFunction ) )
			appendUniqueDefine( out, "MATERIAL_USAGE_LIGHTFUNCTION" );
		if ( hasFlag( usage, MaterialUsageFlags::MorphTargets ) )
			appendUniqueDefine( out, "MATERIAL_USAGE_MORPHTARGETS" );
		if ( hasFlag( usage, MaterialUsageFlags::SplineMesh ) )
			appendUniqueDefine( out, "MATERIAL_USAGE_SPLINEMESH" );
	}

	void appendQualityDefines( MaterialQualityLevel q, vector<string>& out )
	{
		appendUniqueDefine( out, string( "MATERIAL_QUALITY=" ) + to_string( static_cast<uint32>( q ) ) );
		switch ( q )
		{
			case MaterialQualityLevel::Low:
				appendUniqueDefine( out, "MATERIAL_QUALITY_LOW" );
				break;
			case MaterialQualityLevel::Medium:
				appendUniqueDefine( out, "MATERIAL_QUALITY_MEDIUM" );
				break;
			case MaterialQualityLevel::High:
				appendUniqueDefine( out, "MATERIAL_QUALITY_HIGH" );
				break;
			case MaterialQualityLevel::Epic:
				appendUniqueDefine( out, "MATERIAL_QUALITY_EPIC" );
				break;
			case MaterialQualityLevel::Count:
				appendUniqueDefine( out, "MATERIAL_QUALITY_HIGH" );
				break;
		}
	}

	uint64 hashDefines( const vector<string>& defs )
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
