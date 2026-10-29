/**
 * @file RenderExtension.h
 * @brief 모듈(게임 · 키트)이 렌더러를 확장하는 계약입니다 — 확장 객체(`IRenderExtension`), 패스 빌더(`ExtensionPassBuilder`), 패스 실행 컨텍스트(`ExtensionPassContext`),
 *        씬 프록시(`IExtensionSceneProxy`)입니다.
 * @details 언리얼의 `ISceneViewExtension` + `FRDGBuilder::AddPass` + `FPrimitiveSceneProxy` 와 같은 선입니다.
 *          - **확장 훅**: 모듈이 `IRenderExtension` 을 `RenderExtensionRegistration` 으로 등록하면 렌더 스레드가 프레임마다 `addPasses` 를 부릅니다.
 *          - **패스 추가**: `addPasses` 안에서 `ExtensionPassBuilder::addPass( 이름 · 읽는 첨부 · 쓰는 첨부 · 프로그램, 실행 콜백 )` 로 패스를 넣습니다. 패스는 파이프라인 XML 의 패스와
 *            **같은 `RenderGraph` 에 들어가** 같은 위상 정렬 · 배리어 추론을 탑니다 — 읽기 · 쓰기 선언이 곧 의존성이고, 상태 전이는 엔진이 넣습니다. 임시 텍스처는
 *            `createTexture` 로 요청하면 엔진이 뷰의 트랜지언트 풀에 만듭니다.
 *          - **기하**: 게임 스레드의 컴포넌트와 렌더 스레드의 `IExtensionSceneProxy` 가 갈립니다. 게임 스레드는 값을 담은 갱신 함수를 올리고(`ExtensionSceneProxyRegistration::update`),
 *            렌더 스레드가 프레임 시작에 그것을 프록시에 적용합니다. 프록시는 `collectDrawElements` 로 메시 배치(`ExtensionDrawElement`)를 내고, 그리는 것은 렌더러입니다.
 *          - **핫 리로드**: 등록을 푸는 함수는 먼저 렌더 스레드의 모듈 구간(`RenderExtensionRegistry::RenderScope`)이 끝나기를 기다린다(flush). 그 뒤에는 렌더 스레드가 모듈 코드를
 *            다시 부르지 않는다. 프레임이 끝나면 엔진이 모듈이 넣은 콜백을 모두 버린다(람다의 소멸 코드도 모듈에 있다).
 *
 *          다른 상용 엔진과의 차이 — 유니티 SRP `ScriptableRendererFeature` · 고도 `CompositorEffect` 도 같은 방향(렌더 스레드 훅 + 선언 + 실행 콜백)이다. 이 엔진의 패스 목록은 XML 이
 *          정하므로 훅 위치(`ExtensionHookStage`)는 XML 패스에 붙인 고정 지점 셋이다(언리얼은 후처리 패스 열거자마다 구독 지점이 있다).
 *
 *          모듈 코드는 **렌더 스레드(병렬 기록 워커)에서 돈다.** `ExtensionPassContext` 는 커맨드 리스트 · 풀 · 상태를 내주지 않고 검사한 연산(풀스크린 · 요소 그리기)만 연다.
 */
#pragma once
#include "Core/Container/span.h"
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Delegate/Delegate.h"
#include "Core/Foundation/Macros.h"
#include "Core/Foundation/Types.h"
#include "Core/Math/MatrixMath.h"
#include "Core/Math/VectorMath.h"
#include "Core/String/hashed_string.h"

#include "Engine/Graphics/RHI/RHITypes.h"
#include "Engine/Renderer/Pipeline/RenderPassInputSignature.h"

namespace sw
{
    class FrameRenderer;

    /** @brief 패스가 끼는 자리입니다. 파이프라인 XML 패스 목록에서 정해진 첫 자리 앞에 들어갑니다. */
    enum class ExtensionHookStage : uint8
    {
        PreRenderView,  ///< 첫 패스(그림자) 앞 — 장면을 그리기 전에 텍스처를 준비하는 패스
        PrePostProcess, ///< 후처리 효과(피사계 심도 · 모션 블러 등)나 출력 패스 앞 — 장면 색이 다 그려진 뒤, 후처리 전
        PostRenderView, ///< 출력 패스(Present) 앞 — 후처리가 끝난 뒤
    };

    /** @brief 프레임 정보입니다. 렌더 스레드가 `addPasses` · `collectDrawElements` 에 넘깁니다. */
    struct ExtensionFrameInfo
    {
        uint32 _outputWidth{ 0 };
        uint32 _outputHeight{ 0 };
    };

    /** @brief 패스를 실행하는 뷰의 정보입니다(그래프는 뷰마다 같은 패스를 실행한다). */
    struct ExtensionViewInfo
    {
        bool   _bMainView{ true };
        uint32 _width{ 0 }; ///< 이 뷰의 트랜지언트 풀 크기입니다.
        uint32 _height{ 0 };
    };

    /** @brief 패스 상수 하나입니다. 이름은 셰이더의 패스 상수 칸(PassCB) 이름입니다(바인더가 리플렉션 이름으로 맞춘다). */
    struct ExtensionConstant
    {
        hashed_string _name{};
        float4        _value{};
    };

    /** @brief 패스 하나가 그릴 때 쓰는 상수 상한입니다(패스 상수 버퍼는 크기가 정해져 있다). */
    inline constexpr uint32 kExtensionMaxConstantCount = 16;

    /**
     * @struct ExtensionProgramDesc
     * @brief 패스가 그릴 때 쓰는 셰이더와 고정 상태입니다. 셰이더 경로는 파이프라인 XML 의 `_listExtensionShader` 에 올라 있어야 합니다(쿠커가 구우므로).
     * @details 렌더 타깃 포맷은 패스의 첫 쓰기 첨부 선언이 정하고, 깊이 테스트는 패스가 깊이 첨부(`_depthAttachment`)를 선언했는지가 정합니다(어긋나면 렌더 패스가 비호환).
     *          진입점은 스테이지 기본값(VSMain · PSMain)이고 퍼뮤테이션 define 은 없습니다(쿠커가 그 한 벌만 굽는다).
     *          셰이더 경로가 비면 프로그램이 없는 패스입니다(그리기 호출은 실패하고 오류를 한 번 남긴다).
     */
    struct ExtensionProgramDesc
    {
        string _shaderPath{};
        bool   _bCullBack{ false }; ///< 메시를 그리는 패스가 후면 컬링을 쓰는지(풀스크린은 false)
        bool   _bDepthWrite{ false };
        bool   _bBlend{ false }; ///< 프리멀티플라이 알파 블렌드(색 One / InvSrcAlpha)입니다.
    };

    /** @brief 패스가 읽는 첨부 하나입니다 — 첨부 이름과, 셰이더가 읽는 역할(`g_<Role>Index`)입니다. */
    struct ExtensionPassRead
    {
        string              _attachment{};
        RenderPassInputRole _role{ RenderPassInputRole::SourceColor };
    };

    /**
     * @struct ExtensionPassParams
     * @brief 패스 하나의 선언입니다. 읽기 · 쓰기가 그래프의 의존성이고, 배리어는 이 선언만 보고 추론됩니다.
     * @details 이름은 첨부 이름입니다 — 파이프라인 XML 의 첨부, 또는 `ExtensionPassBuilder::createTexture` 가 돌려준 이름입니다. 스왑체인은 쓸 수 없습니다.
     *          첫 쓰기 첨부가 그리는 컬러 타깃이고, 깊이 첨부(`_depthAttachment`)는 쓰기에 자동으로 더해집니다.
     */
    struct ExtensionPassParams
    {
        string                    _name{}; ///< 모듈 안에서 겹치지 않는 이름입니다(그래프 노드 이름은 "Module.<이름>").
        ExtensionHookStage        _stage{ ExtensionHookStage::PrePostProcess };
        vector<ExtensionPassRead> _listRead{};
        vector<string>            _listWrite{};
        string                    _depthAttachment{};
        ExtensionProgramDesc      _program{};
    };

    /** @brief 임시 텍스처 요청입니다. 크기는 뷰의 풀 크기를 `_resolutionDivisor` 로 나눈 값(올림)입니다. */
    struct ExtensionTextureDesc
    {
        string    _name{}; ///< 요청 이름입니다. 첨부 이름은 "Module.<이름>" 입니다.
        RHIFormat _format{ RHIFormat::R8G8B8A8_UNORM };
        uint32    _resolutionDivisor{ 1 };
        float4    _clearColor{};
    };

    /** @brief 패스 실행 콜백입니다. 렌더 스레드(병렬 기록 워커)에서 불립니다. */
    class ExtensionPassContext;
    using ExtensionPassExecuteFn = Delegate<void( ExtensionPassContext& )>;

    /**
     * @class ExtensionPassBuilder
     * @brief `IRenderExtension::addPasses` 가 패스와 임시 텍스처를 선언하는 곳입니다(언리얼 `FRDGBuilder` 의 `AddPass` · `CreateTexture` 자리).
     * @details 선언을 모으기만 하고, 검사 · 그래프 편입 · PSO 만들기 · 텍스처 할당은 엔진이 기록 전에 합니다. 선언이 잘못된 패스는 이유를 오류로 남기고 그려지지 않습니다.
     */
    class ExtensionPassBuilder
    {
    public:
        struct Entry
        {
            ExtensionPassParams    _params{};
            ExtensionPassExecuteFn _execute{};
        };

        /** @brief 임시 텍스처를 요청하고 그 첨부 이름("Module.<이름>")을 돌려줍니다. 패스의 읽기 · 쓰기에 이 이름을 씁니다. 같은 이름을 다시 요청하면 앞 요청을 대체합니다. */
        string createTexture( const ExtensionTextureDesc& desc )
        {
            ExtensionTextureDesc stored = desc;
            stored._name                = string( "Extension." ) + desc._name;
            for ( ExtensionTextureDesc& existing : _listTexture )
            {
                if ( existing._name == stored._name )
                {
                    existing = stored;
                    return stored._name;
                }
            }
            _listTexture.push_back( stored );
            return stored._name;
        }

        /** @brief 패스를 더합니다. 같은 프레임에 더한 순서가 같은 자리(`_stage`)의 패스끼리의 선언 순서입니다(쓰기 사슬 순서). */
        void addPass( ExtensionPassParams params, ExtensionPassExecuteFn execute ) { _listEntry.push_back( Entry{ std::move( params ), std::move( execute ) } ); }

        const vector<Entry>&                getEntries() const { return _listEntry; }
        const vector<ExtensionTextureDesc>& getTextures() const { return _listTexture; }
        /** @brief 콜백과 선언을 모두 놓습니다. 엔진이 프레임이 끝날 때 부른다(모듈 코드의 소멸 코드가 모듈 구간 안에서 돌게). */
        void clear()
        {
            _listEntry.clear();
            _listTexture.clear();
        }

    private:
        vector<Entry>                _listEntry;
        vector<ExtensionTextureDesc> _listTexture;
    };

    /** @brief 메시 배치 요소 하나입니다 — 메시 하나를 월드 행렬 하나로 인스턴스 수만큼 그립니다. 모두 값입니다. */
    struct ExtensionDrawElement
    {
        static constexpr uint32 kMaxConstantCount = 4;
        static constexpr uint32 kMaxInstanceCount = 4096;

        /// @brief 메시 리소스 ID입니다 — 내장 도형 이름("Cube" 등) 또는 `.mesh` 경로(`MeshComponent::_meshID` 와 같은 철자). 비면 요소가 거절됩니다.
        hashed_string     _mesh{};
        float4x4          _world{}; ///< 기본은 단위 행렬입니다.
        uint32            _instanceCount{ 1 };
        ExtensionConstant _arrConstant[kMaxConstantCount]{}; ///< 이 요소를 그릴 때만 쓰는 패스 상수입니다.
        uint32            _constantCount{ 0 };
    };

    /** @brief `IExtensionSceneProxy::collectDrawElements` 가 요소를 내는 곳입니다. 규칙을 어긴 요소는 오류를 남기고 거절합니다. */
    class SW_API ExtensionDrawCollector
    {
    public:
        /** @brief 요소를 더합니다. 메시 ID 가 비었거나, 인스턴스 수가 범위 밖이거나, 상수가 상한을 넘으면 거절하고 false 입니다. */
        [[nodiscard]] bool                  add( const ExtensionDrawElement& element );
        const vector<ExtensionDrawElement>& getElements() const { return _listElement; }

    private:
        vector<ExtensionDrawElement> _listElement;
    };

    /**
     * @class IExtensionSceneProxy
     * @brief 렌더 스레드 쪽 기하 프록시입니다(언리얼 `FPrimitiveSceneProxy`). 게임 스레드의 컴포넌트가 가진 값을 **복사해** 이 객체에 넘기고, 렌더 스레드는 이 객체만 읽습니다.
     * @details 프록시는 모듈이 만들고(`ExtensionSceneProxyRegistration`) 레지스트리가 소유합니다. `collectDrawElements` 는 렌더 스레드에서 프레임당 한 번 불리고, 프록시 상태는
     *          `update` 로 올린 함수만 바꿉니다(프레임 시작에 렌더 스레드가 적용 — 렌더 스레드가 읽는 동안 게임 스레드가 쓰는 일이 없다).
     */
    class IExtensionSceneProxy
    {
    public:
        virtual ~IExtensionSceneProxy() = default;
        /** @brief 이번 프레임에 그릴 요소를 냅니다(언리얼 `GetDynamicMeshElements`). 렌더 스레드에서 불립니다. */
        virtual void collectDrawElements( ExtensionDrawCollector& out, const ExtensionFrameInfo& frame ) const = 0;
    };

    /**
     * @class IRenderExtension
     * @brief 렌더 스레드 확장 객체입니다(언리얼 `ISceneViewExtension`). 모듈이 구현하고 `RenderExtensionRegistration` 으로 등록합니다.
     */
    class IRenderExtension
    {
    public:
        virtual ~IRenderExtension() = default;
        /** @brief 이 프레임에 넣을 패스를 선언합니다. 렌더 스레드에서 프레임당 한 번, 기록 전에 불립니다. 이 안에서 등록을 풀거나 레지스트리 잠금을 잡지 않습니다. */
        virtual void addPasses( ExtensionPassBuilder& builder, const ExtensionFrameInfo& frame ) = 0;
    };

    /**
     * @class ExtensionPassContext
     * @brief 실행 콜백이 받는 컨텍스트입니다. 커맨드 리스트를 내주지 않고, 선언한 첨부 · 프로그램 위에서 검사한 그리기만 합니다.
     * @details 그리기 호출은 읽는 첨부를 역할로 바인딩하고, 첫 쓰기 첨부(와 깊이 첨부)로 렌더 패스를 열고, 상수를 값으로 쓰고, 그린 뒤 렌더 패스를 닫습니다.
     *          선언하지 않은 첨부를 건드릴 길이 없으므로 그래프가 넣은 배리어가 항상 맞습니다. 프로그램이 없거나 PSO 를 만들지 못한 패스는 오류를 한 번 남기고 false 입니다.
     */
    class SW_API ExtensionPassContext
    {
    public:
        /** @brief 이 패스를 실행하는 뷰입니다. */
        const ExtensionViewInfo& getView() const { return _view; }
        /** @brief 풀스크린 삼각형을 그립니다(정점은 셰이더가 `SV_VertexID` 로 만든다). @p constants 는 패스 상수입니다(최대 `kExtensionMaxConstantCount`). */
        [[nodiscard]] bool drawFullscreen( vector_reference<const ExtensionConstant> constants );
        /** @brief 이번 프레임에 모든 프록시가 낸 요소를 그립니다(요소마다 월드 행렬 · 상수 · 인스턴스 수). 메시가 없는 요소는 오류를 한 번 남기고 건너뜁니다. */
        [[nodiscard]] bool drawProxyElements( vector_reference<const ExtensionConstant> constants );

    private:
        friend class FrameRenderer;
        /// @brief 엔진 내부 상태입니다(`FramePassContext` · `ExtensionFramePass`). 형식을 헤더에 드러내지 않으려 불투명 포인터로 둔다.
        ExtensionPassContext( FrameRenderer& renderer, void* pPassContext, const void* pPass, const ExtensionViewInfo& view )
            : _pRenderer{ &renderer }
            , _pPassContext{ pPassContext }
            , _pPass{ pPass }
            , _view{ view }
        {
        }

        FrameRenderer*    _pRenderer;
        void*             _pPassContext;
        const void*       _pPass;
        ExtensionViewInfo _view;
    };
} // namespace sw
