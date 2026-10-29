/**
 * @file FrameExtensionHost.h
 * @brief 모듈 렌더 확장(ExtensionRenderPass M3 · M4)을 렌더러 한 프레임에 받아들이는 자리입니다 — 확장 훅 · 프록시 요소를 모으고, 패스 선언을 검사하고, 프로그램 PSO · 임시 텍스처 · 요소 메시를 기록 전에 갖춥니다.
 * @details 받아들인 패스(`ExtensionFramePass`)를 그래프 노드로 묶고 기록하는 일은 렌더러가 한다(`FramePassExecutor::bindPassCallbacks` · `executeExtensionPass`) — 그리는 길은 엔진 패스와 같다.
 *          언리얼 `FSceneViewExtensions` 가 확장을 모으고(`GatherActiveExtensions`) 렌더러가 훅 자리마다 부르는 것과 같은 나눔이다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/unordered_map.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Memory/Memory.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Extension/RenderExtension.h"

namespace sw
{
    struct FrameViewTarget;

    class FramePSOManager;
    class IRHIDevice;
    class Mesh;
    class RenderExtensionRegistry;
    class RenderPipelineAsset;

    /**
     * @brief 이번 프레임에 모듈 확장이 선언해 검사를 통과한 패스 하나입니다(선언 + 실행 콜백 + 만들어 둔 PSO). 프레임 끝에 비웁니다.
     */
    struct ExtensionFramePass
    {
        ExtensionPassParams    _params{};
        ExtensionPassExecuteFn _execute{};
        hashed_string          _graphName{};   ///< 그래프 노드 이름("Extension.<이름>")
        hashed_string          _colorTarget{}; ///< 첫 쓰기 첨부(그리는 컬러 타깃)
        hashed_string          _depthTarget{}; ///< 깊이 첨부(없으면 빈 이름)
        RHIPipelineStateHandle _pso{ 0 };
    };

    /**
     * @class FrameExtensionHost
     * @brief 렌더러 하나의 모듈 확장 프레임 상태(받아들인 패스 · 임시 텍스처 요청 · 프록시 요소 · 요소 메시 표 · 선언 해시)를 가집니다. 렌더러가 소유합니다.
     * @details 모으기(`collectFrame`) ~ 놓기(`releaseFrame`)는 렌더 스레드의 모듈 구간(`RenderExtensionRegistry::RenderScope`) 안이다 — 모듈이 넣은 콜백의 소멸 코드가 모듈에 있다.
     *          기록 중에는 읽기만 한다(패스는 병렬로 기록된다).
     */
    class SW_API FrameExtensionHost
    {
    public:
        /** @brief 렌더러의 디바이스 변수(`FrameCapture` 와 같이 변수를 참조한다) · 파이프라인 에셋 · PSO 관리자를 쓰는 자리를 만듭니다. 셋은 이 객체보다 오래 산다. */
        FrameExtensionHost( IRHIDevice* const& pDevice, const RenderPipelineAsset& pipeline, FramePSOManager& psoManager );
        FrameExtensionHost( const FrameExtensionHost& )            = delete;
        FrameExtensionHost& operator=( const FrameExtensionHost& ) = delete;

        /**
         * @brief 모듈 구간의 첫 일입니다. 대기 중인 프록시 갱신을 적용하고, 확장의 `addPasses` 와 프록시의 `collectDrawElements` 를 부르고,
         *        패스 선언을 검사해 프로그램 PSO 를 기록 전에 만듭니다(임시 텍스처는 뷰마다 `allocateTextures`).
         * @return 이번 프레임 선언이 그래프에 묶인 것(`markBound`)과 달라 그래프를 다시 묶어야 하면 true 입니다.
         */
        [[nodiscard]] bool collectFrame( RenderExtensionRegistry& registry, uint32 outputWidth, uint32 outputHeight );
        /** @brief 모듈 구간의 끝입니다. 모듈이 넣은 패스 콜백 · 요소를 모두 놓습니다(람다의 소멸 코드가 모듈에 있어 모듈 구간 안에서 놓아야 한다). */
        void releaseFrame();
        /** @brief 요소 메시 표를 비웁니다(GPU 정점 버퍼는 디바이스의 것이다 — 다음 프레임의 셋업이 다시 읽고 올린다). */
        void releaseMeshes() { _mapMesh.clear(); }
        /** @brief 이 뷰의 풀에 이번 프레임에 요청된 모듈 임시 텍스처를 만듭니다(이미 있으면 그대로). */
        void allocateTextures( FrameViewTarget& view ) const;
        /** @brief 지금 선언을 그래프에 묶었다고 적습니다(`FramePassExecutor::bindPassCallbacks`). */
        void markBound() { _boundSignature = _frameSignature; }

        /** @brief 이번 프레임에 받아들인 패스입니다. 모듈 구간 밖에서는 비어 있습니다. */
        const vector<ExtensionFramePass>& getPasses() const { return _listPass; }
        /** @brief 이번 프레임에 프록시들이 낸 요소입니다. */
        const vector<ExtensionDrawElement>& getDrawElements() const { return _listDrawElement; }
        /** @brief 셋업이 읽고 올린 요소 메시입니다. 없으면(읽지 못했다 — 셋업이 이유를 남겼다) nullptr 입니다. 기록 중에는 이 표를 읽기만 합니다. */
        const Mesh* findMesh( const hashed_string& meshID ) const;
        /** @brief 모듈 패스 · 요소의 오류를 키마다 한 번만 남깁니다(그리는 길은 프레임마다 돌아 같은 줄이 쌓이지 않게). */
        static void logFailureOnce( string_view key, const char* pReason, string_view detail );

    private:
        /** @brief 요소가 쓰는 메시를 찾거나 읽고 GPU 에 올립니다. 기록 전에만 부릅니다. 얻지 못하면 nullptr 입니다(이름마다 한 번 오류를 남겼다). */
        Mesh* resolveMesh( const hashed_string& meshID );

    private:
        IRHIDevice* const&         _pDevice; ///< 렌더러의 `_pDevice` 변수를 참조합니다(생성자 참고)
        const RenderPipelineAsset& _pipeline;
        FramePSOManager&           _psoManager;
        /// @brief 이번 프레임의 모듈 확장 패스입니다. 모듈 구간 안(`collectFrame` ~ `releaseFrame`)에서만 비지 않습니다.
        vector<ExtensionFramePass> _listPass;
        /// @brief 이번 프레임에 요청된 모듈 임시 텍스처입니다(`ExtensionPassBuilder::createTexture`).
        vector<ExtensionTextureDesc> _listTexture;
        vector<ExtensionDrawElement> _listDrawElement;
        /// @brief 요소가 쓰는 메시를 쥐는 표입니다(`MeshUtil::acquirePrimitive` · `MeshCache` 는 약한 참조라 렌더러 쪽이 쥔다). 기록 전(셋업)에만 바꾼다.
        unordered_map<hashed_string, shared_ptr<Mesh>> _mapMesh;
        /// @brief 이번 프레임 모듈 패스 선언의 해시(0 = 없음)와 그래프에 묶인 선언의 해시입니다. 다르면 그래프를 다시 묶는다. 프레임 끝에 0 으로 돌린다.
        uint64 _frameSignature{ 0 };
        uint64 _boundSignature{ 0 };
    };
} // namespace sw
