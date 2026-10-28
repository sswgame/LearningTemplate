#include "pch.h"

// 렌더러 헤더를 쓰는 시연이라 클라이언트 빌드에만 있다(서버는 Engine/Renderer 를 짓지 않아 이 번역 단위가 링크 오류가 된다).
#if defined( SW_WITH_CLIENT_CODE )

    #include "Games/Empty/EmptyRenderDemo.h"

    #include "Core/Math/MatrixMath.h"

namespace sw
{
    namespace
    {
        /// @brief 두 패스가 프로그램으로 쓰는 셰이더입니다(파이프라인 `_listExtensionShader` 에 선언돼 있어야 한다).
        constexpr const utf8* kTintShader = "engine/shaders/extensionpassdemo.hlsl";
        constexpr const utf8* kDrawShader = "engine/shaders/extensiondrawdemo.hlsl";

        /// @brief 셰이더의 PassCB 칸 `g_OutlineColor` 를 요소 색 · 틴트로 빌린다(두 셰이더 머리 주석).
        constexpr const utf8* kColorConstant = "g_OutlineColor";

        ExtensionDrawElement makeCube( float32 x, float32 y, float32 z, const float4& color, uint32 instanceCount )
        {
            ExtensionDrawElement element;
            element._mesh           = hashed_string( "Cube" ); // 엔진 내장 큐브(`MeshUtil::acquirePrimitive`)
            element._world          = float4x4::makeTranslation( x, y, z );
            element._instanceCount  = instanceCount;
            element._arrConstant[0] = ExtensionConstant{ hashed_string( kColorConstant ), color };
            element._constantCount  = 1;
            return element;
        }
    } // namespace

    void EmptyDrawDemoProxy::collectDrawElements( ExtensionDrawCollector& out, [[maybe_unused]] const ExtensionFrameInfo& frame ) const
    {
        for ( const ExtensionDrawElement& element : _listElement )
        {
            (void)out.add( element );
        }
    }

    void EmptyRenderExtension::addPasses( ExtensionPassBuilder& builder, [[maybe_unused]] const ExtensionFrameInfo& frame )
    {
        // 큐브 — 장면 색 · 깊이 위에 깊이 테스트로 그린다. 후처리(피사계 심도) 앞이라 뒤 효과가 큐브에도 걸린다.
        ExtensionPassParams drawParams;
        drawParams._name                 = "Empty.DrawDemo";
        drawParams._stage                = ExtensionHookStage::PrePostProcess;
        drawParams._listWrite            = { "SceneColor" };
        drawParams._depthAttachment      = "SceneDepth";
        drawParams._program._shaderPath  = kDrawShader;
        drawParams._program._bCullBack   = true;
        drawParams._program._bDepthWrite = true;
        builder.addPass( std::move( drawParams ), ExtensionPassExecuteFn( []( ExtensionPassContext& context )
        { (void)context.drawProxyElements( {} ); } ) );

        // 틴트 — 큐브까지 그려진 장면 색 위에 붉은 막을 비율 0.15 로 블렌드한다. 상수는 값이고, 그리는 일은 렌더러가 한다.
        ExtensionPassParams tintParams;
        tintParams._name                = "Empty.TintDemo";
        tintParams._stage               = ExtensionHookStage::PrePostProcess;
        tintParams._listWrite           = { "SceneColor" };
        tintParams._program._shaderPath = kTintShader;
        tintParams._program._bBlend     = true;
        builder.addPass( std::move( tintParams ), ExtensionPassExecuteFn(
                                                      []( ExtensionPassContext& context )
        {
            const ExtensionConstant arrConstant[] = {
                ExtensionConstant{ hashed_string( kColorConstant ), float4{ 1.0f, 0.25f, 0.25f, 0.15f } }
            };
            (void)context.drawFullscreen( vector_reference<const ExtensionConstant>( arrConstant ) );
        } ) );
    }

    EmptyRenderDemo::EmptyRenderDemo( RenderExtensionRegistry& registry )
        : _extension{}
        , _extensionRegistration{ registry, _extension }
        , _proxyRegistration{ registry, make_unique<EmptyDrawDemoProxy>() }
    {
        // 게임 스레드가 값을 만들어 프록시 갱신으로 올린다. 렌더 스레드가 다음 프레임 시작에 적용한다(프록시를 읽는 동안 게임 스레드가 쓰는 일이 없다).
        vector<ExtensionDrawElement> listCube;
        listCube.push_back( makeCube( -2.0f, 0.5f, 0.0f, float4{ 0.9f, 0.3f, 0.2f, 1.0f }, 1 ) );
        listCube.push_back( makeCube( 0.0f, 0.5f, 0.0f, float4{ 0.2f, 0.8f, 0.3f, 1.0f }, 1 ) );
        listCube.push_back( makeCube( 2.0f, 0.5f, 2.0f, float4{ 0.2f, 0.4f, 0.9f, 1.0f }, 3 ) ); // 인스턴스 셋 — 셰이더가 X 로 펼친다
        (void)_proxyRegistration.update(
            ExtensionProxyUpdateFn( [listCube]( IExtensionSceneProxy& proxy )
        { static_cast<EmptyDrawDemoProxy&>( proxy ).setElements( listCube ); } ) );
    }
} // namespace sw

#endif // SW_WITH_CLIENT_CODE
