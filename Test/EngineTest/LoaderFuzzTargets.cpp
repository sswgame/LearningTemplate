#include "pch.h"

#include "EngineTest/LoaderFuzzTargets.h"

#include "Core/Compression/CompressionStream.h"
#include "Core/Container/LittleEndianUtil.h"
#include "Core/Container/StringUtil.h"
#include "Core/File/FileUtil.h"
#include "Core/Network/Connection/NetConnection.h"
#include "Core/Network/Connection/NetHost.h"
#include "Core/Network/Message/NetRequest.h"
#include "Core/Network/Message/NetSendBudget.h"
#include "Core/Network/Message/StreamFrame.h"
#include "Core/Network/Replication/NetInputWindow.h"
#include "Core/Network/Transport/NetTransport.h"
#include "Core/Serialization/BitStream.h"

#include "Engine/Animation/Graph/AnimationGraphAsset.h"
#include "Engine/Animation/Sprite/SpriteClipAsset.h"
#include "Engine/Audio/AudioClipDecoder.h"
#include "Engine/Character/Fit/BodyShape.h"
#include "Engine/Character/Fit/SurfaceState.h"
#include "Engine/Character/PairedAnimation/PairedAnimationSnapshot.h"
#include "Engine/Character/Socket/SocketSet.h"
#include "Engine/Common/EngineServices.h"
#include "Engine/Destruction/FractureNetworkSync.h"
#include "Engine/Dialogue/DialogueGraphAsset.h"
#include "Engine/Graphics/Material/Material.h"
#include "Engine/Graphics/Mesh/MeshAssetFormat.h"
#include "Engine/Localization/LocalizationDocuments.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/Serialization/ObjectStateSerializer.h"
#include "Engine/Reflection/TypeRegistry.h"
#include "Engine/Resource/Image/DDSLoader.h"
#include "Engine/Resource/Pack/PackPatch.h"
#include "Engine/Resource/Pack/ResourcePakReader.h"
#include "Engine/Resource/Pack/ResourcePakTypes.h"
#include "Engine/Resource/ResourcePaths.h"
#include "Engine/Scene/SceneDocument.h"
#include "Engine/Scene/SceneStateHash.h"
#include "Engine/Sequencer/SequenceAsset.h"
#include "Engine/Serialization/Base/SchemaMigrate.h"
#include "Engine/Serialization/Base/StringPool.h"
#include "Engine/Serialization/Fixed/FixedLayout.h"
#include "Engine/Serialization/Format/BinarySerializer.h"
#include "Engine/Serialization/JSON/JSONDocument.h"
#include "Engine/Serialization/XML/XMLDocument.h"
#include "Engine/TileMap/TileMapXML.h"
#include "Engine/UserSettings/UserSettingsSchema.h"

#include "EngineTest/ResourcePakTestUtil.h"

#include "GameFramework/Base/Actor/Control/Intent/ControlIntent.h"
#include "GameFramework/Base/Online/HTTP/HTTP2Frame.h"
#include "GameFramework/Base/Online/HTTP/HTTPTypes.h"
#include "GameFramework/Base/Online/HTTP/Hpack.h"
#include "GameFramework/Base/Online/Security/JSONWebToken.h"

#include "TestFramework/Harness/TestStreamEndpointPair.h"
#include "TestFramework/TestTempPath.h"

using namespace sw;

namespace test
{
    namespace
    {
        struct LoaderFuzzTargetsInternal
        {
            /** @brief 이 케이스의 임시 폴더 아래 디스크 경로입니다(파일 경로만 받는 로더용 — 팩 · 씬). 리소스 입구로 읽을 때는 `makeTempResourceID` 로 id 를 얻는다(OS 경로는 거절된다). */
            static string makeScratchPath( string_view tag ) { return ::test::makeTempPath( string( "fuzz_" ) + string( tag ) ); }

            static string_view asText( const uint8* pData, size_t size ) { return string_view( reinterpret_cast<const utf8*>( pData ), size ); }

            /**
             * @brief 씨앗 파일을 고르는 순서입니다.
             * @details 텍스트는 이름 순이다 — 체크아웃마다 줄끝(CRLF · LF)이 달라 크기 순은 플랫폼마다 다른 씨앗을 고른다.
             *          바이너리는 가장 작은 것부터다(크기가 같으면 이름 순) — 이름 순이면 리소스가 늘 때 몇 분짜리 음악이 Ogg 씨앗이 되어
             *          Debug 에서 대상 하나가 85 초를 썼다. 씨앗 비용이 데이터에 끌려가지 않게 한다.
             */
            enum class SeedPick : uint8
            {
                ByName,
                SmallestFirst
            };

            /** @brief 리소스 루트 아래에서 이름이 @p suffix 로 끝나는 파일을 @p pick 순서로 최대 @p limit 개 읽어 씨앗으로 더합니다. */
            static void appendResourceSeeds( string_view suffix, size_t limit, SeedPick pick, vector<vector<uint8>>& outListSeed )
            {
                const string& root = ResourcePaths::getRootFolderPath();
                if ( root.empty() )
                    return;
                const string   extension = string( FileUtil::getExtension( suffix ) );
                vector<string> listPath;
                if ( FileUtil::collectFiles( root, extension, listPath, true ) == false )
                    return;
                vector<std::pair<uint64, string>> listOrderedPath;
                for ( string& path : listPath )
                {
                    if ( StringUtil::endsWith( path, suffix, true ) == false )
                        continue;
                    const uint64 sortSize = pick == SeedPick::SmallestFirst ? FileUtil::getFileSize( path ) : 0;
                    listOrderedPath.emplace_back( sortSize, std::move( path ) );
                }
                std::sort( listOrderedPath.begin(), listOrderedPath.end() );
                size_t added = 0;
                for ( const auto& [sortSize, path] : listOrderedPath )
                {
                    if ( added >= limit )
                        break;
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
                ResourcePakReader reader;
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
                (void)document.loadXML( ::test::makeTempResourceID( path ) ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runSceneBinary( const uint8* pData, size_t size )
            {
                const string path = makeScratchPath( "scene.bin" );
                if ( FileUtil::writeFile( path, pData, size ) == false )
                    return;
                SceneDocument document;
                (void)document.loadBinary( ::test::makeTempResourceID( path ) ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runGameObjectXML( const uint8* pData, size_t size )
            {
                GameObject object( hashed_string( "FuzzObject" ) );
                (void)ObjectStateSerializer::loadFromXMLString( &object, asText( pData, size ), sw::ObjectLoadContext{ sw::SerializePurpose::Authoring } ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
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
                (void)asset.parseXML( asText( pData, size ) ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runAnimationGraph( const uint8* pData, size_t size )
            {
                AnimationGraphAsset asset;
                (void)asset.parseXML( asText( pData, size ) ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runDialogue( const uint8* pData, size_t size )
            {
                DialogueGraphAsset asset;
                (void)asset.parseXML( asText( pData, size ) ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runSequence( const uint8* pData, size_t size )
            {
                SequenceAsset asset;
                (void)asset.parseXML( asText( pData, size ) ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
            }
            static void runStringTable( const uint8* pData, size_t size )
            {
                SourceStringTable table;
                (void)table.loadFromXMLText( asText( pData, size ), "fuzz.strings.xml" ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
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
            // 대상 — 받은 바이트(넷 · 온라인 · 상태 코덱). 앞 바이트를 고르는 값으로 쓰는 대상은 그 뜻을 대상마다 적는다
            // ---------------------------------------------------------------------------------------------------------
            static constexpr uint16 kServerPort        = 4000;    ///< 루프백 망 안의 주소(실제 포트가 아니다)
            static constexpr uint16 kPeerPort          = 5000;    ///< 퍼징 바이트를 보내는 쪽
            static constexpr uint64 kSaltSeed          = 0x5EED;  ///< 챌린지 값을 정해 둔다(같은 입력이면 같은 길)
            static constexpr int32  kMaxStreamBody     = 4096;    ///< 프레임 몸 상한을 작게 — 상한 넘침 길을 자주 지난다
            static constexpr int32  kMaxHTTPBody       = 65536;   ///< 몸 상한(Content-Length · chunked 의 넘침 길)
            static constexpr int32  kInputCapacity     = 64;      ///< 입력 창 링 크기
            static constexpr size_t kPacketLengthBytes = 2;       ///< 연결 대상의 패킷 길이 머리(리틀 엔디언)
            static constexpr size_t kTypeSelectorBytes = 2;       ///< 리플렉션 대상의 타입 고르개(리틀 엔디언)
            static constexpr size_t kTypeSeedCount     = 24;      ///< 리플렉션 대상의 씨앗 타입 수(등록 순서에서 고르게 뽑는다)
            static constexpr int64  kFuzzNowMs         = 1000000; ///< JWKS 를 받은 시각

            static NetHostConfig makeHostConfig()
            {
                NetHostConfig settings;
                settings._saltSeed  = kSaltSeed;
                settings._debugHold = NetDebugHold::Off;
                return settings;
            }

            /**
             * @brief 첫 바이트의 낮은 비트가 1 이면 핸드셰이크 머리(`NetProtocol::kHandshakeID`), 아니면 서버의 프로토콜 머리로 나머지를 싸서 서버 호스트에 보냅니다.
             * @details 체크섬은 하네스가 맞춘다 — 맞추지 않으면 변이가 모두 체크섬에서 끝나 그 뒤(종류 · 핸드셰이크 · 데이터 패킷)를 보지 못한다.
             */
            static void runNetHost( const uint8* pData, size_t size )
            {
                if ( pData == nullptr || size == 0 )
                    return;
                LoopbackNetwork    network;
                LoopbackTransport* pServerEndpoint = network.createEndpoint( kServerPort );
                LoopbackTransport* pPeerEndpoint   = network.createEndpoint( kPeerPort );
                NetHost            server;
                server.initialize( pServerEndpoint, makeHostConfig() );
                if ( server.listen() == false )
                    return;
                const uint32  headerID = ( pData[0] & 1u ) != 0 ? NetProtocol::kHandshakeID : server.getProtocolID();
                const int32   bodySize = static_cast<int32>( size - 1 );
                vector<uint8> listDatagram;
                LittleEndianUtil::appendUint32( listDatagram, headerID );
                LittleEndianUtil::appendUint32( listDatagram, NetHost::computePacketChecksum( headerID, pData + 1, bodySize ) );
                listDatagram.insert( listDatagram.end(), pData + 1, pData + size );
                (void)pPeerEndpoint->send( pServerEndpoint->getLocalAddress(), listDatagram.data(), static_cast<int32>( listDatagram.size() ) ); // 퍼즈 대상 — 거절도 정상이다
                server.update( 0.0 );
                server.update( 0.1 ); // 답(챌린지 · 거절)을 보내는 길까지
            }

            /** @brief 입력을 `[길이 2 바이트][패킷]` 의 나열로 읽어 한 연결에 차례로 먹이고(조각 재조립이 패킷을 건넌다) 받은 메시지를 모두 꺼냅니다. */
            static void runNetConnection( const uint8* pData, size_t size )
            {
                NetConnection connection;
                float64       time   = 0.0;
                size_t        offset = 0;
                while ( pData != nullptr && offset + kPacketLengthBytes <= size )
                {
                    const size_t declared   = static_cast<size_t>( pData[offset] ) | ( static_cast<size_t>( pData[offset + 1] ) << 8 );
                    const size_t packetSize = std::min( declared, size - offset - kPacketLengthBytes );
                    BitReader    reader( pData + offset + kPacketLengthBytes, static_cast<int32>( packetSize ) );
                    (void)connection.readPacket( time, reader ); // 퍼즈 대상 — 거절도 정상이다
                    offset += kPacketLengthBytes + packetSize;
                    time += 1.0 / 60.0;
                }
                vector<uint8> buffer;
                for ( uint8 channel = 0; channel < static_cast<uint8>( NetChannelType::Count ); ++channel )
                {
                    while ( connection.receiveMessage( static_cast<NetChannelType>( channel ), buffer ) )
                    {
                    }
                }
            }
            static void runStreamFrame( const uint8* pData, size_t size )
            {
                if ( pData == nullptr || size == 0 )
                    return;
                StreamFrameDecoder decoder( kMaxStreamBody );
                decoder.append( pData, static_cast<int32>( size ) );
                StreamFrameView frame;
                while ( decoder.next( frame ) == StreamFrameDecodeResult::Frame )
                {
                }
            }

            /** @brief 입력 창 형식 둘 — 롤백(고정 1 바이트)과 스탬프 · 덩어리(권위 서버의 `ControlIntent` 슬롯). */
            static NetInputFormat makeInputFormat( bool bFixed )
            {
                NetInputFormat format;
                format._fixedEntryBytes = bFixed ? 1 : 0;
                format._maxEntryBytes   = ControlIntent::kMaxSerializedBytes;
                format._bStamped        = bFixed ? SW_FALSE : SW_TRUE;
                return format;
            }
            /** @brief 첫 바이트의 비트 0 이 형식(1 = 고정), 비트 1 이 창 방식(1 = 가장 새 틱을 따라감)이고 나머지가 번들의 나열입니다. */
            static void runNetInputWindow( const uint8* pData, size_t size )
            {
                if ( pData == nullptr || size == 0 )
                    return;
                NetInputReceiveBuffer buffer;
                buffer.initialize( kInputCapacity, makeInputFormat( ( pData[0] & 1u ) != 0 ), ( pData[0] & 2u ) != 0 ? NetInputWindowMode::FollowNewest : NetInputWindowMode::Manual );
                BitReader      reader( pData + 1, static_cast<int32>( size - 1 ) );
                vector<uint32> listNewTick;
                while ( reader.getBitsRemaining() >= 8 && buffer.read( reader, &listNewTick ) )
                {
                }
            }
            /** @brief "HTTP/" 로 시작하면 응답, 아니면 요청으로 읽습니다. 반으로 나눠 넣어 증분 경계를 지납니다. */
            static void runHTTPMessage( const uint8* pData, size_t size )
            {
                if ( pData == nullptr || size == 0 )
                    return;
                HTTPMessageParser parser;
                parser.reset( StringUtil::startsWith( asText( pData, size ), "HTTP/" ) ? HTTPMessageKind::Response : HTTPMessageKind::Request, kMaxHTTPBody );
                const size_t half = size / 2;
                (void)parser.append( pData, half ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
                (void)parser.append( pData + half, size - half );
                (void)parser.finishOnClose();
            }
            /** @brief 한 디코더(동적 표를 이어 쓴다)에 블록 둘(앞 반 · 뒤 반)을 먹입니다. */
            static void runHpack( const uint8* pData, size_t size )
            {
                if ( pData == nullptr || size == 0 )
                    return;
                HpackDecoder        decoder;
                vector<HpackHeader> listHeader;
                string              error;
                const size_t        half = size / 2;
                (void)decoder.decode( pData, half, listHeader, error ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
                (void)decoder.decode( pData + half, size - half, listHeader, error );
            }
            static void runHTTP2Frame( const uint8* pData, size_t size )
            {
                if ( pData == nullptr || size == 0 )
                    return;
                HTTP2FrameHeader     header;
                vector<HTTP2Setting> listSetting;
                HTTP2GoAway          goAway;
                size_t               offset = 0;
                while ( HTTP2FrameUtil::readHeader( pData + offset, size - offset, header ) )
                {
                    offset += HTTP2FrameUtil::kHeaderSize;
                    const size_t bodySize = std::min( static_cast<size_t>( header._length ), size - offset );
                    if ( header._type == static_cast<uint8>( HTTP2FrameType::Settings ) )
                        (void)HTTP2FrameUtil::readSettings( pData + offset, bodySize, listSetting ); // 퍼즈 대상 — 실패도 정상 입력이다
                    else if ( header._type == static_cast<uint8>( HTTP2FrameType::GoAway ) )
                        (void)HTTP2FrameUtil::readGoAway( pData + offset, bodySize, goAway ); // 퍼즈 대상 — 실패도 정상 입력이다
                    offset += bodySize;
                }
            }
            static void runControlIntent( const uint8* pData, size_t size )
            {
                BitReader     reader( pData, static_cast<int32>( size ) );
                ControlIntent intent;
                while ( reader.getBitsRemaining() > 0 && intent.read( reader ) ) // 퍼즈 대상 — 거절도 정상이다(입력 창은 슬롯마다 하나씩 읽는다)
                {
                }
            }
            static void runFractureSnapshot( const uint8* pData, size_t size )
            {
                const vector<uint8>       bytes( pData, pData + ( pData != nullptr ? size : 0 ) );
                vector<uint8>             stateBytes;
                vector<FractureGroupPose> listPose;
                (void)FractureNetworkSync::readSnapshot( bytes, stateBytes, listPose ); // 퍼즈 대상 — 거절도 정상이다
            }
            static void runPairedAnimationSnapshot( const uint8* pData, size_t size )
            {
                const vector<uint8>            bytes( pData, pData + ( pData != nullptr ? size : 0 ) );
                PairedAnimationSessionSnapshot snapshot;
                (void)PairedAnimationSessionSnapshot::readBytes( bytes, snapshot ); // 퍼즈 대상 — 거절도 정상이다
            }
            static void runJSONWebToken( const uint8* pData, size_t size )
            {
                JwtUnverified token; // 본문 클레임은 서명을 확인한 JwtVerified 에서만 읽는다 — 퍼즈는 머리 해석까지 본다
                if ( token.parse( asText( pData, pData != nullptr ? size : 0 ) ) == false )
                    return;
                (void)token.getKeyID();
                (void)token.getAlgorithm();
            }
            static void runJwks( const uint8* pData, size_t size )
            {
                JwksKeyCache cache;
                (void)cache.replaceFromJwks( asText( pData, pData != nullptr ? size : 0 ), kFuzzNowMs ); // 퍼즈 대상 — 거절도 정상이다
                (void)cache.findKey( "k1" );
            }
            static void runStateHashReport( const uint8* pData, size_t size )
            {
                const string_view       text = asText( pData, pData != nullptr ? size : 0 );
                vector<StateHashSample> listSample;
                vector<SceneObjectHash> listObject;
                uint64                  tick{ 0 };
                (void)StateHashHistory::parseReport( text, listSample );                  // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
                (void)StateHashHistory::tryParseReportLine( text, listSample );           // 퍼즈 대상 — 실패도 정상 입력이다
                (void)SceneStateHash::parseObjectReport( text, listObject );              // 퍼즈 대상 — 실패도 정상 입력이다
                (void)SceneStateHash::parseTickedObjectReport( text, tick, listObject );  // 퍼즈 대상 — 실패도 정상 입력이다
                (void)SceneStateHash::tryParseObjectReportLine( text, tick, listObject ); // 퍼즈 대상 — 실패도 정상 입력이다
            }
            static void runPackPatch( const uint8* pData, size_t size )
            {
                const string_view text = asText( pData, pData != nullptr ? size : 0 );
                PackPatchManifest manifest;
                PackBuildRecord   record;
                (void)PackPatchUtil::parseBuildRecord( text, "fuzz.packbuild.xml", record ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
                if ( PackPatchUtil::parseManifest( text, "fuzz.packpatch.xml", manifest ) == false )
                    return;
                const vector<PackPatchManifest> listManifest{ manifest };
                vector<size_t>                  listOrder;
                string                          latestBuild;
                (void)PackPatchUtil::orderPatchChainToLatest( manifest._fromBuild, listManifest, listOrder, latestBuild );
            }

            /**
             * @brief 고정 배치 코덱이 적격이고 검증 함수 · 컴포넌트 생성 함수가 없는 값 타입을 이름 순으로 모읍니다(등록이 같으면 같은 순서).
             * @details 검증 함수는 초기화된 객체(카탈로그 · 정의)를 가정하므로 스크래치 인스턴스에 부르지 않는다. 모듈이 내려가면 주소가 바뀌므로 부를 때마다 모은다.
             *          코덱 확인은 레지스트리 잠금 밖에서 한다 — `findCodec` 이 중첩 타입을 찾으며 레지스트리를 다시 잠근다.
             */
            static vector<const TypeInfo*> collectValueTypes()
            {
                vector<const TypeInfo*> listCandidate;
                engine::getSubsystem<TypeRegistry>().forEachType( [&listCandidate]( const TypeInfo& type )
                {
                    const bool bPlainValueType = type.isPrimitive() == false && type._pValidate == nullptr && type._addComponent == nullptr;
                    if ( bPlainValueType )
                        listCandidate.push_back( &type );
                } );
                vector<const TypeInfo*> listType;
                string                  reason;
                for ( const TypeInfo* pType : listCandidate )
                {
                    if ( FixedLayout::findCodec( *pType, reason ) != nullptr )
                        listType.push_back( pType );
                }
                std::sort( listType.begin(), listType.end(), []( const TypeInfo* pLhs, const TypeInfo* pRhs )
                { return pLhs->_fullyQualifiedName.lexicalLess( pRhs->_fullyQualifiedName ); } );
                return listType;
            }
            /** @brief 앞 2 바이트(리틀 엔디언)로 값 타입 하나를 고릅니다. 바이트가 모자라거나 타입이 없으면 nullptr 입니다. */
            static const TypeInfo* pickValueType( const vector<const TypeInfo*>& listType, const uint8* pData, size_t size )
            {
                const bool bCannotPick = pData == nullptr || size < kTypeSelectorBytes || listType.empty();
                if ( bCannotPick )
                    return nullptr;
                const size_t selector = static_cast<size_t>( pData[0] ) | ( static_cast<size_t>( pData[1] ) << 8 );
                return listType[selector % listType.size()];
            }
            /** @brief 고른 타입의 생성 본문 읽기(`FixedCodec::_pRead`)에 나머지를 먹입니다. 스키마 해시 머리는 변이를 대부분 걸러 내므로 본문을 바로 먹인다(`NetMessageChannel` 의 길). */
            static void runFixedLayout( const uint8* pData, size_t size )
            {
                const vector<const TypeInfo*> listType = collectValueTypes();
                const TypeInfo*               pType    = pickValueType( listType, pData, size );
                string                        reason;
                const FixedCodec*             pCodec = pType != nullptr ? FixedLayout::findCodec( *pType, reason ) : nullptr;
                if ( pCodec == nullptr )
                    return;
                vector<uint8> listStorage;
                void*         pInstance = createScratchInstance( *pType, listStorage );
                if ( pInstance == nullptr )
                    return;
                BitReader reader( pData + kTypeSelectorBytes, static_cast<int32>( size - kTypeSelectorBytes ) );
                (void)pCodec->_pRead( reader, pInstance ); // 퍼즈 대상 — 거절도 정상이다
                destroyScratchInstance( pInstance, *pType );
            }
            /** @brief 고른 타입으로 자기 기술형 바이너리를 엄격 · 소프트 두 길로 읽습니다. */
            static void runBinarySerializer( const uint8* pData, size_t size )
            {
                const vector<const TypeInfo*> listType = collectValueTypes();
                const TypeInfo*               pType    = pickValueType( listType, pData, size );
                if ( pType == nullptr )
                    return;
                const uint8* pBody    = pData + kTypeSelectorBytes;
                const size_t bodySize = size - kTypeSelectorBytes;
                for ( int32 pass = 0; pass < 2; ++pass )
                {
                    vector<uint8> listStorage;
                    void*         pInstance = createScratchInstance( *pType, listStorage );
                    if ( pInstance == nullptr )
                        return;
                    if ( pass == 0 )
                    {
                        (void)BinarySerializer::deserialize( pInstance, *pType, pBody, bodySize ); // 퍼즈 대상 — 실패도 정상 입력이다, 크래시만 본다
                    }
                    else
                    {
                        vector<SchemaOrphanValue> listOrphan;
                        (void)BinarySerializer::deserializeSoft( pInstance, *pType, pBody, bodySize, &listOrphan ); // 퍼즈 대상 — 거절도 정상이다
                    }
                    destroyScratchInstance( pInstance, *pType );
                }
            }

            static constexpr uint16 kFuzzRequestMethod = 1; ///< 처리기를 단 메서드(나머지는 UnknownMethod 길)
            static constexpr int32  kRequestSetupSteps = 4; ///< 루프백 연결이 서기까지 돌리는 횟수
            static constexpr int32  kRequestFlushSteps = 2; ///< 응답을 보내 버리는 횟수
            static constexpr uint32 kSeedDeadlineMs    = 5000;

            /** @brief 서버 쪽 연결 하나를 기억하고, 받은 프레임을 요청 서버로 넘기고, 처리기로 빈 성공 응답을 줍니다. 클라이언트 쪽(서버 없음)은 아무것도 하지 않습니다. */
            class RequestFuzzListener final : public IStreamEndpointListener, public INetRequestHandler
            {
            public:
                explicit RequestFuzzListener( NetRequestServer* pServer )
                    : _pServer{ pServer }
                    , _serverHandle{}
                    , _bOpened{ false }
                {
                }

                void onEndpointOpened( StreamConnectionHandle handle, const NetAddress& remote, bool bAccepted ) override
                {
                    (void)remote;
                    if ( bAccepted == false || _pServer == nullptr )
                        return;
                    _serverHandle = handle;
                    _bOpened      = true;
                }
                void onEndpointFrame( StreamConnectionHandle handle, StreamFrameKind kind, const uint8* pBody, int32 bodySize ) override
                {
                    if ( _pServer != nullptr )
                        (void)_pServer->handleFrame( handle, kind, pBody, bodySize ); // 요청 · 취소가 아닌 프레임은 맡지 않는다 — 버린다
                }
                void onEndpointClosed( StreamConnectionHandle handle, StreamCloseReason reason ) override
                {
                    (void)reason;
                    if ( _pServer != nullptr )
                        _pServer->onConnectionClosed( handle );
                }
                void onNetRequest( NetRequestServer& server, const NetRequestContext& context ) override
                {
                    (void)server.respond( context._token, NetRequestStatus::Ok, nullptr, 0 ); // 이미 답한 요청이면 false — 퍼징에서는 정상이다
                }

                StreamConnectionHandle getServerHandle() const { return _serverHandle; }
                bool                   isOpened() const { return _bOpened; }

            private:
                NetRequestServer*      _pServer;
                StreamConnectionHandle _serverHandle;
                bool                   _bOpened;
            };

            /**
             * @brief 첫 바이트의 낮은 비트가 1 이면 취소, 아니면 요청 프레임으로 나머지를 서버 쪽 연결에 넣습니다(요청 머리 · 멱등 키 · 추적 id · 응답 길).
             * @details `OnlineServiceHost::handleHello` 는 이 요청 서버가 넘긴 몸에서 해시 비교 뒤 `readBlob` · `readBits` 둘을 부를 뿐이라 따로 두지 않는다
             *          (호스트를 세우려면 리스너 전송 · 서비스 설정이 들고 그 두 읽기는 `BitReader` 계약이 이미 막는다).
             */
            static void runNetRequest( const uint8* pData, size_t size )
            {
                if ( pData == nullptr || size == 0 )
                    return;
                NetRequestServer           requestServer;
                RequestFuzzListener        serverListener{ &requestServer };
                RequestFuzzListener        clientListener{ nullptr };
                ::test::StreamEndpointPair endpointPair{ serverListener, clientListener, StreamEndpointConfig{}, LoopbackStreamConditions{} };
                requestServer.initialize( &endpointPair._server, NetRequestServerConfig{} );
                if ( requestServer.registerMethod( kFuzzRequestMethod, &serverListener ) == false )
                    return;
                endpointPair.step( kRequestSetupSteps );
                if ( serverListener.isOpened() == false )
                    return;
                const StreamFrameKind kind = ( pData[0] & 1u ) != 0 ? StreamFrameKind::Cancel : StreamFrameKind::Request;
                (void)requestServer.handleFrame( serverListener.getServerHandle(), kind, pData + 1, static_cast<int32>( size - 1 ) ); // 퍼즈 대상 — 깨진 머리는 Malformed 응답이다
                endpointPair.step( kRequestFlushSteps );
                requestServer.shutdown();
            }

            // ---------------------------------------------------------------------------------------------------------
            // 씨앗
            // ---------------------------------------------------------------------------------------------------------
            static void seedXML( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".xml", 12, SeedPick::ByName, outListSeed );
                appendTextSeed( "<a x=\"1\"><b>t&amp;</b><!-- c --><![CDATA[d]]></a>", outListSeed );
            }
            static void seedJSON( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".json", 4, SeedPick::ByName, outListSeed );
                appendTextSeed( R"({"a":[1,2.5e3,-0,true,null,"é"],"b":{"c":"d"}})", outListSeed );
            }
            static void seedDDS( vector<vector<uint8>>& outListSeed ) { appendResourceSeeds( ".dds", 13, SeedPick::SmallestFirst, outListSeed ); }
            static void seedMesh( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".mesh", 3, SeedPick::SmallestFirst, outListSeed );
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

                // 야간 퍼저가 10 초 타임아웃을 낸 모양(실행 38806424034): 원본 620 MB · 압축 0 바이트를 말하는 헤더만 있는 스트림.
                // 해제 상한(decompressBound)이 할당 전에 끊는지를 퍼저가 늘 출발점으로 삼게 한다.
                CompressionHeader header{};
                header._codecType          = CompressionCodecType::RLE;
                header._uncompressedSize   = 620757059;
                header._compressedSize     = 0;
                const uint8* const pHeader = reinterpret_cast<const uint8*>( &header );
                outListSeed.emplace_back( pHeader, pHeader + sizeof( header ) );
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
                    if ( sw::test::ResourcePakTestUtil::createPackFile( path, 0, compression, listFile ) && FileUtil::readFile( path, bytes ) )
                        outListSeed.push_back( std::move( bytes ) );
                }
            }
            static void seedScene( vector<vector<uint8>>& outListSeed ) { appendResourceSeeds( ".scene.xml", 4, SeedPick::ByName, outListSeed ); }
            static void seedSceneBinary( vector<vector<uint8>>& outListSeed )
            {
                vector<vector<uint8>> listXML;
                appendResourceSeeds( ".scene.xml", 2, SeedPick::ByName, listXML );
                for ( const vector<uint8>& xml : listXML )
                {
                    const string  xmlPath = makeScratchPath( "seed.scene.xml" );
                    const string  binPath = makeScratchPath( "seed.scene.bin" );
                    SceneDocument document;
                    vector<uint8> bytes;
                    const bool    bSeedBuilt = FileUtil::writeFile( xmlPath, xml.data(), xml.size() ) && document.loadXML( ::test::makeTempResourceID( xmlPath ) ) && document.saveBinary( binPath ) &&
                                            FileUtil::readFile( binPath, bytes );
                    if ( bSeedBuilt )
                        outListSeed.push_back( std::move( bytes ) );
                }
            }
            static void seedGameObject( vector<vector<uint8>>& outListSeed )
            {
                vector<vector<uint8>> listFile;
                appendResourceSeeds( ".scene.xml", 3, SeedPick::ByName, listFile );
                appendResourceSeeds( ".prefab.xml", 6, SeedPick::ByName, listFile );
                for ( const vector<uint8>& file : listFile )
                {
                    const string_view text  = asText( file.data(), file.size() );
                    const size_t      start = text.find( "<GameObject" );
                    const size_t      end   = text.find( "</GameObject>", start == string_view::npos ? 0 : start );
                    if ( start != string_view::npos && end != string_view::npos )
                        appendTextSeed( text.substr( start, end + 13 - start ), outListSeed );
                }
            }
            static void seedMaterial( vector<vector<uint8>>& outListSeed ) { appendResourceSeeds( ".material", 6, SeedPick::ByName, outListSeed ); }
            static void seedSockets( vector<vector<uint8>>& outListSeed )
            {
                appendTextSeed( "<SocketSet>\n  <VirtualBone name=\"AimRef\" from=\"hand_r\" to=\"hand_l\" weight=\"0.5\"/>\n"
                                "  <Socket name=\"Muzzle\" parent=\"barrel\" kind=\"Attach\" translation=\"0 0 0.42\" rotation=\"0 90 0\" scale=\"1 1 1\"/>\n"
                                "  <Socket name=\"Belly\" parent=\"spine\" kind=\"HitboxCenter\" anchor=\"Surface\" fallback=\"A.B C\"/>\n</SocketSet>\n",
                                outListSeed );
            }
            static void seedBodyShape( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".bodyshape.xml", 2, SeedPick::ByName, outListSeed );
                appendTextSeed( "<BodyShapeSet>\n  <Axis name=\"Weight\" min=\"-1\" max=\"1\">\n"
                                "    <Positive morph=\"Heavy\"><Bone name=\"upperarm\" scale=\"1.2 1 1.2\"/></Positive>\n"
                                "    <Negative morph=\"Thin\"/>\n  </Axis>\n  <Axis name=\"Height\" min=\"-1\" max=\"2\">\n"
                                "    <Positive><Bone name=\"lowerarm\" offset=\"0 0.1 0\"/></Positive>\n  </Axis>\n</BodyShapeSet>\n",
                                outListSeed );
            }
            static void seedSurfaceChannels( vector<vector<uint8>>& outListSeed ) { appendResourceSeeds( ".surfacechannels.xml", 2, SeedPick::ByName, outListSeed ); }
            static void seedUserSettings( vector<vector<uint8>>& outListSeed ) { appendResourceSeeds( ".settings.xml", 2, SeedPick::ByName, outListSeed ); }
            static void seedSpriteClip( vector<vector<uint8>>& outListSeed ) { appendResourceSeeds( ".sprite.xml", 2, SeedPick::ByName, outListSeed ); }
            static void seedAnimationGraph( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".animgraph.xml", 2, SeedPick::ByName, outListSeed );
                appendTextSeed( R"(<AnimationGraphAsset><nodes><node id="1" name="Idle" /></nodes><links><link id="1" from="1" to="2" /></links></AnimationGraphAsset>)", outListSeed );
            }
            static void seedDialogue( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".dialogue.xml", 2, SeedPick::ByName, outListSeed );
                appendTextSeed( R"(<DialogueGraphAsset><_listNode><DialogueAssetNode _id="1" _type="Dialogue" _text="Hi" _speaker="A" /></_listNode><_listLink><DialogueAssetLink _id="1" _fromPin="102" _toPin="101" /></_listLink></DialogueGraphAsset>)", outListSeed );
            }
            static void seedSequence( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( SequenceAsset::kExtension, 2, SeedPick::ByName, outListSeed );
                appendTextSeed( R"(<SequenceAsset _frameMin="0" _frameMax="48"><_listItem><SequenceTrackItem _name="T" _start="12" _end="12" _kind="Event" /></_listItem></SequenceAsset>)", outListSeed );
            }
            static void seedStringTable( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( SourceStringTable::kExtension, 2, SeedPick::ByName, outListSeed );
                appendTextSeed( R"(<SourceStringTable culture="en"><entries><entry key="Menu.Start" source="Start" maxLength="12" /><entry key="Greeting" source="Hi {name}" /></entries></SourceStringTable>)", outListSeed );
            }
            static void seedTileMap( vector<vector<uint8>>& outListSeed )
            {
                appendResourceSeeds( ".tilemap.xml", 2, SeedPick::ByName, outListSeed );
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
                appendResourceSeeds( ".wav", 2, SeedPick::SmallestFirst, outListSeed );
                // 8 kHz 모노 16 비트 PCM 네 표본
                static constexpr uint8 kArrWav[] = { 'R', 'I', 'F', 'F', 44, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x40, 0x1F,
                                                     0, 0, 0x80, 0x3E, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 8, 0, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0 };
                outListSeed.emplace_back( kArrWav, kArrWav + sizeof( kArrWav ) );
            }
            static void seedOgg( vector<vector<uint8>>& outListSeed ) { appendResourceSeeds( ".ogg", 1, SeedPick::SmallestFirst, outListSeed ); }

            /** @brief 고르는 바이트 @p selector 뒤에 @p pBody 를 붙인 씨앗을 더합니다. */
            static void appendSelectedSeed( uint8 selector, const uint8* pBody, size_t bodySize, vector<vector<uint8>>& outListSeed )
            {
                vector<uint8> seedBytes{ selector };
                seedBytes.insert( seedBytes.end(), pBody, pBody + bodySize );
                outListSeed.push_back( std::move( seedBytes ) );
            }
            static void seedNetHost( vector<vector<uint8>>& outListSeed )
            {
                // 진짜 연결 요청 — 클라이언트 호스트가 보낸 데이터그램에서 머리(프로토콜 ID · 체크섬 8 바이트)를 떼고 머리 종류를 고르는 바이트로 바꾼다.
                static constexpr size_t kDatagramHeaderBytes = 8;
                LoopbackNetwork         network;
                LoopbackTransport*      pServerEndpoint = network.createEndpoint( kServerPort );
                LoopbackTransport*      pClientEndpoint = network.createEndpoint( kPeerPort );
                NetHost                 client;
                client.initialize( pClientEndpoint, makeHostConfig() );
                if ( client.connect( pServerEndpoint->getLocalAddress() ) )
                {
                    client.update( 0.0 );
                    network.deliverInFlight();
                    NetAddress    from;
                    vector<uint8> listDatagram;
                    while ( pServerEndpoint->receive( from, listDatagram ) )
                    {
                        if ( listDatagram.size() <= kDatagramHeaderBytes )
                            continue;
                        const bool bHandshake = LittleEndianUtil::readUint32( listDatagram.data() ) == NetProtocol::kHandshakeID;
                        appendSelectedSeed( static_cast<uint8>( bHandshake ? 1 : 0 ), listDatagram.data() + kDatagramHeaderBytes, listDatagram.size() - kDatagramHeaderBytes, outListSeed );
                    }
                }
                // 종류마다 값이 빈 제어 본문(종류 3 비트 + 64 비트 값 둘) — 핸드셰이크 머리로 싸는 종류는 요청과 거절이다.
                for ( uint8 type = 0; type < static_cast<uint8>( NetHost::PacketType::Count ); ++type )
                {
                    BitWriter writer;
                    writer.writeBits( type, 3 );
                    writer.writeUint32( 0 );
                    writer.writeUint32( 0 );
                    writer.writeUint32( 0 );
                    writer.writeUint32( 0 );
                    const NetHost::PacketType packetType = static_cast<NetHost::PacketType>( type );
                    const bool                bHandshake = packetType == NetHost::PacketType::ConnectRequest || packetType == NetHost::PacketType::Denied;
                    appendSelectedSeed( static_cast<uint8>( bHandshake ? 1 : 0 ), writer.getBytes().data(), writer.getBytes().size(), outListSeed );
                }
            }
            static void seedNetConnection( vector<vector<uint8>>& outListSeed )
            {
                // 큰 신뢰 메시지(조각 셋), 신뢰 순서 없음, 순서만, 비신뢰를 실은 패킷 여럿을 `[길이][패킷]` 으로 잇는다.
                static constexpr int32 kLargeMessageSize = NetConnection::kMaxSingleMessageSize * 2 + 100;
                static constexpr int32 kPacketCount      = 6;
                NetConnection          sender;
                vector<uint8>          listLarge( static_cast<size_t>( kLargeMessageSize ) );
                for ( size_t index = 0; index < listLarge.size(); ++index )
                {
                    listLarge[index] = static_cast<uint8>( index * 31u );
                }
                const uint8 arrSmall[] = { 7, 1, 2, 3 };
                const bool  bQueued    = sender.sendMessage( NetChannelType::ReliableOrdered, listLarge.data(), kLargeMessageSize ) &&
                                     sender.sendMessage( NetChannelType::ReliableUnordered, arrSmall, sizeof( arrSmall ) ) &&
                                     sender.sendMessage( NetChannelType::UnreliableSequenced, arrSmall, sizeof( arrSmall ) ) &&
                                     sender.sendMessage( NetChannelType::Unreliable, arrSmall, sizeof( arrSmall ) );
                if ( bQueued == false )
                    return;
                vector<uint8> listStream;
                for ( int32 packetIndex = 0; packetIndex < kPacketCount; ++packetIndex )
                {
                    BitWriter writer;
                    sender.writePacket( packetIndex / 60.0, writer, kNetMaxPacketSize );
                    const vector<uint8>& packet = writer.getBytes();
                    listStream.push_back( static_cast<uint8>( packet.size() ) );
                    listStream.push_back( static_cast<uint8>( packet.size() >> 8 ) );
                    listStream.insert( listStream.end(), packet.begin(), packet.end() );
                }
                outListSeed.push_back( std::move( listStream ) );
            }
            static void seedStreamFrame( vector<vector<uint8>>& outListSeed )
            {
                const uint8   arrBody[] = { 1, 2, 3, 4, 5 };
                vector<uint8> bytes;
                const bool    bWritten = StreamFrameEncoder::appendFrame( bytes, StreamFrameKind::Request, 0, arrBody, sizeof( arrBody ) ) &&
                                      StreamFrameEncoder::appendFrame( bytes, StreamFrameKind::Ping, 0, nullptr, 0 ) &&
                                      StreamFrameEncoder::appendFrame( bytes, StreamFrameKind::Cancel, StreamFrameFlag::kCompressed, arrBody, 2 );
                if ( bWritten )
                    outListSeed.push_back( std::move( bytes ) );
            }
            static void seedNetInputWindow( vector<vector<uint8>>& outListSeed )
            {
                static constexpr uint32 kTickCount = 6;
                for ( uint8 selector = 0; selector < 4; ++selector )
                {
                    const bool         bFixed = ( selector & 1u ) != 0;
                    NetInputSendWindow window;
                    window.initialize( kInputCapacity, makeInputFormat( bFixed ) );
                    const uint8 arrEntry[] = { 3, 9, 27 };
                    for ( uint32 tick = 0; tick < kTickCount; ++tick )
                    {
                        (void)window.push( tick, arrEntry, bFixed ? 1 : static_cast<int32>( sizeof( arrEntry ) ), tick * 7u ); // 씨앗 — 못 넣은 틱은 번들에서 빠질 뿐이다
                    }
                    BitWriter     writer;
                    NetSendBudget budget( NetConnection::kMaxSingleMessageSize );
                    (void)window.write( writer, budget ); // 실은 항목 수는 씨앗에 필요 없다
                    appendSelectedSeed( selector, writer.getBytes().data(), writer.getBytes().size(), outListSeed );
                }
            }
            static void seedHTTPMessage( vector<vector<uint8>>& outListSeed )
            {
                appendTextSeed( "GET /path?q=1 HTTP/1.1\r\nHost: example.com\r\nContent-Length: 5\r\n\r\nhello", outListSeed );
                appendTextSeed( "POST /x HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\nTrailer: t\r\n\r\n", outListSeed );
                appendTextSeed( "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nbody until close", outListSeed );
            }
            static void seedHpack( vector<vector<uint8>>& outListSeed )
            {
                // RFC 7541 C.3.1(리터럴 + 색인) · C.4.1(허프만) · 동적 표 크기 갱신(4096)
                static constexpr uint8 kArrLiteral[] = { 0x82, 0x86, 0x84, 0x41, 0x0f, 'w', 'w', 'w', '.', 'e', 'x', 'a', 'm', 'p', 'l', 'e', '.', 'c', 'o', 'm' };
                static constexpr uint8 kArrHuffman[] = { 0x82, 0x86, 0x84, 0x41, 0x8c, 0xf1, 0xe3, 0xc2, 0xe5, 0xf2, 0x3a, 0x6b, 0xa0, 0xab, 0x90, 0xf4, 0xff };
                static constexpr uint8 kArrResize[]  = { 0x3f, 0xe1, 0x1f, 0x82 };
                outListSeed.emplace_back( kArrLiteral, kArrLiteral + sizeof( kArrLiteral ) );
                outListSeed.emplace_back( kArrHuffman, kArrHuffman + sizeof( kArrHuffman ) );
                outListSeed.emplace_back( kArrResize, kArrResize + sizeof( kArrResize ) );
                const vector<HpackHeader> listHeader{
                    {":method",      "POST"},
                    {  ":path", "/v1/login"},
                    {"x-trace",    "abc123"}
                };
                vector<uint8> listBlock;
                if ( HpackEncoder::encode( listHeader, listBlock ) )
                    outListSeed.push_back( std::move( listBlock ) );
            }
            static void seedHTTP2Frame( vector<vector<uint8>>& outListSeed )
            {
                const vector<HTTP2Setting> listSetting{
                    {HTTP2FrameUtil::kSettingsMaxFrameSize, HTTP2FrameUtil::kDefaultMaxFrameSize},
                    {  HTTP2FrameUtil::kSettingsEnablePush,                                    0}
                };
                const uint8   arrOpaque[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
                const uint8   arrData[]    = { 'h', 'i' };
                vector<uint8> bytes;
                const bool    bWritten = HTTP2FrameUtil::writeSettings( listSetting, false, bytes ) && HTTP2FrameUtil::writePing( arrOpaque, false, bytes ) &&
                                      HTTP2FrameUtil::writeWindowUpdate( 0, 1024, bytes ) &&
                                      HTTP2FrameUtil::writeData( 1, arrData, sizeof( arrData ), true, HTTP2FrameUtil::kDefaultMaxFrameSize, bytes ) &&
                                      HTTP2FrameUtil::writeRstStream( 3, 8, bytes );
                if ( bWritten )
                    outListSeed.push_back( std::move( bytes ) );
                // GOAWAY 는 쓰는 함수가 없다 — 머리(길이 8, 종류 7, 스트림 0) + 마지막 스트림 · 오류 코드.
                static constexpr uint8 kArrGoAway[] = { 0, 0, 8, 7, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 2 };
                outListSeed.emplace_back( kArrGoAway, kArrGoAway + sizeof( kArrGoAway ) );
            }
            static void seedControlIntent( vector<vector<uint8>>& outListSeed )
            {
                ControlIntent intent;
                BitWriter     writer;
                const bool    bIdleWritten = intent.write( writer );
                intent._move               = float2{ 0.5f, -1.0f };
                intent.setButton( 3, true, true );
                intent.setTriggerLook( 0.25f, -0.1f );
                if ( bIdleWritten && intent.write( writer ) )
                    outListSeed.push_back( writer.releaseBytes() );
            }
            static void seedFractureSnapshot( vector<vector<uint8>>& outListSeed )
            {
                const vector<uint8> stateBytes{ 1, 2, 3, 4 };
                FractureGroupPose   live;
                live._groupID  = 3;
                live._position = float3{ 1.0f, 2.0f, 3.0f };
                FractureGroupPose gone;
                gone._groupID = 9;
                gone._bGone   = SW_TRUE;
                vector<uint8> bytes;
                FractureNetworkSync::writeSnapshot( stateBytes, { live, gone }, bytes );
                outListSeed.push_back( std::move( bytes ) );
            }
            static void seedPairedAnimationSnapshot( vector<vector<uint8>>& outListSeed )
            {
                PairedAnimationSessionSnapshot snapshot;
                snapshot._sessionID       = 77;
                snapshot._anchorTime      = 0.5f;
                snapshot._nextNotifyIndex = 1;
                snapshot._listRole.resize( 2 );
                snapshot._listRole[0]._stage           = PairedAnimationRoleStage::Playing;
                snapshot._listRole[0]._entryAnchorTime = 0.25f;
                vector<uint8> bytes;
                snapshot.writeBytes( bytes );
                outListSeed.push_back( std::move( bytes ) );
            }
            static void seedJSONWebToken( vector<vector<uint8>>& outListSeed )
            {
                // {"alg":"RS256","typ":"JWT","kid":"k1"}.{"sub":"1","aud":["a","b"],"exp":2000000000,"data":{"x":"y"}}.sig
                appendTextSeed( "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImsxIn0.eyJzdWIiOiIxIiwiYXVkIjpbImEiLCJiIl0sImV4cCI6MjAwMDAwMDAwMCwiZGF0YSI6eyJ4IjoieSJ9fQ.c2ln",
                                outListSeed );
                // {"alg":"ES256","kid":"k2","x5c":["AAEC"]}.{"iss":"i"}.sig2
                appendTextSeed( "eyJhbGciOiJFUzI1NiIsImtpZCI6ImsyIiwieDVjIjpbIkFBRUMiXX0.eyJpc3MiOiJpIn0.c2lnMg", outListSeed );
            }
            static void seedJwks( vector<vector<uint8>>& outListSeed )
            {
                appendTextSeed( R"({"keys":[{"kty":"RSA","kid":"k1","use":"sig","alg":"RS256","n":"0vx7agoebGcQSuuPiLJXZptN9nndrQmbXEps2aiAFbWhM78LhWx4cbbfAAtVT86zwu1RK7aPFFxuhDR1L6tSoc_BJECPebWKRXjBZCiFV4n3oknjhMstn64tZ_2W-5JsGY4Hc5n9yBXArwl93lqt7_RN5w6Cf0h4QyQ5v-65YGjQR0_FDW2QvzqY368QQMicAtaSqzs8KJZgnYb9c7d0zgdAZHzu6qMQvRL5hajrn1n91CbOpbISD08qNLyrdkt-bFTWhAI4vMQFh6WeZu0fM4lFd2NcRwr3XPksINHaQ-G_xBniIqbw0Ls1jF44-csFCur-kEgU8awapJzKnqDKgw","e":"AQAB"},)"
                                R"({"kty":"EC","crv":"P-256","kid":"k2","x":"f83OJ3D2xF1Bg8vub9tLe1gHMzV76e8Tus9uPHvRVEU","y":"x_FEzRu9m36HLN_tue659LNpXW6pCyStikYjKIWI5a0"},{"kty":"oct","k":"AA"}]})",
                                outListSeed );
            }
            static void seedStateHashReport( vector<vector<uint8>>& outListSeed )
            {
                const vector<StateHashSample> listSample{
                    {10, 0x0123456789ABCDEFull},
                    {11, 0xFEDCBA9876543210ull}
                };
                const vector<SceneObjectHash> listObject{
                    { "Player", 0x1111222233334444ull},
                    {"Crate 2", 0x5555666677778888ull}
                };
                string text;
                StateHashHistory::writeReport( listSample, text );
                appendTextSeed( text, outListSeed );
                if ( StateHashHistory::writeReportLine( listSample, text ) )
                    appendTextSeed( text, outListSeed );
                SceneStateHash::writeTickedObjectReport( 10, listObject, text );
                appendTextSeed( text, outListSeed );
                if ( SceneStateHash::writeObjectReportLine( 10, listObject, text ) )
                    appendTextSeed( text, outListSeed );
            }
            static void seedPackPatch( vector<vector<uint8>>& outListSeed )
            {
                PackPatchManifest manifest;
                manifest._fromBuild       = "100";
                manifest._toBuild         = "101";
                manifest._packFileName    = "patch_101.pack";
                manifest._listRemovedPath = { "old/a.txt", "old/b.xml" };
                appendTextSeed( PackPatchUtil::makeManifestText( manifest ), outListSeed );
                PackBuildRecord record;
                record._build     = "100";
                record._listEntry = {
                    {    "a.txt", 0x0123456789ABCDEFull},
                    {"dir/b.bin",               0x42ull}
                };
                appendTextSeed( PackPatchUtil::makeBuildRecordText( record ), outListSeed );
            }

            /** @brief 요청 머리(`NetRequest.h` 머리말의 형식): 요청 id, 메서드 16 비트, 시한, [멱등 키 128 비트], [추적 id 128 비트], 바이트 정렬, 몸. 취소는 요청 id 하나다. */
            static void seedNetRequest( vector<vector<uint8>>& outListSeed )
            {
                static constexpr int32 kKeyWords = 4; // 32 비트 넷 = 128 비트
                const uint8            arrBody[] = { 1, 2, 3 };
                for ( int32 variant = 0; variant < 3; ++variant )
                {
                    BitWriter writer;
                    writer.writeVarUint( 1 + static_cast<uint64>( variant ) );
                    writer.writeBits( variant == 2 ? kFuzzRequestMethod + 1u : kFuzzRequestMethod, 16 );
                    writer.writeVarUint( kSeedDeadlineMs );
                    const bool bExtras = variant == 1;
                    writer.writeBool( bExtras );
                    for ( int32 word = 0; bExtras && word < kKeyWords; ++word )
                    {
                        writer.writeUint32( 0x11111111u * static_cast<uint32>( word + 1 ) );
                    }
                    writer.writeBool( bExtras );
                    for ( int32 word = 0; bExtras && word < kKeyWords; ++word )
                    {
                        writer.writeUint32( 0x0A0B0C0Du + static_cast<uint32>( word ) );
                    }
                    writer.alignToByte();
                    writer.writeBytes( arrBody, sizeof( arrBody ) );
                    appendSelectedSeed( 0, writer.getBytes().data(), writer.getBytes().size(), outListSeed );
                }
                BitWriter cancel;
                cancel.writeVarUint( 1 );
                appendSelectedSeed( 1, cancel.getBytes().data(), cancel.getBytes().size(), outListSeed );
            }

            /** @brief 값 타입을 등록 순서에서 고르게 `kTypeSeedCount` 개 뽑아 `[고르개 2 바이트][@p pfnWrite 가 쓴 바이트]` 씨앗을 만듭니다. */
            template <typename WriteFunc>
            static void appendTypeSeeds( WriteFunc&& pfnWrite, vector<vector<uint8>>& outListSeed )
            {
                const vector<const TypeInfo*> listType  = collectValueTypes();
                const size_t                  seedCount = std::min( kTypeSeedCount, listType.size() );
                for ( size_t seedIndex = 0; seedIndex < seedCount; ++seedIndex )
                {
                    const size_t    typeIndex = seedIndex * listType.size() / seedCount;
                    const TypeInfo& type      = *listType[typeIndex];
                    vector<uint8>   listStorage;
                    void*           pInstance = createScratchInstance( type, listStorage );
                    if ( pInstance == nullptr )
                        continue;
                    vector<uint8> listBody;
                    const bool    bWritten = pfnWrite( type, pInstance, listBody );
                    destroyScratchInstance( pInstance, type );
                    if ( bWritten == false )
                        continue;
                    vector<uint8> seedBytes{ static_cast<uint8>( typeIndex ), static_cast<uint8>( typeIndex >> 8 ) };
                    seedBytes.insert( seedBytes.end(), listBody.begin(), listBody.end() );
                    outListSeed.push_back( std::move( seedBytes ) );
                }
            }
            static void seedFixedLayout( vector<vector<uint8>>& outListSeed )
            {
                appendTypeSeeds(
                    []( const TypeInfo& type, const void* pInstance, vector<uint8>& outListBody )
                {
                    string            reason;
                    const FixedCodec* pCodec = FixedLayout::findCodec( type, reason );
                    BitWriter         writer;
                    if ( pCodec == nullptr || pCodec->_pWrite( writer, pInstance ) == false )
                        return false;
                    outListBody = writer.releaseBytes();
                    return true;
                },
                    outListSeed );
            }
            static void seedBinarySerializer( vector<vector<uint8>>& outListSeed )
            {
                appendTypeSeeds( []( const TypeInfo& type, const void* pInstance, vector<uint8>& outListBody )
                { return BinarySerializer::serialize( pInstance, type, outListBody ); }, outListSeed );
            }

            static constexpr const utf8* kRegistrarListName = "loader fuzz target";

            /** @brief 키트가 올린 대상의 목록입니다(`LoaderFuzzTargetRegistrar`). 정적 초기화 순서와 상관없이 처음 쓸 때 만들어지도록 함수 안의 정적 객체로 둡니다. */
            static ServiceFactoryList& getRegistrarList()
            {
                static ServiceFactoryList s_list{};
                return s_list;
            }
            /** @brief 기본 표 뒤에 키트가 올린 대상을 이름 순으로 붙인 표입니다. 처음 부를 때(정적 초기화가 끝난 뒤) 한 번 만든다. 기본 표를 쓰므로 표 뒤에 정의한다. */
            static vector<LoaderFuzzTarget> buildTargets();
        };

        constexpr LoaderFuzzTarget kArrLoaderFuzzTarget[] = {
            {                    "XML",                     &LoaderFuzzTargetsInternal::runXML,                     &LoaderFuzzTargetsInternal::seedXML,  true},
            {                   "JSON",                    &LoaderFuzzTargetsInternal::runJSON,                    &LoaderFuzzTargetsInternal::seedJSON,  true},
            {                    "DDS",                     &LoaderFuzzTargetsInternal::runDDS,                     &LoaderFuzzTargetsInternal::seedDDS, false},
            {                   "Mesh",                    &LoaderFuzzTargetsInternal::runMesh,                    &LoaderFuzzTargetsInternal::seedMesh, false},
            {            "Compression",             &LoaderFuzzTargetsInternal::runCompression,             &LoaderFuzzTargetsInternal::seedCompression, false},
            {                   "Pack",                    &LoaderFuzzTargetsInternal::runPack,                    &LoaderFuzzTargetsInternal::seedPack, false},
            {               "SceneXML",                &LoaderFuzzTargetsInternal::runSceneXML,                   &LoaderFuzzTargetsInternal::seedScene,  true},
            {            "SceneBinary",             &LoaderFuzzTargetsInternal::runSceneBinary,             &LoaderFuzzTargetsInternal::seedSceneBinary, false},
            {          "GameObjectXML",           &LoaderFuzzTargetsInternal::runGameObjectXML,              &LoaderFuzzTargetsInternal::seedGameObject,  true},
            {               "Material",                &LoaderFuzzTargetsInternal::runMaterial,                &LoaderFuzzTargetsInternal::seedMaterial,  true},
            {                "Sockets",                 &LoaderFuzzTargetsInternal::runSockets,                 &LoaderFuzzTargetsInternal::seedSockets,  true},
            {              "BodyShape",               &LoaderFuzzTargetsInternal::runBodyShape,               &LoaderFuzzTargetsInternal::seedBodyShape,  true},
            {        "SurfaceChannels",         &LoaderFuzzTargetsInternal::runSurfaceChannels,         &LoaderFuzzTargetsInternal::seedSurfaceChannels,  true},
            {     "UserSettingsSchema",      &LoaderFuzzTargetsInternal::runUserSettingsSchema,            &LoaderFuzzTargetsInternal::seedUserSettings,  true},
            {             "SpriteClip",              &LoaderFuzzTargetsInternal::runSpriteClip,              &LoaderFuzzTargetsInternal::seedSpriteClip,  true},
            {         "AnimationGraph",          &LoaderFuzzTargetsInternal::runAnimationGraph,          &LoaderFuzzTargetsInternal::seedAnimationGraph,  true},
            {               "Dialogue",                &LoaderFuzzTargetsInternal::runDialogue,                &LoaderFuzzTargetsInternal::seedDialogue,  true},
            {               "Sequence",                &LoaderFuzzTargetsInternal::runSequence,                &LoaderFuzzTargetsInternal::seedSequence,  true},
            {            "StringTable",             &LoaderFuzzTargetsInternal::runStringTable,             &LoaderFuzzTargetsInternal::seedStringTable,  true},
            {                "TileMap",                 &LoaderFuzzTargetsInternal::runTileMap,                 &LoaderFuzzTargetsInternal::seedTileMap,  true},
            {             "StringPool",              &LoaderFuzzTargetsInternal::runStringPool,              &LoaderFuzzTargetsInternal::seedStringPool, false},
            {                    "Wav",                     &LoaderFuzzTargetsInternal::runWav,                     &LoaderFuzzTargetsInternal::seedWav, false},
            {                    "Ogg",                     &LoaderFuzzTargetsInternal::runOgg,                     &LoaderFuzzTargetsInternal::seedOgg, false},
            {                "NetHost",                 &LoaderFuzzTargetsInternal::runNetHost,                 &LoaderFuzzTargetsInternal::seedNetHost, false},
            {          "NetConnection",           &LoaderFuzzTargetsInternal::runNetConnection,           &LoaderFuzzTargetsInternal::seedNetConnection, false},
            {            "StreamFrame",             &LoaderFuzzTargetsInternal::runStreamFrame,             &LoaderFuzzTargetsInternal::seedStreamFrame, false},
            {         "NetInputWindow",          &LoaderFuzzTargetsInternal::runNetInputWindow,          &LoaderFuzzTargetsInternal::seedNetInputWindow, false},
            {            "HTTPMessage",             &LoaderFuzzTargetsInternal::runHTTPMessage,             &LoaderFuzzTargetsInternal::seedHTTPMessage,  true},
            {                  "Hpack",                   &LoaderFuzzTargetsInternal::runHpack,                   &LoaderFuzzTargetsInternal::seedHpack, false},
            {             "HTTP2Frame",              &LoaderFuzzTargetsInternal::runHTTP2Frame,              &LoaderFuzzTargetsInternal::seedHTTP2Frame, false},
            {          "ControlIntent",           &LoaderFuzzTargetsInternal::runControlIntent,           &LoaderFuzzTargetsInternal::seedControlIntent, false},
            {       "FractureSnapshot",        &LoaderFuzzTargetsInternal::runFractureSnapshot,        &LoaderFuzzTargetsInternal::seedFractureSnapshot, false},
            {"PairedAnimationSnapshot", &LoaderFuzzTargetsInternal::runPairedAnimationSnapshot, &LoaderFuzzTargetsInternal::seedPairedAnimationSnapshot, false},
            {           "JSONWebToken",            &LoaderFuzzTargetsInternal::runJSONWebToken,            &LoaderFuzzTargetsInternal::seedJSONWebToken,  true},
            {                   "Jwks",                    &LoaderFuzzTargetsInternal::runJwks,                    &LoaderFuzzTargetsInternal::seedJwks,  true},
            {        "StateHashReport",         &LoaderFuzzTargetsInternal::runStateHashReport,         &LoaderFuzzTargetsInternal::seedStateHashReport,  true},
            {              "PackPatch",               &LoaderFuzzTargetsInternal::runPackPatch,               &LoaderFuzzTargetsInternal::seedPackPatch,  true},
            {            "FixedLayout",             &LoaderFuzzTargetsInternal::runFixedLayout,             &LoaderFuzzTargetsInternal::seedFixedLayout, false},
            {       "BinarySerializer",        &LoaderFuzzTargetsInternal::runBinarySerializer,        &LoaderFuzzTargetsInternal::seedBinarySerializer, false},
            {             "NetRequest",              &LoaderFuzzTargetsInternal::runNetRequest,              &LoaderFuzzTargetsInternal::seedNetRequest, false},
        };

        vector<LoaderFuzzTarget> LoaderFuzzTargetsInternal::buildTargets()
        {
            vector<LoaderFuzzTarget>           listTarget( std::begin( kArrLoaderFuzzTarget ), std::end( kArrLoaderFuzzTarget ) );
            vector<LoaderFuzzTargetRegistrar*> listRegistrar;
            getRegistrarList().collectAs( listRegistrar ); // 순서 값이 같으므로 이름 순이다
            for ( const LoaderFuzzTargetRegistrar* pRegistrar : listRegistrar )
            {
                listTarget.push_back( pRegistrar->_target );
            }
            return listTarget;
        }
    } // namespace

    LoaderFuzzTargetRegistrar::LoaderFuzzTargetRegistrar( const LoaderFuzzTarget& target )
        : _target{ target }
    {
        _pName = target._pName;
        // 같은 이름이면 목록이 오류 로그와 함께 거절한다 — 표에 빠진 대상은 `--test_list` 에서 보인다.
        (void)LoaderFuzzTargetsInternal::getRegistrarList().add( this, LoaderFuzzTargetsInternal::kRegistrarListName );
    }

    sw::vector_reference<const LoaderFuzzTarget> getLoaderFuzzTargets()
    {
        static const vector<LoaderFuzzTarget> s_listTarget = LoaderFuzzTargetsInternal::buildTargets();
        return sw::vector_reference<const LoaderFuzzTarget>( s_listTarget.data(), s_listTarget.size() );
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
