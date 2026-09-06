#include "pch.h"

#include "Engine/Graphics/Shader/ShaderCompiler.h"
#include "Engine/Graphics/Shader/ShaderReflectionUtil.h"

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

                    // GetConstantBufferByIndex 는 실제 cbuffer 블록뿐 아니라 StructuredBuffer<T>/ByteAddressBuffer
                    // 의 원소 타입 레이아웃도 "가상 CB" 로 함께 열거한다 (Type == D3D_CT_RESOURCE_BIND_INFO).
                    // 실제 cbuffer 만 받는다 — 아니면 loop index 를 bindPoint 로 오인해 register(bN) 이 어긋난다.
                    if ( cbDesc.Type != D3D_CT_CBUFFER )
                        continue;

                    // cbIndex(열거 순서) 는 register(bN) 과 다를 수 있다 — 이름으로 실제 바인드 포인트를 찾는다.
                    UINT                         realBindPoint = cbIndex;
                    D3D11_SHADER_INPUT_BIND_DESC nameBindDesc{};
                    if ( cbDesc.Name != nullptr && SUCCEEDED( pReflection->GetResourceBindingDescByName( cbDesc.Name, &nameBindDesc ) ) )
                        realBindPoint = nameBindDesc.BindPoint;

                    ShaderBufferInfo bufInfo{};
                    bufInfo._name          = cbDesc.Name != nullptr ? cbDesc.Name : "";
                    bufInfo._registerSpace = 0;
                    bufInfo._bindPoint     = realBindPoint;
                    bufInfo._totalSize     = cbDesc.Size;

                    for ( UINT varIndex = 0; varIndex < cbDesc.Variables; ++varIndex )
                    {
                        ID3D11ShaderReflectionVariable* pVar = pCb->GetVariableByIndex( varIndex );
                        if ( pVar == nullptr )
                            continue;

                        D3D11_SHADER_VARIABLE_DESC varDesc{};
                        pVar->GetDesc( &varDesc );

                        ShaderVariableInfo varInfo{};
                        varInfo._name                   = varDesc.Name != nullptr ? varDesc.Name : "";
                        varInfo._offset                 = varDesc.StartOffset;
                        varInfo._size                   = varDesc.Size;
                        ID3D11ShaderReflectionType* pTy = pVar->GetType();
                        if ( pTy != nullptr )
                        {
                            D3D11_SHADER_TYPE_DESC typeDesc{};
                            pTy->GetDesc( &typeDesc );
                            varInfo._type = hlslVariableTypeName( typeDesc.Class, typeDesc.Type, typeDesc.Rows, typeDesc.Columns );
                        }
                        bufInfo._listVariable.push_back( varInfo );
                    }

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

                    // GetConstantBufferByIndex 는 실제 cbuffer 블록뿐 아니라 StructuredBuffer<T>/ByteAddressBuffer
                    // 의 원소 타입 레이아웃도 "가상 CB" 로 함께 열거한다 (Type == D3D_CT_RESOURCE_BIND_INFO).
                    // 그대로 두면 아래 bindPoint=0 초기값이 실제 CB(주로 PassCB, b0)와 충돌해 dedup 된다.
                    if ( cbDesc.Type != D3D_CT_CBUFFER )
                        continue;

                    ShaderBufferInfo bufInfo{};
                    bufInfo._name          = cbDesc.Name != nullptr ? cbDesc.Name : "";
                    bufInfo._registerSpace = 0;
                    bufInfo._bindPoint     = 0;
                    bufInfo._totalSize     = cbDesc.Size;

                    for ( UINT varIndex = 0; varIndex < cbDesc.Variables; ++varIndex )
                    {
                        ID3D12ShaderReflectionVariable* pVar = pCb->GetVariableByIndex( varIndex );
                        D3D12_SHADER_VARIABLE_DESC      varDesc{};
                        pVar->GetDesc( &varDesc );

                        ShaderVariableInfo varInfo{};
                        varInfo._name                   = varDesc.Name != nullptr ? varDesc.Name : "";
                        varInfo._offset                 = varDesc.StartOffset;
                        varInfo._size                   = varDesc.Size;
                        ID3D12ShaderReflectionType* pTy = pVar->GetType();
                        if ( pTy != nullptr )
                        {
                            D3D12_SHADER_TYPE_DESC typeDesc{};
                            pTy->GetDesc( &typeDesc );
                            varInfo._type = hlslVariableTypeName( typeDesc.Class, typeDesc.Type, typeDesc.Rows, typeDesc.Columns );
                        }
                        bufInfo._listVariable.push_back( varInfo );
                    }

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
                    resBinding._bindCount     = bindDesc.BindCount;
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
