/**
 * @file ShaderCompiler.h
 * @brief DirectX Shader Compiler (DXC) 및 D3DCompiler를 이용한 크로스 백엔드 HLSL 셰이더 컴파일러 인터페이스
 */
#pragma once
#include "Engine/EngineMinimal.h"
#include "Engine/Reflection/ReflectionMacros.h"

namespace sw
{
    /**
     * @enum ShaderStage
     * @brief 단일 셰이더 파이프라인 스테이지 종류 (배열 인덱싱 및 컴파일 단위)
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
     * @brief 리소스 바인딩 가시성(Visibility) 및 파이프라인 스테이지 조합용 비트플래그
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
     * @enum ShaderTargetFormat
     * @brief 컴파일 출력 타깃 바이트코드 포맷
     */
    enum class ShaderTargetFormat : uint8
    {
        DXBC_D3D11,   ///< Direct3D 11용 DXBC 바이트코드 (d3dcompiler 사용)
        DXIL_D3D12,   ///< Direct3D 12용 DXIL 바이트코드 (dxc 사용)
        SPIRV_Vulkan, ///< Vulkan 1.3용 SPIR-V 바이트코드 (dxc -spirv 사용)
        SPIRV_OpenGL, ///< OpenGL용 SPIR-V 바이트코드 (dxc -spirv 사용)
        Count
    };

    /**
     * @struct ShaderMacroDefine
     * @brief 셰이더 전처리 매크로 정의 (NAME=VALUE)
     */
    struct ShaderMacroDefine
    {
        string _name;
        string _value;
    };

    /**
     * @struct ShaderCompileDesc
     * @brief 셰이더 컴파일 요청 서술체
     */
    struct ShaderCompileDesc
    {
        string                    _filePath;                                        ///< HLSL 소스 파일 경로 (상대/절대 경로)
        string                    _entryPoint;                                      ///< 진입점 함수 이름 (예: "VSMain", "CSMain")
        vector<ShaderMacroDefine> _listDefine;                                      ///< 추가 전처리 매크로 (-D NAME=VALUE)
        ShaderStage               _stage        = ShaderStage::Vertex;              ///< 컴파일 대상 셰이더 스테이지
        ShaderTargetFormat        _targetFormat = ShaderTargetFormat::SPIRV_Vulkan; ///< 출력 포맷
    };

    /**
     * @struct ShaderCompileResult
     * @brief 셰이더 컴파일 결과 객체
     */
    struct ShaderCompileResult
    {
        vector<uint8> _bytecode;               ///< 컴파일된 이진 바이트코드 데이터
        string        _errorMessage;           ///< 실패 시 오류 컴파일러 메세지
        string        _normalizedRelativePath; ///< 정규화된 자원 상대 경로
        bool          _bSuccess{ false };      ///< 컴파일 성공 여부
    };

    /**
     * @class ShaderCompiler
     * @brief HLSL 소스를 각 RHI 백엔드 전용 바이트코드(DXIL, SPIR-V, DXBC)로 동적 컴파일하는 파사드 클래스
     */
    class SW_API ShaderCompiler
    {
    public:
        /**
         * @brief HLSL 셰이더 컴파일 실행 (디스크 캐시 활성화 시 바이트코드 캐시 활용)
         * @param desc 컴파일 서술체
         * @return 컴파일 결과 (성공 여부 및 바이트코드 배열)
         */
        static ShaderCompileResult compileHLSL( const ShaderCompileDesc& desc );

        /** @brief 셰이더 바이트코드 디스크 캐시 활성화 여부를 설정합니다. */
        static void enableDiskCache( bool bEnable );

        /** @brief 셰이더 바이트코드 디스크 캐시 활성화 여부를 반환합니다. */
        static bool isDiskCacheEnabled();

        /** @brief 저장된 셰이더 디스크 캐시 파일들을 모두 삭제합니다. */
        static void clearDiskCache();
    };
} // namespace sw
