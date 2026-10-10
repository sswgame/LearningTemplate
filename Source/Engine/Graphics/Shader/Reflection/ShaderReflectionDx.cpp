#include "pch.h"

#include "Engine/Common/EnginePlatformHeaders.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"
#include "Engine/Graphics/Shader/Reflection/ShaderReflectionUtil.h"

#if defined( SW_HAS_DXC_API )
    #include <dxcapi.h>
#endif

#if defined( SW_PLATFORM_WINDOWS )
namespace sw
{
    namespace
    {
        struct ShaderReflectionDxInternal
        {
            static const utf8* resourceTypeName( uint32 sit )
            {
                switch ( sit )
                {
                    case static_cast<uint32>( D3D_SIT_TEXTURE ):
                        return "Texture";
                    case static_cast<uint32>( D3D_SIT_SAMPLER ):
                        return "Sampler";
                    case static_cast<uint32>( D3D_SIT_CBUFFER ):
                        return "ConstantBuffer";
                    case static_cast<uint32>( D3D_SIT_STRUCTURED ):
                    case static_cast<uint32>( D3D_SIT_BYTEADDRESS ):
                        return "StructuredBuffer";
                    case static_cast<uint32>( D3D_SIT_UAV_RWTYPED ):
                        return "RWTexture"; // RWTexture2D 등. 계약이 버퍼 UAV 와 구분한다(RWBuffer<T> 는 쓰지 않는다)
                    case static_cast<uint32>( D3D_SIT_UAV_RWSTRUCTURED ):
                    case static_cast<uint32>( D3D_SIT_UAV_RWBYTEADDRESS ):
                        return "UAV";
                    default:
                        return "OtherResource";
                }
            }

            static string hlslVariableTypeName( D3D_SHADER_VARIABLE_CLASS varClass, D3D_SHADER_VARIABLE_TYPE varType,
                                                uint32 rows, uint32 columns )
            {
                const utf8* pBase = "Float";
                switch ( static_cast<uint32>( varType ) )
                {
                    case static_cast<uint32>( D3D_SVT_FLOAT ):
                    {
                        pBase = "Float";
                        break;
                    }
                    case static_cast<uint32>( D3D_SVT_INT ):
                    {
                        pBase = "Int";
                        break;
                    }
                    case static_cast<uint32>( D3D_SVT_UINT ):
                    case static_cast<uint32>( D3D_SVT_UINT8 ):
                    {
                        pBase = "Uint";
                        break;
                    }
                    case static_cast<uint32>( D3D_SVT_BOOL ):
                    {
                        return "Bool";
                    }
                    default:
                    {
                        break;
                    }
                }

                if ( varClass == D3D_SVC_MATRIX_ROWS || varClass == D3D_SVC_MATRIX_COLUMNS )
                {
                    if ( rows == 4 && columns == 4 && varType == D3D_SVT_FLOAT )
                        return string( "Float4x4" );
                    return string( pBase ) + to_string( rows ) + "x" + to_string( columns );
                }
                if ( varClass == D3D_SVC_VECTOR )
                {
                    if ( columns <= 1 )
                        return string( pBase );
                    return string( pBase ) + to_string( columns );
                }
                return string( pBase );
            }

            /**
             * @brief 타입 하나가 cbuffer 안에서 차지하는 바이트 수입니다(HLSL 패킹 규칙).
             * @details 리플렉션의 타입 서술에는 크기가 없고 변수 서술에만 있습니다. 구조체 **멤버**를 펼칠 때는
             *          여기서 계산합니다. 규칙: 배열 원소와 구조체는 16바이트 정렬, 마지막 원소 뒤 패딩은 없습니다
             *          (D3D 가 변수 Size 로 보고하는 값과 같은 규칙. `float arr[3]` 은 36, `float4 arr[6]` 은 96).
             */
            template <typename TType, typename TTypeDesc>
            static uint32 computePackedSize( TType* pType )
            {
                if ( pType == nullptr )
                    return 0;
                TTypeDesc desc{};
                pType->GetDesc( &desc );

                uint32 elementSize{ 0 };
                if ( desc.Class == D3D_SVC_STRUCT )
                {
                    for ( UINT memberIndex = 0; memberIndex < desc.Members; ++memberIndex )
                    {
                        TType* pMember = pType->GetMemberTypeByIndex( memberIndex );
                        if ( pMember == nullptr )
                            continue;
                        TTypeDesc memberDesc{};
                        pMember->GetDesc( &memberDesc );
                        const uint32 memberEnd = memberDesc.Offset + computePackedSize<TType, TTypeDesc>( pMember );
                        elementSize            = memberEnd > elementSize ? memberEnd : elementSize;
                    }
                }
                else
                {
                    const uint32 rows    = desc.Rows > 0 ? desc.Rows : 1;
                    const uint32 columns = desc.Columns > 0 ? desc.Columns : 1;
                    if ( desc.Class == D3D_SVC_MATRIX_ROWS || desc.Class == D3D_SVC_MATRIX_COLUMNS )
                    {
                        // 행렬은 벡터 하나가 16바이트 레지스터 하나를 쓴다(row_major 면 행, 아니면 열). 마지막 벡터만 실제 폭이다.
                        const uint32 vectorCount = ( desc.Class == D3D_SVC_MATRIX_ROWS ) ? rows : columns;
                        const uint32 vectorWidth = ( desc.Class == D3D_SVC_MATRIX_ROWS ) ? columns : rows;
                        elementSize              = ( vectorCount - 1 ) * 16 + vectorWidth * 4;
                    }
                    else
                    {
                        elementSize = rows * columns * 4;
                    }
                }

                if ( desc.Elements > 1 )
                {
                    const uint32 stride = ( elementSize + 15u ) & ~15u;
                    return ( desc.Elements - 1 ) * stride + elementSize;
                }
                return elementSize;
            }

            /**
             * @brief cbuffer 의 변수 목록을 멤버 표로 만듭니다. **구조체 하나로 감싼 cbuffer 는 한 겹 벗깁니다.**
             * @details 엔진 cbuffer(PassCB 등)는 맨 필드로 선언하지만, `cbuffer name { name_t data; }` 나
             *          `ConstantBuffer<name_t>` 처럼 구조체 하나로 감싼 cbuffer 도 리플렉션에는 "구조체 변수 하나짜리 CB" 로
             *          보입니다. 엔진(ShaderParameterBinder)과 머티리얼 패커는 **필드 이름**으로 오프셋을 찾으므로 변수가
             *          하나뿐이고 그것이 구조체면 그 멤버들을 CB 의 멤버로 올립니다. 오프셋은 구조체 시작(= 변수 StartOffset)
             *          기준으로 더합니다. 맨 필드 cbuffer 는 그대로 통과합니다.
             */
            template <typename TCb, typename TVar, typename TVarDesc, typename TType, typename TTypeDesc>
            static void fillConstantBufferMembers( TCb* pCb, UINT variableCount, ShaderBufferInfo& outBuffer )
            {
                if ( variableCount == 1 )
                {
                    TVar*  pOnly     = pCb->GetVariableByIndex( 0 );
                    TType* pOnlyType = pOnly != nullptr ? pOnly->GetType() : nullptr;
                    if ( pOnlyType != nullptr )
                    {
                        TTypeDesc onlyTypeDesc{};
                        pOnlyType->GetDesc( &onlyTypeDesc );
                        if ( onlyTypeDesc.Class == D3D_SVC_STRUCT && onlyTypeDesc.Elements <= 1 && onlyTypeDesc.Members > 0 )
                        {
                            TVarDesc onlyVarDesc{};
                            pOnly->GetDesc( &onlyVarDesc );
                            for ( UINT memberIndex = 0; memberIndex < onlyTypeDesc.Members; ++memberIndex )
                            {
                                TType* pMemberType = pOnlyType->GetMemberTypeByIndex( memberIndex );
                                if ( pMemberType == nullptr )
                                    continue;
                                TTypeDesc memberDesc{};
                                pMemberType->GetDesc( &memberDesc );
                                const utf8* pMemberName = pOnlyType->GetMemberTypeName( memberIndex );

                                ShaderVariableInfo varInfo{};
                                varInfo._name   = pMemberName != nullptr ? pMemberName : "";
                                varInfo._offset = onlyVarDesc.StartOffset + memberDesc.Offset;
                                varInfo._size   = computePackedSize<TType, TTypeDesc>( pMemberType );
                                varInfo._type   = hlslVariableTypeName( memberDesc.Class, memberDesc.Type, memberDesc.Rows, memberDesc.Columns );
                                outBuffer._listVariable.push_back( varInfo );
                            }
                            return;
                        }
                    }
                }

                for ( UINT varIndex = 0; varIndex < variableCount; ++varIndex )
                {
                    TVar* pVar = pCb->GetVariableByIndex( varIndex );
                    if ( pVar == nullptr )
                        continue;

                    TVarDesc varDesc{};
                    pVar->GetDesc( &varDesc );

                    ShaderVariableInfo varInfo{};
                    varInfo._name   = varDesc.Name != nullptr ? varDesc.Name : "";
                    varInfo._offset = varDesc.StartOffset;
                    varInfo._size   = varDesc.Size;
                    TType* pTy      = pVar->GetType();
                    if ( pTy != nullptr )
                    {
                        TTypeDesc typeDesc{};
                        pTy->GetDesc( &typeDesc );
                        varInfo._type = hlslVariableTypeName( typeDesc.Class, typeDesc.Type, typeDesc.Rows, typeDesc.Columns );
                    }
                    outBuffer._listVariable.push_back( varInfo );
                }
            }

            /**
             * @brief 정점 셰이더의 입력 시그니처를 정점 입력 목록으로 옮깁니다(DX11 · DX12 공통).
             * @details 시스템 값(SV_*)은 정점 버퍼에서 오지 않으므로 뺍니다. `Register` 는 시그니처 순서 = HLSL 선언 순서라
             *          Vulkan · GL 이 매길 location 과 같습니다. DX 는 이름으로 묶어 이 값이 틀려도 화면은 맞지만, 계약 검사가
             *          같은 규칙으로 네 바이너리를 대조할 수 있게 같은 자리에 둡니다.
             */
            template <typename TReflection, typename TParamDesc>
            static void fillVertexInputs( TReflection* pReflection, uint32 inputParameterCount, ShaderReflectionData& outData )
            {
                for ( uint32 paramIndex = 0; paramIndex < inputParameterCount; ++paramIndex )
                {
                    TParamDesc paramDesc{};
                    if ( FAILED( pReflection->GetInputParameterDesc( paramIndex, &paramDesc ) ) )
                        continue;
                    if ( paramDesc.SystemValueType != D3D_NAME_UNDEFINED || paramDesc.SemanticName == nullptr )
                        continue;
                    ShaderVertexInputInfo input{};
                    input._semantic      = paramDesc.SemanticName;
                    input._semanticIndex = paramDesc.SemanticIndex;
                    input._location      = paramDesc.Register;
                    outData._listVertexInput.push_back( std::move( input ) );
                }
            }

            /** @brief D3D11 리플렉션 타입 묶음입니다. SM5.0 에는 레지스터 공간이 없어 늘 0 입니다. */
            struct D3D11ReflectionApi
            {
                using Reflection     = ID3D11ShaderReflection;
                using ShaderDesc     = D3D11_SHADER_DESC;
                using ConstantBuffer = ID3D11ShaderReflectionConstantBuffer;
                using BufferDesc     = D3D11_SHADER_BUFFER_DESC;
                using Variable       = ID3D11ShaderReflectionVariable;
                using VariableDesc   = D3D11_SHADER_VARIABLE_DESC;
                using Type           = ID3D11ShaderReflectionType;
                using TypeDesc       = D3D11_SHADER_TYPE_DESC;
                using BindDesc       = D3D11_SHADER_INPUT_BIND_DESC;
                using ParameterDesc  = D3D11_SIGNATURE_PARAMETER_DESC;

                static bool   isVertexShader( UINT version ) { return D3D11_SHVER_GET_TYPE( version ) == D3D11_SHVER_VERTEX_SHADER; }
                static uint32 getRegisterSpace( const BindDesc& bindDesc )
                {
                    (void)bindDesc;
                    return 0;
                }
            };

            /** @brief D3D12(DXIL) 리플렉션 타입 묶음입니다. */
            struct D3D12ReflectionApi
            {
                using Reflection     = ID3D12ShaderReflection;
                using ShaderDesc     = D3D12_SHADER_DESC;
                using ConstantBuffer = ID3D12ShaderReflectionConstantBuffer;
                using BufferDesc     = D3D12_SHADER_BUFFER_DESC;
                using Variable       = ID3D12ShaderReflectionVariable;
                using VariableDesc   = D3D12_SHADER_VARIABLE_DESC;
                using Type           = ID3D12ShaderReflectionType;
                using TypeDesc       = D3D12_SHADER_TYPE_DESC;
                using BindDesc       = D3D12_SHADER_INPUT_BIND_DESC;
                using ParameterDesc  = D3D12_SIGNATURE_PARAMETER_DESC;

                static bool   isVertexShader( UINT version ) { return D3D12_SHVER_GET_TYPE( version ) == D3D12_SHVER_VERTEX_SHADER; }
                static uint32 getRegisterSpace( const BindDesc& bindDesc ) { return bindDesc.Space; }
            };

            /**
             * @brief D3D11 · D3D12 리플렉션 인터페이스에서 ShaderReflectionData 를 채웁니다(두 API 는 타입 이름과 레지스터 공간만 다르다).
             * @details 상수버퍼의 자리(공간 · 바인드 포인트)는 **이름으로** 바인딩 서술을 찾아 정한다 — `GetConstantBufferByIndex` 의 열거
             *          순서는 register(bN) 과 다를 수 있다. 찾지 못하면 열거 순서를 쓴다.
             */
            template <typename TAPI>
            static ShaderReflectionData fillFromReflection( typename TAPI::Reflection* pReflection )
            {
                ShaderReflectionData      data{};
                typename TAPI::ShaderDesc shaderDesc{};
                pReflection->GetDesc( &shaderDesc );
                if ( TAPI::isVertexShader( shaderDesc.Version ) )
                    fillVertexInputs<typename TAPI::Reflection, typename TAPI::ParameterDesc>( pReflection, shaderDesc.InputParameters, data );

                for ( UINT cbIndex = 0; cbIndex < shaderDesc.ConstantBuffers; ++cbIndex )
                {
                    typename TAPI::ConstantBuffer* pCb = pReflection->GetConstantBufferByIndex( cbIndex );
                    if ( pCb == nullptr )
                        continue;
                    typename TAPI::BufferDesc cbDesc{};
                    pCb->GetDesc( &cbDesc );
                    // StructuredBuffer<T> 의 원소 타입 레이아웃은 "가상 CB"(Type == D3D_CT_RESOURCE_BIND_INFO, 변수 $Element 하나)로 열거된다.
                    // CB 가 아니라 **원소 레이아웃**으로 따로 낸다 — GPUScene 머티리얼 데이터(g_SwMaterials)의 패킹 기준이다.
                    const bool bStructuredElement = cbDesc.Type == D3D_CT_RESOURCE_BIND_INFO;
                    if ( bStructuredElement == false && cbDesc.Type != D3D_CT_CBUFFER )
                        continue;

                    ShaderBufferInfo bufferInfo{};
                    bufferInfo._name      = cbDesc.Name != nullptr ? cbDesc.Name : "";
                    bufferInfo._bindPoint = cbIndex;
                    bufferInfo._totalSize = cbDesc.Size; ///< 원소 레이아웃이면 원소 stride
                    typename TAPI::BindDesc nameBindDesc{};
                    if ( cbDesc.Name != nullptr && SUCCEEDED( pReflection->GetResourceBindingDescByName( cbDesc.Name, &nameBindDesc ) ) )
                    {
                        bufferInfo._registerSpace = TAPI::getRegisterSpace( nameBindDesc );
                        bufferInfo._bindPoint     = nameBindDesc.BindPoint;
                    }
                    fillConstantBufferMembers<typename TAPI::ConstantBuffer, typename TAPI::Variable, typename TAPI::VariableDesc, typename TAPI::Type,
                                              typename TAPI::TypeDesc>( pCb, cbDesc.Variables, bufferInfo );
                    if ( bStructuredElement == false )
                        data._listConstantBuffer.push_back( std::move( bufferInfo ) );
                    else if ( bufferInfo._listVariable.empty() == false )
                        data._listStructuredElement.push_back( std::move( bufferInfo ) );
                }

                for ( UINT resourceIndex = 0; resourceIndex < shaderDesc.BoundResources; ++resourceIndex )
                {
                    typename TAPI::BindDesc bindDesc{};
                    pReflection->GetResourceBindingDesc( resourceIndex, &bindDesc );

                    ShaderReflectedBinding resourceBinding{};
                    resourceBinding._name          = bindDesc.Name != nullptr ? bindDesc.Name : "";
                    resourceBinding._registerSpace = TAPI::getRegisterSpace( bindDesc );
                    resourceBinding._bindPoint     = bindDesc.BindPoint;
                    resourceBinding._bindCount     = bindDesc.BindCount; ///< 무제한 배열([])은 0
                    resourceBinding._type          = resourceTypeName( static_cast<uint32>( bindDesc.Type ) );
                    data._listResource.push_back( std::move( resourceBinding ) );
                }

                return data;
            }

            static ShaderReflectionData reflectDxbc( const vector<uint8>& bytecode )
            {
                Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
                const HRESULT                                  hr = D3DReflect( bytecode.data(), bytecode.size(), IID_PPV_ARGS( reflection.GetAddressOf() ) );
                if ( FAILED( hr ) || reflection == nullptr )
                {
                    SW_LOG_ERROR( "D3DReflect failed for DXBC (hr=0x%#).", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );
                    return ShaderReflectionData{};
                }

                ShaderReflectionData data = fillFromReflection<D3D11ReflectionApi>( reflection.Get() );
                SW_LOG_TRACE( "ConstantBuffers: %# BoundResources: %#",
                              data._listConstantBuffer.size(), data._listResource.size() );
                return data;
            }

    #if defined( SW_HAS_DXC_API )
            static DxcCreateInstanceProc loadDxcCreateInstance()
            {
                static void*                 s_pDxCompiler{ nullptr };
                static DxcCreateInstanceProc s_fnDxcCreateInstance{ nullptr };
                static bool                  s_bTried{ false };
                if ( s_bTried == false )
                {
                    s_bTried      = true;
                    HMODULE hDll  = LoadLibraryW( L"dxcompiler.dll" );
                    s_pDxCompiler = static_cast<void*>( hDll );
                    if ( s_pDxCompiler != nullptr )
                    {
                        s_fnDxcCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(
                            GetProcAddress( hDll, "DxcCreateInstance" ) );
                    }
                    if ( s_fnDxcCreateInstance == nullptr )
                        SW_LOG_WARNING( "dxcompiler.dll loaded but DxcCreateInstance not found." );
                }
                return s_fnDxcCreateInstance;
            }

            static ShaderReflectionData reflectDxil( const vector<uint8>& bytecode )
            {
                DxcCreateInstanceProc createInstance = loadDxcCreateInstance();
                if ( createInstance == nullptr )
                {
                    SW_LOG_WARNING( "DXIL reflection unavailable: dxcompiler.dll not found or DxcCreateInstance failed." );
                    return ShaderReflectionData{};
                }

                Microsoft::WRL::ComPtr<IDxcUtils> utils;
                if ( FAILED( createInstance( CLSID_DxcUtils, IID_PPV_ARGS( utils.GetAddressOf() ) ) ) || utils == nullptr )
                {
                    SW_LOG_ERROR( "Failed to create IDxcUtils for reflection." );
                    return ShaderReflectionData{};
                }

                DxcBuffer buffer{};
                buffer.Ptr      = bytecode.data();
                buffer.Size     = bytecode.size();
                buffer.Encoding = 0;

                ShaderReflectionData data{};

                Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection12;
                HRESULT                                        hr = utils->CreateReflection( &buffer, IID_PPV_ARGS( reflection12.GetAddressOf() ) );
                if ( SUCCEEDED( hr ) && reflection12 != nullptr )
                {
                    data = fillFromReflection<D3D12ReflectionApi>( reflection12.Get() );
                    SW_LOG_TRACE( "ConstantBuffers: %# BoundResources: %#",
                                  data._listConstantBuffer.size(), data._listResource.size() );
                }
                else
                {
                    // DXC 빌드에 따라 DXIL 리플렉션을 ID3D11ShaderReflection 으로 내놓는 경우가 있다.
                    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection11;
                    hr = utils->CreateReflection( &buffer, IID_PPV_ARGS( reflection11.GetAddressOf() ) );
                    if ( SUCCEEDED( hr ) && reflection11 != nullptr )
                    {
                        data = fillFromReflection<D3D11ReflectionApi>( reflection11.Get() );
                        SW_LOG_TRACE( "ConstantBuffers: %# BoundResources: %#",
                                      data._listConstantBuffer.size(), data._listResource.size() );
                    }
                    else
                        SW_LOG_ERROR( "IDxcUtils::CreateReflection failed for DXIL (hr=0x%#).", Fmt( static_cast<uint32>( hr ), Format( 8, Format::Padding::Zero ).hex() ) );
                }

                return data;
            }
    #endif
        };
    } // namespace
} // namespace sw

namespace sw
{
    SW_LOG_CALLER( "ShaderReflection" );

    ShaderReflectionData ShaderReflectionUtil::reflectDx( const vector<uint8>& bytecode, ShaderTargetFormat targetFormat )
    {
        if ( targetFormat == ShaderTargetFormat::DXBC_D3D11 )
            return ShaderReflectionDxInternal::reflectDxbc( bytecode );

    #if defined( SW_HAS_DXC_API )
        if ( targetFormat == ShaderTargetFormat::DXIL_D3D12 )
            return ShaderReflectionDxInternal::reflectDxil( bytecode );
    #endif

        SW_LOG_ERROR( "Unsupported DX target format %#.", static_cast<uint32>( targetFormat ) );
        return {};
    }
} // namespace sw
#endif
