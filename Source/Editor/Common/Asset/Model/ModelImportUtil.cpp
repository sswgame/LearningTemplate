/**
 * @file ModelImportUtil.cpp
 * @brief 모델 임포트 조각이 함께 쓰는 도우미를 구현합니다.
 */
#include "pch.h"

#include "Editor/Common/Asset/Model/ModelImportUtil.h"

#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/HexUtil.h"
#include "Core/String/StringBuilder.h"

#include "Engine/Asset/AssetMeta.h"
#include "Engine/FileSystem/AssetPaths.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ModelImporter" );

    bool ModelImportUtil::isSourceModel( string_view path )
    {
        return FileUtil::hasAnyExtension( path, { ".glb", ".gltf", ".vrm" } );
    }

    void* ModelImportUtil::allocate( void* pUser, cgltf_size size )
    {
        (void)pUser;
        return Memory::allocate( size );
    }

    void ModelImportUtil::free( void* pUser, void* pAddress )
    {
        (void)pUser;
        if ( pAddress != nullptr )
            Memory::free( pAddress );
    }

    cgltf_options ModelImportUtil::makeOptions()
    {
        cgltf_options options{};
        options.memory.alloc_func = &ModelImportUtil::allocate;
        options.memory.free_func  = &ModelImportUtil::free;
        return options;
    }

    float3 ModelImportUtil::convertToEngineSpace( const float3& value )
    {
        return float3{ -value._x, value._y, value._z };
    }

    quaternion ModelImportUtil::convertRotationToEngineSpace( const quaternion& value )
    {
        return quaternion{ value._x, -value._y, -value._z, value._w };
    }

    float4x4 ModelImportUtil::convertMatrixToEngineSpace( const float32 ( &arrColumnMajor )[16] )
    {
        float4x4 matrix{ arrColumnMajor };
        matrix._12 = -matrix._12;
        matrix._13 = -matrix._13;
        matrix._14 = -matrix._14;
        matrix._21 = -matrix._21;
        matrix._31 = -matrix._31;
        matrix._41 = -matrix._41;
        return matrix;
    }

    BoneTransform ModelImportUtil::convertTransformToEngineSpace( const BoneTransform& value )
    {
        BoneTransform result{};
        result._translation = convertToEngineSpace( value._translation );
        result._rotation    = convertRotationToEngineSpace( value._rotation );
        result._scale       = value._scale;
        return result;
    }

    bool ModelImportUtil::readFloats( const cgltf_accessor* pAccessor, cgltf_size index, float32* pOutValue, cgltf_size count )
    {
        return pAccessor != nullptr && cgltf_accessor_read_float( pAccessor, index, pOutValue, count ) != 0;
    }

    bool ModelImportUtil::unpackFloats( const cgltf_accessor* pAccessor, cgltf_size componentCount, vector<float32>& outListValue )
    {
        outListValue.clear();
        if ( pAccessor == nullptr )
        {
            SW_LOG_ERROR( "glTF accessor is missing" );
            return false;
        }
        if ( cgltf_num_components( pAccessor->type ) != componentCount )
        {
            SW_LOG_ERROR( "glTF accessor has %# components per element, expected %#", static_cast<uint32>( cgltf_num_components( pAccessor->type ) ),
                          static_cast<uint32>( componentCount ) );
            return false;
        }
        const cgltf_size floatCount = pAccessor->count * componentCount;
        outListValue.resize( floatCount );
        if ( floatCount > 0 && cgltf_accessor_unpack_floats( pAccessor, outListValue.data(), floatCount ) != floatCount )
        {
            SW_LOG_ERROR( "glTF accessor (%# elements) could not be read", static_cast<uint32>( pAccessor->count ) );
            outListValue.clear();
            return false;
        }
        return true;
    }

    string ModelImportUtil::makeNodeName( const cgltf_data& data, const cgltf_node& node )
    {
        if ( node.name != nullptr && node.name[0] != '\0' )
            return string( node.name );
        return formatToString( "node%#", static_cast<uint64>( &node - data.nodes ) );
    }

    string ModelImportUtil::makeFileStem( string_view name )
    {
        string stem;
        stem.reserve( name.size() );
        for ( const utf8 character : name )
        {
            const utf8 lower    = StringUtil::toLowerChar( character );
            const bool bAllowed = ( 'a' <= lower && lower <= 'z' ) || ( '0' <= lower && lower <= '9' ) || lower == '_' || lower == '-' || lower == '.';
            stem += bAllowed ? lower : '_';
        }
        return stem.empty() ? string( "unnamed" ) : stem;
    }

    string ModelImportUtil::makeNumberedStem( const string& baseStem, uint32 useCount )
    {
        string stem = baseStem;
        if ( useCount > 0 )
        {
            stem += "_";
            stem += to_string( useCount ).c_str();
        }
        return stem;
    }

    hashed_string ModelImportUtil::makeMorphTargetName( const cgltf_mesh* pMesh, cgltf_size targetIndex )
    {
        const bool bHasMorphTargetName = pMesh != nullptr && targetIndex < pMesh->target_names_count && pMesh->target_names[targetIndex] != nullptr;
        if ( bHasMorphTargetName )
            return hashed_string( pMesh->target_names[targetIndex] );
        return hashed_string( formatToString( "target%#", static_cast<uint64>( targetIndex ) ) );
    }

    string ModelImportUtil::makeImportedGUID( string_view assetID )
    {
        const uint64 high = HashUtil::computeStringHash64( assetID.data(), assetID.size(), false );
        const uint64 low  = HashUtil::computeStringHash64( assetID.data(), assetID.size(), false, high ^ HashUtil::kGoldenRatio64 );
        // 8-4-4-4-12 글자 16 진. 셋째 그룹 첫 글자는 버전 4, 넷째 그룹 첫 글자는 변형(8..b)이다.
        const auto appendHex = []( string& inoutText, uint64 value, uint32 digitCount )
        { HexUtil::appendUint64( inoutText, value, digitCount ); };
        string text;
        appendHex( text, high >> 32, 8 );
        text += '-';
        appendHex( text, ( high >> 16 ) & 0xFFFFu, 4 );
        text += "-4";
        appendHex( text, high & 0x0FFFu, 3 );
        text += '-';
        appendHex( text, 0x8000u | ( ( low >> 48 ) & 0x3FFFu ), 4 );
        text += '-';
        appendHex( text, low & 0xFFFFFFFFFFFFull, 12 );
        return text;
    }

    bool ModelImportUtil::writeImportedMeta( const AssetPaths& assetPaths, const string& assetPath )
    {
        const string assetID = assetPaths.toAssetID( assetPath );
        AssetMeta    meta;
        meta._guid       = makeImportedGUID( assetID );
        meta._sourcePath = assetID;
        meta._bImported  = true;
        return FileUtil::writeTextFile( assetPath + path::kMetaExtension, meta.toXML() );
    }
} // namespace sw::editor
