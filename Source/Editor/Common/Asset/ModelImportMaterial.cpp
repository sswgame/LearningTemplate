/**
 * @file ModelImportMaterial.cpp
 * @brief 모델 임포트의 VRM 툰 머티리얼 읽기 · 머티리얼 구간 나누기 · 구간 텍스처 모으기 · 구간 메시와 머티리얼 쓰기입니다.
 */
#include "pch.h"

#include "Core/Common/HashUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"

#include "Editor/Common/Asset/ModelImportUtil.h"
#include "Editor/Common/Asset/ModelImporter.h"
#include "Editor/Common/Asset/TextureImporter.h"
#include "Editor/Common/Asset/VrmMaterialImporter.h"

#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Serialization/JSON/JSONDocument.h"

namespace sw::editor
{
    SW_LOG_CALLER( "ModelImporter" );

    bool ModelImportUtil::readToonMaterials( const cgltf_data& data, string_view sourcePath, vector<ToonMaterialDesc>& outListToon,
                                             vector<string>& outListIgnored )
    {
        outListToon.clear();
        if ( data.json == nullptr || data.json_size == 0 )
            return true;
        JSONDocument document;
        if ( document.parse( string_view( data.json, data.json_size ) ) == false )
            return true; // cgltf 가 이미 읽은 JSON 이다 — 여기서 못 읽을 일은 없지만, 못 읽으면 VRM 이 아닌 것으로 본다
        const JSONValue root       = document.getRoot();
        const JSONValue extensions = root.get( "extensions", false );
        const JSONValue vrm0       = extensions.isObject() ? extensions.get( "VRM", false ) : JSONValue{};
        const JSONValue materials  = root.get( "materials", false );
        const size_t    count      = materials.isArray() ? materials.size() : 0;

        bool bVrm1 = extensions.isObject() && extensions.has( "VRMC_vrm", false );
        for ( size_t index = 0; index < count && bVrm1 == false; ++index )
        {
            const JSONValue materialExtensions = materials.at( index ).get( "extensions", false );
            bVrm1                              = materialExtensions.isObject() && materialExtensions.has( "VRMC_materials_mtoon", false );
        }
        if ( vrm0.isObject() == false && bVrm1 == false )
            return true;

        outListToon.resize( count );
        const JSONValue materialProperties = vrm0.isObject() ? vrm0.get( "materialProperties", false ) : JSONValue{};
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
                    SW_LOG_ERROR( "VRM '%#': material %# has no materialProperties entry", sourcePath, index );
                    return false;
                }
                bRead = VrmMaterialImporter::readVrm0Material( materialProperties.at( index ), materials.at( index ), outListToon[index], &outListIgnored, error );
            }
            if ( bRead == false )
            {
                SW_LOG_ERROR( "VRM '%#': %#", sourcePath, error.c_str() );
                return false;
            }
            if ( outListToon[index]._name.empty() )
                outListToon[index]._name = "material" + to_string( static_cast<uint64>( index ) );
        }
        return true;
    }

    void ModelImportUtil::splitMaterialSections( const MergedMesh& merged, const cgltf_data& data, const vector<ToonMaterialDesc>& listToon, uint32 skinBoneCount,
                                                 ModelImportResult& inoutResult )
    {
        unordered_map<string, uint32> mapStemUse;
        for ( size_t materialIndex = 0; materialIndex < listToon.size(); ++materialIndex )
        {
            const cgltf_material* pMaterial = &data.materials[materialIndex];
            MergedMesh            section;
            vector<uint32>        listRemap( merged._listVertex.size(), 0xFFFFFFFFu );
            for ( size_t triangle = 0; triangle < merged._listTriangleMaterial.size(); ++triangle )
            {
                if ( merged._listTriangleMaterial[triangle] != pMaterial )
                    continue;
                for ( uint32 corner = 0; corner < 3; ++corner )
                {
                    const uint32 source = merged._listIndex[triangle * 3 + corner];
                    if ( listRemap[source] == 0xFFFFFFFFu )
                    {
                        listRemap[source] = static_cast<uint32>( section._listVertex.size() );
                        section._listVertex.push_back( merged._listVertex[source] );
                    }
                    section._listIndex.push_back( listRemap[source] );
                }
            }
            if ( section._listIndex.empty() )
                continue;
            // 모프 타깃(표정)은 이 구간의 정점에 차이가 있는 것만 옮긴다 — 얼굴 타깃이 몸 · 옷 구간에 빈 채로 남으면 그 구간도 모프 풀을 탄다.
            for ( size_t targetIndex = 0; targetIndex < merged._listMorphName.size(); ++targetIndex )
            {
                vector<float3> listPosition( section._listVertex.size() );
                vector<float3> listNormal( section._listVertex.size() );
                bool           bMoves = false;
                for ( size_t source = 0; source < listRemap.size(); ++source )
                {
                    if ( listRemap[source] == 0xFFFFFFFFu )
                        continue;
                    listPosition[listRemap[source]] = merged._listMorphPosition[targetIndex][source];
                    listNormal[listRemap[source]]   = merged._listMorphNormal[targetIndex][source];
                    bMoves                          = bMoves || listPosition[listRemap[source]].getLengthSquared() > 1e-7f || listNormal[listRemap[source]].getLengthSquared() > 1e-7f;
                }
                if ( bMoves == false )
                    continue;
                section._listMorphName.push_back( merged._listMorphName[targetIndex] );
                section._listMorphPosition.push_back( std::move( listPosition ) );
                section._listMorphNormal.push_back( std::move( listNormal ) );
            }
            ModelImportSection output{};
            output._material      = listToon[materialIndex];
            const string baseStem = makeFileStem( output._material._name );
            const uint32 useCount = mapStemUse[baseStem]++;
            output._fileStem      = makeNumberedStem( baseStem, useCount );
            expandTriangles( section, skinBoneCount, output._mesh );
            inoutResult._listSection.push_back( std::move( output ) );
        }
    }

    bool ModelImportUtil::collectSectionTextures( const cgltf_data& data, string_view sourcePath, ModelImportResult& inoutResult )
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
            const string       baseName = ( image.name != nullptr && image.name[0] != '\0' ) ? makeFileStem( image.name ) : "image" + to_string( static_cast<uint64>( imageIndex ) );
            listImageStem[imageIndex]   = makeNumberedStem( baseName, mapStemUse[baseName]++ );
        }

        inoutResult._listTexture.assign( data.textures_count, ModelImportTexture{} );
        for ( cgltf_size textureIndex = 0; textureIndex < data.textures_count; ++textureIndex )
        {
            const cgltf_image* pImage = data.textures[textureIndex].image;
            if ( listUsed[textureIndex] == SW_FALSE || pImage == nullptr )
                continue;
            ModelImportTexture& texture = inoutResult._listTexture[textureIndex];
            texture._fileStem           = listImageStem[static_cast<size_t>( pImage - data.images )];
            const string mimeType       = pImage->mime_type != nullptr ? string( pImage->mime_type ) : string();
            texture._extension          = ( mimeType == "image/jpeg" ) ? ".jpg" : ".png";
            if ( pImage->buffer_view != nullptr )
            {
                const uint8* pBytes = cgltf_buffer_view_data( pImage->buffer_view );
                if ( pBytes != nullptr )
                    texture._bytes.assign( pBytes, pBytes + pImage->buffer_view->size );
            }
            else if ( pImage->uri != nullptr && StringUtil::startsWith( pImage->uri, "data:" ) == false )
            {
                texture._extension = FileUtil::getExtension( pImage->uri );
                // 원본이 가리키는 바깥 파일을 못 읽으면 흰 텍스처로 임포트하지 않고 실패한다(빠진 파일이 결과에 말없이 묻힌다).
                const string imagePath = FileUtil::joinPath( FileUtil::getDirectoryPart( sourcePath ), pImage->uri );
                if ( FileUtil::readFile( imagePath, texture._bytes ) == false )
                {
                    SW_LOG_ERROR( "glTF '%#': texture %# cannot read its image file %#", sourcePath, textureIndex, imagePath.c_str() );
                    return false;
                }
            }
            if ( texture._bytes.empty() )
                SW_LOG_WARNING( "glTF '%#': texture %# has no readable image bytes - the material samples white", sourcePath, textureIndex );
        }
        return true;
    }

    bool ModelImportUtil::writeMaterialSections( const ModelImportResult& result, string_view sourcePath, string_view sideFolder )
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
                FileUtil::ensureDirectoryExists( rawTextureFolder );
                if ( FileUtil::writeFile( rawPath, texture._bytes.data(), texture._bytes.size() ) == false )
                {
                    SW_LOG_ERROR( "Failed to write texture source %#", rawPath.c_str() );
                    return false;
                }
            }
            listTexturePath[textureIndex] = ResourcePaths::toResourceID( TextureImporter::makeImportedTexturePath( rawPath ) );
        }

        for ( const ModelImportSection& section : result._listSection )
        {
            const string meshPath = FileUtil::joinPath( FileUtil::joinPath( sideFolder, kSectionFolder ), section._fileStem + string( MeshAssetFormat::kExtension ) );
            if ( MeshAssetFormat::saveToFile( meshPath, section._mesh ) == false )
            {
                SW_LOG_ERROR( "Failed to write section mesh %#", meshPath.c_str() );
                return false;
            }
            const string materialText = VrmMaterialImporter::makeMaterialXML( section._material, listTexturePath );
            const string materialPath = FileUtil::joinPath( FileUtil::joinPath( sideFolder, kMaterialFolder ), section._fileStem + ".material" );
            FileUtil::ensureDirectoryExists( FileUtil::getDirectoryPart( materialPath ) );
            if ( materialText.empty() || FileUtil::writeTextFile( materialPath, materialText ) == false )
            {
                SW_LOG_ERROR( "Failed to write toon material %#", materialPath.c_str() );
                return false;
            }
            // 머티리얼 캐시는 잡을 때 `.meta` 를 지어 붙인다 — 옆 폴더에 실행마다 다른 GUID 가 생기면 임포트 결과 해시가 어긋난다.
            // 그래서 임포터가 경로에서 정해지는 GUID 로 미리 쓴다(언리얼 · 유니티의 임포트 부산물도 임포터가 식별자를 정한다).
            const string resourceID = ResourcePaths::toResourceID( materialPath );
            if ( FileUtil::writeTextFile( materialPath + path::kMetaExtension, "guid=" + makeImportedGuid( resourceID ) + "\nsourcePath=" + resourceID + "\nimported=1\n" ) == false )
            {
                SW_LOG_ERROR( "Failed to write %#.meta", materialPath.c_str() );
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
            SW_LOG_WARNING( "VRM '%#': material keys the toon material does not support (ignored): %#", sourcePath, joined.c_str() );
        }
        SW_LOG_INFO( "VRM '%#': %# material sections, %# texture sources", sourcePath, result._listSection.size(), result._listTexture.size() );
        return true;
    }

    string ModelImportUtil::makeImportedGuid( string_view resourceID )
    {
        const uint64 high = HashUtil::computeStringHash64( resourceID.data(), resourceID.size(), false );
        const uint64 low  = HashUtil::computeStringHash64( resourceID.data(), resourceID.size(), false, high ^ HashUtil::kGoldenRatio64 );
        // 8-4-4-4-12 글자 16 진. 셋째 그룹 첫 글자는 버전 4, 넷째 그룹 첫 글자는 변형(8..b)이다.
        auto appendHex = []( string& inoutText, uint64 value, uint32 digitCount )
        {
            static constexpr utf8 kArrDigit[] = "0123456789abcdef";
            for ( uint32 digit = digitCount; digit > 0; --digit )
            {
                inoutText += kArrDigit[( value >> ( ( digit - 1 ) * 4 ) ) & 0xFu];
            }
        };
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

    string ModelImportUtil::makeRawTextureFolder( string_view sourcePath )
    {
        const string normalized = FileUtil::normalizeSeparators( sourcePath );
        const string marker     = "/" + string( kRawModelFolder ) + "/";
        const size_t at         = normalized.find( marker );
        if ( at == string::npos )
            return {};
        const string domain   = normalized.substr( 0, at );
        const string relative = FileUtil::removeExtension( normalized.substr( at + marker.size() ) );
        return domain + "/" + string( kRawTextureFolder ) + "/" + relative;
    }
} // namespace sw::editor
