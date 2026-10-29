#include "pch.h"

#include "Editor/Common/Asset/OutfitAtlasBaker.h"

#include "Core/Console/DevCommandRegistry.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Log/Logger.h"
#include "Core/String/StringBuilder.h"

#include "Editor/Common/Asset/OutfitAtlas.h"
#include "Editor/Common/Asset/Texture/ImageUtil.h"

#include "Engine/Asset/Image/ImageFileWriter.h"
#include "Engine/Character/CharacterDataReader.h"
#include "Engine/Character/Fit/CharacterMeshUtil.h"
#include "Engine/FileSystem/AssetPaths.h"
#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Serialization/XML/XMLDocument.h"

namespace sw::editor
{
    SW_LOG_CALLER( "OutfitAtlasBaker" );

    namespace
    {
        struct OutfitAtlasBakerInternal
        {
            static constexpr const utf8* kArrRootAttribute[]   = { "group", "outputMesh", "size", "padding" };
            static constexpr const utf8* kArrPartAttribute[]   = { "name", "mesh" };
            static constexpr const utf8* kArrMapAttribute[]    = { "name", "output" };
            static constexpr const utf8* kArrSourceAttribute[] = { "part", "texture" };

            /** @brief 리소스 루트 기준 경로를 디스크 경로로 풉니다. */
            static string toDiskPath( string_view assetPath ) { return FileUtil::normalizeSeparators( FileUtil::joinPath( AssetPaths::getRootFolderPath(), assetPath ) ); }

            static void readMap( const XMLNode& node, CharacterDataReader& reader, OutfitAtlasMapSpec& outMap )
            {
                reader.reportUnknownAttributes( node, kArrMapAttribute );
                outMap._name   = reader.readName( node, "name", true );
                outMap._output = reader.readName( node, "output", true ).c_str();
                for ( XMLNode child = node.findChild(); child; child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), "Source" ) == false )
                    {
                        reader.reportUnknownElement( child );
                        continue;
                    }
                    reader.reportUnknownAttributes( child, kArrSourceAttribute );
                    OutfitAtlasSourceSpec source;
                    source._part    = reader.readName( child, "part", true );
                    source._texture = reader.readName( child, "texture", true ).c_str();
                    outMap._listSource.push_back( std::move( source ) );
                }
            }

            static void readRoot( const XMLNode& root, CharacterDataReader& reader, OutfitAtlasSpec& outSpec )
            {
                reader.reportUnknownAttributes( root, kArrRootAttribute );
                outSpec._group      = reader.readName( root, "group", true );
                outSpec._outputMesh = reader.readName( root, "outputMesh", true ).c_str();
                reader.readOptionalInt( root, "size", outSpec._size );
                reader.readOptionalInt( root, "padding", outSpec._padding );
                for ( XMLNode child = root.findChild(); child; child = child.findNextSibling() )
                {
                    if ( StringUtil::equals( child.getName(), "Part" ) )
                    {
                        reader.reportUnknownAttributes( child, kArrPartAttribute );
                        OutfitAtlasPartSpec part;
                        part._name = reader.readName( child, "name", true );
                        part._mesh = reader.readName( child, "mesh", true ).c_str();
                        outSpec._listPart.push_back( std::move( part ) );
                    }
                    else if ( StringUtil::equals( child.getName(), "Map" ) )
                    {
                        OutfitAtlasMapSpec map;
                        readMap( child, reader, map );
                        outSpec._listMap.push_back( std::move( map ) );
                    }
                    else
                    {
                        reader.reportUnknownElement( child );
                    }
                }
            }

            /** @brief 의미를 봅니다 — 부품 · 텍스처 종류가 있고, 종류마다 부품마다 텍스처가 하나씩이다. */
            static void validateSpec( const OutfitAtlasSpec& spec, CharacterDataReader& reader )
            {
                if ( spec._listPart.empty() )
                    reader.addError( "Outfit.NoPart", "OutfitAtlas needs at least one Part" );
                if ( spec._listMap.empty() )
                    reader.addError( "Outfit.NoMap", "OutfitAtlas needs at least one Map" );
                if ( spec._size <= 0 || spec._padding < 0 )
                    reader.addError( "Outfit.SizeInvalid", "OutfitAtlas size must be positive and padding 0 or more" );
                for ( const OutfitAtlasMapSpec& map : spec._listMap )
                {
                    if ( map._listSource.size() != spec._listPart.size() )
                        reader.addError( "Outfit.MapSourceCount", string( "Map '" ) + map._name.c_str() + "' needs one Source per Part" );
                    for ( const OutfitAtlasSourceSpec& source : map._listSource )
                    {
                        bool bKnown = false;
                        for ( const OutfitAtlasPartSpec& part : spec._listPart )
                        {
                            bKnown = bKnown || part._name == source._part;
                        }
                        if ( bKnown == false )
                            reader.addError( "Outfit.MapPartUnknown", string( "Map '" ) + map._name.c_str() + "' names unknown part '" + source._part.c_str() + "'" );
                    }
                }
            }

            /** @brief 맵에서 부품의 텍스처 경로입니다. 없으면 빈 글입니다. */
            static const string& findTexture( const OutfitAtlasMapSpec& map, const hashed_string& part )
            {
                static const string s_empty;
                for ( const OutfitAtlasSourceSpec& source : map._listSource )
                {
                    if ( source._part == part )
                        return source._texture;
                }
                return s_empty;
            }

            /** @brief 병합한 메시를 `.mesh` 내용으로 옮깁니다(LOD 는 병합 결과에 없다). */
            static MeshAssetData makeAssetData( const Mesh& mesh )
            {
                MeshAssetData data;
                data._listVertex      = mesh.getVertices();
                data._listSkinVertex  = mesh.getSkinVertices();
                data._listMorphTarget = mesh.getMorphTargets();
                data._listSection     = mesh.getSections();
                data._skinBoneCount   = mesh.getSkinBoneCount();
                return data;
            }

            /** @brief 맵 하나의 이미지를 모두 읽습니다(부품 순서). 읽지 못하면 까닭을 남기고 false 입니다. */
            [[nodiscard]] static bool loadImages( const OutfitAtlasSpec& spec, const OutfitAtlasMapSpec& map, vector<RawImageData>& outListImage )
            {
                outListImage.clear();
                for ( const OutfitAtlasPartSpec& part : spec._listPart )
                {
                    const string& texture = findTexture( map, part._name );
                    RawImageData  image;
                    if ( ImageUtil::loadImage( toDiskPath( texture ), image ) == false || image.isValid() == false )
                    {
                        SW_LOG_ERROR( "Outfit atlas map '%#': texture '%#' of part '%#' could not be read", map._name.c_str(), texture.c_str(), part._name.c_str() );
                        return false;
                    }
                    outListImage.push_back( std::move( image ) );
                }
                return true;
            }

            /** @brief 부품 메시를 모두 읽습니다(부품 순서). 읽지 못하면 까닭을 남기고 false 입니다. */
            [[nodiscard]] static bool loadPartMeshes( const OutfitAtlasSpec& spec, vector<shared_ptr<Mesh>>& outListMesh )
            {
                outListMesh.clear();
                for ( const OutfitAtlasPartSpec& part : spec._listPart )
                {
                    shared_ptr<Mesh> mesh = MeshCache::acquire( part._mesh );
                    if ( mesh == nullptr )
                    {
                        SW_LOG_ERROR( "Outfit atlas part '%#': mesh '%#' could not be read", part._name.c_str(), part._mesh.c_str() );
                        return false;
                    }
                    outListMesh.push_back( std::move( mesh ) );
                }
                return true;
            }

            /** @brief 맵마다 이미지를 읽고, 첫 맵의 크기로 배치합니다. 다른 맵은 부품마다 같은 크기여야 합니다. */
            [[nodiscard]] static bool loadMapImagesAndPack( const OutfitAtlasSpec& spec, uint32 atlasSize, uint32 padding, vector<vector<RawImageData>>& outListMapImage,
                                                            vector<OutfitAtlasRect>& outListRect )
            {
                outListMapImage.clear();
                outListMapImage.resize( spec._listMap.size() );
                vector<OutfitAtlasSource> listSource;
                for ( size_t mapIndex = 0; mapIndex < spec._listMap.size(); ++mapIndex )
                {
                    if ( loadImages( spec, spec._listMap[mapIndex], outListMapImage[mapIndex] ) == false )
                        return false;
                    for ( size_t partIndex = 0; partIndex < spec._listPart.size(); ++partIndex )
                    {
                        const RawImageData& image = outListMapImage[mapIndex][partIndex];
                        if ( mapIndex == 0 )
                        {
                            OutfitAtlasSource source;
                            source._part   = spec._listPart[partIndex]._name;
                            source._width  = static_cast<uint32>( image._width );
                            source._height = static_cast<uint32>( image._height );
                            listSource.push_back( source );
                            continue;
                        }
                        const bool bSameSize = listSource[partIndex]._width == static_cast<uint32>( image._width ) && listSource[partIndex]._height == static_cast<uint32>( image._height );
                        if ( bSameSize == false )
                        {
                            SW_LOG_ERROR( "Outfit atlas map '%#': texture of part '%#' is %#x%# but the first map made it %#x%# - every map must share one layout",
                                          spec._listMap[mapIndex]._name.c_str(), spec._listPart[partIndex]._name.c_str(), image._width, image._height, listSource[partIndex]._width,
                                          listSource[partIndex]._height );
                            return false;
                        }
                    }
                }
                string error;
                if ( OutfitAtlasLayout::pack( listSource, atlasSize, padding, outListRect, &error ) == false )
                {
                    SW_LOG_ERROR( "Outfit atlas layout: %#", error.c_str() );
                    return false;
                }
                return true;
            }

            /** @brief 부품 메시를 하나로 병합합니다 — 훅이 부품을 한 그룹(구간)으로 모으고 UV 를 배치대로 옮깁니다. 타일링 UV 가 있으면 false 입니다. */
            [[nodiscard]] static bool mergeParts( const OutfitAtlasSpec& spec, const vector<shared_ptr<Mesh>>& listMesh, uint32 atlasSize, const vector<OutfitAtlasRect>& listRect,
                                                  MeshMergeResult& outMerged )
            {
                const OutfitAtlasHooks hooks( spec._group, atlasSize, listRect );
                vector<MeshMergePart>  listMergePart;
                for ( size_t partIndex = 0; partIndex < spec._listPart.size(); ++partIndex )
                {
                    listMergePart.push_back( MeshMergePart{ spec._listPart[partIndex]._name, hashed_string{}, listMesh[partIndex].get() } );
                }
                string error;
                if ( CharacterMeshUtil::mergeMeshes( listMergePart, &hooks, outMerged, &error ) == false )
                {
                    SW_LOG_ERROR( "Outfit atlas merge: %#", error.c_str() );
                    return false;
                }
                if ( hooks.getClampedUvCount() > 0 )
                {
                    SW_LOG_ERROR( "Outfit atlas: %# UVs lie outside 0..1 - tiled textures cannot be put into an atlas, nothing was written", hooks.getClampedUvCount() );
                    return false;
                }
                return true;
            }

            /** @brief 맵마다 아틀라스 이미지를 합성합니다. 쓰기 전에 모두 메모리에서 끝내므로 도중에 실패해도 반쪽 결과를 남기지 않습니다. */
            [[nodiscard]] static bool composeAtlases( const OutfitAtlasSpec& spec, uint32 atlasSize, uint32 padding, const vector<vector<RawImageData>>& listMapImage,
                                                      const vector<OutfitAtlasRect>& listRect, vector<vector<uint8>>& outListAtlas )
            {
                outListAtlas.clear();
                outListAtlas.resize( spec._listMap.size() );
                string error;
                for ( size_t mapIndex = 0; mapIndex < spec._listMap.size(); ++mapIndex )
                {
                    OutfitAtlasComposer::initialize( atlasSize, outListAtlas[mapIndex] );
                    for ( size_t partIndex = 0; partIndex < spec._listPart.size(); ++partIndex )
                    {
                        const RawImageData& image = listMapImage[mapIndex][partIndex];
                        if ( OutfitAtlasComposer::blit( atlasSize, listRect[partIndex], padding, image._bytes, static_cast<uint32>( image._width ), static_cast<uint32>( image._height ),
                                                        outListAtlas[mapIndex], &error ) == false )
                        {
                            SW_LOG_ERROR( "Outfit atlas compose: %#", error.c_str() );
                            return false;
                        }
                    }
                }
                return true;
            }

            /** @brief 아틀라스 이미지와 병합한 메시를 디스크에 쓰고 결과를 채웁니다. */
            [[nodiscard]] static bool writeOutputs( const OutfitAtlasSpec& spec, uint32 atlasSize, const vector<vector<uint8>>& listAtlas, const MeshMergeResult& merged,
                                                    OutfitAtlasBakeResult& outResult )
            {
                for ( size_t mapIndex = 0; mapIndex < spec._listMap.size(); ++mapIndex )
                {
                    const string diskPath = toDiskPath( spec._listMap[mapIndex]._output );
                    if ( ImageFileWriter::writePNGRGBA8( diskPath, listAtlas[mapIndex], atlasSize, atlasSize ) == false )
                    {
                        SW_LOG_ERROR( "Outfit atlas image '%#' could not be written", diskPath.c_str() );
                        return false;
                    }
                    outResult._listTexturePath.push_back( spec._listMap[mapIndex]._output );
                }
                const string meshDiskPath = toDiskPath( spec._outputMesh );
                if ( MeshAssetFormat::saveToFile( meshDiskPath, makeAssetData( *merged._mesh ) ) == false )
                {
                    SW_LOG_ERROR( "Outfit atlas mesh '%#' could not be written", meshDiskPath.c_str() );
                    return false;
                }
                return true;
            }

            static bool runBake( const vector<string>& listArgument, string& outReply )
            {
                if ( listArgument.size() != 1 )
                    return false;
                OutfitAtlasSpec spec;
                if ( OutfitAtlasBaker::loadSpec( AssetID{ listArgument[0] }, spec ) == false )
                {
                    outReply = "outfit atlas spec '" + listArgument[0] + "' could not be read (see the log)";
                    return false;
                }
                OutfitAtlasBakeResult result;
                if ( OutfitAtlasBaker::bake( spec, result ) == false )
                {
                    outReply = "outfit atlas '" + listArgument[0] + "' failed (see the log) - nothing was written";
                    return false;
                }
                outReply = formatToString( "baked %# parts (%# vertices) into %# and %# atlas image(s) - run --import-textures for the images", result._partCount, result._vertexCount, result._meshPath, static_cast<uint32>( result._listTexturePath.size() ) );
                return true;
            }
        };
    } // namespace

    bool OutfitAtlasBaker::parseSpec( string_view xmlText, string_view sourceName, OutfitAtlasSpec& outSpec )
    {
        outSpec = OutfitAtlasSpec{};
        CharacterDataReader reader( sourceName );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.parseRoot( document, xmlText, "OutfitAtlas", root ) )
        {
            OutfitAtlasBakerInternal::readRoot( root, reader, outSpec );
            OutfitAtlasBakerInternal::validateSpec( outSpec, reader );
        }
        const bool bRead = reader.finish();
        if ( bRead == false )
            outSpec = OutfitAtlasSpec{};
        return bRead;
    }

    bool OutfitAtlasBaker::loadSpec( const AssetID& assetID, OutfitAtlasSpec& outSpec )
    {
        const string_view assetPath = assetID.view();
        outSpec                     = OutfitAtlasSpec{};
        CharacterDataReader reader( assetPath );
        XMLDocument         document;
        XMLNode             root;
        if ( reader.loadRoot( document, assetID, "OutfitAtlas", root ) )
        {
            OutfitAtlasBakerInternal::readRoot( root, reader, outSpec );
            OutfitAtlasBakerInternal::validateSpec( outSpec, reader );
        }
        const bool bRead = reader.finish();
        if ( bRead == false )
            outSpec = OutfitAtlasSpec{};
        return bRead;
    }

    bool OutfitAtlasBaker::bake( const OutfitAtlasSpec& spec, OutfitAtlasBakeResult& outResult )
    {
        using Internal        = OutfitAtlasBakerInternal;
        outResult             = OutfitAtlasBakeResult{};
        const bool bEmptySpec = spec._listPart.empty() || spec._listMap.empty();
        const bool bBadSize   = spec._size <= 0 || spec._padding < 0;
        if ( bEmptySpec || bBadSize )
        {
            SW_LOG_ERROR( "Outfit atlas spec is empty or has a bad size - nothing baked" );
            return false;
        }

        vector<shared_ptr<Mesh>> listMesh;
        if ( Internal::loadPartMeshes( spec, listMesh ) == false )
            return false;

        const uint32                 atlasSize = static_cast<uint32>( spec._size );
        const uint32                 padding   = static_cast<uint32>( spec._padding );
        vector<vector<RawImageData>> listMapImage;
        vector<OutfitAtlasRect>      listRect;
        if ( Internal::loadMapImagesAndPack( spec, atlasSize, padding, listMapImage, listRect ) == false )
            return false;

        MeshMergeResult merged;
        if ( Internal::mergeParts( spec, listMesh, atlasSize, listRect, merged ) == false )
            return false;

        // 합성은 쓰기 전에 모두 메모리에서 끝낸다 — 도중에 실패해도 반쪽 결과를 남기지 않는다.
        vector<vector<uint8>> listAtlas;
        if ( Internal::composeAtlases( spec, atlasSize, padding, listMapImage, listRect, listAtlas ) == false )
            return false;
        if ( Internal::writeOutputs( spec, atlasSize, listAtlas, merged, outResult ) == false )
            return false;
        outResult._partCount   = static_cast<uint32>( spec._listPart.size() );
        outResult._vertexCount = merged._mesh->getVertexCount();
        outResult._meshPath    = spec._outputMesh;
        SW_LOG_INFO( "Outfit atlas baked: %# parts, %# vertices, %# image(s) at %#x%# -> %#", outResult._partCount, outResult._vertexCount,
                     static_cast<uint32>( outResult._listTexturePath.size() ), atlasSize, atlasSize, spec._outputMesh.c_str() );
        return true;
    }

    SW_DEV_COMMAND( AppearanceBakeAtlas, "appearance.bakeatlas", "appearance.bakeatlas <spec.outfitatlas.xml>",
                    "Bake an outfit atlas: merge the spec's part meshes into one with atlas UVs and compose the atlas image(s) (then run --import-textures)",
                    &OutfitAtlasBakerInternal::runBake );
} // namespace sw::editor
