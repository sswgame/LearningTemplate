/**
 * @file TestSerializationBench.cpp
 * @brief 직렬화 벤치 — 가장 큰 씬(`game/abilityarena/maps/arena.scene.xml`, 282 KB)의 로드와 그 오브젝트 상태의 세 형식 쓰기 · 읽기.
 * @details 숫자를 찍기만 하고 판정하지 않는다(`TaskManagerBenchTest` 와 같은 규칙: Release 로 읽고, 이전/이후 바이너리를 같은 시각에 번갈아 잰다).
 *          EngineTest 에는 게임 모듈이 없어 게임 컴포넌트는 원문을 맡는 길(`MissingComponent`)로 지난다 — 이전/이후가 같은 조건이다.
 */
#include "pch.h"

#include "Core/Container/vector.h"
#include "Core/Time/MonotonicClock.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Object/GameObject/ObjectStateSerializer.h"
#include "Engine/Resource/ResourceUtil.h"
#include "Engine/Scene/Scene.h"
#include "Engine/Scene/SceneDocument.h"

#include "TestFramework/TestBench.h"
#include "TestFramework/TestFramework.h"

namespace
{
    struct SerializationBenchInternal
    {
#if defined( SW_DEBUG )
        static constexpr uint32 kRoundCount = 2;
#else
        static constexpr uint32 kRoundCount = 10;
#endif
        static constexpr const utf8* kSceneId = "game/abilityarena/maps/arena.scene.xml";
    };
} // namespace

/**
 * @brief [SerializationBenchTest] 큰 씬 로드(XML 문서 + 인스턴스화)와 오브젝트 상태의 XML · JSON · 바이너리 쓰기 · 읽기 시간
 */
SW_TEST_CASE( SerializationBenchTest, LargeSceneLoadAndObjectStateRoundTrip )
{
    SW_ASSERT_TRUE( sw::ResourceUtil::initialize() );
    // 게임 모듈 타입의 "모르는 타입" 경고를 재는 동안만 막는다 — 결과 줄([Bench])은 남아야 한다.
    sw::unique_ptr<test::ScopedLogSuppressor> pSuppressor = sw::make_unique<test::ScopedLogSuppressor>();

    sw::vector<int64> listLoadMicro;
    for ( uint32 round = 0; round < SerializationBenchInternal::kRoundCount; ++round )
    {
        const sw::Stopwatch stopwatch;
        sw::SceneDocument   doc;
        SW_ASSERT_TRUE( doc.loadXml( SerializationBenchInternal::kSceneId ) );
        sw::Scene scene{ "SerializationBenchScene" };
        SW_ASSERT_TRUE( scene.instantiate( doc ) );
        listLoadMicro.push_back( stopwatch.getElapsedMicroseconds() );
    }

    sw::SceneDocument doc;
    SW_ASSERT_TRUE( doc.loadXml( SerializationBenchInternal::kSceneId ) );
    sw::Scene scene{ "SerializationBenchScene" };
    SW_ASSERT_TRUE( scene.instantiate( doc ) );
    sw::vector<sw::GameObject*> listObject;
    scene.getObjectManager()->getAllGameObjects( listObject );
    SW_ASSERT_TRUE( listObject.size() >= 100u );

    sw::GameObjectManager scratchManager;
    sw::GameObject*       pScratch = scratchManager.createGameObject( sw::hashed_string( "SerializationBenchScratch" ) );
    SW_ASSERT_NOT_NULL( pScratch );

    sw::vector<sw::string>        listXml( listObject.size() );
    sw::vector<sw::string>        listJson( listObject.size() );
    sw::vector<sw::vector<uint8>> listBinary( listObject.size() );
    sw::vector<int64>             listXmlWriteMicro;
    sw::vector<int64>             listXmlReadMicro;
    sw::vector<int64>             listJsonWriteMicro;
    sw::vector<int64>             listJsonReadMicro;
    sw::vector<int64>             listBinaryWriteMicro;
    sw::vector<int64>             listBinaryReadMicro;
    for ( uint32 round = 0; round < SerializationBenchInternal::kRoundCount; ++round )
    {
        {
            const sw::Stopwatch stopwatch;
            for ( size_t objectIndex = 0; objectIndex < listObject.size(); ++objectIndex )
                listXml[objectIndex] = sw::ObjectStateSerializer::saveToXmlString( listObject[objectIndex] );
            listXmlWriteMicro.push_back( stopwatch.getElapsedMicroseconds() );
        }
        {
            const sw::Stopwatch stopwatch;
            for ( const sw::string& xml : listXml )
                (void)sw::ObjectStateSerializer::loadFromXmlString( pScratch, xml ); // 성공 여부는 SerializationRoundTripTest 가 본다 — 여기서는 시간만
            listXmlReadMicro.push_back( stopwatch.getElapsedMicroseconds() );
        }
        {
            const sw::Stopwatch stopwatch;
            for ( size_t objectIndex = 0; objectIndex < listObject.size(); ++objectIndex )
                listJson[objectIndex] = sw::ObjectStateSerializer::saveToJsonString( listObject[objectIndex] );
            listJsonWriteMicro.push_back( stopwatch.getElapsedMicroseconds() );
        }
        {
            const sw::Stopwatch stopwatch;
            for ( const sw::string& json : listJson )
                (void)sw::ObjectStateSerializer::loadFromJsonString( pScratch, json ); // 시간만 잰다
            listJsonReadMicro.push_back( stopwatch.getElapsedMicroseconds() );
        }
        {
            const sw::Stopwatch stopwatch;
            for ( size_t objectIndex = 0; objectIndex < listObject.size(); ++objectIndex )
            {
                listBinary[objectIndex].clear();
                (void)sw::ObjectStateSerializer::saveToBinaryBuffer( listObject[objectIndex], listBinary[objectIndex] ); // 시간만 잰다
            }
            listBinaryWriteMicro.push_back( stopwatch.getElapsedMicroseconds() );
        }
        {
            const sw::Stopwatch stopwatch;
            for ( const sw::vector<uint8>& bytes : listBinary )
                (void)sw::ObjectStateSerializer::loadFromBinaryBuffer( pScratch, bytes.data(), bytes.size() ); // 시간만 잰다
            listBinaryReadMicro.push_back( stopwatch.getElapsedMicroseconds() );
        }
    }
    pSuppressor.reset();
    test::logBenchSamples( "arena scene load (xml document + instantiate)", listLoadMicro );
    test::logBenchSamples( "object state write xml (all objects)", listXmlWriteMicro );
    test::logBenchSamples( "object state read xml (all objects)", listXmlReadMicro );
    test::logBenchSamples( "object state write json (all objects)", listJsonWriteMicro );
    test::logBenchSamples( "object state read json (all objects)", listJsonReadMicro );
    test::logBenchSamples( "object state write binary (all objects)", listBinaryWriteMicro );
    test::logBenchSamples( "object state read binary (all objects)", listBinaryReadMicro );
}
