#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/Mesh/Mesh.h"
#include "Engine/Graphics/RHI/IRHIDevice.h"
#include "Engine/Graphics/RHI/RHICapabilities.h"
#include "Engine/Object/Component/3D/DirectionalLightComponent.h"
#include "Engine/Object/Component/3D/MeshComponent.h"
#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Object/GameObject/GameObjectManager.h"
#include "Engine/Renderer/Frame/FrameRenderer.h"
#include "Engine/Scene/Scene.h"

#include "EngineTest/GameFramework/MeshModifiers/MeshModifierTestUtil.h"
#include "EngineTest/RHITestDevice.h"
#include "EngineTest/RHITestImage.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierGPUProgram.h"

#include "TestFramework/TestFramework.h"

// ------------------------------------------------------------------------------
// MeshModifierGPUTest.ProgramMatchesCPU — 모디파이어 스택의 GPU 실행(meshmorph.hlsl + meshmodifier.hlsli)이 CPU 기준과 같은 그림을 내는지 네 백엔드에서 본다.
// GPU 버퍼를 CPU 로 되읽는 API 가 없어 그림으로 비교한다: CPU 가 변형한 정점을 올린 정적 메시 vs 레스트 메시 + 변형 프로그램(모프 풀).
// ------------------------------------------------------------------------------

using namespace sw;
using namespace sw::editor;

SW_TEST_REQUIRES_HOST( MeshModifierGPUTest, "runs FrameRenderer on a real GPU device and reads pixels back" );

namespace
{
    struct MeshModifierGPUTestInternal
    {
        /** @brief 매개변수 하나의 값입니다. Vector3 는 셋을 다 쓰고 스칼라는 `_value._x` 만 씁니다. */
        struct ParamValue
        {
            const utf8* _pName{ nullptr };
            float3      _value{ 0.0f, 0.0f, 0.0f };
        };

        /** @brief 테스트 사례 하나 — 종류 id 와 눈에 띄지만 화면 밖으로 나가지 않는 매개변수입니다(상자 2 x 4 x 2, 모디파이어 공간 원점이 상자 가운데). */
        struct Case
        {
            const utf8* _pTypeID{ nullptr };
            ParamValue  _arrParam[5];
        };

        static constexpr float32 kPreviewTime = 0.7f; ///< 시간 매개변수가 있는 모디파이어가 0 이 아닌 위상을 갖게 한다
        static constexpr uint32  kSubdivision = 8;    ///< 6 면 · 8² 칸 · 6 정점 = 2304 정점

        static float3 v( float32 x ) { return float3{ x, 0.0f, 0.0f }; }

        /** @brief GPU 표의 모든 종류를 하나씩 덮습니다(`MeshModifierGPUProgramTest.KindTableMatchesSupportsGPU` 가 표와 등록이 같음을 본다). */
        static const Case* getCases( uint32& outCount )
        {
            static const Case s_arrCase[] = {
                {          "bend",                                                                                          { { "Angle", v( 60.0f ) }, { "Direction", v( 20.0f ) } }},
                {         "twist",                                                                                                                       { { "Angle", v( 90.0f ) } }},
                {         "taper",                                                                                               { { "Amount", v( 0.8f ) }, { "Curve", v( 1.5f ) } }},
                {       "stretch",                                                                                                                       { { "Amount", v( 0.4f ) } }},
                {          "wave",                                    { { "Amplitude", v( 0.25f ) }, { "Wavelength", v( 1.5f ) }, { "Phase", v( 30.0f ) }, { "Speed", v( 40.0f ) } }},
                {        "ripple",                                     { { "Amplitude", v( 0.25f ) }, { "Wavelength", v( 1.2f ) }, { "Decay", v( 0.3f ) }, { "Speed", v( 30.0f ) } }},
                {         "noise",                                         { { "Amplitude", v( 0.3f ) }, { "Frequency", v( 1.3f ) }, { "Seed", v( 7.0f ) }, { "Speed", v( 0.5f ) } }},
                {          "melt",                                                                                              { { "Amount", v( 0.4f ) }, { "Spread", v( 0.6f ) } }},
                {          "push",                                                                                                                       { { "Amount", v( 0.2f ) } }},
                // 반경이 상자 위 면(y = 2)까지 닿아야 보인다 - 옆면 점은 주축(Y)으로 면 안에서만 미끄러져 그림이 그대로다
                {          "poke",                                                                    { { "Amount", v( 0.8f ) }, { "Radius", v( 3.0f ) }, { "Falloff", v( 2.0f ) } }},
                {      "displace",                                                                  { { "Amount", v( 0.2f ) }, { "Frequency", v( 0.7f ) }, { "Phase", v( 20.0f ) } }},
                {        "bubble",                                                  { { "Strength", v( 0.9f ) }, { "Radius", v( 2.5f ) }, { "Center", float3{ 0.0f, 1.0f, 0.0f } } }},
                {         "bulge",                                                                                           { { "Amount", v( 0.6f ) }, { "Curvature", v( 2.0f ) } }},
                {    "cylindrify",                                                                                             { { "Percent", v( 0.7f ) }, { "Radius", v( 1.2f ) } }},
                {         "globe",                                                                                             { { "Percent", v( 0.6f ) }, { "Radius", v( 1.5f ) } }},
                {          "hump",                                                    { { "Height", v( 0.6f ) }, { "Radius", v( 0.8f ) }, { "Center", float3{ 0.3f, 0.0f, 0.2f } } }},
                {    "radialskew",                                                                                                                       { { "Angle", v( 15.0f ) } }},
                {          "skew",                                                            { { "Amount", v( 0.6f ) }, { "Direction", v( 30.0f ) }, { "Lower Limit", v( 0.2f ) } }},
                {      "spherify",                                                                                                                      { { "Percent", v( 0.7f ) } }},
                {       "squeeze",                                                                                                                       { { "Amount", v( 0.4f ) } }},
                {         "scale",                                                                        { { "Uniform", v( 0.8f ) }, { "Axis Scale", float3{ 1.2f, 0.9f, 1.1f } } }},
                {    "sinuscurve",                                  { { "Amplitude", v( 0.3f ) }, { "Frequency", v( 1.5f ) }, { "Phase", v( 20.0f ) }, { "Direction", v( 40.0f ) } }},
                {        "rolled",                                          { { "Radius", v( 0.4f ) }, { "Start", v( 0.5f ) }, { "Turns", v( 0.7f ) }, { "Direction", v( 10.0f ) } }},
                {     "simplemod",             { { "Offset", float3{ 0.1f, 0.0f, 0.0f } }, { "Linear", float3{ 0.1f, 0.05f, 0.0f } }, { "Quadratic", float3{ 0.0f, 0.0f, 0.08f } } }},
                {       "crumple",                                 { { "Amplitude", v( 0.15f ) }, { "Frequency", v( 2.5f ) }, { "Seed", v( 3.0f ) }, { "Normal Blend", v( 0.5f ) } }},
                { "dynamicripple",                                     { { "Amplitude", v( 0.3f ) }, { "Wavelength", v( 0.8f ) }, { "Speed", v( 3.0f ) }, { "Damping", v( 0.2f ) } }},
                {      "pageflip",                                                           { { "Progress", v( 0.4f ) }, { "Curl Radius", v( 0.4f ) }, { "Flip Axis", v( 0.0f ) } }},
                {      "treebend",                { { "Strength", v( 0.8f ) }, { "Wind Direction", float3{ 1.0f, 0.0f, 0.5f } }, { "Sway", v( 0.5f ) }, { "Frequency", v( 0.7f ) } }},
                {     "vertnoise",                                         { { "Amplitude", v( 0.2f ) }, { "Frequency", v( 1.5f ) }, { "Seed", v( 5.0f ) }, { "Speed", v( 0.3f ) } }},
                { "zstretchnoise",             { { "Amount", v( 0.3f ) }, { "Noise", v( 0.2f ) }, { "Frequency", v( 1.2f ) }, { "Seed", v( 9.0f ) }, { "Stretch Axis", v( 1.0f ) } }},
                {        "waving", { { "Amplitude", v( 0.4f ) }, { "Wavelength", v( 1.5f ) }, { "Speed", v( 0.5f ) }, { "Fixed Axis", v( 0.0f ) }, { "Fixed Position", v( 0.2f ) } }},
                {   "pivotadjust",                                                                                               { { "Pivot Offset", float3{ 0.3f, -0.2f, 0.1f } } }},
                {"displacelimits",                                                                                                { { "Mode", v( 2.0f ) }, { "Radius", v( 1.6f ) } }},
            };
            outCount = static_cast<uint32>( sizeof( s_arrCase ) / sizeof( s_arrCase[0] ) );
            return s_arrCase;
        }

        /** @brief 사례의 모디파이어를 스택 맨 아래에 더하고 매개변수를 넣습니다. 이름이 틀리면 false 입니다. */
        static bool addCase( MeshModifierStack& stack, const Case& testCase, const MeshModifierBounds& sourceBounds )
        {
            const int32 index = stack.addModifier( testCase._pTypeID );
            if ( index < 0 )
                return false;
            MeshModifier& modifier = *stack.findModifier( index );
            modifier.fitToBounds( sourceBounds );
            for ( const ParamValue& param : testCase._arrParam )
            {
                if ( param._pName == nullptr )
                    break;
                uint32 paramIndex = 0;
                if ( modifier.tryGetParamIndex( param._pName, paramIndex ) == false )
                    return false;
                MeshModifierParamValue value{};
                value._value = param._value;
                modifier.setParamValue( paramIndex, value );
            }
            modifier.onParamsChanged();
            return true;
        }

        static vector<RHIVertex> toVertices( const MeshModifierMesh& mesh )
        {
            vector<RHIVertex> listVertex( mesh.getVertexCount() );
            for ( size_t index = 0; index < listVertex.size(); ++index )
            {
                RHIVertex&    vertex   = listVertex[index];
                const float3& position = mesh._listPosition[index];
                const float3& normal   = mesh._listNormal[index];
                vertex._arrPosition[0] = position._x;
                vertex._arrPosition[1] = position._y;
                vertex._arrPosition[2] = position._z;
                vertex._arrNormal[0]   = normal._x;
                vertex._arrNormal[1]   = normal._y;
                vertex._arrNormal[2]   = normal._z;
                vertex._arrUv[0]       = mesh._listUV[index]._x * 0.125f;
                vertex._arrUv[1]       = mesh._listUV[index]._y * 0.125f;
                vertex._arrColor[0]    = 0.9f;
                vertex._arrColor[1]    = 0.6f;
                vertex._arrColor[2]    = 0.3f;
                vertex._arrColor[3]    = 1.0f;
            }
            return listVertex;
        }

        struct Snapshot
        {
            uint32             _drawnCount{ 0 };
            bool               _bOk{ false };
            test::RHITestImage _image;
        };

        /** @brief 두 그림에서 어느 채널이든 12 넘게 다른 픽셀 수입니다(스키닝 비교와 같은 지표). */
        static uint32 countDifferentPixels( const Snapshot& a, const Snapshot& b )
        {
            if ( a._image.getWidth() != b._image.getWidth() || a._image.getHeight() != b._image.getHeight() )
                return 0xFFFFFFFFu;
            uint32 count{ 0 };
            for ( uint32 y = 0; y < a._image.getHeight(); ++y )
            {
                for ( uint32 x = 0; x < a._image.getWidth(); ++x )
                {
                    const test::Rgba8 pixelA = a._image.getPixel( x, y );
                    const test::Rgba8 pixelB = b._image.getPixel( x, y );
                    const bool        bFarR  = pixelA._r > pixelB._r + 12 || pixelB._r > pixelA._r + 12;
                    const bool        bFarG  = pixelA._g > pixelB._g + 12 || pixelB._g > pixelA._g + 12;
                    const bool        bFarB  = pixelA._b > pixelB._b + 12 || pixelB._b > pixelA._b + 12;
                    const bool        bFar   = bFarR || bFarG || bFarB;
                    if ( bFar )
                        ++count;
                }
            }
            return count;
        }

        static Snapshot snapshot( FrameRenderer& renderer, IRHIDevice& device, Scene& scene )
        {
            Snapshot result{};
            // 풀 빌드 · 프로그램 업로드 · GPUScene 업로드가 한 프레임 늦으므로 몇 프레임 돌린 뒤 읽는다
            for ( uint32 frame = 0; frame < 4; ++frame )
            {
                device.beginFrame();
                if ( renderer.execute( &device, scene ) == false )
                    return result;
                device.endFrame( false, false );
                device.waitIdle( sw::RHIWaitIdleReason::Tool );
            }
            if ( result._image.readTransient( renderer, "SceneColor" ) == false )
                return result;
            for ( uint32 y = 0; y < result._image.getHeight(); ++y )
            {
                for ( uint32 x = 0; x < result._image.getWidth(); ++x )
                {
                    if ( test::RHITestImage::isDefaultClearBackground( result._image.getPixel( x, y ) ) == false )
                        ++result._drawnCount;
                }
            }
            result._bOk = result._drawnCount > 0;
            return result;
        }

        /** @brief 기본 방향광 하나를 씬에 더합니다. 두 씬이 같은 빛을 받아야 그림을 비교할 수 있습니다. */
        static bool addKeyLight( Scene& scene )
        {
            GameObject* pObject = scene.getObjectManager()->createGameObject( hashed_string( "KeyLight" ) );
            return pObject != nullptr && pObject->addComponent<DirectionalLightComponent>() != nullptr;
        }

        static MeshComponent* addMeshObject( Scene& scene, const utf8* pName )
        {
            GameObject* pObject = scene.getObjectManager()->createGameObject( hashed_string( pName ) );
            if ( pObject == nullptr )
                return nullptr;
            MeshComponent* pMesh = pObject->addComponent<MeshComponent>();
            if ( pMesh == nullptr )
                return nullptr;
            pMesh->setLocalPosition( float3{ 0.0f, 1.0f, 0.0f } );
            pMesh->setLocalScale( float3{ 0.35f, 0.35f, 0.35f } );
            return pMesh;
        }
    };
} // namespace

/**
 * @brief [MeshModifierGPUTest] 모디파이어 스택의 GPU 실행이 CPU 스택과 같은 그림을 낸다(GPU 표의 모든 종류 + 기즈모 회전 + 연쇄, 네 백엔드)
 * @details 사례마다 (1) CPU 가 변형한 정점의 정적 메시와 (2) 레스트 메시 + 변형 프로그램(모프 풀)을 따로 그려 달라진 픽셀이 2 % 안인지 본다.
 *          (3) 프로그램 그림이 레스트와 2 % 넘게 달라야 한다 — 같으면 프로그램이 정점에 닿지 않은 것이다(비교가 빈 변형끼리 맞는 일을 막는다).
 *          지면 그 종류의 셰이더 함수(meshmodifier.hlsli)가 CPU `deformPoint` 와 다르거나, 프로그램 기록(상자 · 행렬 · 매개변수 순서)이 어긋난 것이다.
 */
SW_TEST_CASE( MeshModifierGPUTest, ProgramMatchesCPU )
{
    using Internal                        = MeshModifierGPUTestInternal;
    const MeshModifierMesh   source       = test::MeshModifierTestUtil::makeBoxMesh( Internal::kSubdivision );
    const MeshModifierBounds sourceBounds = MeshModifierBounds::makeFromPoints( source._listPosition );
    const vector<RHIVertex>  listRest     = Internal::toVertices( source );

    // 사례 목록: 종류마다 하나 + 기즈모를 돌리고 옮긴 휘기 + 상자가 앞 결과를 따라가야 하는 연쇄
    uint32                    caseCount = 0;
    const Internal::Case*     pCase     = Internal::getCases( caseCount );
    vector<MeshModifierStack> listStack( caseCount + 2 );
    vector<string>            listLabel;
    for ( uint32 caseIndex = 0; caseIndex < caseCount; ++caseIndex )
    {
        SW_ASSERT_TRUE_MSG( Internal::addCase( listStack[caseIndex], pCase[caseIndex], sourceBounds ), pCase[caseIndex]._pTypeID );
        listLabel.push_back( pCase[caseIndex]._pTypeID );
    }
    {
        MeshModifierStack& rotated = listStack[caseCount];
        SW_ASSERT_TRUE( Internal::addCase( rotated, pCase[0], sourceBounds ) );
        rotated.findModifier( 0 )->getGizmo()._rotationDegree = float3{ 15.0f, 30.0f, -10.0f };
        rotated.findModifier( 0 )->getGizmo()._center         = float3{ 0.2f, -0.3f, 0.1f };
        rotated.findModifier( 0 )->getGizmo()._axisIndex      = 2;
        listLabel.push_back( "bend (rotated gizmo, Z axis)" );
    }
    {
        MeshModifierStack& chain = listStack[caseCount + 1];
        for ( const utf8* pID : { "twist", "bend", "taper", "noise", "spherify" } )
        {
            for ( uint32 caseIndex = 0; caseIndex < caseCount; ++caseIndex )
            {
                if ( string_view( pCase[caseIndex]._pTypeID ) == pID )
                    SW_ASSERT_TRUE( Internal::addCase( chain, pCase[caseIndex], sourceBounds ) );
            }
        }
        listLabel.push_back( "chain twist-bend-taper-noise-spherify" );
    }

    // CPU 기준 결과와 GPU 프로그램은 백엔드와 무관하다 - 한 번만 만든다
    vector<vector<RHIVertex>> listCPUVertex;
    vector<vector<float4>>    listProgram;
    for ( size_t stackIndex = 0; stackIndex < listStack.size(); ++stackIndex )
    {
        vector<MeshModifierBounds> listBounds;
        MeshModifierEvaluation     evaluation{};
        evaluation._time             = Internal::kPreviewTime;
        evaluation._pListInputBounds = &listBounds;
        MeshModifierMesh mesh        = source;
        listStack[stackIndex].apply( mesh, evaluation );
        listCPUVertex.push_back( Internal::toVertices( mesh ) );
        vector<float4> program;
        SW_ASSERT_TRUE_MSG( MeshModifierGPUProgram::make( listStack[stackIndex], evaluation, listBounds, false, program ), listLabel[stackIndex].c_str() );
        listProgram.push_back( std::move( program ) );
    }

    test::RHIBackendSweep sweep;
    for ( test::RHITestDevice& device : sweep )
    {
        const string backendLabel = string( "backend " ) + to_string( static_cast<uint32>( device.getBackend() ) );
        // 씬마다 렌더러를 따로 둔다 — 한 렌더러의 씬 빌더는 그리던 씬의 수집 캐시를 든다
        FrameRenderer gpuRenderer;
        FrameRenderer cpuRenderer;
        bool          bOk = gpuRenderer.initialize( device.get() ) && gpuRenderer.isReady() && cpuRenderer.initialize( device.get() ) && cpuRenderer.isReady();
        if ( bOk && device->getCapabilities()._bGPUMeshMorph == SW_FALSE )
            continue;

        Scene gpuScene( "MeshModifierGPUScene" );
        Scene cpuScene( "MeshModifierCPUScene" );
        bOk = bOk && gpuScene.pickActiveGameCamera() && cpuScene.pickActiveGameCamera();
        // 빛은 씬 데이터로 둔다 — 라이트가 없으면 앰비언트만 남아(키 라이트 폴백 없음, D2-13) 노멀만 바꾸는 변형(crumple)이 그림에 안 보인다.
        bOk                     = bOk && Internal::addKeyLight( gpuScene ) && Internal::addKeyLight( cpuScene );
        MeshComponent* pGPUMesh = bOk ? Internal::addMeshObject( gpuScene, "GPUModified" ) : nullptr;
        MeshComponent* pCPUMesh = bOk ? Internal::addMeshObject( cpuScene, "CPUModified" ) : nullptr;
        bOk                     = pGPUMesh != nullptr && pCPUMesh != nullptr;
        SW_EXPECT_TRUE_MSG( bOk, ( backendLabel + ": 씬·렌더러 준비 실패" ).c_str() );
        if ( bOk == false )
            continue;

        // 레스트 메시 하나에 프로그램만 바꿔 건다 - 미리보기가 쓰는 길(정점 버퍼를 다시 만들지 않는다)
        shared_ptr<Mesh> gpuMesh = Mesh::create();
        gpuMesh->setVertices( listRest );
        gpuMesh->setGPUMorphEnabled( true );
        pGPUMesh->setMesh( gpuMesh );
        shared_ptr<Mesh> restMesh = Mesh::create();
        restMesh->setVertices( listRest );
        pCPUMesh->setMesh( restMesh );
        const Internal::Snapshot rest = Internal::snapshot( cpuRenderer, *device, cpuScene );
        SW_EXPECT_TRUE_MSG( rest._bOk, ( backendLabel + ": 레스트 그림을 못 읽었다" ).c_str() );

        for ( size_t stackIndex = 0; stackIndex < listStack.size() && rest._bOk; ++stackIndex )
        {
            const string label = backendLabel + " / " + listLabel[stackIndex];
            gpuMesh->setGPUDeformProgram( listProgram[stackIndex] );
            shared_ptr<Mesh> cpuMesh = Mesh::create();
            cpuMesh->setVertices( listCPUVertex[stackIndex] );
            pCPUMesh->setMesh( cpuMesh );

            const Internal::Snapshot gpuImage = Internal::snapshot( gpuRenderer, *device, gpuScene );
            const Internal::Snapshot cpuImage = Internal::snapshot( cpuRenderer, *device, cpuScene );
            SW_EXPECT_TRUE_MSG( gpuImage._bOk && cpuImage._bOk, ( label + ": 그림을 못 읽었다" ).c_str() );
            if ( gpuImage._bOk == false || cpuImage._bOk == false )
                continue;
            const uint32 cpuDiff  = Internal::countDifferentPixels( gpuImage, cpuImage );
            const uint32 restDiff = Internal::countDifferentPixels( gpuImage, rest );
            SW_EXPECT_TRUE_MSG( cpuDiff <= cpuImage._drawnCount / 50 + 8, ( label + ": GPU 결과가 CPU 정본과 다르다 (달라진 픽셀 " + to_string( cpuDiff ) + " / 그려진 " +
                                                                            to_string( cpuImage._drawnCount ) + ")" )
                                                                              .c_str() );
            SW_EXPECT_TRUE_MSG( restDiff > rest._drawnCount / 50,
                                ( label + ": 프로그램을 걸었는데 그림이 레스트와 같다 (달라진 픽셀 " + to_string( restDiff ) + ") — 프로그램이 정점에 닿지 않는다" ).c_str() );
        }
    }

    if ( sweep.getReadyCount() == 0 )
        SW_TEST_SKIP( "No RHI backend for the mesh modifier GPU program test" );
}
