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
        , _conflictDiskText{}
        , _status{}
        , _validationState{ ValidationState::None }
        , _bConflictPending{ SW_FALSE }
    {
    }

    void CatalogFileSession::open( string_view assetID, string_view kindLabel, string_view text, IDataInheritanceSource* pInheritanceSource )
    {
        _assetID   = string( assetID );
        _kindLabel = string( kindLabel );
        _document.setInheritanceSource( pInheritanceSource );
        (void)_document.load( text );
        _previousText = string( text );
        _conflictDiskText.clear();
        _bConflictPending = SW_FALSE;
        _status.clear();
        clearValidation();
    }

    bool CatalogFileSession::checkDiskBeforeSave( string_view diskText )
    {
        if ( diskText == string_view( _document.getSavedText() ) )
            return true;
        if ( _bConflictPending == SW_TRUE && diskText == string_view( _conflictDiskText ) )
            return true;
        _conflictDiskText = string( diskText );
        _bConflictPending = SW_TRUE;
        _status           = "The file changed on disk since it was opened - save again to overwrite it";
        return false;
    }

    void CatalogFileSession::onSaved()
    {
        _previousText = _bConflictPending == SW_TRUE ? _conflictDiskText : _document.getSavedText(); // 저장 직전에 디스크에 있던 글
        _conflictDiskText.clear();
        _bConflictPending = SW_FALSE;
        _document.markSaved();
    }

    void CatalogFileSession::onRestored()
    {
        _conflictDiskText.clear();
        _bConflictPending = SW_FALSE;
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
