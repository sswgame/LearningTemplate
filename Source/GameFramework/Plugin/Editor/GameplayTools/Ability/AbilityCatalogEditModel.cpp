#include "pch.h"

#include "GameFramework/Plugin/Editor/GameplayTools/Ability/AbilityCatalogEditModel.h"

#include "Core/String/formatString.h"

namespace sw::editor
{
    namespace
    {
        struct AbilityCatalogEditModelInternal
        {
            /** @brief 종류마다 원소 이름 · 이름 칸입니다(`AbilityCatalog::loadRoot` 가 읽는 이름). */
            static constexpr const utf8* kArrElementName[]   = { "AbilityEffect", "Ability", "AbilitySet", "AbilityPresentation" };
            static constexpr const utf8* kArrNameAttribute[] = { "id", "id", "id", "tag" };
            static_assert( SW_COUNT_OF( kArrElementName ) == static_cast<size_t>( AbilityEntryKind::Count ), "one element name per entry kind" );

            static const utf8* getNameAttribute( AbilityEntryKind kind ) { return kArrNameAttribute[static_cast<size_t>( kind )]; }

            /** @brief 원소의 속성 값입니다. 없으면 nullptr 입니다. */
            static const string* findAttribute( const CatalogXMLElement& element, string_view name )
            {
                const int32 index = element.findAttribute( name );
                return index >= 0 ? &element._listAttribute[static_cast<size_t>( index )]._value : nullptr;
            }

            /** @brief @p tag 가 @p presentationTag 이거나 그 자손이면(`A.B` 는 `A` 의 자손) true 입니다. 큐는 가장 가까운 조상 태그의 연출로 간다. */
            static bool isTagOrDescendant( string_view tag, string_view presentationTag )
            {
                if ( tag.size() < presentationTag.size() || tag.substr( 0, presentationTag.size() ) != presentationTag )
                    return false;
                return tag.size() == presentationTag.size() || tag[presentationTag.size()] == '.';
            }
        };
        using Internal = AbilityCatalogEditModelInternal;
    } // namespace

    AbilityCatalogReference::AbilityCatalogReference()
        : _attribute{}
        , _label{}
        , _targetID{}
        , _from{ -1 }
        , _element{ -1 }
        , _target{}
        , _targetKind{ AbilityEntryKind::Effect }
        , _bRequired{ SW_FALSE }
        , _bInherited{ SW_FALSE }
    {
    }

    AbilityCatalogEditModel::AbilityCatalogEditModel()
        : _document{}
        , _listEntry{}
        , _listReference{}
        , _listIssue{}
    {
    }

    bool AbilityCatalogEditModel::load( string_view text, IDataInheritanceSource* pInheritanceSource )
    {
        _document.setInheritanceSource( pInheritanceSource );
        const bool bLoaded = _document.load( text );
        rebuild();
        return bLoaded;
    }

    const utf8* AbilityCatalogEditModel::getElementName( AbilityEntryKind kind )
    {
        return kind == AbilityEntryKind::Count ? "" : Internal::kArrElementName[static_cast<size_t>( kind )];
    }

    void AbilityCatalogEditModel::rebuild()
    {
        _listEntry.clear();
        _listReference.clear();
        _listIssue.clear();
        if ( _document.isParsed() == false )
            return;
        collectEntries();
        collectReferences();
        collectIssues();
    }

    void AbilityCatalogEditModel::collectEntries()
    {
        const CatalogXMLText& parsed = _document.getParsed();
        for ( const int32 element : parsed.getRowIndices() )
        {
            const CatalogXMLElement& row = parsed.getElement( element );
            for ( uint32 kindIndex = 0; kindIndex < static_cast<uint32>( AbilityEntryKind::Count ); ++kindIndex )
            {
                if ( row._name != Internal::kArrElementName[kindIndex] )
                    continue;
                const AbilityEntryKind kind  = static_cast<AbilityEntryKind>( kindIndex );
                const string*          pName = Internal::findAttribute( row, Internal::getNameAttribute( kind ) );
                _listEntry.push_back( AbilityCatalogEntry{ pName != nullptr ? *pName : string{}, element, kind } );
            }
        }
    }

    bool AbilityCatalogEditModel::findValue( int32 element, string_view attribute, string& outValue, bool& outInherited ) const
    {
        const string* pOwn = _document.findCellValue( element, attribute );
        outInherited       = false;
        if ( pOwn != nullptr )
        {
            outValue = *pOwn;
            return true;
        }
        const utf8* pInherited = _document.findInheritedValue( element, attribute );
        if ( pInherited == nullptr )
            return false;
        outValue     = pInherited;
        outInherited = true;
        return true;
    }

    void AbilityCatalogEditModel::addReference( int32 from, int32 element, string_view attribute, string_view label, AbilityEntryKind targetKind, bool bRequired )
    {
        string value;
        bool   bInherited = false;
        // 빈 값(`cost=""`)은 "없음" 을 적은 것이다 — 부모의 값을 지운다. 참조가 아니다.
        if ( findValue( element, attribute, value, bInherited ) == false || value.empty() )
            return;
        AbilityCatalogReference reference;
        reference._attribute  = string( attribute );
        reference._label      = string( label );
        reference._targetID   = value;
        reference._from       = from;
        reference._element    = element;
        reference._target     = findEntry( targetKind, value );
        reference._targetKind = targetKind;
        reference._bRequired  = bRequired ? SW_TRUE : SW_FALSE;
        reference._bInherited = bInherited ? SW_TRUE : SW_FALSE;
        _listReference.push_back( reference );
    }

    void AbilityCatalogEditModel::collectReferences()
    {
        const CatalogXMLText& parsed = _document.getParsed();
        for ( int32 entryIndex = 0; entryIndex < static_cast<int32>( _listEntry.size() ); ++entryIndex )
        {
            const AbilityCatalogEntry& entry = _listEntry[static_cast<size_t>( entryIndex )];
            const CatalogXMLElement&   row   = parsed.getElement( entry._element );
            // `_base` 는 같은 종류의 부모 항목이다(항목 상속).
            if ( Internal::findAttribute( row, DataInheritance::kBaseAttribute ) != nullptr )
                addReference( entryIndex, entry._element, DataInheritance::kBaseAttribute, "base", entry._kind, true );
            if ( entry._kind == AbilityEntryKind::Ability )
            {
                addReference( entryIndex, entry._element, "cost", "cost", AbilityEntryKind::Effect, true );
                addReference( entryIndex, entry._element, "cooldown", "cooldown", AbilityEntryKind::Effect, true );
            }
            for ( const int32 child : row._listChild )
            {
                const CatalogXMLElement& childElement = parsed.getElement( child );
                if ( entry._kind == AbilityEntryKind::AbilitySet && childElement._name == "Ability" )
                {
                    addReference( entryIndex, child, "id", "set ability", AbilityEntryKind::Ability, true );
                }
                else if ( entry._kind == AbilityEntryKind::AbilitySet && childElement._name == "Effect" )
                {
                    addReference( entryIndex, child, "id", "set effect", AbilityEntryKind::Effect, true );
                }
                else if ( entry._kind == AbilityEntryKind::Ability && childElement._name == "Param" )
                {
                    // 클래스가 이름으로 읽는 값이다 — 이펙트 id 와 같을 때만 연결로 둔다(문제로 세지 않는다).
                    const string* pText = Internal::findAttribute( childElement, "text" );
                    const string* pName = Internal::findAttribute( childElement, "name" );
                    if ( pText != nullptr && findEntry( AbilityEntryKind::Effect, *pText ).has_value() )
                    {
                        StringBuilder<constant::buffer::kMax256> textBuilder;
                        textBuilder.appendFormat( "param %#", pName != nullptr ? *pName : string{} );
                        addReference( entryIndex, child, "text", string( textBuilder.view() ), AbilityEntryKind::Effect, false );
                    }
                }
                else if ( entry._kind == AbilityEntryKind::Effect && childElement._name == "Cue" )
                {
                    const size_t countBefore = _listReference.size();
                    addReference( entryIndex, child, "tag", "cue", AbilityEntryKind::Presentation, false );
                    if ( _listReference.size() == countBefore )
                        continue; // 빈 태그
                    // 큐는 가장 가까운 조상 태그의 연출로 간다(`AbilityCatalog::findCue`) — 가장 긴 접두 태그를 고른다.
                    AbilityCatalogReference& cue        = _listReference.back();
                    size_t                   bestLength = 0;
                    for ( int32 candidate = 0; candidate < static_cast<int32>( _listEntry.size() ); ++candidate )
                    {
                        const AbilityCatalogEntry& presentation = _listEntry[static_cast<size_t>( candidate )];
                        const bool                 bMatches     = presentation._kind == AbilityEntryKind::Presentation && Internal::isTagOrDescendant( cue._targetID, presentation._id );
                        if ( bMatches && presentation._id.size() > bestLength )
                        {
                            bestLength  = presentation._id.size();
                            cue._target = candidate;
                        }
                    }
                }
            }
        }
    }

    void AbilityCatalogEditModel::collectIssues()
    {
        for ( const AbilityCatalogReference& reference : _listReference )
        {
            if ( reference._bRequired == SW_FALSE || reference._target.has_value() )
                continue;
            const AbilityCatalogEntry&               from = _listEntry[static_cast<size_t>( reference._from )];
            StringBuilder<constant::buffer::kMax256> textBuilder;
            textBuilder.appendFormat( "%# '%#' %# '%#' is not an %# in this file", getElementName( from._kind ), from._id, reference._label, reference._targetID,
                                      getElementName( reference._targetKind ) );
            _listIssue.push_back( AbilityCatalogIssue{ string( textBuilder.view() ),
                                                       reference._from } );
        }
        // 같은 종류의 id 가 둘이면 뒤의 것이 앞의 것을 덮는다 — 실수로 보이게 둘째부터 알린다.
        for ( int32 entryIndex = 0; entryIndex < static_cast<int32>( _listEntry.size() ); ++entryIndex )
        {
            const AbilityCatalogEntry& entry = _listEntry[static_cast<size_t>( entryIndex )];
            if ( entry._id.empty() )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "%# has no %#", getElementName( entry._kind ), Internal::getNameAttribute( entry._kind ) );
                _listIssue.push_back( AbilityCatalogIssue{ string( textBuilder.view() ), entryIndex } );
                continue;
            }
            if ( findEntry( entry._kind, entry._id ) != entryIndex )
            {
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "%# '%#' is listed twice", getElementName( entry._kind ), entry._id );
                _listIssue.push_back( AbilityCatalogIssue{ string( textBuilder.view() ), entryIndex } );
            }
        }
    }

    optional<int32> AbilityCatalogEditModel::findEntry( AbilityEntryKind kind, string_view id ) const
    {
        for ( int32 entryIndex = 0; entryIndex < static_cast<int32>( _listEntry.size() ); ++entryIndex )
        {
            const AbilityCatalogEntry& entry = _listEntry[static_cast<size_t>( entryIndex )];
            if ( entry._kind == kind && string_view( entry._id ) == id )
                return entryIndex;
        }
        return nullopt;
    }

    void AbilityCatalogEditModel::collectReferencesFrom( int32 entry, vector<int32>& outListReference ) const
    {
        outListReference.clear();
        for ( int32 referenceIndex = 0; referenceIndex < static_cast<int32>( _listReference.size() ); ++referenceIndex )
        {
            if ( _listReference[static_cast<size_t>( referenceIndex )]._from == entry )
                outListReference.push_back( referenceIndex );
        }
    }

    void AbilityCatalogEditModel::collectReferencesTo( int32 entry, vector<int32>& outListReference ) const
    {
        outListReference.clear();
        for ( int32 referenceIndex = 0; referenceIndex < static_cast<int32>( _listReference.size() ); ++referenceIndex )
        {
            if ( _listReference[static_cast<size_t>( referenceIndex )]._target == entry )
                outListReference.push_back( referenceIndex );
        }
    }

    void AbilityCatalogEditModel::collectIDs( AbilityEntryKind kind, vector<string>& outListID ) const
    {
        outListID.clear();
        for ( const AbilityCatalogEntry& entry : _listEntry )
        {
            if ( entry._kind == kind && entry._id.empty() == false )
                outListID.push_back( entry._id );
        }
    }

    bool AbilityCatalogEditModel::setReference( int32 reference, string_view targetID )
    {
        if ( reference < 0 || reference >= static_cast<int32>( _listReference.size() ) )
            return false;
        const AbilityCatalogReference& target = _listReference[static_cast<size_t>( reference )];
        if ( string_view( target._targetID ) == targetID )
            return false;
        const bool bSet = _document.setCell( target._element, target._attribute, targetID );
        rebuild();
        return bSet;
    }

    bool AbilityCatalogEditModel::setAbilityEffectSlot( int32 entry, string_view attribute, string_view effectID )
    {
        const bool bAbility = 0 <= entry && entry < static_cast<int32>( _listEntry.size() ) && _listEntry[static_cast<size_t>( entry )]._kind == AbilityEntryKind::Ability;
        if ( bAbility == false || ( attribute != "cost" && attribute != "cooldown" ) )
            return false;
        const bool bSet = _document.setCell( _listEntry[static_cast<size_t>( entry )]._element, attribute, effectID );
        rebuild();
        return bSet;
    }

    bool AbilityCatalogEditModel::addSetMember( int32 entry, AbilityEntryKind memberKind, string_view id )
    {
        const bool bSet    = 0 <= entry && entry < static_cast<int32>( _listEntry.size() ) && _listEntry[static_cast<size_t>( entry )]._kind == AbilityEntryKind::AbilitySet;
        const bool bMember = memberKind == AbilityEntryKind::Ability || memberKind == AbilityEntryKind::Effect;
        if ( bSet == false || bMember == false || id.empty() )
            return false;
        const bool bAdded = _document.addChild( _listEntry[static_cast<size_t>( entry )]._element, memberKind == AbilityEntryKind::Ability ? "Ability" : "Effect",
                                                {
                                                    CatalogXMLNewAttribute{ "id", string( id ) }
        } );
        rebuild();
        return bAdded;
    }

    int32 AbilityCatalogEditModel::addEntry( AbilityEntryKind kind )
    {
        if ( kind == AbilityEntryKind::Count || _document.addRow( getElementName( kind ) ) == false )
            return -1;
        rebuild();
        // 새 행은 그 종류 표의 끝이다.
        for ( int32 entryIndex = static_cast<int32>( _listEntry.size() ) - 1; entryIndex >= 0; --entryIndex )
        {
            if ( _listEntry[static_cast<size_t>( entryIndex )]._kind == kind )
                return entryIndex;
        }
        return -1;
    }

    int32 AbilityCatalogEditModel::duplicateEntry( int32 entry )
    {
        if ( entry < 0 || entry >= static_cast<int32>( _listEntry.size() ) )
            return -1;
        const AbilityCatalogEntry source = _listEntry[static_cast<size_t>( entry )];
        if ( _document.duplicateRow( source._element ) == false )
            return -1;
        rebuild();
        // 복사본은 바로 다음 형제다(`CatalogXMLText::duplicateElement`).
        for ( int32 entryIndex = 0; entryIndex + 1 < static_cast<int32>( _listEntry.size() ); ++entryIndex )
        {
            if ( _listEntry[static_cast<size_t>( entryIndex )]._element == source._element )
                return entryIndex + 1;
        }
        return -1;
    }

    bool AbilityCatalogEditModel::removeEntry( int32 entry )
    {
        if ( entry < 0 || entry >= static_cast<int32>( _listEntry.size() ) )
            return false;
        for ( const AbilityCatalogReference& reference : _listReference )
        {
            if ( reference._target == entry && reference._bRequired == SW_TRUE )
            {
                const AbilityCatalogEntry&               user = _listEntry[static_cast<size_t>( reference._from )];
                StringBuilder<constant::buffer::kMax256> textBuilder;
                textBuilder.appendFormat( "'%#' is used by %# '%#' (%#) - change that first", _listEntry[static_cast<size_t>( entry )]._id, getElementName( user._kind ), user._id,
                                          reference._label );
                _document.setError( string( textBuilder.view() ) );
                return false;
            }
        }
        const bool bRemoved = _document.removeRow( _listEntry[static_cast<size_t>( entry )]._element );
        rebuild();
        return bRemoved;
    }
} // namespace sw::editor
