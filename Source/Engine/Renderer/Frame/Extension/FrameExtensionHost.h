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
#include "Core/Name/hashed_string.h"

#include "Engine/Graphics/Mesh/MeshCache.h"
#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Extension/RenderExtension.h"
#include "Engine/Renderer/Pipeline/RenderGraphPassDesc.h"

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
        void releaseMeshes()
        {
            _mapMesh.clear();
            _mapMeshLoad.clear();
        }
        /** @brief 이 뷰의 풀에 이번 프레임에 요청된 모듈 임시 텍스처를 만듭니다(이미 있으면 그대로). */
        void allocateTextures( FrameViewTarget& view ) const;
        /** @brief 지금 선언을 그래프에 묶었다고 적습니다(`FramePassExecutor::bindPassCallbacks`). */
        void markBound() { _boundSignature = _frameSignature; }

        /** @brief 이번 프레임에 받아들인 패스입니다. 모듈 구간 밖에서는 비어 있습니다. */
        const vector<ExtensionFramePass>& getPasses() const { return _listPass; }
        /** @brief 이번 프레임에 프록시들이 낸 요소입니다. */
        const vector<ExtensionDrawElement>& getDrawElements() const { return _listDrawElement; }
        /** @brief 셋업이 읽고 올린 요소 메시입니다. 없으면(아직 읽는 중이거나, 읽지 못했다 — 셋업이 이유를 남겼다) nullptr 입니다. 기록 중에는 이 표를 읽기만 합니다. */
        const Mesh* findMesh( const hashed_string& meshID ) const;
        /** @brief 모듈 패스 · 요소의 오류를 키마다 한 번만 남깁니다(그리는 길은 프레임마다 돌아 같은 줄이 쌓이지 않게). */
        static void logFailureOnce( string_view key, const char* pReason, string_view detail );

    private:
        /**
         * @struct PassDeclarationCheck
         * @brief 패스 선언 하나의 검사 결과입니다. 선언 · 파이프라인 첨부 · 임시 텍스처 요청이 그대로면 다음 프레임에 다시 쓴다(`findPassCheck`).
         * @details 실행 콜백은 프레임마다 새로 오므로 받아들인 패스를 통째로 다시 쓸 수 없다 — 콜백에 기대지 않는 검사(포맷 찾기 · 이름 intern · 선언 해시)만 남긴다.
         */
        struct PassDeclarationCheck
        {
            ExtensionPassParams _params{};            ///< 이 결과를 낸 선언(다음 프레임 선언과 맞대 본다)
            const char*         _pFailure{ nullptr }; ///< 어긋난 이유(문자열 상수, 없으면 받아들일 선언)
            hashed_string       _graphName{};         ///< 그래프 노드 이름("Extension.<이름>")
            hashed_string       _colorTarget{};       ///< 첫 쓰기 컬러 첨부
            hashed_string       _depthTarget{};       ///< 깊이 첨부(없으면 빈 이름)
            RHIFormat           _colorFormat{ RHIFormat::Unknown };
            uint64              _declarationHash{ 0 }; ///< 프레임 선언 서명에 넣는 이 선언의 해시
        };

        /** @brief 검사가 읽는 파이프라인 첨부 · 확장 셰이더 목록 · 임시 텍스처 요청이 지난번과 다르면 지난 검사 결과를 버리고 임시 텍스처 이름 열쇠를 다시 만듭니다. */
        void refreshPassChecks();
        /** @brief 선언 차례 @p entryIndex 의 검사 결과입니다. 같은 자리의 지난 결과와 선언이 같으면 그것, 아니면 새로 검사해 그 자리에 둡니다. */
        const PassDeclarationCheck& findPassCheck( size_t entryIndex, const ExtensionPassParams& params );
        /** @brief 선언 하나를 검사합니다(이름 · 중복 · 실행 콜백은 부르는 쪽이 본다). */
        [[nodiscard]] PassDeclarationCheck checkPassDeclaration( const ExtensionPassParams& params ) const;
        /** @brief 첨부 이름의 포맷 — 파이프라인 XML 의 첨부, 아니면 이번 프레임의 임시 텍스처 요청입니다. 없으면 false 입니다. */
        [[nodiscard]] bool findAttachmentFormat( const string& name, RHIFormat& outFormat ) const;
        /** @brief 요소가 쓰는 메시를 찾거나 읽기를 맡기고 GPU 에 올립니다. 기록 전에만 부릅니다. 아직 읽는 중이면 nullptr(그 요소는 이번 프레임 건너뛴다), 얻지 못하면 nullptr 입니다(이름마다 한 번 오류를 남겼다). */
        Mesh* resolveMesh( const hashed_string& meshID );

    private:
        IRHIDevice* const&         _pDevice; ///< 렌더러의 `_pDevice` 변수를 참조합니다(생성자 참고)
        const RenderPipelineAsset& _pipeline;
        FramePSOManager&           _psoManager;
        /// @brief 이번 프레임의 모듈 확장 패스입니다. 모듈 구간 안(`collectFrame` ~ `releaseFrame`)에서만 비지 않습니다.
        vector<ExtensionFramePass> _listPass;
        /// @brief 이번 프레임에 요청된 모듈 임시 텍스처입니다(`ExtensionPassBuilder::createTexture`).
        vector<ExtensionTextureDesc> _listTexture;
        /// @brief `_listTexture` 의 첨부 이름 열쇠입니다(같은 차례). 요청이 바뀐 프레임에만 다시 만든다(`refreshPassChecks`).
        vector<hashed_string> _listTextureName;
        /// @brief 선언 차례마다의 검사 결과입니다(`findPassCheck`). 프레임을 넘어 남는다.
        vector<PassDeclarationCheck> _listPassCheck;
        /// @brief `_listPassCheck` 를 낸 파이프라인 첨부 · 확장 셰이더 목록 · 임시 텍스처 요청입니다(바뀌면 결과를 버린다).
        vector<RenderPassAttachment> _listCheckedAttachment;
        vector<string>               _listCheckedShader;
        vector<ExtensionTextureDesc> _listCheckedTexture;
        vector<ExtensionDrawElement> _listDrawElement;
        /// @brief 요소가 쓰는 메시를 쥐는 표입니다(`MeshUtil::acquirePrimitive` · `MeshCache` 는 약한 참조라 렌더러 쪽이 쥔다). 기록 전(셋업)에만 바꾼다.
        unordered_map<hashed_string, shared_ptr<Mesh>> _mapMesh;
        /// @brief 워커가 아직 읽는 `.mesh` 요소 메시입니다(`MeshCache::request`). 준비되면 `_mapMesh` 로 옮긴다. 기록 전(셋업)에만 바꾼다.
        unordered_map<hashed_string, MeshHandle> _mapMeshLoad;
        /// @brief 이번 프레임 모듈 패스 선언의 해시(0 = 없음)와 그래프에 묶인 선언의 해시입니다. 다르면 그래프를 다시 묶는다. 프레임 끝에 0 으로 돌린다.
        uint64 _frameSignature{ 0 };
        uint64 _boundSignature{ 0 };
    };
} // namespace sw
