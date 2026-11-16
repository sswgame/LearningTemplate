/**
 * @file ModelImportMaterial.cpp
 * @brief 모델 임포트의 VRM 툰 머티리얼 읽기 · 머티리얼 구간 표 만들기 · 구간 텍스처 모으기 · 구간 머티리얼 쓰기입니다.
 */
#include "pch.h"

#include "Editor/Common/Asset/Model/ModelImportMaterial.h"

#include "Core/Algorithm/VectorUtil.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/unordered_set.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"
#include "Core/String/StringUtil.h"

#include "Editor/Common/Asset/ImportedAssetStore.h"
#include "Editor/Common/Asset/Model/ModelImportGeometry.h"
#include "Editor/Common/Asset/Model/ModelImportUtil.h"
#include "Editor/Common/Asset/Model/ModelImporter.h"
#include "Editor/Common/Asset/Model/ToonMaterialDef.h"
#include "Editor/Common/Asset/Texture/TextureImportConfig.h"
#include "Editor/Common/Asset/Texture/TextureImporter.h"

#include "Engine/Asset/AssetFormat.h"
#include "Engine/Config/ConfigManager.h"
#include "Engine/Config/EngineDefaultAssets.h"
#include "Engine/Content/AssetIO.h"
#include "Engine/Content/AssetPaths.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Material/MaterialXML.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Serialization/Inherit/DataInheritance.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    bool ModelImportMaterial::readToonMaterials( const cgltf_data& data, string_view sourcePath, vector<ToonMaterialDef>& outListToon,
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
            expected<ToonMaterialDef> read = unexpected( Error{ ErrorKind::NotFound, "no material read" } );
            if ( bVrm1 )
            {
                read = ToonMaterialDef::readMtoon1Material( materials.at( index ), &outListIgnored );
            }
            else
            {
                // 0.x 는 materialProperties[i] 가 materials[i] 의 것이다(UniVRM 이 같은 순서로 쓴다).
                if ( materialProperties.isArray() == false || index >= materialProperties.size() )
                {
                    SW_LOG_ERROR_ID( "ModelImporter.VRMMaterialNoMaterialProperties", "VRM '%#': material %# has no materialProperties entry", sourcePath, index );
                    return false;
                }
                read = ToonMaterialDef::readVrm0Material( materialProperties.at( index ), materials.at( index ), &outListIgnored );
            }
            if ( read.has_value() == false )
            {
                SW_LOG_ERROR_ID( "ModelImporter.VRM", "VRM '%#': %# (%#)", sourcePath, read.error().message(), read.error().detail() );
                return false;
            }
            outListToon[index] = std::move( *read );
            if ( outListToon[index]._name.empty() )
            {
                StringBuilder<constant::buffer::kMax256> nameBuilder;
                nameBuilder.appendFormat( "material%#", static_cast<uint64>( index ) );
                outListToon[index]._name = string( nameBuilder.view() );
            }
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

    bool ModelImportMaterial::readSectionPBRMaterials( const cgltf_data& data, string_view sourcePath, ModelImportResult& inoutResult )
    {
        inoutResult._listPBRMaterial.clear();
        bool bAnyTextured = false;
        for ( cgltf_size materialIndex = 0; materialIndex < data.materials_count; ++materialIndex )
        {
            const cgltf_material& material = data.materials[materialIndex];
            bAnyTextured                   = bAnyTextured || ( material.has_pbr_metallic_roughness != 0 && material.pbr_metallic_roughness.base_color_texture.texture != nullptr );
        }
        if ( data.materials_count < 2 || bAnyTextured == false )
            return true;
        inoutResult._listPBRMaterial.resize( data.materials_count );
        for ( cgltf_size materialIndex = 0; materialIndex < data.materials_count; ++materialIndex )
        {
            const cgltf_material&                  material = data.materials[materialIndex];
            fixed_string<constant::buffer::kMax64> fallbackName;
            formatstring( fallbackName.data(), fallbackName.capacity(), "material%#", static_cast<uint64>( materialIndex ) );
            const string rawName = material.name != nullptr && material.name[0] != '\0' ? string( material.name ) : string( fallbackName.view() );
            if ( readPBRMaterialValues( material, rawName, sourcePath, inoutResult._listPBRMaterial[materialIndex] ) == false )
                return false;
        }
        return true;
    }

    void ModelImportMaterial::populateMaterialSections( ModelImportUtil::MergedMesh& inoutBody, const cgltf_data& data, const vector<string>& listMaterialName, ModelImportResult& inoutResult )
    {
        // 1인칭 본(머리) 아래 본 — Auto 메시는 이 본에 가중치가 있는 꼭짓점을 가진 삼각형을 3인칭 전용으로 나눈다(UniVRM 과 같다).
        const Skeleton& skeleton = inoutResult._skeleton;
        vector<uint8>   listHeadBone( skeleton.getBoneCount(), SW_FALSE );
        // 1인칭 본이 없으면(비었거나 스켈레톤에 없음 — VRM 읽기가 없는 본을 알렸다) 3인칭 전용 삼각형이 없다.
        const optional<uint32> headBone = inoutResult._avatar._firstPersonBone.empty() ? optional<uint32>{} : skeleton.findBoneIndex( hashed_string( inoutResult._avatar._firstPersonBone ) );
        for ( uint32 bone = 0; headBone.has_value() && bone < skeleton.getBoneCount(); ++bone )
        {
            const int32 parent = skeleton.getBone( bone )._parentIndex;
            listHeadBone[bone] = ( bone == *headBone || ( parent >= 0 && listHeadBone[static_cast<uint32>( parent )] == SW_TRUE ) ) ? SW_TRUE : SW_FALSE;
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
            if ( pMaterial == nullptr || static_cast<size_t>( pMaterial - data.materials ) >= listMaterialName.size() )
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
            SW_LOG_WARNING_ID( "ModelImporter.VRMModelTrianglesNo", "Model with material sections: %# triangles have no material - no mesh section draws them", noMaterialCount );

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
            section._materialIndex = materialIndex;
            auto stemIt            = mapMaterialStem.find( materialIndex );
            if ( stemIt == mapMaterialStem.end() )
            {
                const string baseStem = ModelImportUtil::makeFileStem( listMaterialName[materialIndex] );
                stemIt                = mapMaterialStem.emplace( materialIndex, ModelImportUtil::makeNumberedStem( baseStem, mapStemUse[baseStem]++ ) ).first;
            }
            section._fileStem                    = stemIt->second;
            section._sectionName                 = section._fileStem + ( visibility == MeshPersonVisibility::ThirdPersonOnly ? "_tp" : ( visibility == MeshPersonVisibility::FirstPersonOnly ? "_fp" : "" ) );
            section._firstIndex                  = static_cast<uint32>( begin * 3 );
            section._indexCount                  = static_cast<uint32>( ( end - begin ) * 3 );
            section._personVisibility            = visibility;
            const cgltf_material& sourceMaterial = data.materials[materialIndex]; // 날 이름(파일 이름 꼴로 바꾸기 전) — 대소문자만 다른 두 머티리얼도 가른다
            section._sourceMaterial              = sourceMaterial.name != nullptr ? string( sourceMaterial.name ) : listMaterialName[materialIndex];
            const cgltf_mesh* pSource            = inoutBody._listTriangleSourceMesh[begin];
            section._sourceNode                  = pSource != nullptr && pSource->name != nullptr ? string( pSource->name ) : string{};
            inoutResult._listSection.push_back( std::move( section ) );
            begin = end;
        }
        // 구간 없는 꼬리(머티리얼 없는 삼각형)도 순서만 최적화한다.
        if ( begin < triangleCount )
            ModelImportGeometry::optimizeTriangleRange( inoutBody, begin, triangleCount );
    }

    uint32 ModelImportMaterial::keepPreviousSectionNames( vector<ModelImportSection>& inoutListSection, const vector<MeshSection>& listPrevious )
    {
        uint32 keptCount = 0;
        // 지금 구간 이름마다 그 이름인 구간 수 — 이름이 쓰였는지 볼 때 구간 목록을 훑지 않는다(구간 수 × 앞 구간 수 × 구간 수). 이름을 바꿀 때 함께 고친다.
        unordered_map<string, uint32> mapNameCount;
        for ( const ModelImportSection& section : inoutListSection )
        {
            ++mapNameCount[section._sectionName];
        }
        for ( ModelImportSection& section : inoutListSection )
        {
            if ( section._sourceMaterial.empty() )
                continue;
            for ( const MeshSection& previous : listPrevious )
            {
                // 날 이름 · 가시성이 같은 앞 구간 — 머티리얼 파일 이름(번호)이 바뀌어도 같은 구간이다.
                const bool bSameSource = previous.hasSourceNames() && previous._personVisibility == section._personVisibility &&
                                         StringUtil::equals( previous._sourceMaterial.c_str(), section._sourceMaterial ) && StringUtil::equals( previous._sourceNode.c_str(), section._sourceNode );
                if ( bSameSource == false || StringUtil::equals( previous._name.c_str(), section._sectionName ) )
                    continue;
                const string previousName( previous._name.c_str() );
                const auto   takenIter = mapNameCount.find( previousName );
                if ( takenIter != mapNameCount.end() && takenIter->second > 0 )
                    continue; // 다른 새 구간이 이미 그 이름이다 — 이름이 겹치는 메시는 구간이 없어진다(`Mesh::setSections`)
                --mapNameCount[section._sectionName];
                ++mapNameCount[previousName];
                section._sectionName = previousName;
                ++keptCount;
                break;
            }
        }
        return keptCount;
    }

    bool ModelImportMaterial::collectSectionTextures( const cgltf_data& data, string_view sourcePath, ModelImportResult& inoutResult )
    {
        vector<uint8> listUsed( data.textures_count, SW_FALSE );
        for ( const ModelImportSection& section : inoutResult._listSection )
        {
            const ToonMaterialDef& toon = inoutResult._listToonMaterial[section._materialIndex];
            for ( const optional<uint32>& textureIndex : { toon._baseColorTexture, toon._shadeTexture, toon._emissiveTexture, toon._matcapTexture } )
            {
                if ( textureIndex.has_value() && *textureIndex < data.textures_count )
                    listUsed[*textureIndex] = SW_TRUE;
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
        if ( result._listPBRMaterial.empty() == false )
        {
            unordered_set<string> uniqueWrittenStem; // 구간마다 쓴 목록을 훑지 않는다
            for ( const ModelImportSection& section : result._listSection )
            {
                if ( uniqueWrittenStem.insert( section._fileStem ).second == false )
                    continue;
                if ( writePBRMaterialFile( assetPaths, result._listPBRMaterial[section._materialIndex], section._fileStem, sourcePath, sideFolder ) == false )
                    return false;
            }
            SW_LOG_INFO_ID( "ModelImporter.GlTFPbrMaterialSections", "glTF '%#': %# material sections, %# PBR materials", sourcePath, result._listSection.size(), uniqueWrittenStem.size() );
            return true;
        }
        const string   rawTextureFolder = makeRawTextureFolder( sourcePath );
        vector<string> listTexturePath( result._listTexture.size() );
        for ( size_t textureIndex = 0; textureIndex < result._listTexture.size(); ++textureIndex )
        {
            const ModelImportTexture& texture = result._listTexture[textureIndex];
            if ( texture._bytes.empty() || rawTextureFolder.empty() )
                continue;
            if ( writeEmbeddedImage( assetPaths, texture, rawTextureFolder, listTexturePath[textureIndex] ) == false )
                return false;
        }

        // 툰 머티리얼 견본의 경로는 엔진 기본 에셋 표에 있다. 이 함수는 워커에서도 도므로 서비스를 찾지 않고 데이터에서 표를 읽는다.
        const unique_ptr<EngineDefaultAssets> pDefaultAssets = EngineDefaultAssets::load();
        if ( pDefaultAssets == nullptr )
        {
            SW_LOG_ERROR_ID( "ModelImporter.GlTFEngineDefaultAssets", "glTF '%#': the engine default assets could not be read - no toon material template for the sections", sourcePath );
            return false;
        }
        string toonTemplateText;
        if ( AssetIO::readTextAsset( AssetID{ pDefaultAssets->_toonMaterialTemplate.get() }, toonTemplateText ) == false )
        {
            SW_LOG_ERROR_ID( "ModelImporter.ToonTemplateUnreadable", "glTF '%#': the toon material template could not be read", sourcePath );
            return false;
        }

        unordered_set<string> uniqueWrittenStem;
        for ( const ModelImportSection& section : result._listSection )
        {
            // 같은 머티리얼의 구간(1인칭 가시성이 다른)은 머티리얼 파일 하나를 쓴다.
            if ( uniqueWrittenStem.insert( section._fileStem ).second == false )
                continue;
            const string materialText = ToonMaterialDef::makeMaterialXML( result._listToonMaterial[section._materialIndex], listTexturePath, pDefaultAssets->_toonMaterialTemplate );
            if ( materialText.empty() || writeSharedMaterial( assetPaths, materialText, toonTemplateText, pDefaultAssets->_toonMaterialTemplate.get(), section._fileStem, sourcePath, sideFolder ) == false )
            {
                SW_LOG_ERROR_ID( "ModelImporter.FailedWriteToonMaterial", "Failed to write toon material '%#' of %#", section._fileStem.c_str(), sourcePath );
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

    const cgltf_material* ModelImportMaterial::findTexturedPBRMaterial( const cgltf_data& data )
    {
        for ( cgltf_size materialIndex = 0; materialIndex < data.materials_count; ++materialIndex )
        {
            const cgltf_material& material = data.materials[materialIndex];
            if ( material.has_pbr_metallic_roughness != 0 && material.pbr_metallic_roughness.base_color_texture.texture != nullptr )
                return &material;
        }
        return nullptr;
    }

    bool ModelImportMaterial::readPBRMaterial( const cgltf_data& data, string_view sourcePath, ModelImportResult& inoutResult )
    {
        const cgltf_material* pMaterial = findTexturedPBRMaterial( data );
        if ( pMaterial == nullptr )
            return true;
        const string rawName = pMaterial->name != nullptr && pMaterial->name[0] != '\0' ? string( pMaterial->name ) : string( "material" );
        return readPBRMaterialValues( *pMaterial, rawName, sourcePath, inoutResult._pbrMaterial );
    }

    bool ModelImportMaterial::readPBRMaterialValues( const cgltf_material& material, const string& rawName, string_view sourcePath, ModelImportPBRMaterial& outPBR )
    {
        const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
        outPBR._name                            = ModelImportUtil::makeFileStem( rawName.c_str() );
        outPBR._baseColor                       = float4{ pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3] };
        outPBR._metallic                        = pbr.metallic_factor;
        outPBR._roughness                       = pbr.roughness_factor;
        auto readTexture                        = [&]( const cgltf_texture* pTexture, ModelImportTexture& outTexture, const utf8* pSuffix )
        {
            if ( pTexture == nullptr || pTexture->image == nullptr )
                return true;
            outTexture._fileStem = outPBR._name + pSuffix;
            return readImageBytes( *pTexture->image, sourcePath, outTexture );
        };
        const cgltf_texture* pORM = pbr.metallic_roughness_texture.texture;
        if ( readTexture( pbr.base_color_texture.texture, outPBR._albedo, "_albedo" ) == false || readTexture( material.normal_texture.texture, outPBR._normal, "_normal" ) == false ||
             readTexture( pORM, outPBR._orm, "_orm" ) == false )
            return false;
        outPBR._normalScale           = material.normal_texture.texture != nullptr ? material.normal_texture.scale : 1.0f;
        const cgltf_texture* pAO      = material.occlusion_texture.texture;
        const bool           bSameORM = pAO != nullptr && pORM != nullptr && pAO->image == pORM->image;
        outPBR._occlusionStrength     = bSameORM ? material.occlusion_texture.scale : 0.0f;
        if ( pAO != nullptr && bSameORM == false )
            SW_LOG_WARNING_ID( "ModelImporter.GlTFOcclusionTextureNot", "glTF '%#': the occlusion texture is not packed with metallic-roughness (ORM) - occlusion is not imported", sourcePath );
        outPBR._bAlphaCutoff = material.alpha_mode == cgltf_alpha_mode_mask ? SW_TRUE : SW_FALSE;
        outPBR._alphaCutoff  = material.alpha_cutoff;
        outPBR._bTwoSided    = material.double_sided != 0 ? SW_TRUE : SW_FALSE;
        if ( material.alpha_mode == cgltf_alpha_mode_blend )
            SW_LOG_WARNING_ID( "ModelImporter.GlTFBlendAlphaOpaque", "glTF '%#': material '%#' is alphaMode BLEND - the PBR material is opaque (no translucent PBR permutation)", sourcePath, outPBR._name.c_str() );
        outPBR._bPresent = SW_TRUE;
        return true;
    }

    bool ModelImportMaterial::writePBRMaterial( const AssetPaths& assetPaths, const ModelImportResult& result, string_view sourcePath, string_view sideFolder )
    {
        const ModelImportPBRMaterial& pbr = result._pbrMaterial;
        if ( pbr._bPresent == SW_FALSE )
            return true;
        return writePBRMaterialFile( assetPaths, pbr, pbr._name, sourcePath, sideFolder );
    }

    bool ModelImportMaterial::writePBRMaterialFile( const AssetPaths& assetPaths, const ModelImportPBRMaterial& pbr, string_view fileStem, string_view sourcePath, string_view sideFolder )
    {
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
            return writeEmbeddedImage( assetPaths, texture, rawTextureFolder, outAssetPath );
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
        const XMLNode root       = bParsed ? document.getRoot( "MaterialDef" ) : XMLNode{};
        const XMLNode properties = root.isValid() ? root.findChild( "_properties" ) : XMLNode{};
        if ( properties.isValid() == false )
        {
            SW_LOG_ERROR_ID( "ModelImporter.GlTFPBRMaterialTemplate2", "glTF '%#': the PBR material template is not a material", sourcePath );
            return false;
        }
        root.setAttribute( "name", fileStem );
        // 견본에 없는 프로퍼티는 조용히 건너뛰지 않는다 — 견본과 이 표가 어긋나면 값이 기본값으로 남은 머티리얼이 나온다.
        bool bMissingProperty = false;
        auto setAttribute     = [&]( string_view name, const utf8* pAttribute, const string& value )
        {
            const XMLNode item = ToonMaterialDef::findMaterialProperty( properties, name );
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
        setAttribute( "_alphaCutoff", "defaultValue", ModelImportUtil::formatMaterialNumber( pbr._alphaCutoff ) );
        // glTF alphaMode · doubleSided 는 머티리얼의 칸이다(MASK 는 `Mask`, 양면은 컬 없음 + 양면 조명).
        root.setAttribute( "alphaMode", pbr._bAlphaCutoff == SW_TRUE ? "Mask" : "Opaque" );
        if ( pbr._bTwoSided == SW_TRUE )
        {
            root.setAttribute( "cullMode", "None" );
            root.setAttribute( "twoSidedLighting", "true" );
        }
        setAttribute( "_albedoMap", "assetPath", albedoPath );
        setAttribute( "_normalMap", "assetPath", normalPath );
        setAttribute( "_ormMap", "assetPath", ormPath );
        if ( bMissingProperty )
        {
            SW_LOG_ERROR_ID( "ModelImporter.GlTFPBRMaterialTemplate3", "glTF '%#': the PBR material template lacks a property the importer fills", sourcePath );
            return false;
        }
        const string materialPath = FileUtil::joinPath( FileUtil::joinPath( sideFolder, ModelImportUtil::kMaterialFolder ), string( fileStem ) + ".material" );
        if ( writeSharedMaterial( assetPaths, document.saveToString(), templateText, pDefaultAssets->_pbrMaterialTemplate.get(), fileStem, sourcePath, sideFolder ) == false )
        {
            SW_LOG_ERROR_ID( "ModelImporter.FailedWritePBRMaterial", "Failed to write PBR material %#", materialPath.c_str() );
            return false;
        }
        SW_LOG_INFO_ID( "ModelImporter.GlTFPBRMaterialAlbedo", "glTF '%#': PBR material %# (albedo%#%#)", sourcePath, materialPath.c_str(), normalPath.empty() ? "" : ", normal", ormPath.empty() ? "" : ", orm" );
        return true;
    }

    bool ModelImportMaterial::writeEmbeddedImage( const AssetPaths& assetPaths, const ModelImportTexture& texture, string_view rawTextureFolder, string& outAssetPath )
    {
        // 규칙은 모델 옆에 꺼냈을 경로로 고른다(이름 꼬리 `_normal` · 모델 폴더 패턴). 고른 규칙을 저장소 폴더 이름에 남겨 텍스처 임포트가 같은 규칙으로 임포트한다(`TextureImportConfig::findMatchingRule`).
        TextureImportConfig config{};
        if ( config.loadFromFile( TextureImporter::makeDefaultImportConfigPath() ) == ConfigReadResult::Invalid )
            return false; // 로드가 오류를 남겼다 — 깨진 설정을 기본 규칙으로 읽지 않는다
        const string           logicalPath = FileUtil::joinPath( rawTextureFolder, texture._fileStem + texture._extension );
        TextureImportRuleMatch match       = config.findMatchingRule( assetPaths.toAssetID( logicalPath ) );
        if ( match.hasError() )
        {
            SW_LOG_ERROR_ID( "ModelImporter.TextureImportRuleRejected", "%#", match._error.c_str() );
            return false;
        }
        const string rawMarker = "/" + string( ModelImportUtil::kRawTextureFolder ) + "/";
        const string domain    = logicalPath.substr( 0, logicalPath.find( rawMarker ) );
        const string key       = ImportedAssetStore::makeKey( texture._bytes.data(), texture._bytes.size(), string_view{} );
        const string rawPath   = TextureImportConfig::makeSharedSourcePath( domain, match._rule._name, key, texture._extension );
        if ( ImportedAssetStore::writeIfAbsent( rawPath, texture._bytes.data(), texture._bytes.size() ) == false )
            return false;
        outAssetPath = assetPaths.toAssetID( TextureImporter::makeImportedTexturePath( rawPath ) );
        return true;
    }

    bool ModelImportMaterial::makeSharedMaterialText( string_view fullText, string_view templateText, string_view templatePath, SharedMaterialText& outShared )
    {
        string deltaText;
        if ( DataInheritance::makeDelta( fullText, templateText, MaterialXML::makeInheritanceSchema(), templatePath, deltaText ) == false )
            return false;
        XMLDocument document;
        if ( document.parse( deltaText ) == false )
            return false;
        const XMLNode root = document.getRoot( "MaterialDef" );
        if ( root.isValid() == false )
            return false;
        // 이름은 슬롯의 것이라 키에서 뺀다 — 값이 같으면 이름이 달라도 한 파일이다.
        root.setAttribute( "name", "" );
        const string nameless = document.saveToString();
        outShared._key        = ImportedAssetStore::makeKey( nameless.data(), nameless.size(), string_view{} );
        root.setAttribute( "name", string_view{ outShared._key } );
        outShared._text = document.saveToString();
        return true;
    }

    string ModelImportMaterial::makeSlotMaterialText( string_view fileStem, string_view sharedAssetID )
    {
        XMLDocument                  document;
        const XMLNode                root           = document.appendRoot( "MaterialDef" );
        constexpr AssetFormatVersion kFormatVersion = Material::kAssetType._formatVersion; // 상수로 읽는다 - 칸을 직접 쓰면 Engine 이 내보내지 않는 접미사 표까지 링크한다
        AssetXMLFormat::writeXMLVersion( root, kFormatVersion );
        root.appendAttribute( "name", fileStem );
        root.appendAttribute( "_base", sharedAssetID );
        return document.saveToString();
    }

    bool ModelImportMaterial::writeSharedMaterial( const AssetPaths& assetPaths, string_view fullText, string_view templateText, string_view templatePath, string_view fileStem, string_view sourcePath, string_view sideFolder )
    {
        SharedMaterialText shared;
        if ( makeSharedMaterialText( fullText, templateText, templatePath, shared ) == false )
            return false;
        const string domain = ImportedAssetStore::findDomainFolder( sourcePath );
        if ( domain.empty() )
        {
            SW_LOG_ERROR_ID( "ModelImporter.MaterialSourceNotUnderRaw", "'%#' is not under %#/ - there is no shared store for its material", sourcePath, ModelImportUtil::kRawModelFolder.data() );
            return false;
        }
        const string sharedPath = ImportedAssetStore::makeStorePath( domain, kSharedMaterialKind, shared._key, ".material" );
        if ( ImportedAssetStore::writeIfAbsent( sharedPath, shared._text.data(), shared._text.size() ) == false )
            return false;
        // 손 데이터(씬 · 프리팹)가 부르는 슬롯 주소는 그대로 둔다 — 값은 공유 파일 한 벌이고 슬롯 파일은 그것을 가리키는 한 줄이다(언리얼 Redirector · 덮어쓰기 없는 Material Instance).
        const string slotPath = FileUtil::joinPath( FileUtil::joinPath( sideFolder, ModelImportUtil::kMaterialFolder ), string( fileStem ) + ".material" );
        return FileUtil::ensureParentDirectoryExists( slotPath ) && FileUtil::writeTextFile( slotPath, makeSlotMaterialText( fileStem, assetPaths.toAssetID( sharedPath ) ) );
    }

    string ModelImportMaterial::findSharedMaterial( string_view sideFolder, string_view fileStem )
    {
        const string slotPath = FileUtil::joinPath( FileUtil::joinPath( sideFolder, ModelImportUtil::kMaterialFolder ), string( fileStem ) + ".material" );
        string       text;
        XMLDocument  document;
        if ( FileUtil::readTextFile( slotPath, text ) == false || document.parse( text ) == false )
            return {};
        return string( document.getRoot( "MaterialDef" ).getAttributeText( "_base" ) );
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
