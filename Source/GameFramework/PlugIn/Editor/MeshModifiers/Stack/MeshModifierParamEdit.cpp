/**
 * @file MeshModifierParamEdit.cpp
 * @brief 패널 매개변수 편집의 순수 로직을 구현합니다 — NumberList 행 연산, 열 선언 검사, 구역 순서(목록 항목 연산은 헤더의 템플릿).
 */
#include "pch.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifierParamEdit.h"

#include "Core/Math/MathUtil.h"

#include "Engine/Object/GameObject/GameObject.h"
#include "Engine/Reflection/TypeRegistry.h"

#include "GameFramework/PlugIn/Editor/MeshModifiers/Stack/MeshModifier.h"

namespace sw::editor
{
    namespace
    {
        struct MeshModifierParamEditInternal
        {
            static size_t getSafeStride( uint32 stride ) { return stride == 0 ? 1 : static_cast<size_t>( stride ); }

            /** @brief 두 행의 원소를 바꿉니다. 호출하는 쪽이 범위를 확인했습니다. */
            static void swapRows( vector<float32>& inoutListNumber, size_t stride, size_t rowA, size_t rowB )
            {
                for ( size_t element = 0; element < stride; ++element )
                {
                    const float32 value                      = inoutListNumber[rowA * stride + element];
                    inoutListNumber[rowA * stride + element] = inoutListNumber[rowB * stride + element];
                    inoutListNumber[rowB * stride + element] = value;
                }
            }
        };
    } // namespace
} // namespace sw::editor

namespace sw::editor
{
    uint32 MeshModifierParamEdit::getColumnWidth( const MeshModifierListColumn& column )
    {
        return column._kind == MeshModifierListColumnKind::Vector3 ? 3u : 1u;
    }

    bool MeshModifierParamEdit::isColumnLayoutValid( const MeshModifierParamDef& def )
    {
        if ( def._pColumn == nullptr || def._columnCount == 0 )
            return false;
        uint32 width = 0;
        for ( uint32 index = 0; index < def._columnCount; ++index )
        {
            const MeshModifierListColumn& column = def._pColumn[index];
            if ( column._kind == MeshModifierListColumnKind::Combo && getComboItemCount( column._pComboItems ) == 0 )
                return false;
            width += getColumnWidth( column );
        }
        return width == def._stride;
    }

    uint32 MeshModifierParamEdit::getComboItemCount( const utf8* pComboItems )
    {
        if ( pComboItems == nullptr )
            return 0;
        uint32      count = 0;
        const utf8* pItem = pComboItems;
        while ( *pItem != '\0' )
        {
            ++count;
            while ( *pItem != '\0' )
            {
                ++pItem;
            }
            ++pItem; // 이름 뒤의 '\0' 을 넘는다
        }
        return count;
    }

    size_t MeshModifierParamEdit::getRowCount( const vector<float32>& listNumber, uint32 stride )
    {
        return listNumber.size() / MeshModifierParamEditInternal::getSafeStride( stride );
    }

    void MeshModifierParamEdit::appendRow( vector<float32>& inoutListNumber, uint32 stride )
    {
        const size_t safeStride = MeshModifierParamEditInternal::getSafeStride( stride );
        inoutListNumber.resize( getRowCount( inoutListNumber, stride ) * safeStride ); // 남는 원소는 버린다(저장 시 어차피 잘린다)
        inoutListNumber.insert( inoutListNumber.end(), safeStride, 0.0f );
    }

    bool MeshModifierParamEdit::removeRow( vector<float32>& inoutListNumber, uint32 stride, size_t row )
    {
        if ( row >= getRowCount( inoutListNumber, stride ) )
            return false;
        const size_t safeStride = MeshModifierParamEditInternal::getSafeStride( stride );
        const auto   itFirst    = inoutListNumber.begin() + static_cast<ptrdiff_t>( row * safeStride );
        inoutListNumber.erase( itFirst, itFirst + static_cast<ptrdiff_t>( safeStride ) );
        return true;
    }

    bool MeshModifierParamEdit::duplicateRow( vector<float32>& inoutListNumber, uint32 stride, size_t row )
    {
        if ( row >= getRowCount( inoutListNumber, stride ) )
            return false;
        const size_t    safeStride = MeshModifierParamEditInternal::getSafeStride( stride );
        vector<float32> listCopy( inoutListNumber.begin() + static_cast<ptrdiff_t>( row * safeStride ),
                                  inoutListNumber.begin() + static_cast<ptrdiff_t>( ( row + 1 ) * safeStride ) );
        inoutListNumber.insert( inoutListNumber.begin() + static_cast<ptrdiff_t>( ( row + 1 ) * safeStride ), listCopy.begin(), listCopy.end() );
        return true;
    }

    bool MeshModifierParamEdit::moveRowUp( vector<float32>& inoutListNumber, uint32 stride, size_t row )
    {
        if ( row == 0 || row >= getRowCount( inoutListNumber, stride ) )
            return false;
        MeshModifierParamEditInternal::swapRows( inoutListNumber, MeshModifierParamEditInternal::getSafeStride( stride ), row - 1, row );
        return true;
    }

    bool MeshModifierParamEdit::moveRowDown( vector<float32>& inoutListNumber, uint32 stride, size_t row )
    {
        if ( row + 1 >= getRowCount( inoutListNumber, stride ) )
            return false;
        MeshModifierParamEditInternal::swapRows( inoutListNumber, MeshModifierParamEditInternal::getSafeStride( stride ), row, row + 1 );
        return true;
    }

    bool MeshModifierParamEdit::matchesComponentHint( const GameObject& object, const utf8* pComponentHint )
    {
        if ( pComponentHint == nullptr || pComponentHint[0] == '\0' )
            return true;
        const hashed_string hintName( pComponentHint );
        const TypeInfo*     pHintType = engine::getTypeRegistry().findType( hintName );
        if ( pHintType == nullptr )
            return object.findComponentByTypeName( hintName ) != nullptr;
        bool bFound{ false };
        object.forEachComponent( [&]( Component* pComponent )
        {
            const TypeInfo* pTypeInfo = pComponent->getTypeInfo();
            if ( pTypeInfo != nullptr && pTypeInfo->isDerivedFrom( pHintType ) )
                bFound = true;
        } );
        return bFound;
    }

    void MeshModifierParamEdit::makeGroupOrder( const vector<MeshModifierParamDef>& listDef, vector<string>& outListGroup )
    {
        outListGroup.clear();
        bool bHasUngrouped = false;
        for ( const MeshModifierParamDef& def : listDef )
        {
            if ( def._pGroup == nullptr || def._pGroup[0] == '\0' )
            {
                bHasUngrouped = true;
                continue;
            }
            bool bKnown = false;
            for ( const string& group : outListGroup )
            {
                if ( string_view( group ) == string_view( def._pGroup ) )
                {
                    bKnown = true;
                    break;
                }
            }
            if ( bKnown == false )
                outListGroup.push_back( string( def._pGroup ) );
        }
        if ( bHasUngrouped )
            outListGroup.insert( outListGroup.begin(), string() );
    }

    bool MeshModifierParamEdit::isInGroup( const MeshModifierParamDef& def, string_view group )
    {
        if ( def._pGroup == nullptr || def._pGroup[0] == '\0' )
            return group.empty();
        return string_view( def._pGroup ) == group;
    }
} // namespace sw::editor
