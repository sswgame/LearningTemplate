#include "pch.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingLayout.h"

#include "Core/Hash/HashUtil.h"
#include "Core/Math/MathUtil.h"

#include "Engine/Graphics/Shader/Binding/ShaderBindingSlots.h"

namespace sw
{
    namespace
    {
        /** @brief `g_ShadowMap` 과 `g_ShadowMapIndex`, `ShadowMap` 을 `"ShadowMap"`(레지스트리 조회 키)으로 모읍니다. */
        string_view canonicalResourceView( string_view identifier, bool bStripIndexSuffix )
        {
            string_view name             = identifier;
            const bool  bHasGlobalPrefix = name.size() > 2 && name[0] == 'g' && name[1] == '_';
            if ( bHasGlobalPrefix )
                name = name.substr( 2 );
            const bool bHasIndexSuffix = bStripIndexSuffix && name.size() > 5 && name.substr( name.size() - 5 ) == "Index";
            if ( bHasIndexSuffix )
                name = name.substr( 0, name.size() - 5 );
            return name;
        }

        /** @brief (종류, space, register) 가 같은 바인딩을 한 슬롯으로 모읍니다. 스테이지마다 가시성만 더합니다. */
        class ShaderBindingSlotCollector
        {
        public:
            explicit ShaderBindingSlotCollector( vector<ShaderBindingSlot>& listSlot )
                : _listSlot{ listSlot } {}

            ShaderBindingSlot& touch( ShaderBindingKind kind, const hashed_string& name, uint32 space, uint32 reg, uint32 arrayCount, ShaderStageFlag visibility )
            {
                const uint64 key = ( static_cast<uint64>( kind ) << 48 ) ^ ( static_cast<uint64>( space ) << 24 ) ^ static_cast<uint64>( reg );
                const auto   it  = _mapKeyToIndex.find( key );
                if ( it != _mapKeyToIndex.end() )
                {
                    ShaderBindingSlot& slot = _listSlot[it->second];
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

                const uint32 index = static_cast<uint32>( _listSlot.size() );
                _listSlot.push_back( std::move( slot ) );
                _mapKeyToIndex[key] = index;
                return _listSlot[index];
            }

            /** @brief 1) 상수 버퍼: 멤버 오프셋까지 채운다. */
            void collectConstantBuffers( const ShaderReflectionData& reflection, ShaderStageFlag visibility )
            {
                for ( const ShaderBufferInfo& cb : reflection._listConstantBuffer )
                {
                    ShaderBindingSlot& slot = touch( ShaderBindingKind::ConstantBuffer, hashed_string( static_cast<string_view>( cb._name ) ),
                                                     cb._registerSpace, cb._bindPoint, 1, visibility );
                    if ( slot._listCbMember.empty() )
                    {
                        slot._listCbMember = cb._listVariable;
                        slot._cbTotalSize  = cb._totalSize;
                    }
                    else
                    {
                        if ( slot._cbTotalSize == 0 )
                            slot._cbTotalSize = cb._totalSize;
                        // 같은 CB 를 다른 스테이지도 본다 — 한 스테이지라도 읽은 멤버는 읽은 것이다(VS 가 안 읽어도 PS 가 읽는다).
                        for ( const ShaderVariableInfo& stageMember : cb._listVariable )
                        {
                            if ( stageMember._usage != ShaderVariableUsage::Used )
                                continue;
                            for ( ShaderVariableInfo& member : slot._listCbMember )
                            {
                                if ( member._name == stageMember._name )
                                    member._usage = ShaderVariableUsage::Used;
                            }
                        }
                    }
                }
            }

            /** @brief 2) 텍스처, 샘플러, 버퍼 바인딩. */
            void collectAssets( const ShaderReflectionData& reflection, ShaderStageFlag visibility )
            {
                for ( const ShaderReflectedBinding& res : reflection._listResource )
                {
                    const ShaderBindingKind kind = res._kind;
                    if ( kind == ShaderBindingKind::Unknown )
                        continue;
                    // CB 는 위에서 이미 멤버 정보까지 등록했으므로 가시성만 갱신한다.
                    const uint32 arrayCount = kind == ShaderBindingKind::ConstantBuffer ? 1u : res._bindCount;
                    touch( kind, hashed_string( static_cast<string_view>( res._name ) ), res._registerSpace, res._bindPoint, arrayCount, visibility );
                }
            }

        private:
            vector<ShaderBindingSlot>&    _listSlot;
            unordered_map<uint64, uint32> _mapKeyToIndex;
        };

        /** @brief 3) 구조 버퍼 원소 stride: 리플렉션의 원소 레이아웃(_listStructuredElement)을 같은 이름의 슬롯에 붙인다.
         *  이 슬롯에 설정하는 버퍼(GPUScene 머티리얼 데이터, 폴백 원소)는 이 stride 로 만들어야 한다. */
        void attachStructuredElementStrides( const ShaderReflectionData& reflection, vector<ShaderBindingSlot>& listSlot )
        {
            for ( const ShaderBufferInfo& element : reflection._listStructuredElement )
            {
                if ( element._totalSize == 0 )
                    continue;
                const hashed_string name{ static_cast<string_view>( element._name ) };
                for ( ShaderBindingSlot& slot : listSlot )
                {
                    const bool bStructured         = ( slot._kind == ShaderBindingKind::StructuredBuffer || slot._kind == ShaderBindingKind::RwStructuredBuffer );
                    const bool bNeedsElementStride = bStructured && slot._name == name && slot._elementStride == 0;
                    if ( bNeedsElementStride )
                    {
                        slot._elementStride     = element._totalSize;
                        slot._listElementMember = element._listVariable;
                    }
                }
            }
        }
    } // namespace

    ShaderBindingLayout ShaderBindingLayout::make( const vector<pair<ShaderStage, const ShaderReflectionData*>>& listStageReflection )
    {
        ShaderBindingLayout        layout;
        ShaderBindingSlotCollector collector( layout._listSlot );
        for ( const auto& [stage, pReflection] : listStageReflection )
        {
            if ( pReflection == nullptr )
                continue;
            const ShaderStageFlag visibility = toShaderStageFlag( stage );
            collector.collectConstantBuffers( *pReflection, visibility );
            collector.collectAssets( *pReflection, visibility );
        }
        // 원소 stride 는 모든 스테이지의 슬롯이 모인 뒤에 붙인다.
        for ( const auto& [stage, pReflection] : listStageReflection )
        {
            if ( pReflection != nullptr )
                attachStructuredElementStrides( *pReflection, layout._listSlot );
        }

        layout.rebuildIndex();
        layout.computeBindPlan();
        return layout;
    }

    void ShaderBindingLayout::computeBindPlan()
    {
        _engineCbSize       = 0;
        _engineCbLayoutHash = 0;
        _bUsesMaterialCb    = SW_FALSE;
        _bUsesMaterialData  = SW_FALSE;
        _listEngineCbMember.clear();
        _listResourceBindOp.clear();

        const hashed_string materialCbName{ shaderslot::cbname::kMaterial };
        const hashed_string materialsName{ shaderslot::resname::kMaterials };

        for ( const ShaderBindingSlot& slot : _listSlot )
        {
            if ( slot._kind == ShaderBindingKind::ConstantBuffer )
            {
                if ( slot._name == materialCbName )
                {
                    _bUsesMaterialCb   = SW_TRUE;
                    _bUsesMaterialData = SW_TRUE;
                    continue;
                }

                uint32 slotEnd = slot._cbTotalSize;
                for ( const ShaderVariableInfo& member : slot._listCbMember )
                {
                    slotEnd = MathUtil::max( slotEnd, member._offset + member._size );

                    ShaderEngineCbMember planned{};
                    planned._valueKey = hashed_string( static_cast<string_view>( member._name ) );
                    planned._offset   = member._offset;
                    planned._size     = member._size;

                    // `g_<Name>Index` 패턴은 명시 값이 없을 때 레지스트리의 bindless 인덱스로 채운다.
                    // 그 조회 키를 지금 만들어 둔다. 드로우마다 문자열을 자를 이유가 없다.
                    if ( member._size == sizeof( uint32 ) && member._name.find( "Index" ) != string::npos )
                        planned._autoIndexKey = hashed_string( canonicalResourceView( static_cast<string_view>( member._name ), true ) );

                    _engineCbLayoutHash = HashUtil::combine( _engineCbLayoutHash, static_cast<uint64>( planned._valueKey.getHash() ) );
                    _engineCbLayoutHash = HashUtil::combine( _engineCbLayoutHash, static_cast<uint64>( planned._autoIndexKey.getHash() ) );
                    _engineCbLayoutHash = HashUtil::combine( _engineCbLayoutHash, ( static_cast<uint64>( planned._offset ) << 32 ) | planned._size );
                    _listEngineCbMember.push_back( planned );
                }
                _engineCbSize       = MathUtil::max( _engineCbSize, slotEnd );
                _engineCbLayoutHash = HashUtil::combine( _engineCbLayoutHash, _engineCbSize );
                continue;
            }

            if ( slot._kind == ShaderBindingKind::Texture || slot._kind == ShaderBindingKind::StructuredBuffer )
            {
                // 텍스처는 무엇이든 머티리얼로 본다 — 번호가 머티리얼 데이터에서 오는지 리플렉션으로 가를 수 없고, 에뮬 백엔드는 칸 순번으로 읽는다(틀려도 덜 묶일 뿐이다).
                if ( slot._kind == ShaderBindingKind::Texture || slot._name == materialsName )
                    _bUsesMaterialData = SW_TRUE;
                ShaderResourceBindOp planned{};
                planned._lookupKey     = hashed_string( canonicalResourceView( slot._name.c_str(), false ) );
                planned._registerIndex = slot._registerIndex;
                planned._kind          = slot._kind;
                planned._elementStride = slot._elementStride;
                _listResourceBindOp.push_back( planned );
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
                SW_LOG_TRACE_ID( "ShaderBindingLayout.DuplicateBindingNameSpace", "Duplicate binding name '%#' (space %#, register %#) — keeping first.",
                                 slot._name.c_str(), slot._space, slot._registerIndex );
                continue;
            }
            _mapNameToSlot[slot._name] = index;

            // 셰이더가 선언한 이름(`g_SwMaterials`)과 엔진이 쓰는 canonical 이름(`SwMaterials`)을 둘 다 등록한다.
            // 리소스 바인딩 목록(_listResourceBindOp)과 리소스 레지스트리는 canonical 키로 미리 계산한다. 주의: canonical 이름이 빠지면
            // find( "SwMaterials" ) 가 **언제나** nullptr 이라 머티리얼 없는 배치가 폴백 버퍼를 못 찾아 t5 를 비운 채 드로우를 내고,
            // Vulkan 이 초기화되지 않은 디스크립터를 읽어 디바이스를 잃는다(GPU-AV: "binding 25 Descriptor index 0 is uninitialized").
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
