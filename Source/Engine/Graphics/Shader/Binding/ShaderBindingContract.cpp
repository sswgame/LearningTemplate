#include "pch.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingContract.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/unordered_map.h"
#include "Core/Log/Logger.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"
#include "Engine/Graphics/Shader/Compile/ShaderCompiler.h"

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

            static const utf8* glNamespaceName( GlNamespace glNamespace )
            {
                switch ( glNamespace )
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

            /**
             * @brief `[shift, shift + width)` 안에 드는가.
             * @details 부호 없는 뺄셈이라 `binding < shift` 면 아주 큰 값으로 감겨 width 를 넘는다 —
             *          그래서 하한 비교가 따로 필요 없다. b 밴드의 shift 는 0 이라 `binding >= 0` 이
             *          늘 참이었고, 컴파일러가 그걸 짚어 줬다.
             */
            static bool inBand( uint32 binding, uint32 shift, uint32 width ) { return ( binding - shift ) < width; }

            /// @brief Vulkan 세트 0 binding 이 어느 레지스터 밴드(b/t/u)인가. 밴드 밖이면 Other.
            static RegisterClass vulkanBandOf( uint32 binding )
            {
                namespace vk = shaderslot::vk;
                if ( inBand( binding, vk::kBShift, vk::kBandWidth ) )
                    return RegisterClass::ConstantBuffer;
                if ( inBand( binding, vk::kTShift, vk::kBandWidth ) )
                    return RegisterClass::ShaderResource;
                if ( inBand( binding, vk::kUShift, vk::kBandWidth ) )
                    return RegisterClass::UnorderedAccess;
                return RegisterClass::Other;
            }

            /// @brief SPIR-V 는 읽기/쓰기 구조버퍼를 구분하지 않으므로 t/u 밴드 모두 StorageBuffer 를 받는다.
            static bool vulkanBandAcceptsKind( RegisterClass band, ShaderBindingKind kind )
            {
                switch ( band )
                {
                    case RegisterClass::ConstantBuffer:
                        return kind == ShaderBindingKind::ConstantBuffer;
                    case RegisterClass::ShaderResource:
                    case RegisterClass::UnorderedAccess:
                        return kind == ShaderBindingKind::StructuredBuffer || kind == ShaderBindingKind::RwStructuredBuffer;
                    case RegisterClass::Sampler:
                    case RegisterClass::Other:
                    default:
                        return false;
                }
            }

            struct Seen
            {
                string            _name;
                ShaderBindingKind _kind{ ShaderBindingKind::Unknown };
                uint32            _space{ 0 };
                uint32            _bindPoint{ 0 };
                uint32            _bindCount{ kUnknownCount }; ///< 리소스 목록에서 왔으면 배열 크기(무제한=0), CB 목록만 있으면 모름

                static constexpr uint32 kUnknownCount = 0xFFFFFFFFu;
            };

            /// @brief CB 목록과 리소스 목록을 (이름, 종류) 로 중복 없이 합칩니다 — DX 리플렉션은 cbuffer 를 양쪽에 다 넣는다.
            static void collect( const ShaderReflectionData& reflection, vector<Seen>& outList )
            {
                auto push = [&]( const string& name, ShaderBindingKind kind, uint32 space, uint32 bindPoint, uint32 bindCount )
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
                    seen._bindCount = bindCount;
                    outList.push_back( std::move( seen ) );
                };
                // 리소스 목록이 바인딩 위치의 1차 출처다(cbuffer 도 여기 들어 있다). CB 목록은 그 다음 — 리플렉터가
                // CB 쪽 bindPoint 를 못 채우는 경우가 있었다(DXIL, move 뒤 이름 비교).
                for ( const ShaderResourceBinding& res : reflection._listResource )
                {
                    const ShaderBindingKind kind = ShaderBindingLayout::kindFromTypeLabel( static_cast<std::string_view>( res._type ) );
                    push( res._name, kind, res._registerSpace, res._bindPoint, res._bindCount );
                }
                for ( const ShaderBufferInfo& cb : reflection._listConstantBuffer )
                    push( cb._name, ShaderBindingKind::ConstantBuffer, cb._registerSpace, cb._bindPoint, Seen::kUnknownCount );
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

            static string formatLocation( const utf8* pFormat, uint32 space, uint32 bindPoint )
            {
                return string( pFormat ) + "(" + to_string( space ) + ", " + to_string( bindPoint ) + ")";
            }
        };

        ShaderReservedLocation at( uint32 space, uint32 bind )
        {
            ShaderReservedLocation loc{};
            loc._space     = space;
            loc._bind      = bind;
            loc._bDeclared = true;
            return loc;
        }

        ShaderReservedLocation none()
        {
            return ShaderReservedLocation{};
        }

        /// @brief 계약 표 — 값은 전부 shaderslot 에서 온다. 백엔드가 선언하지 않는 자리는 none().
        const vector<ShaderReservedBinding>& reservedBindings()
        {
            static const vector<ShaderReservedBinding> s_list = []()
            {
                using R      = ShaderReservedBinding;
                namespace vk = shaderslot::vk;
                vector<R> list;
                auto      add = [&]( const utf8* pName, ShaderBindingKind kind, ShaderReservedLocation dx11, ShaderReservedLocation dx12,
                                ShaderReservedLocation vulkan, ShaderReservedLocation opengl )
                {
                    R r{};
                    r._name   = pName;
                    r._kind   = kind;
                    r._dx11   = dx11;
                    r._dx12   = dx12;
                    r._vulkan = vulkan;
                    r._opengl = opengl;
                    list.push_back( r );
                };
                // 슬롯 리소스는 DX11/DX12/GL 이 (space 0, register) 로 같고, Vulkan 만 세트 0 의 시프트된 binding 이다.
                auto slotB = [&]( uint32 reg )
                { return at( 0, reg ); };
                auto vkB = [&]( uint32 reg )
                { return at( 0, vk::kBShift + reg ); };
                auto vkT = [&]( uint32 reg )
                { return at( 0, vk::kTShift + reg ); };
                auto vkU = [&]( uint32 reg )
                { return at( 0, vk::kUShift + reg ); };

                // 상수버퍼 — b0 / b1
                add( shaderslot::cbname::kPass, ShaderBindingKind::ConstantBuffer,
                     slotB( shaderslot::kPassConstantBuffer ), slotB( shaderslot::kPassConstantBuffer ), vkB( shaderslot::kPassConstantBuffer ), slotB( shaderslot::kPassConstantBuffer ) );
                add( shaderslot::cbname::kMaterial, ShaderBindingKind::ConstantBuffer,
                     slotB( shaderslot::kMaterialConstantBuffer ), slotB( shaderslot::kMaterialConstantBuffer ), vkB( shaderslot::kMaterialConstantBuffer ), slotB( shaderslot::kMaterialConstantBuffer ) );
                add( shaderslot::cbname::kCull, ShaderBindingKind::ConstantBuffer,
                     slotB( shaderslot::kComputeConstantBuffer ), slotB( shaderslot::kComputeConstantBuffer ), vkB( shaderslot::kComputeConstantBuffer ), slotB( shaderslot::kComputeConstantBuffer ) );
                add( shaderslot::cbname::kSort, ShaderBindingKind::ConstantBuffer,
                     slotB( shaderslot::kComputeConstantBuffer ), slotB( shaderslot::kComputeConstantBuffer ), vkB( shaderslot::kComputeConstantBuffer ), slotB( shaderslot::kComputeConstantBuffer ) );
                add( shaderslot::cbname::kAnim, ShaderBindingKind::ConstantBuffer,
                     slotB( shaderslot::kComputeConstantBuffer ), slotB( shaderslot::kComputeConstantBuffer ), vkB( shaderslot::kComputeConstantBuffer ), slotB( shaderslot::kComputeConstantBuffer ) );
                // 루트/푸시 상수 블록 — DX12 b0 space2, DX11/GL b2 에뮬. Vulkan 은 푸시 상수라 바인딩 자리가 없다(리플렉션에 안 나온다).
                add( shaderslot::cbname::kRootConstants, ShaderBindingKind::ConstantBuffer,
                     slotB( shaderslot::kRootConstantEmulSlot ), at( shaderslot::kRootConstantSpace, shaderslot::kRootConstantRegister ), none(), slotB( shaderslot::kRootConstantEmulSlot ) );
                // GPUScene 버퍼 — 인스턴스 t4, 머티리얼 데이터 t9 (네 백엔드 공통)
                add( shaderslot::resname::kInstances, ShaderBindingKind::StructuredBuffer,
                     slotB( shaderslot::kInstanceBuffer ), slotB( shaderslot::kInstanceBuffer ), vkT( shaderslot::kInstanceBuffer ), slotB( shaderslot::kInstanceBuffer ) );
                add( shaderslot::resname::kMaterials, ShaderBindingKind::StructuredBuffer,
                     slotB( shaderslot::kMaterialBuffer ), slotB( shaderslot::kMaterialBuffer ), vkT( shaderslot::kMaterialBuffer ), slotB( shaderslot::kMaterialBuffer ) );
                // 컬링이 만든 가시 인스턴스 ID 목록 — 그래픽스 t10 (네 백엔드 공통).
                add( shaderslot::resname::kVisibleInstances, ShaderBindingKind::StructuredBuffer,
                     slotB( shaderslot::kVisibleInstanceBuffer ), slotB( shaderslot::kVisibleInstanceBuffer ),
                     vkT( shaderslot::kVisibleInstanceBuffer ), slotB( shaderslot::kVisibleInstanceBuffer ) );
                // gpucull 컴퓨트 — t0/t1 읽기, u0/u1 쓰기. GL 의 u# 은 SSBO SW_GL_UAV_BINDING0 + #.
                add( shaderslot::resname::kCullInstances, ShaderBindingKind::StructuredBuffer, slotB( 0 ), slotB( 0 ), vkT( 0 ), slotB( 0 ) );
                add( shaderslot::resname::kCullBatchInfo, ShaderBindingKind::StructuredBuffer, slotB( 1 ), slotB( 1 ), vkT( 1 ), slotB( 1 ) );
                add( shaderslot::resname::kCullIndirectArgs, ShaderBindingKind::RwStructuredBuffer, slotB( 0 ), slotB( 0 ), vkU( 0 ), slotB( shaderslot::gl::kUavBinding0 ) );
                add( shaderslot::resname::kCullVisibleIds, ShaderBindingKind::RwStructuredBuffer, slotB( 1 ), slotB( 1 ), vkU( 1 ), slotB( shaderslot::gl::kUavBinding0 + 1 ) );
                // instanceanim 컴퓨트 — 인스턴스 버퍼를 u0 으로 고쳐 쓴다.
                add( shaderslot::resname::kAnimInstancesRw, ShaderBindingKind::RwStructuredBuffer, slotB( 0 ), slotB( 0 ), vkU( 0 ), slotB( shaderslot::gl::kUavBinding0 ) );
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
                    add( s_arrEngineName[slotIndex].c_str(), ShaderBindingKind::Texture, slotB( reg ), none(), none(), slotB( reg ) );
                    add( s_arrEngineSamplerName[slotIndex].c_str(), ShaderBindingKind::Sampler, slotB( reg ), none(), none(), slotB( reg ) );
                }
                for ( uint32 slotIndex = 0; slotIndex < shaderslot::kMaterialTextureCount; ++slotIndex )
                {
                    s_arrMaterialName[slotIndex]        = string( shaderslot::resname::kMaterialTexture ) + to_string( slotIndex );
                    s_arrMaterialSamplerName[slotIndex] = s_arrMaterialName[slotIndex] + "Sampler";
                    const uint32 reg                    = shaderslot::kMaterialTexture0 + slotIndex;
                    add( s_arrMaterialName[slotIndex].c_str(), ShaderBindingKind::Texture, slotB( reg ), none(), none(), slotB( reg ) );
                    add( s_arrMaterialSamplerName[slotIndex].c_str(), ShaderBindingKind::Sampler, slotB( reg ), none(), none(), slotB( reg ) );
                }
                // 컴퓨트 RW 텍스처 슬롯 u4..u7 — 에뮬 백엔드만. GL 은 이미지 유닛(SSBO 와 다른 이름공간).
                static string s_arrRwTextureName[shaderslot::kComputeTextureUavSlotCount];
                for ( uint32 slotIndex = 0; slotIndex < shaderslot::kComputeTextureUavSlotCount; ++slotIndex )
                {
                    s_arrRwTextureName[slotIndex] = string( shaderslot::resname::kRwTextureSlot ) + to_string( slotIndex );
                    add( s_arrRwTextureName[slotIndex].c_str(), ShaderBindingKind::RwTexture, slotB( shaderslot::kComputeTextureUav0 + slotIndex ), none(), none(),
                         slotB( shaderslot::gl::kImageUnit0 + slotIndex ) );
                }
                // 네이티브 bindless 텍스처 배열 — DX12 t0 space1 / Vulkan set 1 binding 0. RW 배열은 DX12 u0 space1 / Vulkan set 1 binding 3. 에뮬에는 없다.
                add( shaderslot::resname::kBindlessTextures, ShaderBindingKind::Texture, none(),
                     at( shaderslot::bindless::kTextureSpace, 0 ), at( shaderslot::bindless::kVkTextureSet, shaderslot::bindless::kVkTextureBinding ), none() );
                add( shaderslot::resname::kBindlessRwTextures, ShaderBindingKind::RwTexture, none(),
                     at( shaderslot::bindless::kTextureSpace, 0 ), at( shaderslot::bindless::kVkTextureSet, shaderslot::bindless::kVkRwTextureBinding ), none() );
                // 정적 샘플러 세트 — DX12 s0..s6 배열 + s7 비교(루트 시그니처 정적 샘플러), Vulkan set 1 binding 1 배열 + binding 2 비교(immutable).
                // DX11 은 같은 세트를 s9..s15 샘플러 상태로 건다(bindStaticSamplers). GL 은 결합 샘플러뿐이라 없다.
                add( shaderslot::resname::kSamplers, ShaderBindingKind::Sampler, none(),
                     none(), at( shaderslot::bindless::kVkTextureSet, shaderslot::bindless::kVkSamplerBinding ), none() );
                static string s_arrSamplerName[shaderslot::kStaticSamplerArrayCount];
                for ( uint32 samplerIndex = 0; samplerIndex < shaderslot::kStaticSamplerArrayCount; ++samplerIndex )
                {
                    s_arrSamplerName[samplerIndex] = string( shaderslot::resname::kSamplerSlot ) + to_string( samplerIndex );
                    add( s_arrSamplerName[samplerIndex].c_str(), ShaderBindingKind::Sampler, slotB( shaderslot::dx11::kStaticSampler0 + samplerIndex ),
                         at( 0, samplerIndex ), none(), none() );
                }
                add( shaderslot::resname::kShadowSampler, ShaderBindingKind::Sampler, none(),
                     at( 0, shaderslot::kSamplerShadowCmp ), at( shaderslot::bindless::kVkTextureSet, shaderslot::bindless::kVkShadowSamplerBinding ), none() );
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
        using Internal     = ShaderBindingContractInternal;
        namespace bindless = shaderslot::bindless;
        namespace vk       = shaderslot::vk;
        uint32 issueCount{ 0 };

        vector<Internal::Seen> listSeen;
        Internal::collect( reflection, listSeen );

        const bool  bVulkan    = ( targetFormat == ShaderTargetFormat::SPIRV_Vulkan );
        const bool  bOpenGl    = ( targetFormat == ShaderTargetFormat::SPIRV_OpenGL );
        const bool  bDx12      = ( targetFormat == ShaderTargetFormat::DXIL_D3D12 );
        const utf8* pLocFormat = bVulkan ? "set/binding" : "space/register";

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

            const ShaderReservedLocation& expected = bVulkan ? pReserved->_vulkan : ( bOpenGl ? pReserved->_opengl : ( bDx12 ? pReserved->_dx12 : pReserved->_dx11 ) );
            if ( expected._bDeclared == false )
            {
                Internal::report( pOutIssue, shaderLabel, seen._name,
                                  string( "이 백엔드 계약에는 없는 예약 리소스가 선언돼 있습니다 (리플렉션 " ) + Internal::formatLocation( pLocFormat, seen._space, seen._bindPoint ) + ")",
                                  issueCount );
                continue;
            }
            if ( seen._bindPoint != expected._bind || seen._space != expected._space )
            {
                Internal::report( pOutIssue, shaderLabel, seen._name,
                                  string( "위치가 계약과 다릅니다 — 기대 " ) + Internal::formatLocation( pLocFormat, expected._space, expected._bind ) +
                                      ", 리플렉션 " + Internal::formatLocation( pLocFormat, seen._space, seen._bindPoint ),
                                  issueCount );
            }
        }

        // 2) 이름공간 충돌 + 3) 백엔드별 자리 규칙 (DX12 space / Vulkan set·밴드 / GL set 0)
        for ( size_t indexA = 0; indexA < listSeen.size(); ++indexA )
        {
            const Internal::Seen& seenA = listSeen[indexA];

            if ( bDx12 && seenA._kind != ShaderBindingKind::Unknown )
            {
                // space0 = 슬롯(루트 디스크립터·정적 샘플러), space1 = 텍스처 배열(t0, 무제한), space2 = 루트 상수(b0). 그 밖은 루트 시그니처에 없다.
                const Internal::RegisterClass registerClass = Internal::registerClassOf( seenA._kind );
                if ( seenA._space == 0 )
                {
                    uint32 limit = 0;
                    switch ( registerClass )
                    {
                        case Internal::RegisterClass::ConstantBuffer:
                            limit = shaderslot::kConstantBufferSlotCount;
                            break;
                        case Internal::RegisterClass::ShaderResource:
                            limit = shaderslot::kSrvSlotCount;
                            break;
                        case Internal::RegisterClass::UnorderedAccess:
                            limit = shaderslot::kComputeUavSlotCount;
                            break;
                        case Internal::RegisterClass::Sampler:
                            limit = shaderslot::kStaticSamplerCount;
                            break;
                        case Internal::RegisterClass::Other:
                        default:
                            break;
                    }
                    if ( limit > 0 && seenA._bindPoint >= limit )
                    {
                        Internal::report( pOutIssue, shaderLabel, seenA._name,
                                          string( Internal::registerClassLetter( registerClass ) ) + to_string( seenA._bindPoint ) + " 은 루트 시그니처의 슬롯 수(" + to_string( limit ) + ")를 넘습니다",
                                          issueCount );
                    }
                }
                else if ( seenA._space == bindless::kTextureSpace )
                {
                    const bool bKindOk = ( seenA._kind == ShaderBindingKind::Texture || seenA._kind == ShaderBindingKind::RwTexture );
                    if ( bKindOk == false || seenA._bindPoint != 0 || ( seenA._bindCount != Internal::Seen::kUnknownCount && seenA._bindCount != 0 ) )
                        Internal::report( pOutIssue, shaderLabel, seenA._name, "space1 은 무제한 텍스처 배열(t0 / u0, []) 전용입니다", issueCount );
                }
                else if ( seenA._space == shaderslot::kRootConstantSpace )
                {
                    if ( seenA._kind != ShaderBindingKind::ConstantBuffer || seenA._bindPoint != shaderslot::kRootConstantRegister )
                        Internal::report( pOutIssue, shaderLabel, seenA._name, "space2 는 루트 상수(b0) 전용입니다", issueCount );
                }
                else
                {
                    Internal::report( pOutIssue, shaderLabel, seenA._name,
                                      string( "루트 시그니처에 없는 register space " ) + to_string( seenA._space ) + " 을 참조합니다", issueCount );
                }
            }
            if ( bVulkan && seenA._kind != ShaderBindingKind::Unknown )
            {
                if ( seenA._space == 0 )
                {
                    const Internal::RegisterClass band = Internal::vulkanBandOf( seenA._bindPoint );
                    if ( band == Internal::RegisterClass::Other )
                    {
                        Internal::report( pOutIssue, shaderLabel, seenA._name,
                                          string( "세트 0 의 슬롯 밴드 밖 binding " ) + to_string( seenA._bindPoint ) + " 입니다 (seenB 0.., t " + to_string( vk::kTShift ) + ".., u " + to_string( vk::kUShift ) + "..)",
                                          issueCount );
                    }
                    else if ( Internal::vulkanBandAcceptsKind( band, seenA._kind ) == false )
                    {
                        Internal::report( pOutIssue, shaderLabel, seenA._name,
                                          string( "binding " ) + to_string( seenA._bindPoint ) + " 은 " + Internal::registerClassLetter( band ) + " 밴드인데 리소스 종류가 " + Internal::kindName( seenA._kind ) + " 입니다",
                                          issueCount );
                    }
                }
                else if ( seenA._space == bindless::kVkTextureSet )
                {
                    const bool bArray   = ( seenA._bindPoint == bindless::kVkTextureBinding ) && ( seenA._kind == ShaderBindingKind::Texture || seenA._kind == ShaderBindingKind::Sampler );
                    const bool bSampler = ( seenA._bindPoint == bindless::kVkSamplerBinding || seenA._bindPoint == bindless::kVkShadowSamplerBinding ) && seenA._kind == ShaderBindingKind::Sampler;
                    const bool bRwArray = ( seenA._bindPoint == bindless::kVkRwTextureBinding ) && seenA._kind == ShaderBindingKind::RwTexture;
                    if ( bArray == false && bSampler == false && bRwArray == false )
                        Internal::report( pOutIssue, shaderLabel, seenA._name, "세트 1 은 텍스처 배열(binding 0)·샘플러(binding 1·2)·RW 텍스처 배열(binding 3) 전용입니다", issueCount );
                }
                else
                {
                    Internal::report( pOutIssue, shaderLabel, seenA._name,
                                      string( "파이프라인 레이아웃에 없는 descriptor set " ) + to_string( seenA._space ) + " 을 참조합니다 (세트는 0·1)", issueCount );
                }
            }
            if ( bOpenGl && seenA._space != 0 )
            {
                Internal::report( pOutIssue, shaderLabel, seenA._name,
                                  string( "OpenGL 은 descriptor set 을 무시하는데 set " ) + to_string( seenA._space ) + " 로 선언돼 있습니다 — binding " + to_string( seenA._bindPoint ) + " 하나로 취급됩니다",
                                  issueCount );
            }

            for ( size_t indexB = indexA + 1; indexB < listSeen.size(); ++indexB )
            {
                const Internal::Seen& seenB = listSeen[indexB];
                if ( seenA._bindPoint != seenB._bindPoint )
                    continue;

                bool   bCollide{ false };
                string where;
                if ( bVulkan )
                {
                    bCollide = ( seenA._space == seenB._space );
                    where    = Internal::formatLocation( "set/binding", seenA._space, seenA._bindPoint );
                }
                else if ( bOpenGl )
                {
                    const Internal::GlNamespace nsA = Internal::glNamespaceOf( seenA._kind );
                    bCollide                        = ( nsA != Internal::GlNamespace::Other && nsA == Internal::glNamespaceOf( seenB._kind ) );
                    where                           = string( Internal::glNamespaceName( nsA ) ) + " binding " + to_string( seenA._bindPoint );
                }
                else
                {
                    const Internal::RegisterClass clsA = Internal::registerClassOf( seenA._kind );
                    bCollide                           = ( seenA._space == seenB._space && clsA != Internal::RegisterClass::Other && clsA == Internal::registerClassOf( seenB._kind ) );
                    where                              = string( Internal::registerClassLetter( clsA ) ) + to_string( seenA._bindPoint ) + " space" + to_string( seenA._space );
                }
                if ( bCollide )
                {
                    Internal::report( pOutIssue, shaderLabel, seenA._name,
                                      string( "'" ) + seenB._name + "' 와 같은 자리를 차지합니다 (" + where + ")", issueCount );
                }
            }
        }

        return issueCount;
    }
} // namespace sw
