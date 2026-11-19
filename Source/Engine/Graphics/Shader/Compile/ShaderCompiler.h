/**
 * @file ShaderCompiler.h
 * @brief DXC 와 D3DCompiler 로 HLSL 을 여러 백엔드용으로 컴파일하는 셰이더 컴파일러 인터페이스입니다.
 */
#pragma once
#include "Core/Container/EnumArray.h"
#include "Core/String/StringUtil.h"

#include "Engine/EngineMinimal.h"
#include "Engine/Graphics/Shader/Compile/ShaderSource.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @enum ShaderStage
     * @brief 셰이더 파이프라인 스테이지 종류입니다(배열 인덱싱과 컴파일 단위).
     */
    ENUM()
    enum class ShaderStage : uint8
    {
        Vertex,        ///< 버텍스 셰이더 (VS)
        Pixel,         ///< 픽셀/프래그먼트 셰이더 (PS/FS)
        Compute,       ///< 컴퓨트 셰이더 (CS)
        Geometry,      ///< 지오메트리 셰이더 (GS)
        Hull,          ///< 헐/테셀레이션 제어 셰이더 (HS/TCS)
        Domain,        ///< 도메인/테셀레이션 평가 셰이더 (DS/TES)
        Mesh,          ///< 메시 셰이더 (MS)
        Amplification, ///< 앰플리피케이션/태스크 셰이더 (AS/TS)
        Count
    };

    /**
     * @enum ShaderStageFlag
     * @brief 리소스 바인딩 가시성과 파이프라인 스테이지 조합용 비트 플래그입니다.
     */
    ENUM( Flags )
    enum class ShaderStageFlag : uint8
    {
        None          = 0,
        Vertex        = SW_BIT( 0 ),
        Pixel         = SW_BIT( 1 ),
        Compute       = SW_BIT( 2 ),
        Geometry      = SW_BIT( 3 ),
        Hull          = SW_BIT( 4 ),
        Domain        = SW_BIT( 5 ),
        Mesh          = SW_BIT( 6 ),
        Amplification = SW_BIT( 7 ),

        AllGraphics = Vertex | Pixel | Geometry | Hull | Domain | Mesh | Amplification,
        All         = AllGraphics | Compute
    };

    constexpr ShaderStageFlag toShaderStageFlag( ShaderStage stage ) noexcept
    {
        return ( stage < ShaderStage::Count )
                 ? static_cast<ShaderStageFlag>( SW_BIT( static_cast<uint8>( stage ) ) )
                 : ShaderStageFlag::None;
    }

    /**
     * @struct ShaderStageInfo
     * @brief 스테이지 하나의 테이블 항목입니다. 축약 태그, 기본 진입점, 컴파일 프로파일(SM5 / SM6)을 담습니다.
     * @details 쿠커의 태그와 기본 진입점, 컴파일러의 프로파일, 바인딩 레이아웃의 비트 변환이 모두 이 테이블을 본다(switch 를
     *          파일마다 두지 않는다). 스테이지를 추가하려면 열거형과 이 테이블에 한 줄씩 더한다. `_pProfileSm5` 가 nullptr 이면
     *          SM5(DXBC, D3D11)에 그 스테이지가 없다는 뜻이다.
     */
    struct ShaderStageInfo
    {
        const utf8* _pTag;        ///< 파일 이름과 로그에 쓰는 축약("vs")
        const utf8* _pEntryPoint; ///< 기본 진입점("VSMain")
        const utf8* _pProfileSm5; ///< DXBC(D3D11) 프로파일("vs_5_0"). 없으면 nullptr
        const utf8* _pProfileSm6; ///< DXIL과 SPIR-V 프로파일("vs_6_6")
        const utf8* _pStageMacro; ///< 컴파일러가 이 단계 컴파일에 넣는 매크로("SW_SHADER_STAGE_VERTEX") — 셰이더가 단계마다 갈리는 길을 한 곳에서 고른다(머티리얼 샘플의 밉 바이어스)
    };
} // namespace sw

namespace sw
{
    /** @brief 스테이지의 테이블 항목입니다. `Count` 이상이면 버텍스 항목을 반환합니다(폴백). */
    inline const ShaderStageInfo& getShaderStageInfo( ShaderStage stage ) noexcept
    {
        static constexpr auto kArrInfo = makeEnumTable<ShaderStage, ShaderStageInfo>( {
            {"vs", "VSMain", "vs_5_0", "vs_6_6",        "SW_SHADER_STAGE_VERTEX"}, // Vertex
            {"ps", "PSMain", "ps_5_0", "ps_6_6",         "SW_SHADER_STAGE_PIXEL"}, // Pixel
            {"cs", "CSMain", "cs_5_0", "cs_6_6",       "SW_SHADER_STAGE_COMPUTE"}, // Compute
            {"gs", "GSMain", "gs_5_0", "gs_6_6",      "SW_SHADER_STAGE_GEOMETRY"}, // Geometry
            {"hs", "HSMain", "hs_5_0", "hs_6_6",          "SW_SHADER_STAGE_HULL"}, // Hull
            {"ds", "DSMain", "ds_5_0", "ds_6_6",        "SW_SHADER_STAGE_DOMAIN"}, // Domain
            {"ms", "MSMain",  nullptr, "ms_6_6",          "SW_SHADER_STAGE_MESH"}, // Mesh. SM5 에는 없다
            {"as", "ASMain",  nullptr, "as_6_6", "SW_SHADER_STAGE_AMPLIFICATION"}, // Amplification. SM5 에는 없다
        } );
        return kArrInfo[( stage < ShaderStage::Count ) ? stage : ShaderStage::Vertex];
    }

    /** @brief @p entryPoint 가 비어 있으면 스테이지의 기본 진입점(테이블의 `_pEntryPoint`)을 반환합니다. 진입점 기본값은 이 테이블 하나가 정합니다. */
    inline string_view resolveEntryPoint( const string_view entryPoint, const ShaderStage stage ) noexcept
    {
        return entryPoint.empty() ? string_view( getShaderStageInfo( stage )._pEntryPoint ) : entryPoint;
    }

    /**
     * @enum ShaderTargetFormat
     * @brief 컴파일 출력 바이트코드 포맷입니다.
     */
    enum class ShaderTargetFormat : uint8
    {
        DXBC_D3D11,   ///< Direct3D 11용 DXBC 바이트코드(d3dcompiler 사용)
        DXIL_D3D12,   ///< Direct3D 12용 DXIL 바이트코드(dxc 사용)
        SPIRV_Vulkan, ///< Vulkan 1.2용 SPIR-V 바이트코드(dxc -spirv 사용)
        SPIRV_OpenGL, ///< OpenGL용 SPIR-V 바이트코드(dxc -spirv 사용)
        WGSL_WebGPU,  ///< WebGPU 용 WGSL. dxc -spirv 로 낸 SPIR-V 를 Tint 가 WGSL 로 옮기고, 둘을 한 꾸러미에 담는다(`WebGPUShaderContainer` — 리플렉션은 안의 SPIR-V 를 읽는다)
        Count
    };

    /** @brief 그 형식을 내는 컴파일러입니다. */
    enum class ShaderCompilerTool : uint8
    {
        Fxc, ///< d3dcompiler(SM5 프로파일, Windows 전용)
        Dxc, ///< dxc(SM6 프로파일)
    };

    /** @brief 대상 형식 하나의 공통 사실입니다. DXC 인자 · 후처리는 컴파일러 안의 표(`ShaderCompilerInternal::kArrDxcTargetRow`)가 같은 순서로 든다. */
    struct ShaderTargetFormatInfo
    {
        ShaderTargetFormat _format;
        ShaderCompilerTool _tool;
        const utf8*        _pExtension;          ///< 쿠킹 바이너리 확장자. SPIR-V 둘은 같아야 한다
        uint8              _bSpirv          : 1; ///< 리플렉션 · 바인딩 검사가 SPIR-V 를 읽는다(WGSL 꾸러미는 안의 SPIR-V 를 꺼내 읽는다 — `WebGPUShaderContainer::findSpirv`)
        uint8              _bOptionalStages : 1; ///< 헐 · 도메인 · 지오메트리 스테이지가 있다 — WebGPU 는 셋 다 없어(명세) 쿠커가 그 스테이지 요청을 이 형식으로 굽지 않는다(런타임도 그 PSO 를 만들지 않는다)
    };

    /** @brief 형식 표입니다. 새 대상(WebGPU 의 SPIR-V → WGSL)은 여기와 컴파일러 표에 한 줄씩이다(언리얼 `IShaderFormat` 자리, 기억). 행 타입 안에는 자기 타입의 constexpr 배열을 둘 수 없어 표 타입이 따로다. */
    struct ShaderTargetFormatTable
    {
        static constexpr ShaderTargetFormatInfo kArrRow[] = {
            {  ShaderTargetFormat::DXBC_D3D11, ShaderCompilerTool::Fxc, ".dxbc", 0, 1},
            {  ShaderTargetFormat::DXIL_D3D12, ShaderCompilerTool::Dxc, ".dxil", 0, 1},
            {ShaderTargetFormat::SPIRV_Vulkan, ShaderCompilerTool::Dxc,  ".spv", 1, 1},
            {ShaderTargetFormat::SPIRV_OpenGL, ShaderCompilerTool::Dxc,  ".spv", 1, 1},
            { ShaderTargetFormat::WGSL_WebGPU, ShaderCompilerTool::Dxc, ".wgsl", 1, 0},
        };

        /** @brief 형식의 행입니다. `Count` 이상이면 nullptr 입니다. */
        static constexpr const ShaderTargetFormatInfo* find( ShaderTargetFormat format ) noexcept
        {
            return format < ShaderTargetFormat::Count ? &kArrRow[static_cast<size_t>( format )] : nullptr;
        }

        /** @brief 행이 열거 순서로 하나씩 있는지 봅니다. */
        static constexpr bool isInEnumOrder() noexcept
        {
            size_t index = 0;
            for ( const ShaderTargetFormatInfo& row : kArrRow )
            {
                if ( static_cast<size_t>( row._format ) != index )
                    return false;
                ++index;
            }
            return index == static_cast<size_t>( ShaderTargetFormat::Count );
        }
    };
    static_assert( ShaderTargetFormatTable::isInEnumOrder(), "ShaderTargetFormatTable needs one row per ShaderTargetFormat, in enumerator order" );

    /**
     * @struct ShaderMacroDefine
     * @brief 셰이더 전처리 매크로 정의입니다(NAME=VALUE).
     */
    struct ShaderMacroDefine
    {
        string _name;
        string _value;

        /**
         * @brief `"NAME=VALUE"` 또는 `"NAME"`(값 1) 한 줄을 매크로로 해석합니다.
         * @details PSO 생성 경로(DX11, DX12, GL)와 바인딩 레이아웃 캐시가 같은 문자열 형식을 쓰므로 파싱은 여기 하나다.
         */
        static ShaderMacroDefine parse( string_view define )
        {
            string_view name  = StringUtil::trim( define );
            string_view value = "1";
            (void)StringUtil::trySplitKeyValue( define, name, value ); // `=` 가 없으면 이름만 있는 매크로(값 1)다
            return ShaderMacroDefine{ string( name ), string( value ) };
        }
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ShaderEnginePolicy
     * @brief 엔진 설정 중 셰이더 컴파일에 들어가는 값의 사본입니다(`ShaderCompiler::appendEngineDefines` 가 define 으로 펼친다).
     * @details 설정은 다시 읽기가 제자리에서 덮고 그 울타리(`ConfigManager::setWriteFence`)는 렌더 스레드만 기다린다 — 컴파일 워커가 `EngineConfig` 를 직접 읽으면 덮기와 겹친다.
     *          그래서 요청을 만드는 쪽(게임 · 렌더 스레드의 `ShaderCache`, 쿠킹 시작)이 `capture` 로 사본을 떠 요청에 실어 보낸다(언리얼이 셰이더 컴파일 환경을 잡에 값으로 싣는 것과 같다).
     */
    struct ShaderEnginePolicy
    {
        float32 _taaHistoryWeight;            ///< `SW_TAA_HISTORY_WEIGHT` — `EngineConfig::_render._taaHistoryWeight`
        float32 _probeNormalBias;             ///< `SW_PROBE_NORMAL_BIAS` — `EngineConfig::_render._probeVisibility._normalBias`
        float32 _probeViewBias;               ///< `SW_PROBE_VIEW_BIAS` — `_probeVisibility._viewBias`
        float32 _probeBackfaceFloor;          ///< `SW_PROBE_BACKFACE_FLOOR` — `_probeVisibility._backfaceFloor`
        float32 _probeChebyshevExponent;      ///< `SW_PROBE_CHEBYSHEV_EXPONENT` — `_probeVisibility._chebyshevExponent`
        float32 _taaMinHistoryWeight;         ///< `SW_TAA_MIN_HISTORY_WEIGHT` — `_render._taaMinHistoryWeight`
        float32 _taaVarianceClipGamma;        ///< `SW_TAA_VARIANCE_CLIP_GAMMA` — `_render._taaVarianceClipGamma`
        float32 _taaVelocityRejectPixels;     ///< `SW_TAA_VELOCITY_REJECT_PIXELS` — `_render._taaVelocityRejectPixels`
        float32 _autoExposureLowPercent;      ///< `SW_AUTO_EXPOSURE_LOW_PERCENT` — `_render._autoExposureLowPercent`
        float32 _autoExposureHighPercent;     ///< `SW_AUTO_EXPOSURE_HIGH_PERCENT` — `_render._autoExposureHighPercent`
        float32 _autoExposureHistogramLogMin; ///< `SW_AUTO_EXPOSURE_HISTOGRAM_LOG_MIN` — `_render._autoExposureHistogramLogMin`
        float32 _autoExposureHistogramLogMax; ///< `SW_AUTO_EXPOSURE_HISTOGRAM_LOG_MAX` — `_render._autoExposureHistogramLogMax`

        /** @brief 엔진 설정에서 사본을 뜹니다. 설정 덮기와 겹치지 않는 스레드(게임 스레드 · 렌더 스레드)에서만 부른다. */
        [[nodiscard]] static ShaderEnginePolicy capture();
    };

    /**
     * @struct ShaderCompileDesc
     * @brief 셰이더 컴파일 요청 디스크립터입니다.
     */
    struct ShaderCompileDesc
    {
        ShaderSourceID            _source;                                          ///< HLSL 소스의 리소스 경로(디스크 경로가 아니다 — 쿠커도 리소스 경로를 넣는다)
        string                    _entryPoint;                                      ///< 진입점 함수 이름(예: "VSMain", "CSMain")
        vector<ShaderMacroDefine> _listDefine;                                      ///< 추가 전처리 매크로(-D NAME=VALUE)
        ShaderStage               _stage        = ShaderStage::Vertex;              ///< 컴파일 대상 셰이더 스테이지
        ShaderTargetFormat        _targetFormat = ShaderTargetFormat::SPIRV_Vulkan; ///< 출력 포맷
        /**
         * @brief 디버그 정보를 넣고 최적화를 끄는(`-Zi -Od` / `D3DCOMPILE_DEBUG|SKIP_OPTIMIZATION`) 컴파일인지 나타냅니다.
         * @details **요청하는 쪽**이 정한다. 주의: **빌드 구성**(SW_DEBUG)이 정하면 쿠킹된 바이너리(저장소에 커밋되고 배포에
         *          실리는 것)의 내용이 "어느 App.exe 가 쿠커로 돌았나" 에 따라 달라지고, 쿠커가 Debug 를 먼저 집으면
         *          커밋된 셰이더가 모두 무최적화가 된다. 런타임
         *          라이브 컴파일(ShaderCache)은 Debug 에서 켜고, 쿠커는 절대 켜지 않는다.
         */
        uint8 _bDebugCodegen = SW_FALSE;
        /**
         * @brief 이 컴파일에 들어가는 엔진 설정 값의 사본입니다(`ShaderEnginePolicy`). 컴파일(`ShaderCompiler::compileHLSL`)은 이것이 없으면 오류로 진다 — 워커가 설정을 직접 읽어 채우지 않는다.
         * @details 요청을 만드는 쪽이 비워 두면 `ShaderCache` 가 요청을 받을 때 자기 사본을 싣고, 쿠커는 쿠킹 시작에 뜬 사본을 싣는다. 메모리 캐시 키에는 넣지 않는다(지문이 섞는다).
         */
        optional<ShaderEnginePolicy> _enginePolicy;
    };
} // namespace sw

namespace sw
{
    /**
     * @struct ShaderCompileResult
     * @brief 셰이더 컴파일 결과입니다.
     */
    struct ShaderCompileResult
    {
        vector<uint8> _bytecode;     ///< 컴파일된 바이트코드
        string        _errorMessage; ///< 실패했을 때의 컴파일러 오류 메시지
        /**
         * @brief 컴파일러의 `#include` 해석이 기댄 파일입니다(해석된 경로, `/` 구분자, 중복 없이 정렬). 실제로 연 헤더와, **찾아봤지만 없던 후보**가 함께 든다. 주 소스 파일은 넣지 않습니다.
         * @details 쿠킹 최신 판정과 컴파일 캐시가 "이 셰이더가 어느 파일의 상태에 기대는가" 를 이 목록으로 안다. 바이트코드의 리플렉션은 바인딩 정보라
         *          어느 헤더를 열었는지 담지 못해서, 소스를 읽는 순간 include 처리기가 적는다. 없던 후보는 검색 순서의 앞자리다 — 거기에 같은 이름의 헤더가 생기면
         *          다음 컴파일은 그것을 연다(가림). 그래서 "없음" 도 의존이다(빌드 도구의 부재 의존과 같다). 판정은 경로의 지금 상태로 본다: 있으면 내용 해시, 없으면 없어야 한다.
         *          디스크 캐시에 맞았을 때는 캐시와 함께 저장해 둔 목록이 담긴다. 컴파일에 실패한 결과의 목록은 믿지 않는다(비어 있을 수 있다).
         */
        vector<string> _listIncludedFile;
        /** @brief 컴파일한 주 소스의 절대 경로입니다(해석기 `ShaderSourceResolver` 의 답). 메모리 캐시가 이것과 `_listIncludedFile` 로 낡음을 본다. 쿠킹본에서 읽은 결과는 비어 있다. */
        string _sourceFile;
        bool   _bSuccess{ false }; ///< 컴파일 성공 여부 NOLINT(sw-bool-flag): 참조(`bool&`)로 넘긴다
    };
} // namespace sw

namespace sw
{
    /**
     * @class ShaderCompiler
     * @brief HLSL 소스를 RHI 백엔드별 바이트코드(DXIL, SPIR-V, DXBC)로 컴파일하는 파사드입니다.
     */
    class SW_API ShaderCompiler
    {
    public:
        /**
         * @brief HLSL 셰이더를 컴파일합니다. 이 기계의 셰이더 DDC(`Saved/ShaderCompileCache` — 셰이더 디스크 캐시는 이것 하나다)에 맞으면 컴파일하지 않습니다.
         * @param desc 컴파일 디스크립터. `_enginePolicy` 가 없으면 컴파일하지 않고 실패를 돌려준다(엔진 설정을 이 스레드에서 읽지 않는다).
         * @return 컴파일 결과(성공 여부와 바이트코드)
         * @details 스레드 계약: 워커에서 동시에 불린다(병렬 쿠킹). 공유 상태는 읽기 전용(`AssetPaths` 읽기)이거나 원자값이다.
         *          서로 다른 @p desc 만 동시에 넣는다 — 같은 셰이더 변형을 두 스레드가 동시에 컴파일하면 같은 캐시 파일을 함께 쓴다.
         */
        static ShaderCompileResult compileHLSL( const ShaderCompileDesc& desc );

        /**
         * @brief 엔진이 모든 컴파일에 넣는 define(엔진 텍스처 칸 표 `SW_EMU_SLOT_<이름>=<칸>` · `SW_ENGINE_TEXTURE_INDEX_<칸>=g_<이름>Index`, 렌더 정책 `SW_TAA_*` · `SW_PROBE_*` · `SW_AUTO_EXPOSURE_*`)을 @p inoutList 뒤에 더합니다.
         * @details 렌더 정책 값은 `EngineConfig::_render`(Config/Engine/EngineConfig.json)가 정본이다 — 셰이더가 읽는 정책 값은 이 한 경로로만 간다(셰이더에 기본값이 없다).
         *          값은 요청이 실어 온 사본 @p enginePolicy 다 — 컴파일 워커에서 불리므로 설정을 직접 읽지 않는다.
         * @details 요청에 없고 엔진 바이너리가 정하는 컴파일 입력이라 도구 지문(`ShaderCookStamp::getToolchainFingerprint`)이 이 목록을 섞는다 —
         *          그래야 표가 바뀌면 모든 셰이더 캐시(실행 캐시 · 컴파일 캐시 · 쿠킹 도장)가 함께 낡는다. 요청 desc 로 키를 만드는 캐시가 이 값을 몰라
         *          옛 칸 번호의 바이트를 새것으로 돌려준 결함(DX11 · GL 검정 화면)이 있었다.
         */
        static void appendEngineDefines( vector<ShaderMacroDefine>& inoutList, const ShaderEnginePolicy& enginePolicy );

        /**
         * @brief DXC 의 `DxcCreateInstance` 주소입니다(`DxcCreateInstanceProc` 로 바꿔 쓴다). 라이브러리는 처음 부를 때 실행 파일 옆에서 한 번 로드한다. 없으면 nullptr 입니다.
         * @details 컴파일과 DXIL 리플렉션이 함께 쓰는 한 입구다. 핸들은 일부러 내리지 않는다 - 만든 DXC 객체(COM)가 프로세스 끝까지 쓰일 수 있고
         *          함수 정적의 소멸 순서에서 내리면 로더 잠금 아래 FreeLibrary 가 된다.
         */
        [[nodiscard]] static void* findDXCCreateInstance();
    };
} // namespace sw
