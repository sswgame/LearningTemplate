#include "pch.h"

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
                        return "RWTexture"; // RWTexture2D 등 — 계약이 버퍼 UAV 와 구분한다 (RWBuffer<T> 는 쓰지 않는다)
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
                        pBase = "Float";
                        break;
                    case static_cast<uint32>( D3D_SVT_INT ):
                        pBase = "Int";
                        break;
                    case static_cast<uint32>( D3D_SVT_UINT ):
                    case static_cast<uint32>( D3D_SVT_UINT8 ):
                        pBase = "Uint";
                        break;
                    case static_cast<uint32>( D3D_SVT_BOOL ):
                        return "Bool";
                    default:
                        break;
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
             * @brief 타입 하나가 cbuffer 안에서 차지하는 바이트 수 (HLSL 패킹 규칙).
             * @details 리플렉션의 타입 서술에는 크기가 없고 변수 서술에만 있다 — 구조체 **멤버** 를 펼칠 때는
             *          여기서 계산한다. 규칙: 배열 원소와 구조체는 16바이트 정렬, 마지막 원소 뒤 패딩은 없다
             *          (D3D 가 변수 Size 로 보고하는 값과 같은 규칙 — `float arr[3]` 은 36, `float4 arr[6]` 은 96).
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
                        // 행렬은 벡터 하나가 16바이트 레지스터 하나를 쓴다 (row_major 면 행, 아니면 열) — 마지막 벡터만 실제 폭.
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
             * @brief cbuffer 의 변수 목록을 멤버 표로 만든다. **구조체 하나로 감싼 cbuffer 는 한 겹 벗긴다.**
             * @details 엔진 cbuffer(PassCB 등)는 맨 필드로 선언하지만, `cbuffer name { name_t data; }` 나
             *          `ConstantBuffer<name_t>` 처럼 구조체 하나로 감싼 cbuffer 도 리플렉션에는 "구조체 변수 하나짜리 CB" 로
             *          보인다. 엔진(ShaderBindingBinder)과 머티리얼 패커는 **필드 이름** 으로 오프셋을 찾으므로 변수가
             *          하나뿐이고 그것이 구조체면 그 멤버들을 CB 의 멤버로 올린다. 오프셋은 구조체 시작(= 변수 StartOffset)
             *          기준으로 더한다. 맨 필드 cbuffer 는 그대로 통과한다.
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

            static ShaderReflectionData fillFromId3d11Reflection( ID3D11ShaderReflection* pReflection )
            {
                ShaderReflectionData data{};
                D3D11_SHADER_DESC    shaderDesc{};
                pReflection->GetDesc( &shaderDesc );

                for ( UINT cbIndex = 0; cbIndex < shaderDesc.ConstantBuffers; ++cbIndex )
                {
                    ID3D11ShaderReflectionConstantBuffer* pCb = pReflection->GetConstantBufferByIndex( cbIndex );
                    if ( pCb == nullptr )
                        continue;

                    D3D11_SHADER_BUFFER_DESC cbDesc{};
                    pCb->GetDesc( &cbDesc );

                    // cbIndex(열거 순서) 는 register(bN) 과 다를 수 있다 — 이름으로 실제 바인드 포인트를 찾는다.
                    UINT                         realBindPoint = cbIndex;
                    D3D11_SHADER_INPUT_BIND_DESC nameBindDesc{};
                    if ( cbDesc.Name != nullptr && SUCCEEDED( pReflection->GetResourceBindingDescByName( cbDesc.Name, &nameBindDesc ) ) )
                        realBindPoint = nameBindDesc.BindPoint;

                    // GetConstantBufferByIndex 는 실제 cbuffer 블록뿐 아니라 StructuredBuffer<T> 의 원소 타입 레이아웃도
                    // "가상 CB"(Type == D3D_CT_RESOURCE_BIND_INFO, 변수 $Element 하나) 로 열거한다. 그건 CB 가 아니라
                    // **원소 레이아웃** 으로 따로 낸다 — GPUScene 머티리얼 데이터(g_SwMaterials)의 패킹 정본이다.
                    if ( cbDesc.Type == D3D_CT_RESOURCE_BIND_INFO )
                    {
                        ShaderBufferInfo element{};
                        element._name          = cbDesc.Name != nullptr ? cbDesc.Name : "";
                        element._registerSpace = 0;
                        element._bindPoint     = realBindPoint;
                        element._totalSize     = cbDesc.Size; ///< 원소 stride
                        fillConstantBufferMembers<ID3D11ShaderReflectionConstantBuffer, ID3D11ShaderReflectionVariable, D3D11_SHADER_VARIABLE_DESC,
                                                  ID3D11ShaderReflectionType, D3D11_SHADER_TYPE_DESC>( pCb, cbDesc.Variables, element );
                        if ( element._listVariable.empty() == false )
                            data._listStructuredElement.push_back( std::move( element ) );
                        continue;
                    }
                    if ( cbDesc.Type != D3D_CT_CBUFFER )
                        continue;

                    ShaderBufferInfo bufInfo{};
                    bufInfo._name          = cbDesc.Name != nullptr ? cbDesc.Name : "";
                    bufInfo._registerSpace = 0;
                    bufInfo._bindPoint     = realBindPoint;
                    bufInfo._totalSize     = cbDesc.Size;

                    fillConstantBufferMembers<ID3D11ShaderReflectionConstantBuffer, ID3D11ShaderReflectionVariable, D3D11_SHADER_VARIABLE_DESC,
                                              ID3D11ShaderReflectionType, D3D11_SHADER_TYPE_DESC>( pCb, cbDesc.Variables, bufInfo );

                    data._listConstantBuffer.push_back( std::move( bufInfo ) );
                }

                for ( UINT resourceIndex = 0; resourceIndex < shaderDesc.BoundResources; ++resourceIndex )
                {
                    D3D11_SHADER_INPUT_BIND_DESC bindDesc{};
                    pReflection->GetResourceBindingDesc( resourceIndex, &bindDesc );

                    ShaderResourceBinding resBinding{};
                    resBinding._name          = bindDesc.Name != nullptr ? bindDesc.Name : "";
                    resBinding._registerSpace = 0;
                    resBinding._bindPoint     = bindDesc.BindPoint;
                    resBinding._bindCount     = bindDesc.BindCount;
                    resBinding._type          = resourceTypeName( static_cast<uint32>( bindDesc.Type ) );
                    data._listResource.push_back( std::move( resBinding ) );
                }

                return data;
            }

            static ShaderReflectionData fillFromId3d12Reflection( ID3D12ShaderReflection* pReflection )
            {
                ShaderReflectionData data{};
                D3D12_SHADER_DESC    shaderDesc{};
                pReflection->GetDesc( &shaderDesc );

                for ( UINT cbIndex = 0; cbIndex < shaderDesc.ConstantBuffers; ++cbIndex )
                {
                    ID3D12ShaderReflectionConstantBuffer* pCb = pReflection->GetConstantBufferByIndex( cbIndex );
                    D3D12_SHADER_BUFFER_DESC              cbDesc{};
                    pCb->GetDesc( &cbDesc );

                    // StructuredBuffer<T> 의 원소 타입 레이아웃("가상 CB", Type == D3D_CT_RESOURCE_BIND_INFO) 은 원소 레이아웃 목록으로.
                    if ( cbDesc.Type == D3D_CT_RESOURCE_BIND_INFO )
                    {
                        ShaderBufferInfo element{};
                        element._name      = cbDesc.Name != nullptr ? cbDesc.Name : "";
                        element._totalSize = cbDesc.Size; ///< 원소 stride
                        D3D12_SHADER_INPUT_BIND_DESC nameBindDesc{};
                        if ( cbDesc.Name != nullptr && SUCCEEDED( pReflection->GetResourceBindingDescByName( cbDesc.Name, &nameBindDesc ) ) )
                        {
                            element._registerSpace = nameBindDesc.Space;
                            element._bindPoint     = nameBindDesc.BindPoint;
                        }
                        fillConstantBufferMembers<ID3D12ShaderReflectionConstantBuffer, ID3D12ShaderReflectionVariable, D3D12_SHADER_VARIABLE_DESC,
                                                  ID3D12ShaderReflectionType, D3D12_SHADER_TYPE_DESC>( pCb, cbDesc.Variables, element );
                        if ( element._listVariable.empty() == false )
                            data._listStructuredElement.push_back( std::move( element ) );
                        continue;
                    }
                    if ( cbDesc.Type != D3D_CT_CBUFFER )
                        continue;

                    ShaderBufferInfo bufInfo{};
                    bufInfo._name          = cbDesc.Name != nullptr ? cbDesc.Name : "";
                    bufInfo._registerSpace = 0;
                    bufInfo._bindPoint     = 0;
                    bufInfo._totalSize     = cbDesc.Size;

                    fillConstantBufferMembers<ID3D12ShaderReflectionConstantBuffer, ID3D12ShaderReflectionVariable, D3D12_SHADER_VARIABLE_DESC,
                                              ID3D12ShaderReflectionType, D3D12_SHADER_TYPE_DESC>( pCb, cbDesc.Variables, bufInfo );

                    data._listConstantBuffer.push_back( std::move( bufInfo ) );
                }

                for ( UINT resourceIndex = 0; resourceIndex < shaderDesc.BoundResources; ++resourceIndex )
                {
                    D3D12_SHADER_INPUT_BIND_DESC bindDesc{};
                    pReflection->GetResourceBindingDesc( resourceIndex, &bindDesc );

                    ShaderResourceBinding resBinding{};
                    resBinding._name          = bindDesc.Name != nullptr ? bindDesc.Name : "";
                    resBinding._registerSpace = bindDesc.Space;
                    resBinding._bindPoint     = bindDesc.BindPoint;
                    resBinding._bindCount     = bindDesc.BindCount; ///< 무제한 배열([])은 0
                    resBinding._type          = resourceTypeName( static_cast<uint32>( bindDesc.Type ) );

                    // 이 짝맞추기는 **move 하기 전에** 해야 한다 — 예전엔 push_back( std::move ) 뒤에 비어 버린 이름과
                    // 비교해 한 번도 맞지 않았고, DXIL 의 모든 cbuffer 가 bindPoint 0 으로 보고됐다(MaterialCB 도 b0).
                    // 엔진 바인더는 정본 슬롯으로 걸어 가려졌지만 계약 검증이 "PassCB 와 MaterialCB 가 같은 자리" 로 잡아냈다.
                    if ( bindDesc.Type == D3D_SIT_CBUFFER )
                    {
                        for ( ShaderBufferInfo& cb : data._listConstantBuffer )
                        {
                            if ( cb._name == resBinding._name )
                            {
                                cb._registerSpace = bindDesc.Space;
                                cb._bindPoint     = bindDesc.BindPoint;
                                break;
                            }
                        }
                    }
                    data._listResource.push_back( std::move( resBinding ) );
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

                ShaderReflectionData data = fillFromId3d11Reflection( reflection.Get() );
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
                    HMODULE hDll  = LoadLibraryA( "dxcompiler.dll" );
                    s_pDxCompiler = static_cast<void*>( hDll );
                    if ( s_pDxCompiler != nullptr )
                    {
                        s_fnDxcCreateInstance = reinterpret_cast<DxcCreateInstanceProc>(
                            GetProcAddress( hDll, "DxcCreateInstance" ) );
                    }
                    if ( s_fnDxcCreateInstance == nullptr )
                    {
                        SW_LOG_WARNING( "dxcompiler.dll loaded but DxcCreateInstance not found." );
                    }
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
                    data = fillFromId3d12Reflection( reflection12.Get() );
                    SW_LOG_TRACE( "ConstantBuffers: %# BoundResources: %#",
                                  data._listConstantBuffer.size(), data._listResource.size() );
                }
                else
                {
                    // Some DXC builds expose DXIL reflection as ID3D11ShaderReflection.
                    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection11;
                    hr = utils->CreateReflection( &buffer, IID_PPV_ARGS( reflection11.GetAddressOf() ) );
                    if ( SUCCEEDED( hr ) && reflection11 != nullptr )
                    {
                        data = fillFromId3d11Reflection( reflection11.Get() );
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
