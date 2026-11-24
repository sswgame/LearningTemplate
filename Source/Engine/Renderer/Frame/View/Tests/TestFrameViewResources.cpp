#include "pch.h"

#include "Core/Math/MathUtil.h"
#include "Core/Testing/TestFramework.h"

#include "Engine/Graphics/RHI/IRHICommandList.h"
#include "Engine/Graphics/RHI/Tests/RHIFakeDevice.h"
#include "Engine/Renderer/Extension/FrameExtensionHost.h"
#include "Engine/Renderer/Frame/FrameRendererUtil.h"
#include "Engine/Renderer/Frame/Packet/RenderFrameSettings.h"
#include "Engine/Renderer/Frame/PostProcess/FrameAutoExposure.h"
#include "Engine/Renderer/Frame/PostProcess/FrameUpscale.h"
#include "Engine/Renderer/Frame/Resource/FramePSOManager.h"
#include "Engine/Renderer/Frame/View/FrameViewResources.h"
#include "Engine/Renderer/Frame/View/FrameViewTarget.h"
#include "Engine/Renderer/Graph/RenderGraphAliasPlan.h"
#include "Engine/Renderer/Pipeline/RenderPipelineAsset.h"
#include "Engine/Renderer/Scene/GPUScene.h"

// 자동 렌더 배율의 단계(RS-4) — 풀은 그대로 두고 그리는 사각형만 줄인다(할당 0), 지난 사각형 배율이 남아 히스토리를 지난 배율로 읽는다. 디바이스 없이 돈다.

using namespace sw;

namespace
{
    bool isNear( float32 a, float32 b )
    {
        return MathUtil::abs( a - b ) < 1.0e-5f;
    }

    /** @brief 가짜 디바이스로 뷰 하나의 패스 고르기를 돌립니다(디퍼드 파이프라인, 64x64). */
    struct ViewSelectionFixture
    {
        test::FakeRHIBufferFactory _factory;
        test::FakeRHIDevice        _device;
        IRHIDevice*                _pDevice{ &_device };
        RenderPipelineAsset        _pipeline;
        GPUScene                   _gpuScene;
        RenderFrameSettings        _settings{};
        FramePSOManager            _psoManager{ _pDevice, _pipeline, _gpuScene, _settings };
        FrameAutoExposure          _autoExposure{ _pDevice, _pipeline, _psoManager };
        FrameUpscale               _upscale{ _pDevice, _pipeline, _settings, _psoManager };
        FrameViewResources         _viewResources{ _pDevice, _pipeline, _settings, _autoExposure, _upscale };
        RenderGraphAliasPlan       _aliasPlan{};
        FrameViewTarget            _view;

        ~ViewSelectionFixture() { _viewResources.releaseViewTransients( _view ); }

        [[nodiscard]] bool load()
        {
            _factory._nextTexture     = 1;
            _device._pResourceFactory = &_factory;
            if ( _pipeline.loadFromXMLFile( AssetID{ string_view( "engine/pipeline/deferredpipeline.xml" ) } ) == false )
                return false;
            _view._transientPool.setSize( 64, 64, 64, 64 );
            _view._shadowMapResolution = _settings._shadowMapResolution;
            return true;
        }

        bool select( const FrameViewResources::ViewPassSceneState& sceneState, vector_reference<const ExtensionFramePass> listExtensionPass = {} )
        {
            return _viewResources.applyViewPassSelection( _view, float4x4::Identity, sceneState, _aliasPlan, listExtensionPass )._bTargetsChanged == SW_TRUE;
        }

        bool has( const utf8* pName ) const { return _view._transientPool.contains( hashed_string( pName ) ); }
    };
} // namespace

/** @brief [FrameViewResourcesTest] 배율 단계는 풀 크기를 바꾸지 않고(다시 만들지 않는다) 사각형만 줄인다. 반해상도 첨부는 비율로, 크기를 따로 준 첨부(그림자 맵)는 그대로다. */
SW_TEST_CASE( FrameViewResourcesTest, AutoScaleStepAllocatesNothing )
{
    FrameViewTarget view;
    view._transientPool.setSize( 1280, 720, 1280, 720 );
    view.updateViewRect( 1.0f );
    SW_EXPECT_EQUAL( view._rect._width, 1280u );
    SW_EXPECT_EQUAL( view._rect._height, 720u );
    SW_EXPECT_EQUAL( view.scaleToRect( 1280, true, RenderResolutionSpace::Render, true ), 1280u ); // 사각형 = 풀이면 뷰포트를 바꾸지 않는다

    view.updateViewRect( 0.7f );
    SW_EXPECT_EQUAL( view._transientPool.getWidth(), 1280u );
    SW_EXPECT_EQUAL( view._transientPool.getHeight(), 720u );
    SW_EXPECT_TRUE( view._transientPool.isEmpty() ); // 단계가 첨부를 만들지 않았다
    SW_EXPECT_EQUAL( view._rect._width, 896u );
    SW_EXPECT_EQUAL( view._rect._height, 504u );
    SW_EXPECT_EQUAL( view.scaleToRect( 1280, true, RenderResolutionSpace::Render, true ), 896u );
    SW_EXPECT_EQUAL( view.scaleToRect( 640, true, RenderResolutionSpace::Render, true ), 448u );    // 반해상도
    SW_EXPECT_EQUAL( view.scaleToRect( 2048, false, RenderResolutionSpace::Render, true ), 2048u ); // 그림자 맵
    view.updateViewRect( 0.0f );
    SW_EXPECT_EQUAL( view._rect._width, static_cast<uint32>( 1280.0f * FrameViewTarget::kMinRectScale + 0.5f ) ); // 하한
}

/** @brief [FrameViewResourcesTest] 단계가 바뀐 프레임에도 히스토리를 버리지 않는다 — 지난 사각형 배율이 셰이더 상수의 zw 로 간다(TAA 는 지난 배율로 읽는다). */
SW_TEST_CASE( FrameViewResourcesTest, HistorySurvivesAutoScaleStep )
{
    FrameViewTarget view;
    view._transientPool.setSize( 1000, 500, 1000, 500 );
    view.updateViewRect( 1.0f );
    view.beginViewHistory();
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._bHistoryReset == SW_FALSE );

    view.updateViewRect( 0.8f ); // 단계 — 풀도 기록 플래그도 그대로다
    view.beginViewHistory();
    SW_EXPECT_TRUE( view._bHistoryReset == SW_FALSE );
    SW_EXPECT_TRUE( isNear( view._previousRectScale._x, 1.0f ) && isNear( view._previousRectScale._y, 1.0f ) );
    const FrameRendererUtil::ViewRectConstants constants =
        FrameRendererUtil::makeViewRectConstants( view._rect._width, view._rect._height, 1000, 500, view._previousRectScale );
    SW_EXPECT_TRUE( isNear( constants._scale._x, 0.8f ) && isNear( constants._scale._z, 1.0f ) );

    view.updateViewRect( 0.8f ); // 다음 프레임 — 지난 배율도 0.8
    SW_EXPECT_TRUE( isNear( view._previousRectScale._x, 0.8f ) );
}

/** @brief [FrameViewResourcesTest] 깊이 히스토리를 읽는 쪽(가림 타일 · 컬링 가림 판정 · GPU 입자 깊이 충돌)이 받는 크기는 지난 프레임 사각형이다 — 단계 프레임에도 가림 · 충돌을 끄지 않는다. */
SW_TEST_CASE( FrameViewResourcesTest, DepthHistoryReadsPreviousRect )
{
    FrameViewTarget view;
    view._transientPool.setSize( 1280, 720, 1280, 720 );
    view.updateViewRect( 1.0f );
    view.updateViewRect( 0.5f ); // 단계 프레임 — 히스토리는 배율 1 로 그린 지난 프레임이다
    SW_EXPECT_EQUAL( view.scaleToPreviousRect( 1280, true ), 1280u );
    SW_EXPECT_EQUAL( view.scaleToPreviousRect( 720, false ), 720u );
    view.updateViewRect( 0.5f ); // 다음 프레임 — 히스토리는 반 크기 사각형에 그렸다
    SW_EXPECT_EQUAL( view.scaleToPreviousRect( 1280, true ), 640u );
    SW_EXPECT_EQUAL( view.scaleToPreviousRect( 720, false ), 360u );
    SW_EXPECT_EQUAL( view.scaleToPreviousRect( 1, true ), 1u ); // 0 이 되지 않는다
}

/** @brief [FrameViewResourcesTest] 깊이 히스토리 크기는 지난 프레임에 실제로 그린 사각형 픽셀 그대로다 — 배율로 다시 셈한 값과 반올림이 갈리는 홀수 크기에서도 같다. */
SW_TEST_CASE( FrameViewResourcesTest, PreviousRectIsRecordedNotRecomputed )
{
    FrameViewTarget view;
    view._transientPool.setSize( 999, 555, 999, 555 );
    view.updateViewRect( 0.37f );
    const RenderViewRect drawn = view._rect;
    view.updateViewRect( 0.9f );
    SW_EXPECT_EQUAL( view._previousRect._width, drawn._width );
    SW_EXPECT_EQUAL( view._previousRect._height, drawn._height );
    SW_EXPECT_EQUAL( view.scaleToPreviousRect( 999, true ), drawn._width );
    SW_EXPECT_EQUAL( view.scaleToPreviousRect( 555, false ), drawn._height );
}

/**
 * @brief [FrameViewResourcesTest] 첨부는 도는 패스가 쓸 때만 생긴다 — 전용 실행 패스의 출력(DBuffer)과 깊이 첨부(점광 그림자 아틀라스)도 그 패스가 처음 도는 프레임에 만들고, 오래 쉬면 놓는다
 * @details 변이: 처음 쓸 때 만들기를 일반 풀스크린 패스의 출력에만 걸면 데칼 없는 장면에도 DBuffer 셋과 2048 정사각 아틀라스가 생긴다.
 */
SW_TEST_CASE( FrameViewResourcesTest, AttachmentsExistOnlyWhenWritten )
{
    ViewSelectionFixture fixture;
    SW_REQUIRE_TRUE( fixture.load() );
    FrameViewResources::ViewPassSceneState sceneState{};
    SW_EXPECT_TRUE( fixture.select( sceneState ) );
    SW_EXPECT_TRUE( fixture.has( "GBufferAlbedo" ) );
    SW_EXPECT_TRUE( fixture.has( "SceneDepth" ) );
    SW_EXPECT_TRUE( fixture.has( "ShadowMap" ) );
    SW_EXPECT_FALSE( fixture.has( "DBufferA" ) );
    SW_EXPECT_FALSE( fixture.has( "LocalShadowMap" ) );
    SW_EXPECT_TRUE( fixture._view._depthHistory.isValid() ); // 장면 깊이가 생긴 프레임에 따라 생긴다
    SW_EXPECT_FALSE( fixture.select( sceneState ) );         // 같은 상태의 다음 프레임은 만들지 않는다

    sceneState._bDecals       = SW_TRUE;
    sceneState._bLocalShadows = SW_TRUE;
    SW_EXPECT_TRUE( fixture.select( sceneState ) );
    SW_EXPECT_TRUE( fixture.has( "DBufferA" ) && fixture.has( "DBufferB" ) && fixture.has( "DBufferC" ) );
    const TransientAttachmentPool::Attachment atlas = fixture._view._transientPool.find( hashed_string( "LocalShadowMap" ) );
    SW_EXPECT_EQUAL( atlas._width, fixture._settings._shadowMapResolution ); // 화면 크기가 아니다

    sceneState._bDecals       = SW_FALSE;
    sceneState._bLocalShadows = SW_FALSE;
    for ( uint32 frameIndex = 1; frameIndex < TransientAttachmentPool::kIdleFramesBeforeRelease; ++frameIndex )
    {
        (void)fixture.select( sceneState );
    }
    SW_EXPECT_TRUE( fixture.has( "DBufferA" ) );
    SW_EXPECT_TRUE( fixture.select( sceneState ) );
    SW_EXPECT_FALSE( fixture.has( "DBufferA" ) );
    SW_EXPECT_FALSE( fixture.has( "LocalShadowMap" ) );
    SW_EXPECT_TRUE( fixture.has( "GBufferAlbedo" ) );
}

/** @brief [FrameViewResourcesTest] 모듈 확장 패스가 선언한 첨부도 같은 규칙이다 — 쓰는 첨부는 만들고, 읽는 첨부의 쓰는 패스(읽는 쪽이 없어 빠지던 장면 깊이 사본)는 돈다. */
SW_TEST_CASE( FrameViewResourcesTest, ExtensionDeclarationsKeepAttachments )
{
    ViewSelectionFixture fixture;
    SW_REQUIRE_TRUE( fixture.load() );
    const FrameViewResources::ViewPassSceneState sceneState{};
    SW_EXPECT_TRUE( fixture.select( sceneState ) );
    SW_EXPECT_FALSE( fixture.has( "SceneDepthCopy" ) );

    ExtensionFramePass reader{};
    reader._params._listRead.push_back( ExtensionPassRead{ string( "SceneDepthCopy" ), RenderPassInputRole::SceneDepthCopy } );
    reader._params._listWrite.push_back( string( "DBufferA" ) );
    const vector<ExtensionFramePass> listExtensionPass{ reader };
    SW_EXPECT_TRUE( fixture.select( sceneState, listExtensionPass ) );
    SW_EXPECT_TRUE( fixture.has( "SceneDepthCopy" ) );
    SW_EXPECT_TRUE( fixture.has( "DBufferA" ) );
    SW_EXPECT_FALSE( fixture.has( "DBufferB" ) );
}
