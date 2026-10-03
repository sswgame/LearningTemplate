#include "pch.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingContract.h"

#include "Core/Concurrency/atomic.h"
#include "Core/Container/unordered_map.h"
#include "Core/Log/Logger.h"

#include "Engine/Graphics/RHI/RHITypes.h"
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

            /// @brief 한 리소스를 이름공간 키로 만들 때 쓰는 레지스터 종류입니다(DX 기준).
            enum class RegisterClass : uint8
            {
                ConstantBuffer,  // b
                ShaderResource,  // t
                UnorderedAccess, // u
                Sampler,         // s
                Other
            };

            /// @brief GL 이름공간입니다. GL 은 set 을 버리고 binding 만 보므로 종류별로 번호 공간이 갈립니다.
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
                        return "Unknown";
                }
            }

            static bool isSpirv( ShaderTargetFormat target )
            {
                return target == ShaderTargetFormat::SPIRV_Vulkan || target == ShaderTargetFormat::SPIRV_OpenGL;
            }

            /**
             * @brief 종류가 계약과 "같다" 고 볼 수 있는지 반환합니다.
             * @details SPIR-V 는 읽기/쓰기 구조버퍼를 모두 StorageBuffer 로만 보고하고, 결합 이미지 샘플러는
             *          텍스처와 샘플러를 하나로 보고합니다. 그 차이는 어긋남이 아닙니다.
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
             * @brief `[shift, shift + width)` 안에 드는지 반환합니다.
             * @details 부호 없는 뺄셈이라 `binding < shift` 면 아주 큰 값으로 감겨 width 를 넘습니다.
             *          그래서 하한 비교가 따로 필요 없습니다. b 범위의 shift 는 0 이라 `binding >= 0` 이
             *          늘 참이었고, 컴파일러가 그것을 짚어 줬습니다.
             */
            static bool isInRange( uint32 binding, uint32 shift, uint32 width ) { return ( binding - shift ) < width; }

            /// @brief Vulkan 세트 0 binding 이 어느 레지스터 범위(b/t/u)인지 반환합니다. 범위 밖이면 Other 입니다.
            static RegisterClass vulkanRangeClassOf( uint32 binding )
            {
                namespace vk = shaderslot::vk;
                if ( isInRange( binding, vk::kBShift, vk::kRangeSize ) )
                    return RegisterClass::ConstantBuffer;
                if ( isInRange( binding, vk::kTShift, vk::kRangeSize ) )
                    return RegisterClass::ShaderResource;
                if ( isInRange( binding, vk::kUShift, vk::kRangeSize ) )
                    return RegisterClass::UnorderedAccess;
                return RegisterClass::Other;
            }

            /// @brief SPIR-V 는 읽기/쓰기 구조버퍼를 구분하지 않으므로 t/u 범위 모두 StorageBuffer 를 받습니다.
            static bool vulkanRangeAcceptsKind( RegisterClass rangeClass, ShaderBindingKind kind )
            {
                switch ( rangeClass )
                {
                    case RegisterClass::ConstantBuffer:
                        return kind == ShaderBindingKind::ConstantBuffer;
                    case RegisterClass::ShaderResource:
                    case RegisterClass::UnorderedAccess:
                        return kind == ShaderBindingKind::StructuredBuffer || kind == ShaderBindingKind::RwStructuredBuffer;
                    case RegisterClass::Sampler:
                    case RegisterClass::Other:
                        return false;
                }
            }

            struct ReflectedBinding
            {
                string            _name;
                ShaderBindingKind _kind{ ShaderBindingKind::Unknown };
                uint32            _space{ 0 };
                uint32            _bindPoint{ 0 };
                uint32            _bindCount{ kUnknownCount }; ///< 리소스 목록에서 왔으면 배열 크기(무제한=0), CB 목록만 있으면 모름

                static constexpr uint32 kUnknownCount = 0xFFFFFFFFu;
            };

            /// @brief CB 목록과 리소스 목록을 (이름, 종류)로 중복 없이 합칩니다. DX 리플렉션은 cbuffer 를 양쪽에 다 넣습니다.
            static void collect( const ShaderReflectionData& reflection, vector<ReflectedBinding>& outList )
            {
                auto push = [&]( const string& name, ShaderBindingKind kind, uint32 space, uint32 bindPoint, uint32 bindCount )
                {
                    for ( const ReflectedBinding& reflected : outList )
                    {
                        if ( reflected._name == name && reflected._kind == kind )
                            return;
                    }
                    ReflectedBinding reflected{};
                    reflected._name      = name;
                    reflected._kind      = kind;
                    reflected._space     = space;
                    reflected._bindPoint = bindPoint;
                    reflected._bindCount = bindCount;
                    outList.push_back( std::move( reflected ) );
                };
                // 리소스 목록이 바인딩 위치의 1차 출처다(cbuffer 도 여기 들어 있다). CB 목록은 그 다음이다. 리플렉터가
                // CB 쪽 bindPoint 를 못 채우는 경우가 있었다(DXIL, move 뒤 이름 비교).
                for ( const ShaderResourceBinding& res : reflection._listResource )
                {
                    const ShaderBindingKind kind = ShaderBindingLayout::kindFromTypeLabel( static_cast<string_view>( res._type ) );
                    push( res._name, kind, res._registerSpace, res._bindPoint, res._bindCount );
                }
                for ( const ShaderBufferInfo& cb : reflection._listConstantBuffer )
                    push( cb._name, ShaderBindingKind::ConstantBuffer, cb._registerSpace, cb._bindPoint, ReflectedBinding::kUnknownCount );
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
            ShaderReservedLocation location{};
            location._space     = space;
            location._bind      = bind;
            location._bDeclared = true;
            return location;
        }

        ShaderReservedLocation none()
        {
            return ShaderReservedLocation{};
        }

        /// @brief 계약 표입니다. 값은 모두 shaderslot 에서 옵니다. 백엔드가 선언하지 않는 자리는 none() 입니다.
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
                // 슬롯 리소스는 DX11 · DX12 · GL 이 (space 0, register) 로 같고, Vulkan 만 세트 0 의 시프트된 binding 이다.
                auto slotB = [&]( uint32 reg )
                { return at( 0, reg ); };
                auto vkB = [&]( uint32 reg )
                { return at( 0, vk::kBShift + reg ); };
                auto vkT = [&]( uint32 reg )
                { return at( 0, vk::kTShift + reg ); };
                auto vkU = [&]( uint32 reg )
                { return at( 0, vk::kUShift + reg ); };

                // 상수버퍼: b0 / b1
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
                // 루트/푸시 상수 블록: DX12 b0 space2, DX11 · GL b2 에뮬. Vulkan 은 푸시 상수라 바인딩 자리가 없다(리플렉션에 안 나온다).
                add( shaderslot::cbname::kRootConstants, ShaderBindingKind::ConstantBuffer,
                     slotB( shaderslot::kRootConstantEmulationSlot ), at( shaderslot::kRootConstantSpace, shaderslot::kRootConstantRegister ), none(), slotB( shaderslot::kRootConstantEmulationSlot ) );
                // GPUScene 버퍼: 인스턴스 t4, 머티리얼 데이터 t9 (네 백엔드 공통)
                add( shaderslot::resname::kInstances, ShaderBindingKind::StructuredBuffer,
                     slotB( shaderslot::kInstanceBuffer ), slotB( shaderslot::kInstanceBuffer ), vkT( shaderslot::kInstanceBuffer ), slotB( shaderslot::kInstanceBuffer ) );
                add( shaderslot::resname::kMaterials, ShaderBindingKind::StructuredBuffer,
                     slotB( shaderslot::kMaterialBuffer ), slotB( shaderslot::kMaterialBuffer ), vkT( shaderslot::kMaterialBuffer ), slotB( shaderslot::kMaterialBuffer ) );
                // 컬링이 만든 가시 인스턴스 ID 목록: 그래픽스 t10 (네 백엔드 공통).
                add( shaderslot::resname::kVisibleInstances, ShaderBindingKind::StructuredBuffer,
                     slotB( shaderslot::kVisibleInstanceBuffer ), slotB( shaderslot::kVisibleInstanceBuffer ),
                     vkT( shaderslot::kVisibleInstanceBuffer ), slotB( shaderslot::kVisibleInstanceBuffer ) );
                // GPU 가 변형한 정점 풀 t11 · 씬 라이트 목록 t12 (네 백엔드 공통).
                add( shaderslot::resname::kMorphVertices, ShaderBindingKind::StructuredBuffer,
                     slotB( shaderslot::kMorphVertexBuffer ), slotB( shaderslot::kMorphVertexBuffer ), vkT( shaderslot::kMorphVertexBuffer ), slotB( shaderslot::kMorphVertexBuffer ) );
                add( shaderslot::resname::kLights, ShaderBindingKind::StructuredBuffer,
                     slotB( shaderslot::kLightBuffer ), slotB( shaderslot::kLightBuffer ), vkT( shaderslot::kLightBuffer ), slotB( shaderslot::kLightBuffer ) );
                // 씬 배치 표: 그래픽스 t13 (네 백엔드 공통). 정점 셰이더가 자기 배치 번호로 읽는다.
                add( shaderslot::resname::kBatches, ShaderBindingKind::StructuredBuffer,
                     slotB( shaderslot::kBatchBuffer ), slotB( shaderslot::kBatchBuffer ), vkT( shaderslot::kBatchBuffer ), slotB( shaderslot::kBatchBuffer ) );
                // gpucull 컴퓨트: t0/t1 읽기, u0/u1 쓰기. GL 의 u# 은 SSBO SW_GL_UAV_BINDING0 + #.
                add( shaderslot::resname::kCullInstances, ShaderBindingKind::StructuredBuffer, slotB( 0 ), slotB( 0 ), vkT( 0 ), slotB( 0 ) );
                add( shaderslot::resname::kCullBatchInfo, ShaderBindingKind::StructuredBuffer, slotB( 1 ), slotB( 1 ), vkT( 1 ), slotB( 1 ) );
                add( shaderslot::resname::kCullIndirectArgs, ShaderBindingKind::RwStructuredBuffer, slotB( 0 ), slotB( 0 ), vkU( 0 ), slotB( shaderslot::gl::kUavBinding0 ) );
                add( shaderslot::resname::kCullVisibleIds, ShaderBindingKind::RwStructuredBuffer, slotB( 1 ), slotB( 1 ), vkU( 1 ), slotB( shaderslot::gl::kUavBinding0 + 1 ) );
                // instanceanim 컴퓨트: 인스턴스 버퍼를 u0 으로 고쳐 쓴다.
                add( shaderslot::resname::kAnimInstancesRw, ShaderBindingKind::RwStructuredBuffer, slotB( 0 ), slotB( 0 ), vkU( 0 ), slotB( shaderslot::gl::kUavBinding0 ) );
                // 엔진 텍스처 슬롯 t0..t3 / 머티리얼 텍스처 t5..t8: 에뮬 백엔드(DX11 · GL)만. Vulkan · DX12 는 선언 자체가 없어야 한다.
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
                // 컴퓨트 RW 텍스처 슬롯 u4..u7: 에뮬 백엔드만. GL 은 이미지 유닛(SSBO 와 다른 이름공간).
                static string s_arrRwTextureName[shaderslot::kComputeTextureUavSlotCount];
                for ( uint32 slotIndex = 0; slotIndex < shaderslot::kComputeTextureUavSlotCount; ++slotIndex )
                {
                    s_arrRwTextureName[slotIndex] = string( shaderslot::resname::kRwTextureSlot ) + to_string( slotIndex );
                    add( s_arrRwTextureName[slotIndex].c_str(), ShaderBindingKind::RwTexture, slotB( shaderslot::kComputeTextureUav0 + slotIndex ), none(), none(),
                         slotB( shaderslot::gl::kImageUnit0 + slotIndex ) );
                }
                // 네이티브 bindless 텍스처 배열: DX12 t0 space1 / Vulkan set 1 binding 0. RW 배열은 DX12 u0 space1 / Vulkan set 1 binding 3. 에뮬에는 없다.
                add( shaderslot::resname::kBindlessTextures, ShaderBindingKind::Texture, none(),
                     at( shaderslot::bindless::kTextureSpace, 0 ), at( shaderslot::bindless::kVkTextureSet, shaderslot::bindless::kVkTextureBinding ), none() );
                add( shaderslot::resname::kBindlessRwTextures, ShaderBindingKind::RwTexture, none(),
                     at( shaderslot::bindless::kTextureSpace, 0 ), at( shaderslot::bindless::kVkTextureSet, shaderslot::bindless::kVkRwTextureBinding ), none() );
                // 정적 샘플러 세트: DX12 s0..s6 배열 + s7 비교(루트 시그니처 정적 샘플러), Vulkan set 1 binding 1 배열 + binding 2 비교(immutable).
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

        vector<Internal::ReflectedBinding> listReflected;
        Internal::collect( reflection, listReflected );

        const bool  bVulkan         = ( targetFormat == ShaderTargetFormat::SPIRV_Vulkan );
        const bool  bOpenGl         = ( targetFormat == ShaderTargetFormat::SPIRV_OpenGL );
        const bool  bDx12           = ( targetFormat == ShaderTargetFormat::DXIL_D3D12 );
        const utf8* pLocationFormat = bVulkan ? "set/binding" : "space/register";

        // 1) 예약 리소스: 종류와 위치
        for ( const Internal::ReflectedBinding& reflected : listReflected )
        {
            const ShaderReservedBinding* pReserved{ nullptr };
            for ( const ShaderReservedBinding& reserved : reservedBindings() )
            {
                if ( reflected._name == reserved._name )
                {
                    pReserved = &reserved;
                    break;
                }
            }
            if ( pReserved == nullptr )
                continue;

            if ( Internal::kindMatches( pReserved->_kind, reflected._kind, targetFormat ) == false )
            {
                Internal::report( pOutIssue, shaderLabel, reflected._name,
                                  string( "종류가 계약과 다릅니다 — 기대 " ) + Internal::kindName( pReserved->_kind ) + ", 리플렉션 " + Internal::kindName( reflected._kind ),
                                  issueCount );
            }

            const ShaderReservedLocation& expected = bVulkan ? pReserved->_vulkan : ( bOpenGl ? pReserved->_opengl : ( bDx12 ? pReserved->_dx12 : pReserved->_dx11 ) );
            if ( expected._bDeclared == false )
            {
                Internal::report( pOutIssue, shaderLabel, reflected._name,
                                  string( "이 백엔드 계약에는 없는 예약 리소스가 선언돼 있습니다 (리플렉션 " ) + Internal::formatLocation( pLocationFormat, reflected._space, reflected._bindPoint ) + ")",
                                  issueCount );
                continue;
            }
            if ( reflected._bindPoint != expected._bind || reflected._space != expected._space )
            {
                Internal::report( pOutIssue, shaderLabel, reflected._name,
                                  string( "위치가 계약과 다릅니다 — 기대 " ) + Internal::formatLocation( pLocationFormat, expected._space, expected._bind ) +
                                      ", 리플렉션 " + Internal::formatLocation( pLocationFormat, reflected._space, reflected._bindPoint ),
                                  issueCount );
            }
        }

        // 2) 이름공간 충돌 + 3) 백엔드별 자리 규칙 (DX12 space / Vulkan set·범위 / GL set 0)
        for ( size_t indexA = 0; indexA < listReflected.size(); ++indexA )
        {
            const Internal::ReflectedBinding& reflectedA = listReflected[indexA];

            if ( bDx12 && reflectedA._kind != ShaderBindingKind::Unknown )
            {
                // space0 = 슬롯(CB 는 루트 CBV, t · u 는 디스크립터 테이블, 그리고 정적 샘플러), space1 = 텍스처 배열(t0, 무제한), space2 = 루트 상수(b0). 그 밖은 루트 시그니처에 없다.
                const Internal::RegisterClass registerClass = Internal::registerClassOf( reflectedA._kind );
                if ( reflectedA._space == 0 )
                {
                    uint32 limit = 0;
                    switch ( registerClass )
                    {
                        case Internal::RegisterClass::ConstantBuffer:
                        {
                            limit = shaderslot::kConstantBufferSlotCount;
                            break;
                        }
                        case Internal::RegisterClass::ShaderResource:
                        {
                            limit = shaderslot::kSrvSlotCount;
                            break;
                        }
                        case Internal::RegisterClass::UnorderedAccess:
                        {
                            limit = shaderslot::kComputeUavSlotCount;
                            break;
                        }
                        case Internal::RegisterClass::Sampler:
                        {
                            limit = shaderslot::kStaticSamplerCount;
                            break;
                        }
                        case Internal::RegisterClass::Other:
                            break;
                    }
                    if ( limit > 0 && reflectedA._bindPoint >= limit )
                    {
                        Internal::report( pOutIssue, shaderLabel, reflectedA._name,
                                          string( Internal::registerClassLetter( registerClass ) ) + to_string( reflectedA._bindPoint ) + " 은 루트 시그니처의 슬롯 수(" + to_string( limit ) + ")를 넘습니다",
                                          issueCount );
                    }
                }
                else if ( reflectedA._space == bindless::kTextureSpace )
                {
                    const bool bKindOk = ( reflectedA._kind == ShaderBindingKind::Texture || reflectedA._kind == ShaderBindingKind::RwTexture );
                    if ( bKindOk == false || reflectedA._bindPoint != 0 || ( reflectedA._bindCount != Internal::ReflectedBinding::kUnknownCount && reflectedA._bindCount != 0 ) )
                        Internal::report( pOutIssue, shaderLabel, reflectedA._name, "space1 은 무제한 텍스처 배열(t0 / u0, []) 전용입니다", issueCount );
                }
                else if ( reflectedA._space == shaderslot::kRootConstantSpace )
                {
                    if ( reflectedA._kind != ShaderBindingKind::ConstantBuffer || reflectedA._bindPoint != shaderslot::kRootConstantRegister )
                        Internal::report( pOutIssue, shaderLabel, reflectedA._name, "space2 는 루트 상수(b0) 전용입니다", issueCount );
                }
                else
                {
                    Internal::report( pOutIssue, shaderLabel, reflectedA._name,
                                      string( "루트 시그니처에 없는 register space " ) + to_string( reflectedA._space ) + " 을 참조합니다", issueCount );
                }
            }
            if ( bVulkan && reflectedA._kind != ShaderBindingKind::Unknown )
            {
                if ( reflectedA._space == 0 )
                {
                    const Internal::RegisterClass rangeClass = Internal::vulkanRangeClassOf( reflectedA._bindPoint );
                    if ( rangeClass == Internal::RegisterClass::Other )
                    {
                        Internal::report( pOutIssue, shaderLabel, reflectedA._name,
                                          string( "세트 0 의 슬롯 범위 밖 binding " ) + to_string( reflectedA._bindPoint ) + " 입니다 (b 0.., t " + to_string( vk::kTShift ) + ".., u " + to_string( vk::kUShift ) + "..)",
                                          issueCount );
                    }
                    else if ( Internal::vulkanRangeAcceptsKind( rangeClass, reflectedA._kind ) == false )
                    {
                        Internal::report( pOutIssue, shaderLabel, reflectedA._name,
                                          string( "binding " ) + to_string( reflectedA._bindPoint ) + " 은 " + Internal::registerClassLetter( rangeClass ) + " 범위인데 리소스 종류가 " + Internal::kindName( reflectedA._kind ) + " 입니다",
                                          issueCount );
                    }
                }
                else if ( reflectedA._space == bindless::kVkTextureSet )
                {
                    const bool bArray   = ( reflectedA._bindPoint == bindless::kVkTextureBinding ) && ( reflectedA._kind == ShaderBindingKind::Texture || reflectedA._kind == ShaderBindingKind::Sampler );
                    const bool bSampler = ( reflectedA._bindPoint == bindless::kVkSamplerBinding || reflectedA._bindPoint == bindless::kVkShadowSamplerBinding ) && reflectedA._kind == ShaderBindingKind::Sampler;
                    const bool bRwArray = ( reflectedA._bindPoint == bindless::kVkRwTextureBinding ) && reflectedA._kind == ShaderBindingKind::RwTexture;
                    if ( bArray == false && bSampler == false && bRwArray == false )
                        Internal::report( pOutIssue, shaderLabel, reflectedA._name, "세트 1 은 텍스처 배열(binding 0)·샘플러(binding 1·2)·RW 텍스처 배열(binding 3) 전용입니다", issueCount );
                }
                else
                {
                    Internal::report( pOutIssue, shaderLabel, reflectedA._name,
                                      string( "파이프라인 레이아웃에 없는 descriptor set " ) + to_string( reflectedA._space ) + " 을 참조합니다 (세트는 0·1)", issueCount );
                }
            }
            if ( bOpenGl && reflectedA._space != 0 )
            {
                Internal::report( pOutIssue, shaderLabel, reflectedA._name,
                                  string( "OpenGL 은 descriptor set 을 무시하는데 set " ) + to_string( reflectedA._space ) + " 로 선언돼 있습니다 — binding " + to_string( reflectedA._bindPoint ) + " 하나로 취급됩니다",
                                  issueCount );
            }

            for ( size_t indexB = indexA + 1; indexB < listReflected.size(); ++indexB )
            {
                const Internal::ReflectedBinding& reflectedB = listReflected[indexB];
                if ( reflectedA._bindPoint != reflectedB._bindPoint )
                    continue;

                bool   bCollide{ false };
                string where;
                if ( bVulkan )
                {
                    bCollide = ( reflectedA._space == reflectedB._space );
                    where    = Internal::formatLocation( "set/binding", reflectedA._space, reflectedA._bindPoint );
                }
                else if ( bOpenGl )
                {
                    const Internal::GlNamespace nsA = Internal::glNamespaceOf( reflectedA._kind );
                    bCollide                        = ( nsA != Internal::GlNamespace::Other && nsA == Internal::glNamespaceOf( reflectedB._kind ) );
                    where                           = string( Internal::glNamespaceName( nsA ) ) + " binding " + to_string( reflectedA._bindPoint );
                }
                else
                {
                    const Internal::RegisterClass registerClassA = Internal::registerClassOf( reflectedA._kind );
                    bCollide                                     = ( reflectedA._space == reflectedB._space && registerClassA != Internal::RegisterClass::Other && registerClassA == Internal::registerClassOf( reflectedB._kind ) );
                    where                                        = string( Internal::registerClassLetter( registerClassA ) ) + to_string( reflectedA._bindPoint ) + " space" + to_string( reflectedA._space );
                }
                if ( bCollide )
                {
                    Internal::report( pOutIssue, shaderLabel, reflectedA._name,
                                      string( "'" ) + reflectedB._name + "' 와 같은 자리를 차지합니다 (" + where + ")", issueCount );
                }
            }
        }

        // 5) 정점 입력: 시맨틱이 정점 레이아웃 표(constant::arrVertexAttribute)에 있고, Vulkan · GL 은 location 까지 같은가.
        //    DX 는 시맨틱 이름으로 묶어 순서가 달라도 맞지만, 두 백엔드는 **선언 순서**가 location 이라 중간 속성을
        //    빼먹으면 그 뒤가 한 칸씩 당겨진다(색을 읽으려다 노멀을 읽는다). 예전에는 픽셀로만 드러났다.
        for ( const ShaderVertexInputInfo& input : reflection._listVertexInput )
        {
            const RHIVertexAttribute* pAttribute{ nullptr };
            for ( const RHIVertexAttribute& attribute : constant::arrVertexAttribute )
            {
                if ( input._semantic == attribute._pSemanticName )
                {
                    pAttribute = &attribute;
                    break;
                }
            }
            const string label = input._semantic + to_string( input._semanticIndex );
            if ( pAttribute == nullptr || input._semanticIndex != 0 )
            {
                Internal::report( pOutIssue, shaderLabel, label,
                                  "정점 레이아웃 표(constant::arrVertexAttribute)에 없는 정점 입력입니다 — 정점을 받는 셰이더는 SwVertexInput 을 쓸 것",
                                  issueCount );
                continue;
            }
            if ( ( bVulkan || bOpenGl ) && input._location != pAttribute->_location )
            {
                Internal::report( pOutIssue, shaderLabel, label,
                                  string( "location 이 정점 레이아웃 표와 다릅니다 — 기대 " ) + to_string( pAttribute->_location ) + ", 리플렉션 " +
                                      to_string( input._location ) + " (Vulkan·GL 은 선언 순서로 location 을 매긴다 — SwVertexInput 을 쓸 것)",
                                  issueCount );
            }
        }

        return issueCount;
    }
} // namespace sw
