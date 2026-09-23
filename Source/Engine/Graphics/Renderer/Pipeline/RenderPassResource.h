/**
 * @file RenderPassResource.h
 * @brief 렌더 패스 XML 에셋(어태치먼트 템플릿)과 파이프라인 패스 서술 타입입니다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Math/VectorMath.h"
#include "Core/Task/TaskTypes.h"

#include "Engine/Common/Common.h"
#include "Engine/Reflection/ReflectionCore.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @enum RenderPassType
     * @brief 파이프라인 XML 의 `_type` 이 가리키는 패스 종류입니다.
     * @details 예전에는 이 값이 문자열이라 디스패치(executePass)와 PSO 생성(findPassDescByType)이
     *          각자 문자열을 비교했습니다. 두 곳이 받아 주는 표기가 달라서 `Shading` 으로 적은 패스가
     *          디스패치는 되는데 PSO 는 desc 를 못 찾고 기본 포맷으로 만들어졌습니다(`ae7fb078`).
     *          표기 흔들림은 **같은 값을 갖는 별칭 열거자**로 여기 한 곳에 모읍니다. 리플렉션이
     *          문자열 ↔ 값 변환을 제공하므로 파서도 검증도 이 표만 보면 됩니다.
     * @note 이름은 널리 쓰이는 표기 하나로 통일합니다. 아직 개발 중이라 여러 표기를
     *       받아 줄 이유가 없고, 표기가 갈리는 순간 "어느 쪽으로 적었나" 를 매번 확인해야 합니다.
     *       XML 에 적히는 철자가 곧 열거자 이름이고, 여기에 없는 표기는 Invalid 로 파싱되어
     *       RenderPipelineResource::validate 가 잡습니다.
     * @note 나중에 이름을 바꿔야 하면 기존 XML 을 깨지 않도록 `ENUM( ValueAlias = "Old:New" )` 를
     *       씁니다. 지금은 통일된 상태라 비워 둡니다.
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
        /// @brief GPUScene 인스턴스 애니메이션 컴퓨트(instanceanim.hlsl)입니다. 인스턴스마다 다른 각속도로 회전시킵니다.
        InstanceAnim,
        /// @brief 배치 안의 가시 인스턴스를 깊이순으로 정렬하는 컴퓨트(instancesort.hlsl)입니다. 투명 블렌딩 순서를 맞춥니다.
        InstanceSort,
        /// @brief 레스트 정점을 읽어 변형 결과를 쓰는 컴퓨트(meshmorph.hlsl)입니다. 정점 셰이더가 그 결과를 풀링합니다.
        MeshMorph,
    };

    /**
     * @brief 파이프라인 출력에 쓰는 예약어입니다. 디바이스가 주는 백버퍼를 가리킵니다.
     * @details `_attachments` 에 선언되지 않는 유일한 출력이라 검증에서 예외로 다룹니다.
     */
    inline constexpr const utf8* kSwapchainOutputName = "Swapchain";

    /** @brief 파이프라인 XML 의 `_type` 으로 쓸 수 있는 값인지 확인합니다(내부 슬롯 · Invalid 제외). */
    inline bool isPipelinePassType( RenderPassType type )
    {
        return type != RenderPassType::Invalid && static_cast<uint32>( type ) <= static_cast<uint32>( RenderPassType::Present );
    }

    /// @brief 렌더 패스 어태치먼트 하나(이름 · 포맷 · 클리어 색 · 클리어 여부)입니다.

    REFLECT()
    struct RenderPassAttachment
    {
        REFLECT_BODY();
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
     * @brief 렌더 파이프라인 그래프 안의 패스 노드 하나입니다.
     * @details 그래프 연결(입력 · 출력)과 선택적인 PSO 레시피(셰이더 · 엔트리 · 상태 · 퍼뮤테이션)를 담습니다.
     *          RenderPass XML 은 어태치먼트만 다루고, 이 필드들은 파이프라인의 패스에 둡니다.
     */
    REFLECT()
    struct RenderGraphPassDesc
    {
        REFLECT_BODY();
        PROPERTY()
        string _name = "Pass";

        PROPERTY()
        string _type = "Opaque";

        /**
         * @brief `_type` 을 해석한 값입니다. XML 로드 시 RenderPipelineResource 가 채웁니다.
         * @details 직렬화 대상이 아닙니다(원본 철자는 `_type` 이 그대로 갖고 있습니다). 디스패치와 PSO
         *          생성이 같은 값을 보게 하려고 한 번만 해석해 둡니다. 예전에는 두 곳이 각자 문자열을
         *          비교하다가 서로 다른 표기를 받아 줘서 어긋났습니다.
         */
        RenderPassType _resolvedType{ RenderPassType::Invalid };

        PROPERTY()
        vector<string> _listInput;

        PROPERTY()
        vector<string> _listOutput;

        /**
         * @brief 이 패스가 바인딩할 뎁스 첨부 이름입니다. **비어 있으면 뎁스 없이 엽니다.**
         * @details 예전에는 패스 타입마다 코드에 박혀 있었습니다("GBuffer 면 SceneDepth"). 그러면 "이 패스는
         *          일부러 뎁스를 쓰지 않는다" 를 표현할 방법이 없어서, DepthPrepass 를 넣거나 빼는
         *          구성을 바꾸려면 엔진 코드를 고쳐야 했습니다. 선언으로 빼면 파이프라인 XML 만으로 바뀝니다.
         * @note 읽기 · 쓰기 여부는 `_listInput` / `_listOutput` 이 따로 말합니다. Transparent 는 SceneDepth 를
         *       **입력으로 읽으면서** 뎁스로 바인딩합니다(테스트만 하고 쓰지 않습니다). 그래서 출력에서
         *       유추하지 않고 별도 필드로 둡니다.
         */
        PROPERTY( SkipIfEmpty )
        string _depthAttachment;

        /**
         * @brief `_depthAttachment` 를 intern 해 둔 값입니다. XML 로드 시 RenderPipelineResource 가 채웁니다.
         * @details 직렬화 대상이 아닙니다(원본 철자는 `_depthAttachment` 가 갖고 있습니다). `_resolvedType` 과
         *          같은 이유로 한 번만 해석해 둡니다. 이 이름은 패스마다 어태치먼트 클리어 여부를
         *          판정하는 데 쓰이는데, 매 프레임 다시 intern 하면 그때마다 전역 문자열 레지스트리의
         *          샤드 뮤텍스를 잡게 됩니다.
         */
        hashed_string _resolvedDepthAttachment;

        /**
         * @brief `_listInput` 하나하나의 (첨부 이름, 역할)입니다. XML 로드 시 RenderPipelineResource 가 채웁니다.
         * @details 직렬화 대상이 아닙니다. 실행은 이 목록을 그대로 걸고(역할 이름 = 셰이더의 `g_<Role>Index`),
         *          검증은 패스 타입의 계약(RenderPassInputContract)과 대조합니다. 둘이 같은 해석을 봅니다.
         *          역할 값은 `RenderPassInputRole` 인데 이 헤더가 그 enum 을 모르므로(계약 헤더가 이 헤더를
         *          포함합니다) 정수로 둡니다.
         */
        struct ResolvedInput
        {
            hashed_string _attachment;
            uint8         _role{ 0 };
        };
        vector<ResolvedInput> _listResolvedInput;

        /** @brief `_listOutput` 을 intern 해 둔 값입니다. 풀스크린 패스가 "첫 번째로 존재하는 출력" 을 타깃으로 고릅니다. */
        vector<hashed_string> _listResolvedOutput;

        /** @brief HLSL 경로(engine/... 또는 common/...)입니다. 비면 FrameRenderer 가 패스 타입의 기본 셰이더를 씁니다. */
        PROPERTY( SkipIfEmpty )
        string _shaderPath;

        PROPERTY()
        string _vertexEntryPoint = "VSMain";

        PROPERTY()
        string _pixelEntryPoint = "PSMain";

        PROPERTY( SkipIfEmpty )
        string _computeEntryPoint;

        PROPERTY( SkipIfEmpty )
        string _geometryEntryPoint;

        PROPERTY( SkipIfEmpty )
        string _hullEntryPoint;

        PROPERTY( SkipIfEmpty )
        string _domainEntryPoint;

        PROPERTY( SkipIfEmpty )
        string _meshEntryPoint;

        PROPERTY( SkipIfEmpty )
        string _amplificationEntryPoint;

        /** @brief 셰이더 매크로(퍼뮤테이션)입니다. "NAME" 또는 "NAME=VALUE" 꼴로 적습니다. */
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
     * @brief RHI 렌더 패스 템플릿의 어태치먼트 묶음 서술입니다.
     * 프레임 구성(패스 그래프)은 RenderPipelineDesc 의 몫입니다(RenderPipelineResource.h).
     */
    REFLECT()
    struct RenderPassDesc
    {
        REFLECT_BODY();
        PROPERTY()
        string _name = "DefaultMainPass";

        PROPERTY()
        vector<RenderPassAttachment> _listAttachment;
    };

    /// @brief RenderPass XML 에셋(어태치먼트 템플릿)입니다.
    class SW_API RenderPassResource
    {
    public:
        /** @brief 빈 렌더 패스 서술로 만듭니다. */
        RenderPassResource() = default;
        /** @brief 가상 소멸자입니다. */
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
        /** @brief 비동기 로드 태스크 본문입니다. TaskArgs 는 this · 경로 문자열입니다. */
        static void loadFromXmlFileAsyncJob( const TaskArgs& args );

    private:
        RenderPassDesc _desc;
    };
} // namespace sw
