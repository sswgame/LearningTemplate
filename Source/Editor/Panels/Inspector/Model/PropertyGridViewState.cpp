#include "pch.h"

#include "Editor/Panels/Inspector/Model/PropertyGridViewState.h"

#include "Core/Foundation/Constants.h"

namespace sw::editor
{
    PropertyValueRow::PropertyValueRow()
        : _bReadOnly{ SW_FALSE }
        , _bDisabled{ SW_FALSE }
        , _bModified{ SW_FALSE }
        , _bCanReset{ SW_FALSE }
        , _bMixed{ SW_FALSE }
        , _bFavoriteTarget{ SW_FALSE }
        , _bFavorite{ SW_FALSE }
        , _bTransient{ SW_FALSE }
        , _bPasteAllowed{ SW_FALSE }
        , _bExpanded{ SW_FALSE }
        , _bColor{ SW_FALSE }
        , _bColorHDR{ SW_FALSE }
        , _bAssetPath{ SW_FALSE }
        , _bMultiline{ SW_FALSE }
        , _bHandlePicker{ SW_FALSE }
        , _bHandleUnavailable{ SW_FALSE }
        , _bCanAddRemove{ SW_FALSE }
        , _bRemovable{ SW_FALSE }
    {
    }

    void PropertyValueRow::clear()
    {
        _label.clear();
        _tooltip.clear();
        _valueText.clear();
        _markName.clear();
        _favoriteKey.clear();
        _value.reset();
        _listChoice.clear();
        _range                = InspectorNumericRange{};
        _typeName             = hashed_string{};
        _propName             = hashed_string{};
        _handleTargetTypeName = hashed_string{};
        _enumValue            = 0;
        _key                  = 0;
        _height               = 0.0f;
        _scale                = 1.0f;
        _dragSpeed            = 0.0f;
        _rowID                = 0;
        _parentRowID          = 0;
        _ordinal              = 0;
        _elementCount         = 0;
        _depth                = 0;
        _kind                 = PropertyRowKind::Builtin;
        _child                = PropertyRowChild::None;
        _widget               = InspectorValueWidget::None;
        _handleState          = InspectorHandleState{};
        _builtinIndex         = 0;
        _bReadOnly            = SW_FALSE;
        _bDisabled            = SW_FALSE;
        _bModified            = SW_FALSE;
        _bCanReset            = SW_FALSE;
        _bMixed               = SW_FALSE;
        _bFavoriteTarget      = SW_FALSE;
        _bFavorite            = SW_FALSE;
        _bTransient           = SW_FALSE;
        _bPasteAllowed        = SW_FALSE;
        _bExpanded            = SW_FALSE;
        _bColor               = SW_FALSE;
        _bColorHDR            = SW_FALSE;
        _bAssetPath           = SW_FALSE;
        _bMultiline           = SW_FALSE;
        _bHandlePicker        = SW_FALSE;
        _bHandleUnavailable   = SW_FALSE;
        _bCanAddRemove        = SW_FALSE;
        _bRemovable           = SW_FALSE;
    }

    PropertyGridRowWindow::PropertyGridRowWindow()
        : _bSkipOffscreen{ SW_TRUE }
        , _bNameSkippedRows{ SW_FALSE }
    {
    }

    PropertyGridGatherParams::PropertyGridGatherParams()
        : _bFavoritesOnly{ SW_FALSE }
        , _bModifiedOnly{ SW_FALSE }
    {
    }

    PropertyGridViewState::PropertyGridViewState()
        : _bNoMatch{ SW_FALSE }
        , _bHasRows{ SW_FALSE }
    {
    }
} // namespace sw::editor
