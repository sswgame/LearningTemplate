/**
 * @file Material.cpp
 * @brief Material / MaterialInstance 구현
 */
#include "Core/CoreMinimal.h"

#include "Material.h"
#include "Core/Graphics/RHI/IRHIDevice.h"
#include "Core/Graphics/Shader/ShaderCompiler.h"
#include "Core/Graphics/Shader/ShaderReflection.h"

#include "Core/Utility/File/FileUtil.h"
#include "Core/Utility/Log/Logger.h"
#include "Core/Utility/Resource/ResourceUtil.h"
#include "Core/Utility/String/StringUtil.h"
#include "Core/Utility/Task/TaskManager.h"
#include <sstream>

namespace sw
{
	static struct PropertyTypeDesc
	{
		const utf8*			 name;
		MaterialPropertyType type;
		uint32				 size;
	} s_PropertyTypes[] = {
		{	  "Float",	   MaterialPropertyType::Float,	4},
		{  "Float2",	MaterialPropertyType::Float2,  8},
		{  "Float3",	MaterialPropertyType::Float3, 12},
		{  "Float4",	MaterialPropertyType::Float4, 16},
		{"Float4x4", MaterialPropertyType::Float4x4, 64},
		{	  "Uint",	  MaterialPropertyType::Uint,  4},
		{	  "Uint2",	   MaterialPropertyType::Uint2,	8},
		{	  "Uint3",	   MaterialPropertyType::Uint3, 12},
		{	  "Uint4",	   MaterialPropertyType::Uint4, 16},
		{	  "Int",		 MaterialPropertyType::Int,	4},
		{	  "Int2",	  MaterialPropertyType::Int2,  8},
		{	  "Int3",	  MaterialPropertyType::Int3, 12},
		{	  "Int4",	  MaterialPropertyType::Int4, 16},
	};

	static MaterialPropertyType stringToType( const std::string& str, uint32& outSize )
	{
		for ( const auto& desc : s_PropertyTypes )
		{
			if ( str == desc.name )
			{
				outSize = desc.size;
				return desc.type;
			}
		}
		outSize = 0;
		return MaterialPropertyType::Unknown;
	}

	static const utf8* typeToString( MaterialPropertyType type )
	{
		for ( const auto& desc : s_PropertyTypes )
		{
			if ( desc.type == type )
			{
				return desc.name;
			}
		}
		return "Unknown";
	}

	bool Material::initialize( IRHIDevice* rhi, const std::string& assetRelativePath )
	{
		if ( rhi == nullptr )
			return false;

		_rhiDevice = rhi;

		if ( loadFromFile( assetRelativePath ) == false )
		{
			SW_LOG_WARNING( "[Material] Failed to load material file '%#'. Using fallback defaults.", assetRelativePath.c_str() );
		}

		uint32 bufferSize = static_cast<uint32>( _data.buffer.size() );
		if ( bufferSize == 0 )
		{
			bufferSize = 256;
			_data.buffer.resize( bufferSize, 0 );
		}
		else
		{

			uint32 alignedSize = ( bufferSize + 255 ) & ~255u;
			_data.buffer.resize( alignedSize, 0 );
			bufferSize = alignedSize;
		}

		_constantBuffer = rhi->createConstantBuffer( bufferSize );
		if ( _constantBuffer == 0 )
		{
			SW_LOG_ERROR( "[Material] Failed to create Constant Buffer!" );
			return false;
		}

		rhi->updateConstantBuffer( _constantBuffer, _data.buffer.data(), bufferSize );
		_descriptorIndex = rhi->registerBindlessResource( _constantBuffer );

		SW_LOG_INFO( "[Material] Initialized '%#' with Bindless Descriptor Index %#", _name.c_str(), _descriptorIndex );
		return _descriptorIndex != kInvalidDescriptorIndex;
	}

	void Material::hotRefreshShader( IRHIDevice* rhi, const ShaderCompileResult& result )
	{
		if ( rhi == nullptr || result._bSuccess == false )
		{
			return;
		}

		if ( _constantBuffer != 0 )
		{
			rhi->updateConstantBuffer( _constantBuffer, _data.buffer.data(), static_cast<uint32>( _data.buffer.size() ) );
			SW_LOG_INFO( "[Material] HotRefresh '%#': Shader recompile detected, Constant Buffer re-uploaded. (Bytecode: %# bytes)", _name.c_str(), result._bytecode.size() );
		}
	}

	bool Material::loadFromFile( const std::string& assetRelativePath )
	{
		std::string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
		{
			absPath = assetRelativePath;
		}

		if ( FileUtil::isFileExist( absPath ) == false )
			return false;

		std::vector<uint8> fileData;
		if ( FileUtil::readFile( absPath, fileData ) == false )
			return false;

		std::stringstream file( std::string( reinterpret_cast<const utf8*>( fileData.data() ), fileData.size() ) );

		_data.properties.clear();
		_data.buffer.clear();
		uint32 currentOffset = 0;

		std::string line;
		while ( std::getline( file, line ) )
		{
			line = StringUtil::trim( line );
			if ( line.empty() || line[0] == '#' || line[0] == '/' )
				continue;

			auto colonPos = line.find( ':' );
			if ( colonPos == std::string::npos )
				continue;

			std::string key	  = StringUtil::trim( line.substr( 0, colonPos ) );
			std::string value = StringUtil::trim( line.substr( colonPos + 1 ) );

			if ( key == "name" )
			{
				_name = value;
			}
			else if ( key == "shader" )
			{
				_shaderPath = value;
			}
			else
			{
				auto spacePos = key.find( ' ' );
				if ( spacePos != std::string::npos )
				{
					std::string typeStr	 = key.substr( 0, spacePos );
					std::string propName = StringUtil::trim( key.substr( spacePos + 1 ) );

					uint32				 typeSize = 0;
					MaterialPropertyType type	  = stringToType( typeStr, typeSize );

					if ( type != MaterialPropertyType::Unknown )
					{

						uint32 align = ( typeSize > 4 && typeSize <= 16 ) ? 16 : 4;
						if ( typeSize == 64 )
							align = 16;
						currentOffset = ( currentOffset + align - 1 ) & ~( align - 1 );

						MaterialProperty prop;
						prop.name	= propName;
						prop.type	= type;
						prop.offset = currentOffset;
						prop.size	= typeSize;
						_data.properties.push_back( prop );

						if ( _data.buffer.size() < currentOffset + typeSize )
						{
							_data.buffer.resize( currentOffset + typeSize, 0 );
						}

						std::stringstream ss( value );
						if ( type == MaterialPropertyType::Float || type == MaterialPropertyType::Float2 || type == MaterialPropertyType::Float3 || type == MaterialPropertyType::Float4 || type == MaterialPropertyType::Float4x4 )
						{
							float32* ptr   = reinterpret_cast<float32*>( _data.buffer.data() + currentOffset );
							uint32	 count = typeSize / 4;
							for ( uint32 i = 0; i < count; ++i )
								ss >> ptr[i];
						}
						else if ( type == MaterialPropertyType::Uint || type == MaterialPropertyType::Uint2 || type == MaterialPropertyType::Uint3 || type == MaterialPropertyType::Uint4 )
						{
							uint32* ptr	  = reinterpret_cast<uint32*>( _data.buffer.data() + currentOffset );
							uint32	count = typeSize / 4;
							for ( uint32 i = 0; i < count; ++i )
								ss >> ptr[i];
						}
						else if ( type == MaterialPropertyType::Int || type == MaterialPropertyType::Int2 || type == MaterialPropertyType::Int3 || type == MaterialPropertyType::Int4 )
						{
							int32* ptr	 = reinterpret_cast<int32*>( _data.buffer.data() + currentOffset );
							uint32 count = typeSize / 4;
							for ( uint32 i = 0; i < count; ++i )
								ss >> ptr[i];
						}
						currentOffset += typeSize;
					}
				}
				else if ( key == "color" )
				{
					MaterialProperty prop;
					prop.name	  = "color";
					prop.type	  = MaterialPropertyType::Float4;
					prop.size	  = 16;
					currentOffset = ( currentOffset + 15 ) & ~15u;
					prop.offset	  = currentOffset;
					_data.properties.push_back( prop );
					if ( _data.buffer.size() < currentOffset + 16 )
						_data.buffer.resize( currentOffset + 16, 0 );

					float32* ptr = reinterpret_cast<float32*>( _data.buffer.data() + currentOffset );
					sscanf_s( value.c_str(), "%f %f %f %f", &ptr[0], &ptr[1], &ptr[2], &ptr[3] );
					currentOffset += 16;
				}
			}
		}

		uint32 alignedTotalSize = ( currentOffset + 255 ) & ~255u;
		_data.buffer.resize( alignedTotalSize, 0 );

		return true;
	}

	TaskHandle Material::loadFromFileAsync( const std::string& assetRelativePath )
	{
		return sw::getTaskManager().emplaceTask( "LoadMaterialAsync", SW_DELEGATE_LAMBDA( TaskDelegate, [this, assetRelativePath]()
		{
			this->loadFromFile( assetRelativePath );
		} ) );
	}

	bool Material::saveToFile( const std::string& assetRelativePath ) const
	{
		std::string absPath = ResourceUtil::getResourcePath( assetRelativePath );
		if ( absPath.empty() )
		{
			absPath = assetRelativePath;
		}

		std::ostringstream file;
		file << "# Material Asset File\n";
		file << "name: " << _name << "\n";
		file << "shader: " << _shaderPath << "\n";

		for ( const auto& prop : _data.properties )
		{
			file << typeToString( prop.type ) << " " << prop.name << ": ";

			uint32 count = prop.size / 4;
			if ( prop.type == MaterialPropertyType::Float || prop.type == MaterialPropertyType::Float2 || prop.type == MaterialPropertyType::Float3 || prop.type == MaterialPropertyType::Float4 || prop.type == MaterialPropertyType::Float4x4 )
			{
				const float32* ptr = reinterpret_cast<const float32*>( _data.buffer.data() + prop.offset );
				for ( uint32 i = 0; i < count; ++i )
					file << ptr[i] << ( i == count - 1 ? "" : " " );
			}
			else if ( prop.type == MaterialPropertyType::Uint || prop.type == MaterialPropertyType::Uint2 || prop.type == MaterialPropertyType::Uint3 || prop.type == MaterialPropertyType::Uint4 )
			{
				const uint32* ptr = reinterpret_cast<const uint32*>( _data.buffer.data() + prop.offset );
				for ( uint32 i = 0; i < count; ++i )
					file << ptr[i] << ( i == count - 1 ? "" : " " );
			}
			else if ( prop.type == MaterialPropertyType::Int || prop.type == MaterialPropertyType::Int2 || prop.type == MaterialPropertyType::Int3 || prop.type == MaterialPropertyType::Int4 )
			{
				const int32* ptr = reinterpret_cast<const int32*>( _data.buffer.data() + prop.offset );
				for ( uint32 i = 0; i < count; ++i )
					file << ptr[i] << ( i == count - 1 ? "" : " " );
			}
			file << "\n";
		}

		const std::string content = file.str();
		return FileUtil::writeFile( absPath, reinterpret_cast<const uint8*>( content.data() ), static_cast<uint64>( content.size() ) );
	}

	const void* Material::getPropertyData( const std::string& name ) const
	{
		for ( const auto& prop : _data.properties )
		{
			if ( prop.name == name )
			{
				return _data.buffer.data() + prop.offset;
			}
		}
		return nullptr;
	}

	void Material::setPropertyData( IRHIDevice* rhi, uint32 offset, uint32 size, const void* data )
	{
		if ( data == nullptr || offset + size > _data.buffer.size() )
			return;

		std::memcpy( _data.buffer.data() + offset, data, size );

		if ( rhi != nullptr && _constantBuffer != 0 )
		{
			rhi->updateConstantBuffer( _constantBuffer, _data.buffer.data(), static_cast<uint32>( _data.buffer.size() ) );
		}
	}

	void Material::shutdown( IRHIDevice* rhi )
	{
		if ( rhi != nullptr )
		{
			if ( _descriptorIndex != kInvalidDescriptorIndex )
			{
				rhi->unregisterBindlessResource( _descriptorIndex );
			}
			if ( _constantBuffer != 0 )
			{
				rhi->destroyBuffer( _constantBuffer );
			}
		}
		_constantBuffer	 = 0;
		_descriptorIndex = kInvalidDescriptorIndex;
	}

	MaterialInstance::MaterialInstance( Material* parentMaterial )
		: _parentMaterial{ parentMaterial }
	{
	}

	void MaterialInstance::setParent( Material* parentMaterial )
	{
		_parentMaterial = parentMaterial;
	}

	void MaterialInstance::setScalarParameter( hashed_string name, float32 value )
	{
		_scalarOverrides.insert_or_assign( name, value );
	}

	float32 MaterialInstance::getScalarParameter( hashed_string name, float32 defaultValue ) const
	{
		std::unordered_map<hashed_string, float32>::const_iterator iter = _scalarOverrides.find( name );
		if ( iter != _scalarOverrides.end() )
		{
			return iter->second;
		}
		return defaultValue;
	}

	void MaterialInstance::setVectorParameter( hashed_string name, const float32 color[4] )
	{
		if ( color != nullptr )
		{
			std::array<float32, 4> val = { color[0], color[1], color[2], color[3] };
			_vectorOverrides.insert_or_assign( name, val );
		}
	}

	const float32* MaterialInstance::getVectorParameter( hashed_string name ) const
	{
		std::unordered_map<hashed_string, std::array<float32, 4>>::const_iterator iter = _vectorOverrides.find( name );
		if ( iter != _vectorOverrides.end() )
		{
			return iter->second.data();
		}
		if ( _parentMaterial != nullptr )
		{
			return _parentMaterial->getPropertyData( "color" ) ? reinterpret_cast<const float32*>( _parentMaterial->getPropertyData( "color" ) ) : nullptr;
		}
		return nullptr;
	}

	void MaterialInstance::setTextureParameter( hashed_string name, RHIDescriptorIndex descIdx )
	{
		_textureOverrides.insert_or_assign( name, descIdx );
	}

	RHIDescriptorIndex MaterialInstance::getTextureParameter( hashed_string name ) const
	{
		std::unordered_map<hashed_string, RHIDescriptorIndex>::const_iterator iter = _textureOverrides.find( name );
		if ( iter != _textureOverrides.end() )
		{
			return iter->second;
		}
		if ( _parentMaterial != nullptr )
		{
			return _parentMaterial->getDescriptorIndex();
		}
		return kInvalidDescriptorIndex;
	}

	bool MaterialInstance::isParameterOverridden( hashed_string name ) const
	{
		if ( _scalarOverrides.find( name ) != _scalarOverrides.end() )
			return true;
		if ( _vectorOverrides.find( name ) != _vectorOverrides.end() )
			return true;
		if ( _textureOverrides.find( name ) != _textureOverrides.end() )
			return true;
		return false;
	}

	bool MaterialInstance::validateParametersWithReflection( const ShaderReflectionData& reflectionData ) const
	{
		auto checkParam = [&]( hashed_string paramName ) -> bool
		{
			for ( const auto& cb : reflectionData._constantBuffers )
			{
				for ( const auto& var : cb._variables )
				{
					if ( hashed_string( var._name.c_str() ) == paramName )
						return true;
				}
			}
			for ( const auto& res : reflectionData._resources )
			{
				if ( hashed_string( res._name.c_str() ) == paramName )
					return true;
			}
			return false;
		};

		for ( const auto& [name, val] : _scalarOverrides )
		{
			if ( checkParam( name ) == false )
				return false;
		}
		for ( const auto& [name, val] : _vectorOverrides )
		{
			if ( checkParam( name ) == false )
				return false;
		}
		for ( const auto& [name, val] : _textureOverrides )
		{
			if ( checkParam( name ) == false )
				return false;
		}
		return true;
	}

	void MaterialInstance::clearOverrides()
	{
		_scalarOverrides.clear();
		_vectorOverrides.clear();
		_textureOverrides.clear();
	}
} // namespace sw
