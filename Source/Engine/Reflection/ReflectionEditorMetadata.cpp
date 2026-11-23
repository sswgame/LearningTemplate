#include "pch.h"

#include "Engine/Reflection/ReflectionEditorMetadata.h"

#include "Core/Log/Logger.h"
#include "Core/Registry/RegistrationList.h"

#include "Engine/Reflection/ReflectionConstants.h"

/**
 * @file ReflectionEditorMetadata.cpp
 * @brief 에디터 메타데이터의 기본값과 개발 전용 생성 단위가 올리는 줄의 정적 등록 목록입니다.
 */
namespace sw
{
    namespace
    {
        /** @brief FQN 을 이름으로 든 줄 목록입니다. 함수 정적이라 다른 TU · DLL 의 정적 등록자가 먼저 와도 비어 있는 채로 선다. */
        SW_DEFINE_REGISTRATION_LIST( getTypeEditorMetadataSources, const TypeEditorMetadataSource, "TypeEditorMetadataRegistry", RegistrationOrder::Insertion, true )
        SW_DEFINE_REGISTRATION_LIST( getEnumEditorMetadataSources, const EnumEditorMetadataSource, "EnumEditorMetadataRegistry", RegistrationOrder::Insertion, true )

        struct ReflectionEditorMetadataInternal
        {
            static const string* findInMap( const unordered_map<hashed_string, string>& mapCustomMeta, const hashed_string& key ) noexcept
            {
                const auto iter = mapCustomMeta.find( key );
                return iter != mapCustomMeta.end() ? &iter->second : nullptr;
            }

            /** @brief 커스텀 메타 줄을 넣습니다. 같은 키가 이미 있으면 먼저 적은 값이 남습니다. */
            static void addCustomMeta( unordered_map<hashed_string, string>& outMap, const EditorMetadataRow& row )
            {
                outMap.emplace( hashed_string{ row._pKey }, string{ row._pText } );
            }

            /** @brief 그 대상에 없는 칸입니다 — 생성기가 낸 줄과 엔진 메타데이터 구조가 어긋났다. */
            [[noreturn]] static void failUnknownField( const EditorMetadataRow& row )
            {
                reportFatalError( "Editor metadata row of '%#' has field %# that its target %# does not carry - the generator and ReflectionEditorMetadata.h disagree",
                                  row._pName != nullptr ? row._pName : "(type)", static_cast<uint32>( row._field ), static_cast<uint32>( row._target ) );
            }

            static void applyRow( TypeEditorMetadata& outMetadata, const EditorMetadataRow& row )
            {
                switch ( row._field )
                {
                    case EditorMetadataField::Category:
                        outMetadata._category = row._pText;
                        break;
                    case EditorMetadataField::DisplayName:
                        outMetadata._displayName = row._pText;
                        break;
                    case EditorMetadataField::Tooltip:
                        outMetadata._tooltip = row._pText;
                        break;
                    case EditorMetadataField::CustomMeta:
                        addCustomMeta( outMetadata._mapCustomMeta, row );
                        break;
                    case EditorMetadataField::HideInMenu:
                        outMetadata._bHideInMenu = SW_TRUE;
                        break;
                    default:
                        failUnknownField( row );
                }
            }

            static void applyRow( PropertyEditorMetadata& outMetadata, const EditorMetadataRow& row )
            {
                switch ( row._field )
                {
                    case EditorMetadataField::Category:
                        outMetadata._category = row._pText;
                        break;
                    case EditorMetadataField::DisplayName:
                        outMetadata._displayName = row._pText;
                        break;
                    case EditorMetadataField::Tooltip:
                        outMetadata._tooltip = row._pText;
                        break;
                    case EditorMetadataField::CustomMeta:
                        addCustomMeta( outMetadata._mapCustomMeta, row );
                        break;
                    case EditorMetadataField::EditCondition:
                        outMetadata._editCondition = row._pText;
                        break;
                    case EditorMetadataField::FileFilter:
                        outMetadata._fileFilter = row._pText;
                        break;
                    case EditorMetadataField::HideInInspector:
                        outMetadata._bHideInInspector = SW_TRUE;
                        break;
                    case EditorMetadataField::EditConditionHides:
                        outMetadata._bEditConditionHides = SW_TRUE;
                        break;
                    case EditorMetadataField::ColorHDR:
                        outMetadata._bColorHDR = SW_TRUE;
                        break;
                    case EditorMetadataField::Multiline:
                        outMetadata._bMultiline = SW_TRUE;
                        break;
                    case EditorMetadataField::UIMinRange:
                        outMetadata._uiMinRange     = row._number;
                        outMetadata._bHasUIMinRange = SW_TRUE;
                        break;
                    case EditorMetadataField::UIMaxRange:
                        outMetadata._uiMaxRange     = row._number;
                        outMetadata._bHasUIMaxRange = SW_TRUE;
                        break;
                    default:
                        failUnknownField( row );
                }
            }

            static void applyRow( FunctionEditorMetadata& outMetadata, const EditorMetadataRow& row )
            {
                switch ( row._field )
                {
                    case EditorMetadataField::Category:
                        outMetadata._category = row._pText;
                        break;
                    case EditorMetadataField::DisplayName:
                        outMetadata._displayName = row._pText;
                        break;
                    case EditorMetadataField::Tooltip:
                        outMetadata._tooltip = row._pText;
                        break;
                    case EditorMetadataField::CustomMeta:
                        addCustomMeta( outMetadata._mapCustomMeta, row );
                        break;
                    case EditorMetadataField::EditorPreview:
                        outMetadata._editorPreview = row._pText;
                        break;
                    case EditorMetadataField::CallInEditor:
                        outMetadata._bCallInEditor = SW_TRUE;
                        break;
                    case EditorMetadataField::Exec:
                        outMetadata._bExec = SW_TRUE;
                        break;
                    default:
                        failUnknownField( row );
                }
            }

            static void applyRow( EnumeratorEditorMetadata& outMetadata, const EditorMetadataRow& row )
            {
                switch ( row._field )
                {
                    case EditorMetadataField::DisplayName:
                        outMetadata._displayName = row._pText;
                        break;
                    case EditorMetadataField::Tooltip:
                        outMetadata._tooltip = row._pText;
                        break;
                    case EditorMetadataField::CustomMeta:
                        addCustomMeta( outMetadata._mapCustomMeta, row );
                        break;
                    case EditorMetadataField::Hidden:
                        outMetadata._bHidden = SW_TRUE;
                        break;
                    default:
                        failUnknownField( row );
                }
            }
        };
    } // namespace

    TypeEditorMetadata::TypeEditorMetadata() noexcept
        : _category{ constant::reflection::kDefaultCategory }
        , _displayName{}
        , _tooltip{}
        , _mapCustomMeta{}
        , _bHideInMenu{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    const string* TypeEditorMetadata::findCustomMeta( const hashed_string& key ) const noexcept
    {
        return ReflectionEditorMetadataInternal::findInMap( _mapCustomMeta, key );
    }

    const TypeEditorMetadata& TypeEditorMetadata::getDefault() noexcept
    {
        static const TypeEditorMetadata s_default{};
        return s_default;
    }

    PropertyEditorMetadata::PropertyEditorMetadata() noexcept
        : _category{ constant::reflection::kDefaultCategory }
        , _displayName{}
        , _tooltip{}
        , _mapCustomMeta{}
        , _editCondition{}
        , _fileFilter{}
        , _uiMinRange{ 0.0f }
        , _uiMaxRange{ 1.0f }
        , _bHasUIMinRange{ SW_FALSE }
        , _bHasUIMaxRange{ SW_FALSE }
        , _bEditConditionHides{ SW_FALSE }
        , _bColorHDR{ SW_FALSE }
        , _bMultiline{ SW_FALSE }
        , _bHideInInspector{ SW_FALSE }
        , _reservedDisplay{ 0 }
    {
    }

    const string* PropertyEditorMetadata::findCustomMeta( const hashed_string& key ) const noexcept
    {
        return ReflectionEditorMetadataInternal::findInMap( _mapCustomMeta, key );
    }

    const PropertyEditorMetadata& PropertyEditorMetadata::getDefault() noexcept
    {
        static const PropertyEditorMetadata s_default{};
        return s_default;
    }

    FunctionEditorMetadata::FunctionEditorMetadata() noexcept
        : _category{ constant::reflection::kDefaultCategory }
        , _displayName{}
        , _tooltip{}
        , _editorPreview{}
        , _mapCustomMeta{}
        , _bCallInEditor{ SW_FALSE }
        , _bExec{ SW_FALSE }
        , _reserved{ 0 }
    {
    }

    const string* FunctionEditorMetadata::findCustomMeta( const hashed_string& key ) const noexcept
    {
        return ReflectionEditorMetadataInternal::findInMap( _mapCustomMeta, key );
    }

    const FunctionEditorMetadata& FunctionEditorMetadata::getDefault() noexcept
    {
        static const FunctionEditorMetadata s_default{};
        return s_default;
    }

    EnumeratorEditorMetadata::EnumeratorEditorMetadata() noexcept
        : _displayName{}
        , _tooltip{}
        , _mapCustomMeta{}
        , _bHidden{ SW_FALSE }
        , _reservedFlags{ 0 }
    {
    }

    const string* EnumeratorEditorMetadata::findCustomMeta( const hashed_string& key ) const noexcept
    {
        return ReflectionEditorMetadataInternal::findInMap( _mapCustomMeta, key );
    }

    const EnumeratorEditorMetadata& EnumeratorEditorMetadata::getDefault() noexcept
    {
        static const EnumeratorEditorMetadata s_default{};
        return s_default;
    }

    const string* EnumEditorMetadata::findCustomMeta( const hashed_string& key ) const noexcept
    {
        return ReflectionEditorMetadataInternal::findInMap( _mapCustomMeta, key );
    }

    const EnumeratorEditorMetadata& EnumEditorMetadata::findEnumerator( const hashed_string& name ) const noexcept
    {
        const auto iter = _mapEnumerator.find( name );
        return iter != _mapEnumerator.end() ? iter->second : EnumeratorEditorMetadata::getDefault();
    }

    void TypeEditorMetadataSource::fill( TypeEditorMetadataSet& outSet ) const
    {
        for ( uint32 rowIndex = 0; rowIndex < _rowCount; ++rowIndex )
        {
            const EditorMetadataRow& row = _pRow[rowIndex];
            switch ( row._target )
            {
                case EditorMetadataTarget::Type:
                    ReflectionEditorMetadataInternal::applyRow( outSet._type, row );
                    break;
                case EditorMetadataTarget::Property:
                    ReflectionEditorMetadataInternal::applyRow( outSet.addProperty( hashed_string{ row._pName } ), row );
                    break;
                case EditorMetadataTarget::Event:
                    ReflectionEditorMetadataInternal::applyRow( outSet.addEvent( hashed_string{ row._pName } ), row );
                    break;
                case EditorMetadataTarget::Function:
                    ReflectionEditorMetadataInternal::applyRow( outSet.addFunction( hashed_string{ row._pName } ), row );
                    break;
                case EditorMetadataTarget::Enum:
                case EditorMetadataTarget::Enumerator:
                    ReflectionEditorMetadataInternal::failUnknownField( row );
            }
        }
    }

    void EnumEditorMetadataSource::fill( EnumEditorMetadata& outMetadata ) const
    {
        for ( uint32 rowIndex = 0; rowIndex < _rowCount; ++rowIndex )
        {
            const EditorMetadataRow& row = _pRow[rowIndex];
            if ( row._target == EditorMetadataTarget::Enumerator )
                ReflectionEditorMetadataInternal::applyRow( outMetadata.addEnumerator( hashed_string{ row._pName } ), row );
            else if ( row._target == EditorMetadataTarget::Enum && row._field == EditorMetadataField::CustomMeta )
                ReflectionEditorMetadataInternal::addCustomMeta( outMetadata._mapCustomMeta, row );
            else
                ReflectionEditorMetadataInternal::failUnknownField( row );
        }
    }

    bool TypeEditorMetadataSource::add( const TypeEditorMetadataSource* pSource )
    {
        return getTypeEditorMetadataSources().addLogged( pSource, "Type editor metadata", pSource->_pFqn ) == RegistrationResult::Added;
    }

    void TypeEditorMetadataSource::remove( const TypeEditorMetadataSource* pSource )
    {
        (void)getTypeEditorMetadataSources().remove( pSource ); // 올라 있지 않은 줄을 빼는 것은 할 일이 없다
    }

    bool EnumEditorMetadataSource::add( const EnumEditorMetadataSource* pSource )
    {
        return getEnumEditorMetadataSources().addLogged( pSource, "Enum editor metadata", pSource->_pFqn ) == RegistrationResult::Added;
    }

    void EnumEditorMetadataSource::remove( const EnumEditorMetadataSource* pSource )
    {
        (void)getEnumEditorMetadataSources().remove( pSource );
    }

    const TypeEditorMetadataSource* TypeEditorMetadataSource::find( const string_view fqn )
    {
        return getTypeEditorMetadataSources().findByName( fqn );
    }

    const EnumEditorMetadataSource* EnumEditorMetadataSource::find( const string_view fqn )
    {
        return getEnumEditorMetadataSources().findByName( fqn );
    }
} // namespace sw
