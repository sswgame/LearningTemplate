#include "pch.h"

#include "Engine/Graphics/Shader/ShaderReflectionInternal.h"

namespace sw::shader_reflection_detail
{
	namespace
	{

		constexpr uint32 kSpirvMagic		 = 0x07230203u;
		constexpr uint32 kOpName			 = 5u;
		constexpr uint32 kOpMemberName		 = 6u;
		constexpr uint32 kOpDecorate		 = 71u;
		constexpr uint32 kOpMemberDecorate	 = 72u;
		constexpr uint32 kOpTypeBool		 = 20u;
		constexpr uint32 kOpTypeInt			 = 21u;
		constexpr uint32 kOpTypeFloat		 = 22u;
		constexpr uint32 kOpTypeVector		 = 23u;
		constexpr uint32 kOpTypeMatrix		 = 24u;
		constexpr uint32 kOpTypeImage		 = 25u;
		constexpr uint32 kOpTypeSampler		 = 26u;
		constexpr uint32 kOpTypeSampledImage = 27u;
		constexpr uint32 kOpTypeStruct		 = 30u;
		constexpr uint32 kOpTypePointer		 = 32u;
		constexpr uint32 kOpVariable		 = 59u;

		constexpr uint32 kDecorationBinding		  = 33u;
		constexpr uint32 kDecorationDescriptorSet = 34u;
		constexpr uint32 kDecorationOffset		  = 35u;

		constexpr uint32 kStorageClassUniformConstant = 0u;
		constexpr uint32 kStorageClassUniform		  = 2u;
		constexpr uint32 kStorageClassStorageBuffer	  = 12u;

		struct SpirvType
		{
			enum class Kind : uint8
			{
				Unknown,
				Bool,
				Int,
				Uint,
				Float,
				Vector,
				Matrix,
				Struct,
				Pointer,
				Image,
				Sampler,
				SampledImage
			};

			Kind		   _kind  = Kind::Unknown;
			uint32		   _width = 32;
			uint32		   _count{ 0 };
			uint32		   _subTypeId{ 0 };
			uint32		   _storageClass{ 0 };
			vector<uint32> _listMemberTypeIds;
		};

		string resolveSpirvTypeName( uint32 typeId, const unordered_map<uint32, SpirvType>& mapTypes, uint32& outSize )
		{
			auto it = mapTypes.find( typeId );
			if ( it == mapTypes.end() )
			{
				outSize = 4;
				return "Float";
			}

			const SpirvType& t = it->second;
			switch ( t._kind )
			{
				case SpirvType::Kind::Bool:
					outSize = 4;
					return "Bool";
				case SpirvType::Kind::Int:
					outSize = t._width / 8;
					return ( t._width == 64 ) ? "Int64" : ( t._width == 16 ? "Int16" : "Int" );
				case SpirvType::Kind::Uint:
					outSize = t._width / 8;
					return ( t._width == 64 ) ? "Uint64" : ( t._width == 16 ? "Uint16" : "Uint" );
				case SpirvType::Kind::Float:
					outSize = t._width / 8;
					return ( t._width == 64 ) ? "Double" : "Float";
				case SpirvType::Kind::Vector:
				{
					uint32 subSize = 4;
					string subName = resolveSpirvTypeName( t._subTypeId, mapTypes, subSize );
					outSize		   = subSize * t._count;
					return subName + to_string( t._count );
				}
				case SpirvType::Kind::Matrix:
				{
					uint32 colSize = 16;
					resolveSpirvTypeName( t._subTypeId, mapTypes, colSize );
					outSize = colSize * t._count;
					if ( t._count == 4 )
						return "Float4x4";
					return "Float" + to_string( t._count ) + "x" + to_string( t._count );
				}
				case SpirvType::Kind::Unknown:
				case SpirvType::Kind::Struct:
				case SpirvType::Kind::Pointer:
				case SpirvType::Kind::Image:
				case SpirvType::Kind::Sampler:
				case SpirvType::Kind::SampledImage:
				default:
					outSize = 4;
					return "Float";
			}
		}

	} // namespace

	ShaderReflectionData reflectSpirv( const vector<uint8>& bytecode )
	{
		if ( bytecode.size() < 20 || ( bytecode.size() % 4 ) != 0 )
		{
			SW_LOG_WARNING( "[ShaderReflection] SPIR-V bytecode size invalid." );
			return {};
		}

		const uint32* pWords	= reinterpret_cast<const uint32*>( bytecode.data() );
		const size_t  wordCount = bytecode.size() / 4;
		if ( pWords[0] != kSpirvMagic )
		{
			SW_LOG_WARNING( "[ShaderReflection] Not a SPIR-V module (bad magic)." );
			return {};
		}

		ShaderReflectionData data{};

		unordered_map<uint32, string>						 mapNames;
		unordered_map<uint32, unordered_map<uint32, string>> mapMemberNames;
		unordered_map<uint32, uint32>						 mapBindings;
		unordered_map<uint32, uint32>						 mapDescriptorSets;
		unordered_map<uint32, unordered_map<uint32, uint32>> mapMemberOffsets;
		unordered_map<uint32, SpirvType>					 mapTypes;

		struct VariableInfo
		{
			uint32 _storageClass{ 0 };
			uint32 _typeId{ 0 };
		};
		unordered_map<uint32, VariableInfo> mapVariables;

		size_t offset = 5;
		while ( offset < wordCount )
		{
			const uint32 first		= pWords[offset];
			const uint32 instrWords = first & 0xFFFFu;
			const uint32 opcode		= first >> 16;
			if ( instrWords == 0 || offset + instrWords > wordCount )
				break;

			if ( opcode == kOpName && instrWords >= 3 )
			{
				const uint32 target = pWords[offset + 1];
				const utf8*	 pStr	= reinterpret_cast<const utf8*>( &pWords[offset + 2] );
				const size_t maxLen = ( instrWords - 2 ) * 4;
				mapNames[target]	= string( pStr, strnlen( pStr, maxLen ) );
			}
			else if ( opcode == kOpMemberName && instrWords >= 4 )
			{
				const uint32 target					= pWords[offset + 1];
				const uint32 memberIndex			= pWords[offset + 2];
				const utf8*	 pStr					= reinterpret_cast<const utf8*>( &pWords[offset + 3] );
				const size_t maxLen					= ( instrWords - 3 ) * 4;
				mapMemberNames[target][memberIndex] = string( pStr, strnlen( pStr, maxLen ) );
			}
			else if ( opcode == kOpDecorate && instrWords >= 3 )
			{
				const uint32 target		= pWords[offset + 1];
				const uint32 decoration = pWords[offset + 2];
				if ( decoration == kDecorationBinding && instrWords >= 4 )
					mapBindings[target] = pWords[offset + 3];
				else if ( decoration == kDecorationDescriptorSet && instrWords >= 4 )
					mapDescriptorSets[target] = pWords[offset + 3];
			}
			else if ( opcode == kOpMemberDecorate && instrWords >= 5 )
			{
				const uint32 target		 = pWords[offset + 1];
				const uint32 memberIndex = pWords[offset + 2];
				const uint32 decoration	 = pWords[offset + 3];
				if ( decoration == kDecorationOffset )
					mapMemberOffsets[target][memberIndex] = pWords[offset + 4];
			}
			else if ( opcode == kOpTypeBool && instrWords >= 2 )
			{
				mapTypes[pWords[offset + 1]] = SpirvType{ SpirvType::Kind::Bool, 32, 1, 0, 0, {} };
			}
			else if ( opcode == kOpTypeInt && instrWords >= 4 )
			{
				const uint32 id			= pWords[offset + 1];
				const uint32 width		= pWords[offset + 2];
				const uint32 signedness = pWords[offset + 3];
				mapTypes[id]			= SpirvType{ signedness ? SpirvType::Kind::Int : SpirvType::Kind::Uint, width, 1, 0, 0, {} };
			}
			else if ( opcode == kOpTypeFloat && instrWords >= 3 )
			{
				const uint32 id	   = pWords[offset + 1];
				const uint32 width = pWords[offset + 2];
				mapTypes[id]	   = SpirvType{ SpirvType::Kind::Float, width, 1, 0, 0, {} };
			}
			else if ( opcode == kOpTypeVector && instrWords >= 4 )
			{
				const uint32 id		  = pWords[offset + 1];
				const uint32 compType = pWords[offset + 2];
				const uint32 count	  = pWords[offset + 3];
				mapTypes[id]		  = SpirvType{ SpirvType::Kind::Vector, 32, count, compType, 0, {} };
			}
			else if ( opcode == kOpTypeMatrix && instrWords >= 4 )
			{
				const uint32 id		 = pWords[offset + 1];
				const uint32 colType = pWords[offset + 2];
				const uint32 count	 = pWords[offset + 3];
				mapTypes[id]		 = SpirvType{ SpirvType::Kind::Matrix, 32, count, colType, 0, {} };
			}
			else if ( opcode == kOpTypeImage && instrWords >= 2 )
			{
				mapTypes[pWords[offset + 1]] = SpirvType{ SpirvType::Kind::Image, 0, 0, 0, 0, {} };
			}
			else if ( opcode == kOpTypeSampler && instrWords >= 2 )
			{
				mapTypes[pWords[offset + 1]] = SpirvType{ SpirvType::Kind::Sampler, 0, 0, 0, 0, {} };
			}
			else if ( opcode == kOpTypeSampledImage && instrWords >= 2 )
			{
				mapTypes[pWords[offset + 1]] = SpirvType{ SpirvType::Kind::SampledImage, 0, 0, 0, 0, {} };
			}
			else if ( opcode == kOpTypeStruct && instrWords >= 2 )
			{
				const uint32 id = pWords[offset + 1];
				SpirvType	 st{ SpirvType::Kind::Struct, 0, 0, 0, 0, {} };
				for ( uint32 wordIndex = 2; wordIndex < instrWords; ++wordIndex )
					st._listMemberTypeIds.push_back( pWords[offset + wordIndex] );
				mapTypes[id] = std::move( st );
			}
			else if ( opcode == kOpTypePointer && instrWords >= 4 )
			{
				const uint32 id			  = pWords[offset + 1];
				const uint32 storageClass = pWords[offset + 2];
				const uint32 subType	  = pWords[offset + 3];
				mapTypes[id]			  = SpirvType{ SpirvType::Kind::Pointer, 0, 0, subType, storageClass, {} };
			}
			else if ( opcode == kOpVariable && instrWords >= 4 )
			{
				const uint32 typeId		  = pWords[offset + 1];
				const uint32 resultId	  = pWords[offset + 2];
				const uint32 storageClass = pWords[offset + 3];
				mapVariables[resultId]	  = VariableInfo{ storageClass, typeId };
			}

			offset += instrWords;
		}

		for ( const auto& [id, var] : mapVariables )
		{
			const auto nameIt	 = mapNames.find( id );
			const auto bindingIt = mapBindings.find( id );
			const auto setIt	 = mapDescriptorSets.find( id );
			if ( bindingIt == mapBindings.end() )
				continue;

			string name;
			if ( nameIt != mapNames.end() )
				name = nameIt->second;
			else
				name = string( "Resource_" ) + to_string( id );

			const uint32 space	   = ( setIt != mapDescriptorSets.end() ) ? setIt->second : 0;
			const uint32 bindPoint = bindingIt->second;

			if ( var._storageClass == kStorageClassUniform || var._storageClass == kStorageClassStorageBuffer )
			{
				ShaderBufferInfo buf{};
				buf._name		   = name;
				buf._registerSpace = space;
				buf._bindPoint	   = bindPoint;
				buf._totalSize	   = 0;

				// Pointer -> Struct 타입 분석 및 내부 멤버 변수 추출
				auto ptrTypeIt = mapTypes.find( var._typeId );
				if ( ptrTypeIt != mapTypes.end() && ptrTypeIt->second._kind == SpirvType::Kind::Pointer )
				{
					const uint32 structTypeId = ptrTypeIt->second._subTypeId;
					auto		 structTypeIt = mapTypes.find( structTypeId );
					if ( structTypeIt != mapTypes.end() && structTypeIt->second._kind == SpirvType::Kind::Struct )
					{
						const auto& memberNamesMap	 = mapMemberNames[structTypeId];
						const auto& memberOffsetsMap = mapMemberOffsets[structTypeId];

						uint32 memberIdx{ 0 };
						for ( uint32 memberTypeId : structTypeIt->second._listMemberTypeIds )
						{
							ShaderVariableInfo varInfo{};
							auto			   mNameIt = memberNamesMap.find( memberIdx );
							if ( mNameIt != memberNamesMap.end() )
								varInfo._name = mNameIt->second;
							else
								varInfo._name = string( "member_" ) + to_string( memberIdx );

							auto mOffIt		= memberOffsetsMap.find( memberIdx );
							varInfo._offset = ( mOffIt != memberOffsetsMap.end() ) ? mOffIt->second : 0;
							varInfo._type	= resolveSpirvTypeName( memberTypeId, mapTypes, varInfo._size );

							buf._listVariables.push_back( varInfo );
							buf._totalSize = (MathUtil::max)( buf._totalSize, varInfo._offset + varInfo._size );
							++memberIdx;
						}
						buf._totalSize = ( buf._totalSize + 255u ) & ~255u;
					}
				}

				data._listConstantBuffers.push_back( std::move( buf ) );
			}

			ShaderResourceBinding res{};
			res._name		   = name;
			res._registerSpace = space;
			res._bindPoint	   = bindPoint;
			res._bindCount	   = 1;
			if ( var._storageClass == kStorageClassUniform )
				res._type = "ConstantBuffer";
			else if ( var._storageClass == kStorageClassStorageBuffer )
				res._type = "StorageBuffer";
			else if ( var._storageClass == kStorageClassUniformConstant )
				res._type = "TextureOrSampler";
			else
				res._type = "OtherResource";
			data._listResources.push_back( std::move( res ) );
		}

		SW_LOG_INFO( "[ShaderReflection SPIR-V] ConstantBuffers: %# BoundResources: %#",
					 data._listConstantBuffers.size(), data._listResources.size() );
		return data;
	}

} // namespace sw::shader_reflection_detail
