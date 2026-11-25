#include "pch.h"

#include "Editor/Panels/Catalog/Model/CatalogFileSession.h"

namespace sw::editor
{
    CatalogFileSession::CatalogFileSession()
        : _document{}
        , _listWarning{}
        , _assetID{}
        , _kindLabel{}
        , _previousText{}
        , _status{}
        , _validationState{ ValidationState::None }
    {
    }

    void CatalogFileSession::open( string_view assetID, string_view kindLabel, string_view text, DataInheritanceSource* pInheritanceSource )
    {
        _assetID   = string( assetID );
        _kindLabel = string( kindLabel );
        _document.setInheritanceSource( pInheritanceSource );
        (void)_document.load( text );
        _previousText = string( text );
        _status.clear();
        clearValidation();
    }

    void CatalogFileSession::onSaved( string replacedText )
    {
        _previousText = std::move( replacedText );
        _document.markSaved();
    }

    void CatalogFileSession::onRestored()
    {
        _document.replaceSavedText( _previousText ); // 되돌린 글이 디스크와 문서의 새 기준이다
    }

    void CatalogFileSession::clearValidation()
    {
        _listWarning.clear();
        _validationState = ValidationState::None;
    }

    void CatalogFileSession::onValidated( bool bLoaded, vector<string>&& listWarning )
    {
        _listWarning     = std::move( listWarning );
        _validationState = bLoaded && _listWarning.empty() ? ValidationState::Passed : ValidationState::Failed;
        _status          = _document.isDirty() ? "Validated the saved file - unsaved edits are not included" : string();
    }
} // namespace sw::editor
