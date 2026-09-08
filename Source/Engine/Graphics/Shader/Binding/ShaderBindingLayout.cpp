#include "pch.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    SW_LOG_CALLER( "ShaderBindingLayout" );

    namespace
    {
        /** @brief 리플렉션 문자열 타입 라벨 → ShaderBindingKind. */
        ShaderBindingKind shaderBindingKindFromTypeLabel( string_view typeLabel )
        {
            if ( typeLabel == "Texture" || typeLabel == "TextureOrSampler" )
                return ShaderBindingKind::Texture;
            if ( typeLabel == "Sampler" )
                return ShaderBindingKind::Sampler;
            if ( typeLabel == "ConstantBuffer" )
                return ShaderBindingKind::ConstantBuffer;
            if ( typeLabel == "StructuredBuffer" || typeLabel == "StorageBuffer" || typeLabel == "ByteAddressBuffer" )
                return ShaderBindingKind::StructuredBuffer;
            if ( typeLabel == "UAV" || typeLabel == "RWStructuredBuffer" || typeLabel == "RWByteAddressBuffer" )
                return ShaderBindingKind::RwStructuredBuffer;
            if ( typeLabel == "RWTexture" )
                return ShaderBindingKind::RwTexture;
            return ShaderBindingKind::Unknown;
        }

        ShaderStageFlag toStageFlag( ShaderStage stage )
        {
            switch ( stage )
            {
                case ShaderStage::Vertex:
                    return ShaderStageFlag::Vertex;
                case ShaderStage::Pixel:
                    return ShaderStageFlag::Pixel;
                case ShaderStage::Compute:
                    return ShaderStageFlag::Compute;
                case ShaderStage::Geometry:
                    return ShaderStageFlag::Geometry;
                case ShaderStage::Hull:
                    return ShaderStageFlag::Hull;
                case ShaderStage::Domain:
                    return ShaderStageFlag::Domain;
                case ShaderStage::Mesh:
                    return ShaderStageFlag::Mesh;
                case ShaderStage::Amplification:
                    return ShaderStageFlag::Amplification;
                case ShaderStage::Count:
                default:
                    return ShaderStageFlag::None;
            }
        }
    } // namespace

    ShaderBindingKind ShaderBindingLayout::kindFromTypeLabel( string_view typeLabel )
    {
        return shaderBindingKindFromTypeLabel( typeLabel );
    }

    ShaderBindingLayout ShaderBindingLayout::build( const vector<pair<ShaderStage, const ShaderReflectionData*>>& listStageReflection )
    {
        ShaderBindingLayout layout;

        auto slotKey = []( ShaderBindingKind kind, uint32 space, uint32 reg ) -> uint64
        {
            return ( static_cast<uint64>( kind ) << 48 ) ^ ( static_cast<uint64>( space ) << 24 ) ^ static_cast<uint64>( reg );
        };

        unordered_map<uint64, uint32> mapKeyToIndex;

        auto touchSlot = [&]( ShaderBindingKind kind, const hashed_string& name, uint32 space, uint32 reg,
                              uint32 arrayCount, ShaderStageFlag visibility ) -> ShaderBindingSlot&
        {
            const uint64 key = slotKey( kind, space, reg );
            auto         it  = mapKeyToIndex.find( key );
            if ( it != mapKeyToIndex.end() )
            {
                ShaderBindingSlot& slot = layout._listSlot[it->second];
                slot._visibility        = static_cast<ShaderStageFlag>( static_cast<uint8>( slot._visibility ) | static_cast<uint8>( visibility ) );
                if ( slot._name.getHash() == 0 )
                    slot._name = name;
                return slot;
            }

            ShaderBindingSlot slot{};
            slot._name          = name;
            slot._kind          = kind;
            slot._space         = space;
            slot._registerIndex = reg;
            slot._arrayCount    = arrayCount;
            slot._visibility    = visibility;

            const uint32 index = static_cast<uint32>( layout._listSlot.size() );
            layout._listSlot.push_back( std::move( slot ) );
            mapKeyToIndex[key] = index;
            return layout._listSlot[index];
        };

        for ( const auto& [stage, pReflection] : listStageReflection )
        {
            if ( pReflection == nullptr )
                continue;
            const ShaderStageFlag visibility = toStageFlag( stage );

            // 1) 상수 버퍼 — 멤버 오프셋까지 채운다.
            for ( const ShaderBufferInfo& cb : pReflection->_listConstantBuffer )
            {
                ShaderBindingSlot& slot = touchSlot( ShaderBindingKind::ConstantBuffer, hashed_string( static_cast<std::string_view>( cb._name ) ),
                                                     cb._registerSpace, cb._bindPoint, 1, visibility );
                if ( slot._listCbMember.empty() )
                {
                    slot._listCbMember = cb._listVariable;
                    slot._cbTotalSize  = cb._totalSize;
                }
                else if ( slot._cbTotalSize == 0 )
                {
                    slot._cbTotalSize = cb._totalSize;
                }
            }

            // 2) 텍스처 / 샘플러 / 버퍼 바인딩
            for ( const ShaderResourceBinding& res : pReflection->_listResource )
            {
                const ShaderBindingKind kind = shaderBindingKindFromTypeLabel( static_cast<std::string_view>( res._type ) );
                if ( kind == ShaderBindingKind::Unknown )
                    continue;
                // CB 는 위에서 이미 멤버 정보까지 등록했으므로 가시성만 갱신.
                if ( kind == ShaderBindingKind::ConstantBuffer )
                {
                    touchSlot( kind, hashed_string( static_cast<std::string_view>( res._name ) ),
                               res._registerSpace, res._bindPoint, 1, visibility );
                    continue;
                }
                touchSlot( kind, hashed_string( static_cast<std::string_view>( res._name ) ),
                           res._registerSpace, res._bindPoint, res._bindCount, visibility );
            }
        }

        // 3) 구조버퍼 원소 stride — 리플렉션의 원소 레이아웃(_listStructuredElement)을 같은 이름의 슬롯에 붙인다.
        //    이 슬롯에 거는 버퍼(GPUScene 머티리얼 데이터, 폴백 원소)는 이 stride 로 만들어야 한다.
        for ( const auto& [stage, pReflection] : listStageReflection )
        {
            if ( pReflection == nullptr )
                continue;
            for ( const ShaderBufferInfo& element : pReflection->_listStructuredElement )
            {
                if ( element._totalSize == 0 )
                    continue;
                const hashed_string name{ static_cast<std::string_view>( element._name ) };
                for ( ShaderBindingSlot& slot : layout._listSlot )
                {
                    const bool bStructured = ( slot._kind == ShaderBindingKind::StructuredBuffer || slot._kind == ShaderBindingKind::RwStructuredBuffer );
                    if ( bStructured && slot._name == name && slot._elementStride == 0 )
                        slot._elementStride = element._totalSize;
                }
            }
        }

        layout.rebuildIndex();
        layout.buildBindPlan();
        return layout;
    }

    namespace
    {
        /** @brief `g_ShadowMap` / `g_ShadowMapIndex` / `ShadowMap` → `"ShadowMap"` (레지스트리 조회 키). */
        string_view canonicalResourceView( string_view identifier, bool bStripIndexSuffix )
        {
            string_view name = identifier;
            if ( name.size() > 2 && name[0] == 'g' && name[1] == '_' )
                name = name.substr( 2 );
            if ( bStripIndexSuffix && name.size() > 5 && name.substr( name.size() - 5 ) == "Index" )
                name = name.substr( 0, name.size() - 5 );
            return name;
        }
    } // namespace

    void ShaderBindingLayout::buildBindPlan()
    {
        _engineCbSize = 0;
        _listEngineCbMember.clear();
        _listResourceBind.clear();

        const hashed_string materialCbName{ shaderslot::cbname::kMaterial };

        for ( const ShaderBindingSlot& slot : _listSlot )
        {
            if ( slot._kind == ShaderBindingKind::ConstantBuffer )
            {
                if ( slot._name == materialCbName )
                    continue;

                uint32 slotEnd = slot._cbTotalSize;
                for ( const ShaderVariableInfo& member : slot._listCbMember )
                {
                    slotEnd = MathUtil::max( slotEnd, member._offset + member._size );

                    ShaderEngineCbMember planned{};
                    planned._valueKey = hashed_string( static_cast<std::string_view>( member._name ) );
                    planned._offset   = member._offset;
                    planned._size     = member._size;

                    // `g_<Name>Index` 패턴은 명시 값이 없을 때 레지스트리의 bindless 인덱스로 채운다.
                    // 그 조회 키를 지금 만들어 둔다 — 드로우마다 문자열을 자를 이유가 없다.
                    if ( member._size == sizeof( uint32 ) && member._name.find( "Index" ) != string::npos )
                        planned._autoIndexKey = hashed_string( canonicalResourceView( static_cast<std::string_view>( member._name ), true ) );

                    _listEngineCbMember.push_back( planned );
                }
                _engineCbSize = MathUtil::max( _engineCbSize, slotEnd );
                continue;
            }

            if ( slot._kind == ShaderBindingKind::Texture || slot._kind == ShaderBindingKind::StructuredBuffer )
            {
                ShaderResourceBind planned{};
                planned._lookupKey     = hashed_string( canonicalResourceView( slot._name.c_str(), false ) );
                planned._registerIndex = slot._registerIndex;
                planned._kind          = slot._kind;
                _listResourceBind.push_back( planned );
            }
        }
    }

    void ShaderBindingLayout::rebuildIndex()
    {
        _mapNameToSlot.clear();
        for ( uint32 index = 0; index < _listSlot.size(); ++index )
        {
            const ShaderBindingSlot& slot = _listSlot[index];
            if ( slot._name.getHash() == 0 )
                continue;
            auto it = _mapNameToSlot.find( slot._name );
            if ( it != _mapNameToSlot.end() )
            {
                SW_LOG_TRACE( "Duplicate binding name '%#' (space %#, register %#) — keeping first.",
                              slot._name.c_str(), slot._space, slot._registerIndex );
                continue;
            }
            _mapNameToSlot[slot._name] = index;

            // 셰이더가 선언한 이름(`g_SwMaterials`)과 엔진이 쓰는 canonical 이름(`SwMaterials`)을 둘 다 건다.
            // 리소스 바인딩 표(_listResourceBind)와 리소스 레지스트리는 canonical 키로 굽는데 이 색인만
            // 원본 이름이라, find( "SwMaterials" ) 가 **언제나** nullptr 이었다. 그래서 머티리얼 없는 배치가
            // 폴백 버퍼를 못 찾아 t9 를 비운 채 드로우를 냈고, Vulkan 이 초기화되지 않은 디스크립터를 읽어
            // 디바이스를 잃었다(GPU-AV: "binding 25 Descriptor index 0 is uninitialized").
            const hashed_string canonicalName{ canonicalResourceView( slot._name.c_str(), false ) };
            if ( canonicalName != slot._name && _mapNameToSlot.find( canonicalName ) == _mapNameToSlot.end() )
                _mapNameToSlot[canonicalName] = index;
        }
    }

    const ShaderBindingSlot* ShaderBindingLayout::find( hashed_string name ) const
    {
        auto it = _mapNameToSlot.find( name );
        return it != _mapNameToSlot.end() ? &_listSlot[it->second] : nullptr;
    }
} // namespace sw
