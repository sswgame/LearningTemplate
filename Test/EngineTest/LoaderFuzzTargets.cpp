#include "pch.h"

#include "EngineTest/LoaderFuzzTargets.h"

#include "Core/Compression/CompressionStream.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Process/Process.h"

#include "Engine/Animation/Graph/AnimGraphAsset.h"
#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Audio/AudioClipDecoder.h"
#include "Engine/Character/Fit/BodyShape.h"
#include "Engine/Character/Fit/SurfaceState.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Resource/Image/DDSLoader.h"
#include "Engine/Resource/Pack/ResourcePackReader.h"
#include "Engine/Resource/Pack/ResourcePackTypes.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Serialization/Base/StringPool.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/TileMap/TileMapXML.h"
#include "Engine/UserSettings/UserSettingsSchema.h"
#include "Engine/Utility/KeyValueFile.h"

#include "EngineTest/ResourcePackTestUtil.h"

using namespace sw;

namespace test
{
    namespace
    {
        struct LoaderFuzzTargetsInternal
        {
            /** @brief 같은 프로세스 · 같은 이름표의 임시 파일 경로입니다(파일 경로만 받는 로더용 — 팩 · 씬). */
            static string makeScratchPath( string_view tag )
            {
                const string tempDir = FileUtil::getTempDirectory();
                return FileUtil::joinPath( tempDir, string( "sw_fuzz_" ) + to_string( Process::getCurrentProcessId() ) + "_" + string( tag ) );
            }

            static string_view asText( const uint8* pData, size_t size ) { return string_view( reinterpret_cast<const utf8*>( pData ), size ); }

            /** @brief 리소스 루트 아래에서 이름이 @p suffix 로 끝나는 파일을 최대 @p limit 개 읽어 씨앗으로 더합니다. */
            static void appendResourceSeeds( string_view suffix, size_t limit, vector<vector<uint8>>& outListSeed )
            {
                const string& root = ResourceUtil::getRootFolderPath();
                if ( root.empty() )
                    return;
                const string   extension = string( FileUtil::getExtension( suffix ) );
                vector<string> listPath;
                if ( FileUtil::collectFiles( root, extension, listPath, true ) == false )
                    return;
                std::sort( listPath.begin(), listPath.end() );
                size_t added = 0;
                for ( const string& path : listPath )
                {
                    if ( added >= limit || StringUtil::endsWith( path, suffix, true ) == false )
                        continue;
                    vector<uint8> bytes;
                    if ( FileUtil::readFile( path, bytes ) && bytes.empty() == false )
                    {
                        outListSeed.push_back( std::move( bytes ) );
                        ++added;
                    }
                }
            }

            static void appendTextSeed( string_view text, vector<vector<uint8>>& outListSeed )
            {
                outListSeed.emplace_back( reinterpret_cast<const uint8*>( text.data() ), reinterpret_cast<const uint8*>( text.data() ) + text.size() );
            }

            // ---------------------------------------------------------------------------------------------------------
            // 대상 — 결과는 보지 않는다(퍼징이 보는 것은 죽음 · 단언 · 끝나지 않음이다)
            // ---------------------------------------------------------------------------------------------------------
            static void runXML( const uint8* pData, size_t size )
            {
                sw::XMLDocument document;
                (void)document.parse( asText( pData, size ) ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runJSON( const uint8* pData, size_t size )
            {
                JSONDocument document;
                (void)document.tryParse( asText( pData, size ) ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runKeyValue( const uint8* pData, size_t size )
            {
                KeyValueMap mapValue;
                (void)KeyValueFile::parse( asText( pData, size ), mapValue ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runDDS( const uint8* pData, size_t size )
            {
                DDSImageData image;
                (void)DDSLoader::loadFromMemory( pData, size, image ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runMesh( const uint8* pData, size_t size )
            {
                vector<RHIVertex> listVertex;
                (void)MeshAssetFormat::readFromBytes( pData, size, listVertex ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runCompression( const uint8* pData, size_t size )
            {
                vector<uint8> bytes;
                (void)CompressionStream::decompressBuffer( pData, size, bytes );
            }
            static void runPack( const uint8* pData, size_t size )
            {
                const string path = makeScratchPath( "pack.pack" );
                if ( FileUtil::writeFile( path, pData, size ) == false )
                    return;
                ResourcePackReader reader;
                if ( reader.open( path ) )
                {
                    vector<uint8>                bytes;
                    static constexpr string_view kArrPath[] = { "a.txt", "dir/b.bin", "c.xml" };
                    for ( const string_view entryPath : kArrPath )
                    {
                        (void)reader.readFile( entryPath, bytes ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
                    }
                    reader.close();
                }
            }
            static void runSceneXML( const uint8* pData, size_t size )
            {
                const string path = makeScratchPath( "scene.scene.xml" );
                if ( FileUtil::writeFile( path, pData, size ) == false )
                    return;
                SceneDocument document;
                (void)document.loadXML( path ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runSceneBinary( const uint8* pData, size_t size )
            {
                const string path = makeScratchPath( "scene.bin" );
                if ( FileUtil::writeFile( path, pData, size ) == false )
                    return;
                SceneDocument document;
                (void)document.loadBinary( path ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runGameObjectXML( const uint8* pData, size_t size )
            {
                GameObject object( hashed_string( "FuzzObject" ) );
                (void)ObjectStateSerializer::loadFromXMLString( &object, asText( pData, size ) ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runMaterial( const uint8* pData, size_t size )
            {
                const shared_ptr<Material> pMaterial = Material::create();
                (void)pMaterial->loadFromXML( asText( pData, size ) ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runSockets( const uint8* pData, size_t size )
            {
                SocketKindTable kinds;
                kinds.addKind( hashed_string( "Attach" ) );
                kinds.addKind( hashed_string( "GroundPoint" ) );
                kinds.addKind( hashed_string( "HitboxCenter" ) );
                SocketSet sockets;
                (void)sockets.loadFromXMLText( asText( pData, size ), "fuzz.sockets.xml", kinds ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runBodyShape( const uint8* pData, size_t size )
            {
                BodyShapeSet shapes;
                (void)shapes.loadFromXMLText( asText( pData, size ), "fuzz.bodyshape.xml" ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runSurfaceChannels( const uint8* pData, size_t size )
            {
                SurfaceChannelTable table;
                (void)table.loadFromXMLText( asText( pData, size ), "fuzz.surfacechannels.xml" ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runUserSettingsSchema( const uint8* pData, size_t size )
            {
                UserSettingsSchema schema;
                (void)schema.loadFromXMLText( asText( pData, size ), "fuzz.settings.xml" ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runSpriteClip( const uint8* pData, size_t size )
            {
                SpriteClipAsset asset;
                (void)asset.parseJSON( asText( pData, size ) ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runAnimGraph( const uint8* pData, size_t size )
            {
                AnimGraphAsset asset;
                (void)asset.parseJSON( asText( pData, size ) ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runDialogue( const uint8* pData, size_t size )
            {
                DialogueGraphAsset asset;
                (void)asset.parseJSON( asText( pData, size ) ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runSequence( const uint8* pData, size_t size )
            {
                SequenceAsset asset;
                (void)asset.parseJSON( asText( pData, size ) ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runStringTable( const uint8* pData, size_t size )
            {
                SourceStringTable table;
                (void)table.loadFromJSONText( asText( pData, size ), "fuzz.strings.json" ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runTileMap( const uint8* pData, size_t size )
            {
                TileMapXMLData data;
                (void)data.loadFromXML( asText( pData, size ) ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runStringPool( const uint8* pData, size_t size )
            {
                StringPool pool;
                size_t     offset = 0;
                (void)pool.loadFromBinaryBuffer( pData, size, offset ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runWav( const uint8* pData, size_t size )
            {
                AudioPcm pcm;
                (void)AudioClipDecoder::decodeWav( pData, size, pcm );
            }
            static void runOgg( const uint8* pData, size_t size )
            {
                AudioPcm pcm;
                (void)AudioClipDecoder::decodeOgg( pData, size, pcm );
            }

            // ---------------------------------------------------------------------------------------------------------
            // 씨앗
            // ---------------------------------------------------------------------------------------------------------
            static void seedXML( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".xml", 12, outListSeed );
                appendTextSeed( "<a x=\"1\"><b>t&amp;</b><!-- c --><![CDATA[d]]></a>", outListSeed );
            }
            static void seedJSON( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".json", 4, outListSeed );
                appendTextSeed( R"({"a":[1,2.5e3,-0,true,null,"é"],"b":{"c":"d"}})", outListSeed );
            }
            static void seedKeyValue( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".meta", 4, outListSeed );
                appendTextSeed( "# c\nguid=11111111-1111-1111-1111-111111111111\nimported=0\nkey = value with spaces\n", outListSeed );
            }
            static void seedDDS( vector<vector<uint8>>& outListSeed ) { appendResourceSeeds( ".dds", 13, outListSeed ); }
            static void seedMesh( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".mesh", 3, outListSeed );
                vector<RHIVertex> listVertex( 3 );
                vector<uint8>     bytes;
                MeshAssetFormat::makeBytes( listVertex, bytes );
                outListSeed.push_back( std::move( bytes ) );
            }
            static void seedCompression( vector<vector<uint8>>& outListSeed )
            {
                const string  text = "compressible compressible compressible compressible text 0123456789";
                vector<uint8> bytes;
                if ( CompressionStream::compressBuffer( text.data(), text.size(), bytes, CompressionCodecType::Zlib, 6 ) )
                    outListSeed.push_back( bytes );
                if ( CompressionStream::compressBuffer( text.data(), text.size(), bytes, CompressionCodecType::RLE, 0 ) )
                    outListSeed.push_back( bytes );
            }
            static void seedPack( vector<vector<uint8>>& outListSeed )
            {
                const string                           path = makeScratchPath( "seed.pack" );
                const vector<sw::pair<string, string>> listFile{
                    { "a.txt", "alpha alpha alpha" },
                    { "dir/b.bin", string( 300, 'b' ) },
                    { "c.xml", "<c/>" }
                };
                static constexpr PackCompressionType kArrCompression[] = { PackCompressionType::None, PackCompressionType::RLE };
                for ( const PackCompressionType compression : kArrCompression )
                {
                    vector<uint8> bytes;
                    if ( sw::test::ResourcePackTestUtil::createPackFile( path, 0, compression, listFile, true ) && FileUtil::readFile( path, bytes ) )
                        outListSeed.push_back( std::move( bytes ) );
                }
            }
            static void seedScene( vector<vector<uint8>>& outListSeed ) { appendResourceSeeds( ".scene.xml", 4, outListSeed ); }
            static void seedSceneBinary( vector<vector<uint8>>& outListSeed )
            {
                vector<vector<uint8>> listXML;
                appendResourceSeeds( ".scene.xml", 2, listXML );
                for ( const vector<uint8>& xml : listXML )
                {
                    const string  xmlPath = makeScratchPath( "seed.scene.xml" );
                    const string  binPath = makeScratchPath( "seed.scene.bin" );
                    SceneDocument document;
                    vector<uint8> bytes;
                    if ( FileUtil::writeFile( xmlPath, xml.data(), xml.size() ) && document.loadXML( xmlPath ) && document.saveBinary( binPath ) &&
                         FileUtil::readFile( binPath, bytes ) )
                        outListSeed.push_back( std::move( bytes ) );
                }
            }
            static void seedGameObject( vector<vector<uint8>>& outListSeed )
            {
                vector<vector<uint8>> listFile;
                appendResourceSeeds( ".scene.xml", 3, listFile );
                appendResourceSeeds( ".prefab.xml", 6, listFile );
                for ( const vector<uint8>& file : listFile )
                {
                    const string_view text  = asText( file.data(), file.size() );
                    const size_t      start = text.find( "<GameObject" );
                    const size_t      end   = text.find( "</GameObject>", start == string_view::npos ? 0 : start );
                    if ( start != string_view::npos && end != string_view::npos )
                        appendTextSeed( text.substr( start, end + 13 - start ), outListSeed );
                }
            }
            static void seedMaterial( vector<vector<uint8>>& outListSeed ) { appendResourceSeeds( ".material", 6, outListSeed ); }
            static void seedSockets( vector<vector<uint8>>& outListSeed )
            {
                appendTextSeed( "<SocketSet>\n  <VirtualBone name=\"AimRef\" from=\"hand_r\" to=\"hand_l\" weight=\"0.5\"/>\n"
                                "  <Socket name=\"Muzzle\" parent=\"barrel\" kind=\"Attach\" translation=\"0 0 0.42\" rotation=\"0 90 0\" scale=\"1 1 1\"/>\n"
                                "  <Socket name=\"Belly\" parent=\"spine\" kind=\"HitboxCenter\" anchor=\"Surface\" fallback=\"A.B C\"/>\n</SocketSet>\n",
                                outListSeed );
            }
            static void seedBodyShape( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".bodyshape.xml", 2, outListSeed );
                appendTextSeed( "<BodyShapeSet>\n  <Axis name=\"Weight\" min=\"-1\" max=\"1\">\n"
                                "    <Positive morph=\"Heavy\"><Bone name=\"upperarm\" scale=\"1.2 1 1.2\"/></Positive>\n"
                                "    <Negative morph=\"Thin\"/>\n  </Axis>\n  <Axis name=\"Height\" min=\"-1\" max=\"2\">\n"
                                "    <Positive><Bone name=\"lowerarm\" offset=\"0 0.1 0\"/></Positive>\n  </Axis>\n</BodyShapeSet>\n",
                                outListSeed );
            }
            static void seedSurfaceChannels( vector<vector<uint8>>& outListSeed ) { appendResourceSeeds( ".surfacechannels.xml", 2, outListSeed ); }
            static void seedUserSettings( vector<vector<uint8>>& outListSeed ) { appendResourceSeeds( ".settings.xml", 2, outListSeed ); }
            static void seedSpriteClip( vector<vector<uint8>>& outListSeed ) { appendResourceSeeds( ".sprite.json", 2, outListSeed ); }
            static void seedAnimGraph( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".animgraph.json", 2, outListSeed );
                appendTextSeed( R"({"nodes":[{"id":1,"type":"Clip","name":"Idle","x":0,"y":0}],"links":[{"from":1,"to":2}]})", outListSeed );
            }
            static void seedDialogue( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".dialogue.json", 2, outListSeed );
                appendTextSeed( R"({"nodes":[{"id":1,"kind":"Line","text":"Hi","speaker":"A"}],"links":[{"from":1,"to":1,"pin":0}]})", outListSeed );
            }
            static void seedSequence( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".sequence.json", 2, outListSeed );
                appendTextSeed( R"({"duration":3.5,"tracks":[{"name":"T","items":[{"kind":"Event","start":0.5,"end":1.0}]}]})", outListSeed );
            }
            static void seedStringTable( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( SourceStringTable::kExtension, 2, outListSeed );
                appendTextSeed( R"({"culture":"en","entries":{"Menu.Start":{"source":"Start","maxLength":12},"Greeting":{"source":"Hi {name}"}}})", outListSeed );
            }
            static void seedTileMap( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".tilemap.xml", 2, outListSeed );
                appendTextSeed( "<TileMap width=\"4\" height=\"2\" tileSize=\"16\"><Layer name=\"Ground\">1,1,1,1,0,0,2,2</Layer></TileMap>", outListSeed );
            }
            static void seedStringPool( vector<vector<uint8>>& outListSeed )
            {
                StringPool pool;
                (void)pool.internString( "alpha" );
                (void)pool.internString( "beta gamma" );
                vector<uint8> bytes;
                pool.saveToBinaryBuffer( bytes );
                outListSeed.push_back( std::move( bytes ) );
            }
            static void seedWav( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".wav", 2, outListSeed );
                // 8 kHz 모노 16 비트 PCM 네 표본
                static constexpr uint8 kArrWav[] = { 'R', 'I', 'F', 'F', 44, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x40, 0x1F,
                                                     0, 0, 0x80, 0x3E, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 8, 0, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0 };
                outListSeed.emplace_back( kArrWav, kArrWav + sizeof( kArrWav ) );
            }
            static void seedOgg( vector<vector<uint8>>& outListSeed ) { appendResourceSeeds( ".ogg", 1, outListSeed ); }
        };

        constexpr LoaderFuzzTarget kArrLoaderFuzzTarget[] = {
            {               "XML",                &LoaderFuzzTargetsInternal::runXML,             &LoaderFuzzTargetsInternal::seedXML,  true},
            {              "JSON",               &LoaderFuzzTargetsInternal::runJSON,            &LoaderFuzzTargetsInternal::seedJSON,  true},
            {          "KeyValue",           &LoaderFuzzTargetsInternal::runKeyValue,        &LoaderFuzzTargetsInternal::seedKeyValue,  true},
            {               "DDS",                &LoaderFuzzTargetsInternal::runDDS,             &LoaderFuzzTargetsInternal::seedDDS, false},
            {              "Mesh",               &LoaderFuzzTargetsInternal::runMesh,            &LoaderFuzzTargetsInternal::seedMesh, false},
            {       "Compression",        &LoaderFuzzTargetsInternal::runCompression,     &LoaderFuzzTargetsInternal::seedCompression, false},
            {              "Pack",               &LoaderFuzzTargetsInternal::runPack,            &LoaderFuzzTargetsInternal::seedPack, false},
            {          "SceneXML",           &LoaderFuzzTargetsInternal::runSceneXML,           &LoaderFuzzTargetsInternal::seedScene,  true},
            {       "SceneBinary",        &LoaderFuzzTargetsInternal::runSceneBinary,     &LoaderFuzzTargetsInternal::seedSceneBinary, false},
            {     "GameObjectXML",      &LoaderFuzzTargetsInternal::runGameObjectXML,      &LoaderFuzzTargetsInternal::seedGameObject,  true},
            {          "Material",           &LoaderFuzzTargetsInternal::runMaterial,        &LoaderFuzzTargetsInternal::seedMaterial,  true},
            {           "Sockets",            &LoaderFuzzTargetsInternal::runSockets,         &LoaderFuzzTargetsInternal::seedSockets,  true},
            {         "BodyShape",          &LoaderFuzzTargetsInternal::runBodyShape,       &LoaderFuzzTargetsInternal::seedBodyShape,  true},
            {   "SurfaceChannels",    &LoaderFuzzTargetsInternal::runSurfaceChannels, &LoaderFuzzTargetsInternal::seedSurfaceChannels,  true},
            {"UserSettingsSchema", &LoaderFuzzTargetsInternal::runUserSettingsSchema,    &LoaderFuzzTargetsInternal::seedUserSettings,  true},
            {        "SpriteClip",         &LoaderFuzzTargetsInternal::runSpriteClip,      &LoaderFuzzTargetsInternal::seedSpriteClip,  true},
            {         "AnimGraph",          &LoaderFuzzTargetsInternal::runAnimGraph,       &LoaderFuzzTargetsInternal::seedAnimGraph,  true},
            {          "Dialogue",           &LoaderFuzzTargetsInternal::runDialogue,        &LoaderFuzzTargetsInternal::seedDialogue,  true},
            {          "Sequence",           &LoaderFuzzTargetsInternal::runSequence,        &LoaderFuzzTargetsInternal::seedSequence,  true},
            {       "StringTable",        &LoaderFuzzTargetsInternal::runStringTable,     &LoaderFuzzTargetsInternal::seedStringTable,  true},
            {           "TileMap",            &LoaderFuzzTargetsInternal::runTileMap,         &LoaderFuzzTargetsInternal::seedTileMap,  true},
            {        "StringPool",         &LoaderFuzzTargetsInternal::runStringPool,      &LoaderFuzzTargetsInternal::seedStringPool, false},
            {               "Wav",                &LoaderFuzzTargetsInternal::runWav,             &LoaderFuzzTargetsInternal::seedWav, false},
            {               "Ogg",                &LoaderFuzzTargetsInternal::runOgg,             &LoaderFuzzTargetsInternal::seedOgg, false},
        };
    } // namespace

    sw::vector_reference<const LoaderFuzzTarget> getLoaderFuzzTargets()
    {
        return sw::vector_reference<const LoaderFuzzTarget>( kArrLoaderFuzzTarget, sizeof( kArrLoaderFuzzTarget ) / sizeof( kArrLoaderFuzzTarget[0] ) );
    }

    sw::string makeFuzzTracePath( sw::string_view targetName )
    {
        const string tempDir = FileUtil::getTempDirectory();
        return FileUtil::joinPath( tempDir, string( "sw_fuzz_last_" ) + string( targetName ) + ".bin" );
    }

    const LoaderFuzzTarget* findLoaderFuzzTarget( sw::string_view name )
    {
        for ( const LoaderFuzzTarget& target : getLoaderFuzzTargets() )
        {
            if ( name == target._pName )
                return &target;
        }
        return nullptr;
    }
} // namespace test
