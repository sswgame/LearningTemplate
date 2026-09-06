/**
 * @file RenderPassResource.h
 * @brief 렌더 패스용 리소스 핸들/디스크립터
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Common/Common.h"
#include "Engine/Reflection/ReflectionCore.h"

namespace sw
{
    /**
     * @enum RenderPassType
     * @brief 파이프라인 XML 의 `_type` 이 가리키는 패스 종류.
     * @details 예전에는 이 값이 문자열이라, 디스패치(executePass)와 PSO 생성(findPassDescByType)이
     *          각자 문자열을 비교했다. 두 곳이 받아주는 표기가 달라서 `Shading` 으로 적은 패스가
     *          디스패치는 되는데 PSO 는 desc 를 못 찾고 기본 포맷으로 만들어졌다(`ae7fb078`).
     *          표기 흔들림은 **같은 값을 갖는 별칭 열거자**로 여기 한 곳에 모은다 — 리플렉션이
     *          문자열↔값 변환을 제공하므로 파서도 검증도 이 표만 보면 된다.
     * @note 이름은 상용 엔진에서 통용되는 표기 하나로 통일한다 — 아직 개발 중이라 여러 표기를
     *       받아줄 이유가 없고, 표기가 갈리는 순간 "어느 쪽으로 적었나" 를 매번 확인해야 한다.
     *       XML 에 적히는 철자가 곧 열거자 이름이고, 여기에 없는 표기는 Invalid 로 파싱되어
     *       RenderPipelineResource::validate 가 잡는다.
     * @note 나중에 이름을 바꿔야 하면 기존 XML 을 깨지 않도록 `ENUM( ValueAlias = "Old:New" )` 를
     *       쓴다 — 지금은 통일된 상태라 비워 둔다.
     */
    ENUM()
    enum class RenderPassType : uint32
    {
        Invalid = 0, ///< 알 수 없는 표기 (검증에서 오류로 보고)

        Shadow,
        DepthPrepass,
        ForwardOpaque,
        GBuffer,
        GBufferAlbedo,
        GBufferNormal,
        Lighting,
        Transparent,
        SSAO,
        Bloom,
        Outline,
        TAA,
        Tonemap,
        Present,

        // --- 엔진 내부 PSO 슬롯. 파이프라인 XML 에는 나올 수 없다(검증이 거부한다). ---
        ForwardOpaqueNoDepthWrite,
        GpuCull,
    };

    /**
     * @brief 파이프라인 출력에 쓰는 예약어 — 디바이스가 주는 백버퍼를 가리킨다.
     * @details `_attachments` 에 선언되지 않는 유일한 출력이라 검증에서 예외로 다룬다.
     */
    inline constexpr const utf8* kSwapchainOutputName = "Swapchain";

    /** @brief 파이프라인 XML 의 `_type` 으로 쓸 수 있는 값인가 (내부 슬롯·Invalid 제외). */
    inline bool isPipelinePassType( RenderPassType type )
    {
        return type != RenderPassType::Invalid && static_cast<uint32>( type ) <= static_cast<uint32>( RenderPassType::Present );
    }

    /// @brief 렌더 패스 어태치먼트 (포맷, 로드/스토어, 클리어)

    REFLECT()
    struct RenderPassAttachment
    {
        PROPERTY()
        string _name = "ColorAttachment0";

        PROPERTY()
        string _format = "R8G8B8A8_UNORM";

        PROPERTY()
        float4 _clearColor = { 0.1f, 0.2f, 0.3f, 1.0f };

        PROPERTY()
        bool _bClear{ true };
    };

    /**
     * @brief Single pass node inside a Render Pipeline graph.
     * @details Graph wiring (inputs/outputs) + optional PSO recipe (shader, entries, state, permutations).
     *          RenderPass XML stays attachment-only; these fields belong on pipeline passes.
     */
    REFLECT()
    struct RenderGraphPassDesc
    {
        PROPERTY()
        string _name = "Pass";

        PROPERTY()
        string _type = "Opaque";

        /**
         * @brief `_type` 을 해석한 값. XML 로드 시 RenderPipelineResource 가 채운다.
         * @details 직렬화 대상이 아니다(원본 철자는 `_type` 이 그대로 갖고 있다). 디스패치와 PSO
         *          생성이 같은 값을 보게 하려고 한 번만 해석해 둔다 — 예전엔 두 곳이 각자 문자열을
         *          비교하다가 서로 다른 표기를 받아줘서 어긋났다.
         */
        RenderPassType _resolvedType{ RenderPassType::Invalid };

        PROPERTY()
        vector<string> _listInput;

        PROPERTY()
        vector<string> _listOutput;

        /** @brief HLSL path (engine/... or common/...). Empty → FrameRenderer type default. */
        PROPERTY()
        string _shaderPath;

        PROPERTY()
        string _vertexEntryPoint = "VSMain";

        PROPERTY()
        string _pixelEntryPoint = "PSMain";

        PROPERTY()
        string _computeEntryPoint;

        PROPERTY()
        string _geometryEntryPoint;

        PROPERTY()
        string _hullEntryPoint;

        PROPERTY()
        string _domainEntryPoint;

        PROPERTY()
        string _meshEntryPoint;

        PROPERTY()
        string _amplificationEntryPoint;

        /** @brief Shader macros / permutations as "NAME" or "NAME=VALUE". */
        PROPERTY()
        vector<string> _listPermutation;

        PROPERTY()
        string _cullMode = "Back"; ///< None / Front / Back

        PROPERTY()
        bool _bEnableDepthTest{ true };

        PROPERTY()
        bool _bEnableDepthWrite{ true };

        PROPERTY()
        bool _bEnableBlend{ false };
    };

    /**
     * @brief Attachment-set descriptor for an RHI render pass template.
     * Frame composition (pass graph) belongs in RenderPipelineDesc — see RenderPipelineResource.h.
     */
    REFLECT()
    struct RenderPassDesc
    {
        PROPERTY()
        string _name = "DefaultMainPass";

        PROPERTY()
        vector<RenderPassAttachment> _listAttachment;
    };

    /// @brief RenderPass XML 에셋 (어태치먼트 템플릿)
    class SW_API RenderPassResource
    {
    public:
        /** @brief 빈 렌더패스 디스크립터. */
        RenderPassResource() = default;
        /** @brief 가상 소멸. */
        virtual ~RenderPassResource() = default;

        /** @brief 복사를 금지합니다. */
        RenderPassResource( const RenderPassResource& ) = delete;
        /** @brief 대입을 금지합니다. */
        RenderPassResource& operator=( const RenderPassResource& ) = delete;

        /** @brief XML 파일에서 렌더 패스 디스크립터를 로드합니다. */
        bool loadFromXmlFile( string_view assetRelativePath );

        /** @brief 렌더 패스 디스크립터를 XML 파일로 저장합니다. */
        bool saveToXmlFile( string_view assetRelativePath ) const;

        /** @brief XML 로드를 비동기 작업으로 예약합니다. */
        TaskHandle loadFromXmlFileAsync( string_view assetRelativePath );

        const RenderPassDesc& getDesc() const { return _desc; }
        RenderPassDesc&       getDesc() { return _desc; }

    private:
        /** @brief TaskArgs: this, path string. */
        static void loadFromXmlFileAsyncJob( const TaskArgs& args );

    private:
        RenderPassDesc _desc;
    };
} // namespace sw
