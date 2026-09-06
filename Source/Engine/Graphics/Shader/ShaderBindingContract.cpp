#include "pch.h"

#include "Engine/Graphics/Shader/ShaderBindingContract.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/unordered_map.h"
#include "Core/Log/Logger.h"

#include "Engine/Graphics/Shader/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/ShaderCompiler.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderBindingContract" );

    namespace
    {
        struct ShaderBindingContractInternal
        {
            static inline atomic<uint32> s_totalViolation{ 0 };

            /// @brief 한 리소스를 이름공간 키로 만들 때 쓰는 레지스터 종류 (DX 기준).
            enum class RegisterClass : uint8
            {
                ConstantBuffer,  // b
                ShaderResource,  // t
                UnorderedAccess, // u
                Sampler,         // s
                Other
            };

            /// @brief GL 이름공간 — GL 은 set 을 버리고 binding 만 보므로 종류별로 번호 공간이 갈린다.
            enum class GlNamespace : uint8
            {
                UniformBuffer,
                TextureUnit,
                StorageBuffer,
                Image,
                Other
            };

            static RegisterClass registerClassOf( ShaderBindingKind kind )
            {
                switch ( kind )
                {
                    case ShaderBindingKind::ConstantBuffer:
                        return RegisterClass::ConstantBuffer;
                    case ShaderBindingKind::Texture:
                    case ShaderBindingKind::StructuredBuffer:
                        return RegisterClass::ShaderResource;
                    case ShaderBindingKind::RwStructuredBuffer:
                    case ShaderBindingKind::RwTexture:
                        return RegisterClass::UnorderedAccess;
                    case ShaderBindingKind::Sampler:
                        return RegisterClass::Sampler;
                    case ShaderBindingKind::Unknown:
                    default:
                        return RegisterClass::Other;
                }
            }

            static GlNamespace glNamespaceOf( ShaderBindingKind kind )
            {
                switch ( kind )
                {
                    case ShaderBindingKind::ConstantBuffer:
                        return GlNamespace::UniformBuffer;
                    case ShaderBindingKind::Texture:
                    case ShaderBindingKind::Sampler:
                        return GlNamespace::TextureUnit;
                    case ShaderBindingKind::StructuredBuffer:
                    case ShaderBindingKind::RwStructuredBuffer:
                        return GlNamespace::StorageBuffer;
                    case ShaderBindingKind::RwTexture:
                        return GlNamespace::Image;
                    case ShaderBindingKind::Unknown:
                    default:
                        return GlNamespace::Other;
                }
            }

            static const utf8* registerClassLetter( RegisterClass cls )
            {
                switch ( cls )
                {
                    case RegisterClass::ConstantBuffer:
                        return "b";
                    case RegisterClass::ShaderResource:
                        return "t";
                    case RegisterClass::UnorderedAccess:
                        return "u";
                    case RegisterClass::Sampler:
                        return "s";
                    case RegisterClass::Other:
                    default:
                        return "?";
                }
            }

            static const utf8* glNamespaceName( GlNamespace ns )
            {
                switch ( ns )
                {
                    case GlNamespace::UniformBuffer:
                        return "UBO";
                    case GlNamespace::TextureUnit:
                        return "texture unit";
                    case GlNamespace::StorageBuffer:
                        return "SSBO";
                    case GlNamespace::Image:
                        return "image unit";
                    case GlNamespace::Other:
                    default:
                        return "?";
                }
            }

            static const utf8* kindName( ShaderBindingKind kind )
            {
                switch ( kind )
                {
                    case ShaderBindingKind::ConstantBuffer:
                        return "ConstantBuffer";
                    case ShaderBindingKind::Texture:
                        return "Texture";
                    case ShaderBindingKind::Sampler:
                        return "Sampler";
                    case ShaderBindingKind::StructuredBuffer:
                        return "StructuredBuffer";
                    case ShaderBindingKind::RwStructuredBuffer:
                        return "RwStructuredBuffer";
                    case ShaderBindingKind::RwTexture:
                        return "RwTexture";
                    case ShaderBindingKind::Unknown:
                    default:
                        return "Unknown";
                }
            }

            static bool isSpirv( ShaderTargetFormat target )
            {
                return target == ShaderTargetFormat::SPIRV_Vulkan || target == ShaderTargetFormat::SPIRV_OpenGL;
            }

            /**
             * @brief 종류가 계약과 "같다" 고 볼 수 있는가.
             * @details SPIR-V 는 읽기/쓰기 구조버퍼를 모두 StorageBuffer 로만 보고하고, 결합 이미지 샘플러는
             *          텍스처와 샘플러를 하나로 보고한다 — 그 차이는 어긋남이 아니다.
             */
            static bool kindMatches( ShaderBindingKind expected, ShaderBindingKind actual, ShaderTargetFormat target )
            {
                if ( expected == actual )
                    return true;
                if ( isSpirv( target ) )
                {
                    const bool bExpectedStorage = ( expected == ShaderBindingKind::StructuredBuffer || expected == ShaderBindingKind::RwStructuredBuffer );
                    const bool bActualStorage   = ( actual == ShaderBindingKind::StructuredBuffer || actual == ShaderBindingKind::RwStructuredBuffer );
                    if ( bExpectedStorage && bActualStorage )
                        return true;
                    const bool bExpectedImage = ( expected == ShaderBindingKind::Texture || expected == ShaderBindingKind::Sampler );
                    const bool bActualImage   = ( actual == ShaderBindingKind::Texture || actual == ShaderBindingKind::Sampler );
                    if ( bExpectedImage && bActualImage )
                        return true;
                }
                return false;
            }

            /// @brief Vulkan set 번호가 파이프라인 레이아웃에서 어떤 디스크립터 타입인가. 없으면 false.
            static bool vulkanSetAcceptsKind( uint32 set, ShaderBindingKind kind )
            {
                namespace vk = shaderslot::vk;
                if ( set == vk::kSetPassCb || set == vk::kSetMaterialCb )
                    return kind == ShaderBindingKind::ConstantBuffer;
                if ( set == vk::kSetBindlessTexture )
                    return kind == ShaderBindingKind::Texture || kind == ShaderBindingKind::Sampler;
                if ( set == vk::kSetStaticSampler )
                    return kind == ShaderBindingKind::Sampler || kind == ShaderBindingKind::Texture;
                if ( set >= vk::kSetStorage0 && set < vk::kSetStorage0 + vk::kStorageSetCount )
                    return kind == ShaderBindingKind::StructuredBuffer || kind == ShaderBindingKind::RwStructuredBuffer;
                // 나머지(2,3,5)는 예약·미사용 텍스처 세트 — 어떤 셰이더도 참조하면 안 된다.
                return false;
            }

            struct Seen
            {
                string            _name;
                ShaderBindingKind _kind{ ShaderBindingKind::Unknown };
                uint32            _space{ 0 };
                uint32            _bindPoint{ 0 };
            };

            /// @brief CB 목록과 리소스 목록을 (이름, 종류) 로 중복 없이 합칩니다 — DX 리플렉션은 cbuffer 를 양쪽에 다 넣는다.
            static void collect( const ShaderReflectionData& reflection, vector<Seen>& outList )
            {
                auto push = [&]( const string& name, ShaderBindingKind kind, uint32 space, uint32 bindPoint )
                {
                    for ( const Seen& seen : outList )
                    {
                        if ( seen._name == name && seen._kind == kind )
                            return;
                    }
                    Seen seen{};
                    seen._name      = name;
                    seen._kind      = kind;
                    seen._space     = space;
                    seen._bindPoint = bindPoint;
                    outList.push_back( std::move( seen ) );
                };
                // 리소스 목록이 바인딩 위치의 1차 출처다(cbuffer 도 여기 들어 있다). CB 목록은 그 다음 — 리플렉터가
                // CB 쪽 bindPoint 를 못 채우는 경우가 있었다(DXIL, move 뒤 이름 비교).
                for ( const ShaderResourceBinding& res : reflection._listResource )
                {
                    const ShaderBindingKind kind = ShaderBindingLayout::kindFromTypeLabel( static_cast<std::string_view>( res._type ) );
                    push( res._name, kind, res._registerSpace, res._bindPoint );
                }
                for ( const ShaderBufferInfo& cb : reflection._listConstantBuffer )
                    push( cb._name, ShaderBindingKind::ConstantBuffer, cb._registerSpace, cb._bindPoint );
            }

            static void report( vector<ShaderBindingContractIssue>* pOutIssue, string_view shaderLabel, const string& resource, string&& message, uint32& ioCount )
            {
                ++ioCount;
                s_totalViolation.fetch_add( 1, std::memory_order_relaxed );
                SW_LOG_ERROR( "[바인딩 계약] %# — %#: %#", string( shaderLabel ).c_str(), resource.c_str(), message.c_str() );
                if ( pOutIssue != nullptr )
                {
                    ShaderBindingContractIssue issue{};
                    issue._resource = resource;
                    issue._message  = std::move( message );
                    pOutIssue->push_back( std::move( issue ) );
                }
            }

            static string fmt( const utf8* pFormat, uint32 a, uint32 b )
            {
                return string( pFormat ) + "(" + to_string( a ) + ", " + to_string( b ) + ")";
            }
        };

        /// @brief 계약 표 — 값은 전부 shaderslot 에서 온다. 백엔드가 선언하지 않는 자리는 kNotDeclared.
        const vector<ShaderReservedBinding>& reservedBindings()
        {
            static const vector<ShaderReservedBinding> s_list = []()
            {
                using R                = ShaderReservedBinding;
                namespace vk           = shaderslot::vk;
                constexpr uint32 kNone = R::kNotDeclared;
                vector<R>        list;
                auto             add = [&]( const utf8* pName, ShaderBindingKind kind, uint32 dxReg, uint32 dxSpace, uint32 vkSet, uint32 vkBinding, uint32 glBinding )
                {
                    R r{};
                    r._name       = pName;
                    r._kind       = kind;
                    r._dxRegister = dxReg;
                    r._dxSpace    = dxSpace;
                    r._vkSet      = vkSet;
                    r._vkBinding  = vkBinding;
                    r._glBinding  = glBinding;
                    list.push_back( r );
                };
                // 상수버퍼
                add( shaderslot::cbname::kPass, ShaderBindingKind::ConstantBuffer, shaderslot::kPassConstantBuffer, 0, vk::kSetPassCb, 0, shaderslot::kPassConstantBuffer );
                add( shaderslot::cbname::kMaterial, ShaderBindingKind::ConstantBuffer, shaderslot::kMaterialConstantBuffer, 0, vk::kSetMaterialCb, 0, shaderslot::kMaterialConstantBuffer );
                add( shaderslot::cbname::kCull, ShaderBindingKind::ConstantBuffer, shaderslot::kComputeConstantBuffer, 0, vk::kSetPassCb, 0, shaderslot::kComputeConstantBuffer );
                // 인스턴스 구조버퍼 — DX/GL 은 t4, Vulkan 은 set 6 binding 0 (register(t0, space6))
                add( shaderslot::resname::kInstances, ShaderBindingKind::StructuredBuffer, shaderslot::kInstanceBuffer, 0, vk::kSetStorage0, 0, shaderslot::kInstanceBuffer );
                // gpucull 컴퓨트 — t0 / u0
                add( shaderslot::resname::kCullInstances, ShaderBindingKind::StructuredBuffer, 0, 0, vk::kSetStorage0, 0, 0 );
                add( shaderslot::resname::kCullIndirectArgs, ShaderBindingKind::RwStructuredBuffer, 0, 0, vk::kSetUav0, 0, shaderslot::gl::kUavBinding0 );
                // 엔진 텍스처 슬롯 t0..t3 / 머티리얼 텍스처 t5..t8 — 에뮬 백엔드(DX11/GL)만. Vulkan/DX12 는 선언 자체가 없어야 한다.
                static string s_arrEngineName[shaderslot::kEngineTextureCount];
                static string s_arrEngineSamplerName[shaderslot::kEngineTextureCount];
                static string s_arrMaterialName[shaderslot::kMaterialTextureCount];
                static string s_arrMaterialSamplerName[shaderslot::kMaterialTextureCount];
                for ( uint32 slotIndex = 0; slotIndex < shaderslot::kEngineTextureCount; ++slotIndex )
                {
                    s_arrEngineName[slotIndex]        = string( shaderslot::resname::kEngineTexture ) + to_string( slotIndex );
                    s_arrEngineSamplerName[slotIndex] = s_arrEngineName[slotIndex] + "Sampler";
                    const uint32 reg                  = shaderslot::kEngineTexture0 + slotIndex;
                    add( s_arrEngineName[slotIndex].c_str(), ShaderBindingKind::Texture, reg, 0, kNone, kNone, reg );
                    add( s_arrEngineSamplerName[slotIndex].c_str(), ShaderBindingKind::Sampler, reg, 0, kNone, kNone, reg );
                }
                for ( uint32 slotIndex = 0; slotIndex < shaderslot::kMaterialTextureCount; ++slotIndex )
                {
                    s_arrMaterialName[slotIndex]        = string( shaderslot::resname::kMaterialTexture ) + to_string( slotIndex );
                    s_arrMaterialSamplerName[slotIndex] = s_arrMaterialName[slotIndex] + "Sampler";
                    const uint32 reg                    = shaderslot::kMaterialTexture0 + slotIndex;
                    add( s_arrMaterialName[slotIndex].c_str(), ShaderBindingKind::Texture, reg, 0, kNone, kNone, reg );
                    add( s_arrMaterialSamplerName[slotIndex].c_str(), ShaderBindingKind::Sampler, reg, 0, kNone, kNone, reg );
                }
                // 네이티브 bindless — DX12 t0 space1 / Vulkan set 1. GL 에는 없다.
                add( shaderslot::resname::kBindlessTextures, ShaderBindingKind::Texture, 0, shaderslot::kBindlessTextureSpace, vk::kSetBindlessTexture, 0, kNone );
                // 정적 샘플러 — DX s0 space0, Vulkan set 4 binding 0. GL 은 결합 샘플러만 쓴다.
                add( shaderslot::resname::kLinearWrapSampler, ShaderBindingKind::Sampler, SW_SAMPLER_LINEAR_WRAP, 0, vk::kSetStaticSampler, SW_SAMPLER_LINEAR_WRAP, kNone );
                return list;
            }();
            return s_list;
        }
    } // namespace

    const vector<ShaderReservedBinding>& ShaderBindingContract::getReservedBindings()
    {
        return reservedBindings();
    }

    uint32 ShaderBindingContract::getTotalViolationCount()
    {
        return ShaderBindingContractInternal::s_totalViolation.load( std::memory_order_relaxed );
    }

    uint32 ShaderBindingContract::validate( const ShaderReflectionData& reflection, ShaderTargetFormat targetFormat, string_view shaderLabel,
                                            vector<ShaderBindingContractIssue>* pOutIssue )
    {
        using Internal = ShaderBindingContractInternal;
        uint32 issueCount{ 0 };

        vector<Internal::Seen> listSeen;
        Internal::collect( reflection, listSeen );

        const bool bVulkan = ( targetFormat == ShaderTargetFormat::SPIRV_Vulkan );
        const bool bOpenGl = ( targetFormat == ShaderTargetFormat::SPIRV_OpenGL );
        const bool bDx12   = ( targetFormat == ShaderTargetFormat::DXIL_D3D12 );

        // 1) 예약 리소스 — 종류와 위치
        for ( const Internal::Seen& seen : listSeen )
        {
            const ShaderReservedBinding* pReserved{ nullptr };
            for ( const ShaderReservedBinding& reserved : reservedBindings() )
            {
                if ( seen._name == reserved._name )
                {
                    pReserved = &reserved;
                    break;
                }
            }
            if ( pReserved == nullptr )
                continue;

            if ( Internal::kindMatches( pReserved->_kind, seen._kind, targetFormat ) == false )
            {
                Internal::report( pOutIssue, shaderLabel, seen._name,
                                  string( "종류가 계약과 다릅니다 — 기대 " ) + Internal::kindName( pReserved->_kind ) + ", 리플렉션 " + Internal::kindName( seen._kind ),
                                  issueCount );
            }

            uint32 expectedSpace{ 0 };
            uint32 expectedBind{ 0 };
            bool   bDeclaredHere{ true };
            if ( bVulkan )
            {
                bDeclaredHere = ( pReserved->_vkSet != ShaderReservedBinding::kNotDeclared );
                expectedSpace = pReserved->_vkSet;
                expectedBind  = pReserved->_vkBinding;
            }
            else if ( bOpenGl )
            {
                bDeclaredHere = ( pReserved->_glBinding != ShaderReservedBinding::kNotDeclared );
                expectedSpace = 0;
                expectedBind  = pReserved->_glBinding;
            }
            else
            {
                expectedSpace = bDx12 ? pReserved->_dxSpace : 0;
                expectedBind  = pReserved->_dxRegister;
            }

            if ( bDeclaredHere == false )
            {
                Internal::report( pOutIssue, shaderLabel, seen._name,
                                  string( "이 백엔드 계약에는 없는 예약 리소스가 선언돼 있습니다 (리플렉션 " ) + Internal::fmt( "space/binding", seen._space, seen._bindPoint ) + ")",
                                  issueCount );
                continue;
            }
            if ( seen._bindPoint != expectedBind || seen._space != expectedSpace )
            {
                Internal::report( pOutIssue, shaderLabel, seen._name,
                                  string( "위치가 계약과 다릅니다 — 기대 " ) + Internal::fmt( bVulkan ? "set/binding" : "space/register", expectedSpace, expectedBind ) +
                                      ", 리플렉션 " + Internal::fmt( bVulkan ? "set/binding" : "space/register", seen._space, seen._bindPoint ),
                                  issueCount );
            }
        }

        // 2) 이름공간 충돌 + 3) Vulkan 세트 범위/타입 + 4) GL set 0
        for ( size_t indexA = 0; indexA < listSeen.size(); ++indexA )
        {
            const Internal::Seen& a = listSeen[indexA];

            if ( bVulkan )
            {
                if ( a._space >= shaderslot::vk::kBoundSetCount )
                {
                    Internal::report( pOutIssue, shaderLabel, a._name,
                                      string( "파이프라인 레이아웃 밖의 descriptor set 을 참조합니다 — set " ) + to_string( a._space ) + " (레이아웃은 " + to_string( shaderslot::vk::kBoundSetCount ) + "개)",
                                      issueCount );
                }
                else if ( a._kind != ShaderBindingKind::Unknown && Internal::vulkanSetAcceptsKind( a._space, a._kind ) == false )
                {
                    Internal::report( pOutIssue, shaderLabel, a._name,
                                      string( "descriptor set " ) + to_string( a._space ) + " 의 타입과 리소스 종류(" + Internal::kindName( a._kind ) + ")가 맞지 않습니다",
                                      issueCount );
                }
            }
            if ( bOpenGl && a._space != 0 )
            {
                Internal::report( pOutIssue, shaderLabel, a._name,
                                  string( "OpenGL 은 descriptor set 을 무시하는데 set " ) + to_string( a._space ) + " 로 선언돼 있습니다 — binding " + to_string( a._bindPoint ) + " 하나로 취급됩니다",
                                  issueCount );
            }

            for ( size_t indexB = indexA + 1; indexB < listSeen.size(); ++indexB )
            {
                const Internal::Seen& b = listSeen[indexB];
                if ( a._bindPoint != b._bindPoint )
                    continue;

                bool   bCollide{ false };
                string where;
                if ( bVulkan )
                {
                    bCollide = ( a._space == b._space );
                    where    = Internal::fmt( "set/binding", a._space, a._bindPoint );
                }
                else if ( bOpenGl )
                {
                    const Internal::GlNamespace nsA = Internal::glNamespaceOf( a._kind );
                    bCollide                        = ( nsA != Internal::GlNamespace::Other && nsA == Internal::glNamespaceOf( b._kind ) );
                    where                           = string( Internal::glNamespaceName( nsA ) ) + " binding " + to_string( a._bindPoint );
                }
                else
                {
                    const Internal::RegisterClass clsA = Internal::registerClassOf( a._kind );
                    bCollide                           = ( a._space == b._space && clsA != Internal::RegisterClass::Other && clsA == Internal::registerClassOf( b._kind ) );
                    where                              = string( Internal::registerClassLetter( clsA ) ) + to_string( a._bindPoint ) + " space" + to_string( a._space );
                }
                if ( bCollide )
                {
                    Internal::report( pOutIssue, shaderLabel, a._name,
                                      string( "'" ) + b._name + "' 와 같은 자리를 차지합니다 (" + where + ")", issueCount );
                }
            }
        }

        return issueCount;
    }
} // namespace sw
