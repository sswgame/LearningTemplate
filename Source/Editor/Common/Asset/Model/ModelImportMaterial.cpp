/**
 * @file ModelImportMaterial.cpp
 * @brief 모델 임포트의 VRM 툰 머티리얼 읽기 · 머티리얼 구간 표 만들기 · 구간 텍스처 모으기 · 구간 머티리얼 쓰기입니다.
 */
#include "pch.h"

#include "Editor/Common/Asset/Model/ModelImportMaterial.h"

#include "Core/Container/StringUtil.h"
#include "Core/Container/VectorUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Asset/Model/ModelImportGeometry.h"
#include "Editor/Common/Asset/Model/ModelImportUtil.h"
#include "Editor/Common/Asset/Model/ModelImporter.h"
#include "Editor/Common/Asset/Model/VrmMaterialImporter.h"
#include "Editor/Common/Asset/Texture/TextureImporter.h"

#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/FileSystem/AssetIO.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    bool ModelImportMaterial::readToonMaterials( const cgltf_data& data, string_view sourcePath, vector<ToonMaterialDesc>& outListToon,
                                                 vector<string>& outListIgnored )
    {
        outListToon.clear();
        if ( data.json == nullptr || data.json_size == 0 )
            return true;
        JSONDocument document;
        if ( document.parse( string_view( data.json, data.json_size ) ) == false )
            return true; // cgltf 가 이미 읽은 JSON 이다 — 여기서 못 읽을 일은 없지만, 못 읽으면 VRM 이 아닌 것으로 본다
        const JSONValue root       = document.getRoot();
        const JSONValue extensions = root.get( "extensions" );
        const JSONValue vrm0       = extensions.isObject() ? extensions.get( "VRM" ) : JSONValue{};
        const JSONValue materials  = root.get( "materials" );
        const size_t    count      = materials.isArray() ? materials.size() : 0;

        bool bVrm1 = extensions.isObject() && extensions.has( "VRMC_vrm" );
        for ( size_t index = 0; index < count && bVrm1 == false; ++index )
        {
            const JSONValue materialExtensions = materials.at( index ).get( "extensions" );
            bVrm1                              = materialExtensions.isObject() && materialExtensions.has( "VRMC_materials_mtoon" );
        }
        if ( vrm0.isObject() == false && bVrm1 == false )
            return true;

        outListToon.resize( count );
        const JSONValue materialProperties = vrm0.isObject() ? vrm0.get( "materialProperties" ) : JSONValue{};
        for ( size_t index = 0; index < count; ++index )
        {
            string error;
            bool   bRead = false;
            if ( bVrm1 )
            {
                bRead = VrmMaterialImporter::readMtoon1Material( materials.at( index ), outListToon[index], &outListIgnored, error );
            }
            else
            {
                // 0.x 는 materialProperties[i] 가 materials[i] 의 것이다(UniVRM 이 같은 순서로 쓴다).
                if ( materialProperties.isArray() == false || index >= materialProperties.size() )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.VRMMaterialNoMaterialProperties", "VRM '%#': material %# has no materialProperties entry", sourcePath, index );
                    return false;
                }
                bRead = VrmMaterialImporter::readVrm0Material( materialProperties.at( index ), materials.at( index ), outListToon[index], &outListIgnored, error );
            }
            if ( bRead == false )
            {
                SW_LOG_ERROR_ID( "ModelImporter.VRM", "VRM '%#': %#", sourcePath, error.c_str() );
                return false;
            }
            if ( outListToon[index]._name.empty() )
                outListToon[index]._name = formatToString( "material%#", static_cast<uint64>( index ) );
        }
        return true;
    }

    VrmFirstPersonMode ModelImportMaterial::findFirstPersonMode( const cgltf_data& data, const VrmAvatarDef& avatar, const cgltf_mesh* pMesh )
    {
        if ( pMesh == nullptr )
            return VrmFirstPersonMode::Auto;
        const size_t meshIndex = static_cast<size_t>( pMesh - data.meshes );
        const string name      = ( pMesh->name != nullptr && pMesh->name[0] != '\0' ) ? string( pMesh->name ) : "mesh" + to_string( static_cast<uint64>( meshIndex ) );
        for ( const VrmFirstPersonMesh& annotated : avatar._listFirstPersonMesh )
        {
            if ( annotated._mesh == name )
                return annotated._mode;
        }
        return VrmFirstPersonMode::Auto;
    }

    bool ModelImportMaterial::triangleTouchesHead( const ModelImportUtil::MergedMesh& body, const vector<uint8>& listHeadBone, size_t triangle )
    {
        for ( uint32 corner = 0; corner < 3; ++corner )
        {
            const ModelImportUtil::ImportVertex& vertex = body._listVertex[body._listIndex[triangle * 3 + corner]];
            for ( uint32 influence = 0; influence < ModelImportUtil::kInfluenceCount; ++influence )
            {
                const uint32 joint          = vertex._arrJoint[influence];
                const bool   bHeadInfluence = vertex._arrWeight[influence] > 0.0f && joint < listHeadBone.size() && listHeadBone[joint] == SW_TRUE;
                if ( bHeadInfluence )
                    return true;
            }
        }
        return false;
    }

    void ModelImportMaterial::populateMaterialSections( ModelImportUtil::MergedMesh& inoutBody, const cgltf_data& data, const vector<ToonMaterialDesc>& listToon, ModelImportResult& inoutResult )
    {
        // 1인칭 본(머리) 아래 본 — Auto 메시는 이 본에 가중치가 있는 꼭짓점을 가진 삼각형을 3인칭 전용으로 나눈다(UniVRM 과 같다).
        const Skeleton& skeleton = inoutResult._skeleton;
        vector<uint8>   listHeadBone( skeleton.getBoneCount(), SW_FALSE );
        const int32     headBone = inoutResult._avatar._firstPersonBone.empty() ? -1 : skeleton.findBoneIndex( hashed_string( inoutResult._avatar._firstPersonBone ) );
        for ( uint32 bone = 0; headBone >= 0 && bone < skeleton.getBoneCount(); ++bone )
        {
            const int32 parent = skeleton.getBone( bone )._parentIndex;
            listHeadBone[bone] = ( static_cast<int32>( bone ) == headBone || ( parent >= 0 && listHeadBone[static_cast<uint32>( parent )] == SW_TRUE ) ) ? SW_TRUE : SW_FALSE;
        }

        // 삼각형마다 묶음 열쇠 — (머티리얼 번호, 가시성). 머티리얼이 없는 삼각형은 어느 구간에도 들지 않고 뒤에 남는다(그리는 컴포넌트가 없다).
        const size_t     triangleCount    = inoutBody._listTriangleMaterial.size();
        constexpr uint32 kNoSection       = 0xFFFFFFFFu;
        constexpr uint32 kVisibilityCount = static_cast<uint32>( MeshPersonVisibility::Count );
        vector<uint32>   listKey( triangleCount, kNoSection );
        uint32           noMaterialCount{ 0 };
        for ( size_t triangle = 0; triangle < triangleCount; ++triangle )
        {
            const cgltf_material* pMaterial = inoutBody._listTriangleMaterial[triangle];
            if ( pMaterial == nullptr || static_cast<size_t>( pMaterial - data.materials ) >= listToon.size() )
            {
                ++noMaterialCount;
                continue;
            }
            const VrmFirstPersonMode mode       = findFirstPersonMode( data, inoutResult._avatar, triangle < inoutBody._listTriangleSourceMesh.size() ? inoutBody._listTriangleSourceMesh[triangle] : nullptr );
            MeshPersonVisibility     visibility = MeshPersonVisibility::Both;
            if ( mode == VrmFirstPersonMode::ThirdPersonOnly )
                visibility = MeshPersonVisibility::ThirdPersonOnly;
            else if ( mode == VrmFirstPersonMode::FirstPersonOnly )
                visibility = MeshPersonVisibility::FirstPersonOnly;
            else if ( mode == VrmFirstPersonMode::Auto && triangleTouchesHead( inoutBody, listHeadBone, triangle ) )
                visibility = MeshPersonVisibility::ThirdPersonOnly;
            listKey[triangle] = static_cast<uint32>( pMaterial - data.materials ) * kVisibilityCount + static_cast<uint32>( visibility );
        }
        if ( noMaterialCount > 0 )
            SW_LOG_WARNING_ID( "ModelImporter.VRMModelTrianglesNo", "VRM model: %# triangles have no material - no mesh section draws them", noMaterialCount );

        // 열쇠 순서로 삼각형을 다시 놓고(안정 정렬 — 같은 묶음 안의 원본 순서를 지킨 뒤 묶음마다 최적화한다), 구간 표를 만든다.
        vector<uint32> listOrder( triangleCount );
        for ( uint32 triangle = 0; triangle < static_cast<uint32>( triangleCount ); ++triangle )
        {
            listOrder[triangle] = triangle;
        }
        VectorUtil::stableSort( listOrder, [&listKey]( uint32 lhs, uint32 rhs )
        { return listKey[lhs] < listKey[rhs]; } );
        vector<uint32>                listIndex;
        vector<const cgltf_material*> listMaterial;
        vector<const cgltf_mesh*>     listSourceMesh;
        listIndex.reserve( inoutBody._listIndex.size() );
        for ( const uint32 triangle : listOrder )
        {
            for ( uint32 corner = 0; corner < 3; ++corner )
            {
                listIndex.push_back( inoutBody._listIndex[triangle * 3 + corner] );
            }
            listMaterial.push_back( inoutBody._listTriangleMaterial[triangle] );
            listSourceMesh.push_back( triangle < inoutBody._listTriangleSourceMesh.size() ? inoutBody._listTriangleSourceMesh[triangle] : nullptr );
        }
        inoutBody._listIndex              = std::move( listIndex );
        inoutBody._listTriangleMaterial   = std::move( listMaterial );
        inoutBody._listTriangleSourceMesh = std::move( listSourceMesh );

        unordered_map<string, uint32> mapStemUse;
        unordered_map<uint32, string> mapMaterialStem;
        size_t                        begin = 0;
        while ( begin < triangleCount && listKey[listOrder[begin]] != kNoSection )
        {
            const uint32 key = listKey[listOrder[begin]];
            size_t       end = begin;
            while ( end < triangleCount && listKey[listOrder[end]] == key )
            {
                ++end;
            }
            ModelImportGeometry::optimizeTriangleRange( inoutBody, begin, end );
            const uint32               materialIndex = key / kVisibilityCount;
            const MeshPersonVisibility visibility    = static_cast<MeshPersonVisibility>( key % kVisibilityCount );
            ModelImportSection         section{};
            section._material = listToon[materialIndex];
            auto stemIt       = mapMaterialStem.find( materialIndex );
            if ( stemIt == mapMaterialStem.end() )
            {
                const string baseStem = ModelImportUtil::makeFileStem( section._material._name );
                stemIt                = mapMaterialStem.emplace( materialIndex, ModelImportUtil::makeNumberedStem( baseStem, mapStemUse[baseStem]++ ) ).first;
            }
            section._fileStem         = stemIt->second;
            section._sectionName      = section._fileStem + ( visibility == MeshPersonVisibility::ThirdPersonOnly ? "_tp" : ( visibility == MeshPersonVisibility::FirstPersonOnly ? "_fp" : "" ) );
            section._firstVertex      = static_cast<uint32>( begin * 3 );
            section._vertexCount      = static_cast<uint32>( ( end - begin ) * 3 );
            section._personVisibility = visibility;
            inoutResult._listSection.push_back( std::move( section ) );
            begin = end;
        }
        // 구간 없는 꼬리(머티리얼 없는 삼각형)도 순서만 최적화한다.
        if ( begin < triangleCount )
            ModelImportGeometry::optimizeTriangleRange( inoutBody, begin, triangleCount );
    }

    bool ModelImportMaterial::collectSectionTextures( const cgltf_data& data, string_view sourcePath, ModelImportResult& inoutResult )
    {
        vector<uint8> listUsed( data.textures_count, SW_FALSE );
        for ( const ModelImportSection& section : inoutResult._listSection )
        {
            for ( const int32 textureIndex : { section._material._baseColorTexture, section._material._shadeTexture, section._material._emissiveTexture,
                                               section._material._matcapTexture } )
            {
                if ( 0 <= textureIndex && static_cast<cgltf_size>( textureIndex ) < data.textures_count )
                    listUsed[static_cast<size_t>( textureIndex )] = SW_TRUE;
            }
        }

        // 이미지 이름 → 파일 이름(이미지 순서로 번호를 매겨 결정적이다).
        vector<string>                listImageStem( data.images_count );
        unordered_map<string, uint32> mapStemUse;
        for ( cgltf_size imageIndex = 0; imageIndex < data.images_count; ++imageIndex )
        {
            const cgltf_image& image    = data.images[imageIndex];
            const string       baseName = ( image.name != nullptr && image.name[0] != '\0' ) ? ModelImportUtil::makeFileStem( image.name ) : "image" + to_string( static_cast<uint64>( imageIndex ) );
            listImageStem[imageIndex]   = ModelImportUtil::makeNumberedStem( baseName, mapStemUse[baseName]++ );
        }

        inoutResult._listTexture.assign( data.textures_count, ModelImportTexture{} );
        for ( cgltf_size textureIndex = 0; textureIndex < data.textures_count; ++textureIndex )
        {
            const cgltf_image* pImage = data.textures[textureIndex].image;
            if ( listUsed[textureIndex] == SW_FALSE || pImage == nullptr )
                continue;
            ModelImportTexture& texture = inoutResult._listTexture[textureIndex];
            texture._fileStem           = listImageStem[static_cast<size_t>( pImage - data.images )];
            if ( readImageBytes( *pImage, sourcePath, texture ) == false )
                return false;
            if ( texture._bytes.empty() )
                SW_LOG_WARNING_ID( "ModelImporter.GlTFTextureNoReadable", "glTF '%#': texture %# has no readable image bytes - the material samples white", sourcePath, textureIndex );
        }
        return true;
    }

    bool ModelImportMaterial::writeMaterialSections( const AssetPaths& assetPaths, const ModelImportResult& result, string_view sourcePath, string_view sideFolder )
    {
        if ( result._listSection.empty() )
            return true;
        const string   rawTextureFolder = makeRawTextureFolder( sourcePath );
        vector<string> listTexturePath( result._listTexture.size() );
        for ( size_t textureIndex = 0; textureIndex < result._listTexture.size(); ++textureIndex )
        {
            const ModelImportTexture& texture = result._listTexture[textureIndex];
            if ( texture._bytes.empty() || rawTextureFolder.empty() )
                continue;
            const string  rawPath = FileUtil::joinPath( rawTextureFolder, texture._fileStem + texture._extension );
            vector<uint8> existingBytes;
            const bool    bSame = FileUtil::exists( rawPath ) && FileUtil::readFile( rawPath, existingBytes ) && existingBytes == texture._bytes;
            if ( bSame == false )
            {
                if ( FileUtil::ensureDirectoryExists( rawTextureFolder ) == false || FileUtil::writeFile( rawPath, texture._bytes.data(), texture._bytes.size() ) == false )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.FailedWriteTextureSource", "Failed to write texture source %#", rawPath.c_str() );
                    return false;
                }
            }
            listTexturePath[textureIndex] = assetPaths.toAssetID( TextureImporter::makeImportedTexturePath( rawPath ) );
        }

        // 툰 머티리얼 견본의 경로는 엔진 기본 에셋 표에 있다. 이 함수는 워커에서도 도므로 서비스를 찾지 않고 데이터에서 표를 읽는다.
        const unique_ptr<EngineDefaultAssets> pDefaultAssets = EngineDefaultAssets::load();
        if ( pDefaultAssets == nullptr )
        {
            SW_LOG_ERROR_ID( "ModelImporter.GlTFEngineDefaultAssets", "glTF '%#': the engine default assets could not be read - no toon material template for the sections", sourcePath );
            return false;
        }

        vector<string> listWrittenStem;
        for ( const ModelImportSection& section : result._listSection )
        {
            // 같은 머티리얼의 구간(1인칭 가시성이 다른)은 머티리얼 파일 하나를 쓴다.
            if ( std::find( listWrittenStem.begin(), listWrittenStem.end(), section._fileStem ) != listWrittenStem.end() )
                continue;
            listWrittenStem.push_back( section._fileStem );
            const string materialText = VrmMaterialImporter::makeMaterialXML( section._material, listTexturePath, pDefaultAssets->_toonMaterialTemplate );
            const string materialPath = FileUtil::joinPath( FileUtil::joinPath( sideFolder, ModelImportUtil::kMaterialFolder ), section._fileStem + ".material" );
            const bool   bWritten     = materialText.empty() == false && FileUtil::ensureParentDirectoryExists( materialPath ) && FileUtil::writeTextFile( materialPath, materialText );
            if ( bWritten == false )
            {
                SW_LOG_ERROR_ID( "ModelImporter.FailedWriteToonMaterial", "Failed to write toon material %#", materialPath.c_str() );
                return false;
            }
            // 머티리얼 캐시는 잡을 때 `.meta` 를 지어 붙인다 — 옆 폴더에 실행마다 다른 GUID 가 생기면 임포트 결과 해시가 어긋난다.
            // 그래서 임포터가 경로에서 정해지는 GUID 로 미리 쓴다(언리얼 · 유니티의 임포트 부산물도 임포터가 식별자를 정한다).
            if ( ModelImportUtil::writeImportedMeta( assetPaths, materialPath ) == false )
            {
                SW_LOG_ERROR_ID( "ModelImporter.FailedWriteMeta", "Failed to write %#.meta", materialPath.c_str() );
                return false;
            }
        }
        if ( result._listIgnoredMaterialKey.empty() == false )
        {
            string joined;
            for ( const string& key : result._listIgnoredMaterialKey )
            {
                joined += ( joined.empty() ? "" : ", " ) + key;
            }
            SW_LOG_WARNING_ID( "ModelImporter.VRMMaterialKeysToon", "VRM '%#': material keys the toon material does not support (ignored): %#", sourcePath, joined.c_str() );
        }
        SW_LOG_INFO_ID( "ModelImporter.VRMMaterialSectionsTexture", "VRM '%#': %# material sections, %# texture sources", sourcePath, result._listSection.size(), result._listTexture.size() );
        return true;
    }

    bool ModelImportMaterial::readImageBytes( const cgltf_image& image, string_view sourcePath, ModelImportTexture& outTexture )
    {
        const string mimeType = image.mime_type != nullptr ? string( image.mime_type ) : string();
        outTexture._extension = ( mimeType == "image/jpeg" ) ? ".jpg" : ".png";
        if ( image.buffer_view != nullptr )
        {
            const uint8* pBytes = cgltf_buffer_view_data( image.buffer_view );
            if ( pBytes != nullptr )
                outTexture._bytes.assign( pBytes, pBytes + image.buffer_view->size );
            return true;
        }
        if ( image.uri == nullptr || StringUtil::startsWith( image.uri, "data:" ) )
            return true;
        outTexture._extension = FileUtil::getExtension( image.uri );
        // 원본이 가리키는 바깥 파일을 못 읽으면 흰 텍스처로 임포트하지 않고 실패한다(빠진 파일이 결과에 말없이 묻힌다).
        string imagePath = FileUtil::joinPath( FileUtil::getDirectoryPart( sourcePath ), image.uri );
        string canonicalPath;
        if ( FileUtil::makeCanonicalPath( imagePath, canonicalPath ) ) // `../` 을 푼다 — 원본 옆이 아닌 textures_raw 의 이미지를 가리키는 glTF
            imagePath = FileUtil::normalizeSeparators( canonicalPath );
        outTexture._sourcePath = imagePath;
        if ( FileUtil::readFile( imagePath, outTexture._bytes ) == false )
        {
            SW_LOG_ERROR_ID( "ModelImport.ImageFileMissing", "glTF '%#': cannot read its image file %#", sourcePath, imagePath.c_str() );
            return false;
        }
        return true;
    }

    bool ModelImportMaterial::readPbrMaterial( const cgltf_data& data, string_view sourcePath, ModelImportResult& inoutResult )
    {
        const cgltf_material* pMaterial = nullptr;
        for ( cgltf_size materialIndex = 0; materialIndex < data.materials_count && pMaterial == nullptr; ++materialIndex )
        {
            const cgltf_material& material = data.materials[materialIndex];
            if ( material.has_pbr_metallic_roughness != 0 && material.pbr_metallic_roughness.base_color_texture.texture != nullptr )
                pMaterial = &material;
        }
        if ( pMaterial == nullptr )
            return true;
        if ( data.materials_count > 1 )
            SW_LOG_WARNING_ID( "ModelImport.MultipleMaterials", "glTF '%#' has %# materials - a static mesh takes one, the first textured one ('%#') is imported", sourcePath, data.materials_count,
                               pMaterial->name != nullptr ? pMaterial->name : "" );
        const cgltf_pbr_metallic_roughness& pbr     = pMaterial->pbr_metallic_roughness;
        ModelImportPbrMaterial&             outPbr  = inoutResult._pbrMaterial;
        const string                        rawName = pMaterial->name != nullptr && pMaterial->name[0] != '\0' ? string( pMaterial->name ) : string( "material" );
        outPbr._name                                = ModelImportUtil::makeFileStem( rawName.c_str() );
        outPbr._baseColor                           = float4{ pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3] };
        outPbr._metallic                            = pbr.metallic_factor;
        outPbr._roughness                           = pbr.roughness_factor;
        auto readTexture                            = [&]( const cgltf_texture* pTexture, ModelImportTexture& outTexture, const utf8* pSuffix )
        {
            if ( pTexture == nullptr || pTexture->image == nullptr )
                return true;
            outTexture._fileStem = outPbr._name + pSuffix;
            return readImageBytes( *pTexture->image, sourcePath, outTexture );
        };
        const cgltf_texture* pORM = pbr.metallic_roughness_texture.texture;
        if ( readTexture( pbr.base_color_texture.texture, outPbr._albedo, "_albedo" ) == false || readTexture( pMaterial->normal_texture.texture, outPbr._normal, "_normal" ) == false ||
             readTexture( pORM, outPbr._orm, "_orm" ) == false )
            return false;
        outPbr._normalScale           = pMaterial->normal_texture.texture != nullptr ? pMaterial->normal_texture.scale : 1.0f;
        const cgltf_texture* pAO      = pMaterial->occlusion_texture.texture;
        const bool           bSameORM = pAO != nullptr && pORM != nullptr && pAO->image == pORM->image;
        outPbr._occlusionStrength     = bSameORM ? pMaterial->occlusion_texture.scale : 0.0f;
        if ( pAO != nullptr && bSameORM == false )
            SW_LOG_WARNING_ID( "ModelImporter.GlTFOcclusionTextureNot", "glTF '%#': the occlusion texture is not packed with metallic-roughness (ORM) - occlusion is not imported", sourcePath );
        outPbr._bPresent = SW_TRUE;
        return true;
    }

    bool ModelImportMaterial::writePbrMaterial( const AssetPaths& assetPaths, const ModelImportResult& result, string_view sourcePath, string_view sideFolder )
    {
        const ModelImportPbrMaterial& pbr = result._pbrMaterial;
        if ( pbr._bPresent == SW_FALSE )
            return true;
        const string rawTextureFolder = makeRawTextureFolder( sourcePath );
        if ( rawTextureFolder.empty() )
        {
            SW_LOG_ERROR_ID( "ModelImporter.GlTFNotUnderNo", "glTF '%#' is not under %#/ - there is no texture source folder for its material", sourcePath, ModelImportUtil::kRawModelFolder.data() );
            return false;
        }
        // 텍스처 원본을 꺼내 두고(바이트가 같으면 건드리지 않는다) 텍스처 임포트가 만들 DDS 경로를 머티리얼에 적는다.
        auto writeSource = [&]( const ModelImportTexture& texture, string& outAssetPath )
        {
            if ( texture._bytes.empty() )
                return true;
            // 원본이 이미 텍스처 원본 폴더의 파일이면 그대로 가리킨다(같은 이미지를 두 벌 두지 않는다 — 내려받은 에셋은 이미지를 textures_raw 에 둔다).
            const string rawMarker = "/" + string( ModelImportUtil::kRawTextureFolder ) + "/";
            if ( texture._sourcePath.find( rawMarker ) != string::npos )
            {
                outAssetPath = assetPaths.toAssetID( TextureImporter::makeImportedTexturePath( texture._sourcePath ) );
                return true;
            }
            const string  rawPath = FileUtil::joinPath( rawTextureFolder, texture._fileStem + texture._extension );
            vector<uint8> existingBytes;
            const bool    bSame = FileUtil::exists( rawPath ) && FileUtil::readFile( rawPath, existingBytes ) && existingBytes == texture._bytes;
            if ( bSame == false && ( FileUtil::ensureDirectoryExists( rawTextureFolder ) == false || FileUtil::writeFile( rawPath, texture._bytes.data(), texture._bytes.size() ) == false ) )
            {
                SW_LOG_ERROR_ID( "ModelImporter.FailedWriteTextureSource", "Failed to write texture source %#", rawPath.c_str() );
                return false;
            }
            outAssetPath = assetPaths.toAssetID( TextureImporter::makeImportedTexturePath( rawPath ) );
            return true;
        };
        string albedoPath;
        string normalPath;
        string ormPath;
        if ( writeSource( pbr._albedo, albedoPath ) == false || writeSource( pbr._normal, normalPath ) == false || writeSource( pbr._orm, ormPath ) == false )
            return false;

        const unique_ptr<EngineDefaultAssets> pDefaultAssets = EngineDefaultAssets::load();
        string                                templateText;
        if ( pDefaultAssets == nullptr || AssetIO::readTextAsset( AssetID{ pDefaultAssets->_pbrMaterialTemplate.get() }, templateText ) == false )
        {
            SW_LOG_ERROR_ID( "ModelImporter.GlTFPBRMaterialTemplate", "glTF '%#': the PBR material template could not be read", sourcePath );
            return false;
        }
        XMLDocument   document;
        const bool    bParsed    = document.parse( templateText );
        const XMLNode root       = bParsed ? document.getRoot( "MaterialDesc" ) : XMLNode{};
        const XMLNode properties = root.isValid() ? root.findChild( "_properties" ) : XMLNode{};
        if ( properties.isValid() == false )
        {
            SW_LOG_ERROR_ID( "ModelImporter.GlTFPBRMaterialTemplate2", "glTF '%#': the PBR material template is not a material", sourcePath );
            return false;
        }
        root.setAttribute( "name", string_view( pbr._name ) );
        // 견본에 없는 프로퍼티는 조용히 건너뛰지 않는다 — 견본과 이 표가 어긋나면 값이 기본값으로 남은 머티리얼이 나온다.
        bool bMissingProperty = false;
        auto setAttribute     = [&]( string_view name, const utf8* pAttribute, const string& value )
        {
            const XMLNode item = VrmMaterialImporter::findMaterialProperty( properties, name );
            if ( item.isValid() == false )
            {
                bMissingProperty = true;
                return;
            }
            if ( value.empty() == false )
                item.setAttribute( pAttribute, string_view( value ) );
        };
        setAttribute( "_color", "defaultValue", ModelImportUtil::formatMaterialColor( pbr._baseColor ) );
        setAttribute( "_roughness", "defaultValue", ModelImportUtil::formatMaterialNumber( pbr._roughness ) );
        setAttribute( "_metallic", "defaultValue", ModelImportUtil::formatMaterialNumber( pbr._metallic ) );
        setAttribute( "_normalScale", "defaultValue", ModelImportUtil::formatMaterialNumber( pbr._normalScale ) );
        setAttribute( "_occlusionStrength", "defaultValue", ModelImportUtil::formatMaterialNumber( pbr._occlusionStrength ) );
        setAttribute( "_albedoMap", "assetPath", albedoPath );
        setAttribute( "_normalMap", "assetPath", normalPath );
        setAttribute( "_ormMap", "assetPath", ormPath );
        if ( bMissingProperty )
        {
            SW_LOG_ERROR_ID( "ModelImporter.GlTFPBRMaterialTemplate3", "glTF '%#': the PBR material template lacks a property the importer fills", sourcePath );
            return false;
        }
        const string materialPath = FileUtil::joinPath( FileUtil::joinPath( sideFolder, ModelImportUtil::kMaterialFolder ), pbr._name + ".material" );
        const bool   bWritten     = FileUtil::ensureParentDirectoryExists( materialPath ) && FileUtil::writeTextFile( materialPath, document.saveToString() );
        if ( bWritten == false || ModelImportUtil::writeImportedMeta( assetPaths, materialPath ) == false )
        {
            SW_LOG_ERROR_ID( "ModelImporter.FailedWritePBRMaterial", "Failed to write PBR material %#", materialPath.c_str() );
            return false;
        }
        SW_LOG_INFO_ID( "ModelImporter.GlTFPBRMaterialAlbedo", "glTF '%#': PBR material %# (albedo%#%#)", sourcePath, materialPath.c_str(), normalPath.empty() ? "" : ", normal", ormPath.empty() ? "" : ", orm" );
        return true;
    }

    string ModelImportMaterial::makeRawTextureFolder( string_view sourcePath )
    {
        const string normalized = FileUtil::normalizeSeparators( sourcePath );
        const string marker     = "/" + string( ModelImportUtil::kRawModelFolder ) + "/";
        const size_t at         = normalized.find( marker );
        if ( at == string::npos )
            return {};
        const string domain   = normalized.substr( 0, at );
        const string relative = FileUtil::removeExtension( normalized.substr( at + marker.size() ) );
        return domain + "/" + string( ModelImportUtil::kRawTextureFolder ) + "/" + relative;
    }
} // namespace sw::editor
