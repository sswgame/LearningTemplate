#include "pch.h"

#include "Editor/Panels/Inspector/Model/PropertyGridViewState.h"

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
