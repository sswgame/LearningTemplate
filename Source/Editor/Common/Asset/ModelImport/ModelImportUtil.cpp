/**
 * @file ModelImportUtil.cpp
 * @brief 모델 임포트 조각이 함께 쓰는 도우미를 구현합니다.
 */
#include "pch.h"

#include "Editor/Common/Asset/ModelImport/ModelImportUtil.h"

#include "Core/File/FileUtil.h"
#include "Core/Hash/HashUtil.h"
#include "Core/Log/Logger.h"
#include "Core/Memory/Memory.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"
#include "Core/String/formatString.h"

namespace sw::editor
{
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

    bool ModelImportUtil::isExternalURI( const utf8* pURI )
    {
        return pURI != nullptr && pURI[0] != '\0' && StringUtil::startsWith( pURI, "data:" ) == false;
    }

    AbsolutePath ModelImportUtil::resolveURI( const AbsolutePath& sourcePath, const utf8* pURI )
    {
        string decoded{ pURI };
        decoded.push_back( '\0' ); // cgltf 는 널로 끝나는 글을 제자리에서 푼다
        decoded.resize( cgltf_decode_uri( decoded.data() ) );
        // `../` 을 푼다 — 원본 옆이 아닌 textures_raw 의 이미지를 가리키는 glTF(`join` 은 상위 탈출을 거절한다).
        const optional<AbsolutePath> path = AbsolutePath::tryResolve( decoded, sourcePath.getParent() );
        if ( path.has_value() == false )
            return {};
        string canonicalPath;
        return FileUtil::makeCanonicalPath( *path, canonicalPath ) ? FileUtil::toDiskPath( canonicalPath ) : *path;
    }

    uint64 ModelImportUtil::mixExternalFiles( const AbsolutePath& sourcePath, const vector<uint8>& listByte, string_view imageRuleText, uint64 hash )
    {
        const cgltf_options options = makeOptions();
        cgltf_data*         pData   = nullptr;
        if ( cgltf_parse( &options, listByte.data(), listByte.size(), &pData ) != cgltf_result_success )
        {
            cgltf_free( pData );
            return hash; // 깨진 원본은 임포트가 오류로 알린다
        }
        StringBuilder<constant::buffer::kMax64> rawMarker;
        rawMarker.appendFormat( "/%#/", kRawTextureFolder );
        const auto mixFile = [&hash]( const string& path )
        {
            vector<uint8> fileBytes;
            const bool    bRead = FileUtil::exists( FileUtil::toDiskPath( path ) ) && FileUtil::readFile( FileUtil::toDiskPath( path ), fileBytes );
            if ( bRead && fileBytes.empty() == false )
                hash = HashUtil::computeStringHash64( reinterpret_cast<const utf8*>( fileBytes.data() ), fileBytes.size(), false, hash );
        };
        for ( cgltf_size bufferIndex = 0; bufferIndex < pData->buffers_count; ++bufferIndex )
        {
            if ( isExternalURI( pData->buffers[bufferIndex].uri ) )
                mixFile( resolveURI( sourcePath, pData->buffers[bufferIndex].uri ).str() );
        }
        bool bHasEmbeddedImage = false; // 텍스처 원본 폴더 밖의 이미지는 공유 원본으로 꺼낸다 — 어느 폴더인지는 텍스처 규칙이 정한다
        for ( cgltf_size imageIndex = 0; imageIndex < pData->images_count; ++imageIndex )
        {
            if ( isExternalURI( pData->images[imageIndex].uri ) == false )
            {
                bHasEmbeddedImage = true;
                continue;
            }
            const string imagePath = resolveURI( sourcePath, pData->images[imageIndex].uri ).str();
            if ( imagePath.find( rawMarker.view() ) != string::npos )
                continue;
            bHasEmbeddedImage = true;
            mixFile( imagePath );
        }
        if ( bHasEmbeddedImage )
            hash = HashUtil::computeStringHash64( imageRuleText.data(), imageRuleText.size(), false, hash );
        cgltf_free( pData );
        return hash;
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
            SW_LOG_ERROR_ID( "ModelImporter.GlTFAccessorMissing", "glTF accessor is missing" );
            return false;
        }
        if ( cgltf_num_components( pAccessor->type ) != componentCount )
        {
            SW_LOG_ERROR_ID( "ModelImporter.GlTFAccessorComponentsPer", "glTF accessor has %# components per element, expected %#", static_cast<uint32>( cgltf_num_components( pAccessor->type ) ),
                             static_cast<uint32>( componentCount ) );
            return false;
        }
        const cgltf_size floatCount = pAccessor->count * componentCount;
        outListValue.resize( floatCount );
        if ( floatCount > 0 && cgltf_accessor_unpack_floats( pAccessor, outListValue.data(), floatCount ) != floatCount )
        {
            SW_LOG_ERROR_ID( "ModelImporter.GlTFAccessorElementsCould", "glTF accessor (%# elements) could not be read", static_cast<uint32>( pAccessor->count ) );
            outListValue.clear();
            return false;
        }
        return true;
    }

    string ModelImportUtil::makeNodeName( const cgltf_data& data, const cgltf_node& node )
    {
        if ( node.name != nullptr && node.name[0] != '\0' )
            return string( node.name );
        StringBuilder<constant::buffer::kMax256> resultBuilder;
        resultBuilder.appendFormat( "node%#", static_cast<uint64>( &node - data.nodes ) );
        return string( resultBuilder.view() );
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
        StringBuilder<constant::buffer::kMax256> textBuilder;
        textBuilder.appendFormat( "target%#", static_cast<uint64>( targetIndex ) );
        return hashed_string( string( textBuilder.view() ) );
    }

    string ModelImportUtil::formatMaterialNumber( float32 value )
    {
        utf8 arrText[constant::buffer::kMax32]{};
        formatstring( arrText, sizeof( arrText ), "%.6g", static_cast<float64>( value ) + 0.0 );
        return string( arrText );
    }

    string ModelImportUtil::formatMaterialColor( const float4& color )
    {
        StringBuilder<constant::buffer::kMax128> colorBuilder;
        colorBuilder.appendFormat( "%# %# %# %#", formatMaterialNumber( color._x ), formatMaterialNumber( color._y ), formatMaterialNumber( color._z ), formatMaterialNumber( color._w ) );
        return string( colorBuilder.view() );
    }
} // namespace sw::editor
