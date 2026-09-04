#include "pch.h"

#include "Engine/Graphics/Shader/ShaderBindingLayout.h"

#include "Core/Math/MathUtil.h"

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

        layout.rebuildIndex();
        layout.computeFingerprint();
        return layout;
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
        }
    }

    void ShaderBindingLayout::computeFingerprint()
    {
        // 정렬된 (space, register, kind) 순서로 이름/멤버를 섞어 FNV-1a.
        vector<const ShaderBindingSlot*> listSorted;
        listSorted.reserve( _listSlot.size() );
        for ( const ShaderBindingSlot& slot : _listSlot )
            listSorted.push_back( &slot );
        std::sort( listSorted.begin(), listSorted.end(), []( const ShaderBindingSlot* pA, const ShaderBindingSlot* pB )
        {
            if ( pA->_space != pB->_space )
                return pA->_space < pB->_space;
            if ( pA->_registerIndex != pB->_registerIndex )
                return pA->_registerIndex < pB->_registerIndex;
            return static_cast<uint8>( pA->_kind ) < static_cast<uint8>( pB->_kind );
        } );

        uint64 hash = 1469598103934665603ull; // FNV-1a offset basis
        auto   mix  = [&hash]( const void* pData, size_t size )
        {
            const uint8* pBytes = static_cast<const uint8*>( pData );
            for ( size_t byteIndex = 0; byteIndex < size; ++byteIndex )
            {
                hash ^= pBytes[byteIndex];
                hash *= 1099511628211ull; // FNV-1a prime
            }
        };

        for ( const ShaderBindingSlot* pSlot : listSorted )
        {
            mix( &pSlot->_kind, sizeof( pSlot->_kind ) );
            mix( &pSlot->_space, sizeof( pSlot->_space ) );
            mix( &pSlot->_registerIndex, sizeof( pSlot->_registerIndex ) );
            const uint32 nameHash = pSlot->_name.getHash();
            mix( &nameHash, sizeof( nameHash ) );
            for ( const ShaderVariableInfo& member : pSlot->_listCbMember )
            {
                mix( member._name.c_str(), member._name.size() );
                mix( &member._offset, sizeof( member._offset ) );
                mix( &member._size, sizeof( member._size ) );
            }
        }

        _fingerprint = hash;
    }

    const ShaderBindingSlot* ShaderBindingLayout::find( hashed_string name ) const
    {
        auto it = _mapNameToSlot.find( name );
        return it != _mapNameToSlot.end() ? &_listSlot[it->second] : nullptr;
    }

    const ShaderBindingSlot* ShaderBindingLayout::findByRegister( ShaderBindingKind kind, uint32 space, uint32 registerIndex ) const
    {
        for ( const ShaderBindingSlot& slot : _listSlot )
        {
            if ( slot._kind == kind && slot._space == space && slot._registerIndex == registerIndex )
                return &slot;
        }
        return nullptr;
    }

    bool ShaderBindingLayout::resolveCbMember( hashed_string cbName, hashed_string memberName, uint32& outOffset, uint32& outSize ) const
    {
        const ShaderBindingSlot* pSlot = find( cbName );
        if ( pSlot == nullptr || pSlot->_kind != ShaderBindingKind::ConstantBuffer )
            return false;

        for ( const ShaderVariableInfo& member : pSlot->_listCbMember )
        {
            if ( hashed_string( static_cast<std::string_view>( member._name ) ) == memberName )
            {
                outOffset = member._offset;
                outSize   = member._size;
                return true;
            }
        }
        return false;
    }
} // namespace sw
