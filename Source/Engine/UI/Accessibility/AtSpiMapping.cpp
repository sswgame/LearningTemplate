#include "pch.h"

#include "Engine/UI/Accessibility/AtSpiMapping.h"

namespace sw
{
    namespace
    {
        /** @brief `AtspiStateType` 번호 중 쓰는 것(atspi-constants.h). */
        namespace AtSpiState
        {
            inline constexpr uint32 kChecked   = 4;
            inline constexpr uint32 kEditable  = 7;
            inline constexpr uint32 kEnabled   = 8;
            inline constexpr uint32 kFocusable = 11;
            inline constexpr uint32 kFocused   = 12;
            inline constexpr uint32 kPressed   = 20;
            inline constexpr uint32 kSensitive = 24;
            inline constexpr uint32 kShowing   = 25;
            inline constexpr uint32 kVisible   = 30;
            inline constexpr uint32 kCheckable = 41;
            inline constexpr uint32 kReadOnly  = 43;
        } // namespace AtSpiState

        void setBit( AtSpiStateSet& inoutSet, uint32 state )
        {
            inoutSet._word[state / 32] |= 1u << ( state % 32 );
        }
    } // namespace

    uint32 AtSpiMapping::getRole( AccessibleRole role )
    {
        switch ( role )
        {
            case AccessibleRole::None:
                return 0; // ATSPI_ROLE_INVALID
            case AccessibleRole::Group:
                return 39; // PANEL
            case AccessibleRole::Text:
                return 29; // LABEL
            case AccessibleRole::Image:
                return 27; // IMAGE
            case AccessibleRole::Button:
                return 43; // PUSH_BUTTON
            case AccessibleRole::CheckBox:
                return 7; // CHECK_BOX
            case AccessibleRole::Slider:
                return 51; // SLIDER
            case AccessibleRole::ProgressBar:
                return 42; // PROGRESS_BAR
            case AccessibleRole::ComboBox:
                return 11; // COMBO_BOX
            case AccessibleRole::SpinButton:
                return 52; // SPIN_BUTTON
            case AccessibleRole::TextInput:
                return 61; // TEXT
            case AccessibleRole::List:
                return 31; // LIST
        }
        return 0;
    }

    const utf8* AtSpiMapping::getRoleName( AccessibleRole role )
    {
        switch ( role )
        {
            case AccessibleRole::None:
                return "invalid";
            case AccessibleRole::Group:
                return "panel";
            case AccessibleRole::Text:
                return "label";
            case AccessibleRole::Image:
                return "image";
            case AccessibleRole::Button:
                return "push button";
            case AccessibleRole::CheckBox:
                return "check box";
            case AccessibleRole::Slider:
                return "slider";
            case AccessibleRole::ProgressBar:
                return "progress bar";
            case AccessibleRole::ComboBox:
                return "combo box";
            case AccessibleRole::SpinButton:
                return "spin button";
            case AccessibleRole::TextInput:
                return "text";
            case AccessibleRole::List:
                return "list";
        }
        return "invalid";
    }

    AtSpiStateSet AtSpiMapping::makeStateSet( AccessibleRole role, uint32 states )
    {
        AtSpiStateSet result;
        setBit( result, AtSpiState::kVisible );
        setBit( result, AtSpiState::kShowing );
        if ( ( states & AccessibleState::kDisabled ) == 0 )
        {
            setBit( result, AtSpiState::kEnabled );
            setBit( result, AtSpiState::kSensitive );
        }
        if ( ( states & AccessibleState::kFocusable ) != 0 )
            setBit( result, AtSpiState::kFocusable );
        if ( ( states & AccessibleState::kFocused ) != 0 )
            setBit( result, AtSpiState::kFocused );
        if ( role == AccessibleRole::CheckBox )
            setBit( result, AtSpiState::kCheckable );
        if ( ( states & AccessibleState::kChecked ) != 0 )
            setBit( result, AtSpiState::kChecked );
        if ( ( states & AccessibleState::kPressed ) != 0 )
            setBit( result, AtSpiState::kPressed );
        if ( role == AccessibleRole::TextInput && ( states & AccessibleState::kReadOnly ) == 0 )
            setBit( result, AtSpiState::kEditable );
        if ( ( states & AccessibleState::kReadOnly ) != 0 )
            setBit( result, AtSpiState::kReadOnly );
        return result;
    }

    uint32 AtSpiMapping::getInterfaces( AccessibleRole role )
    {
        uint32 interfaces = AtSpiInterface::kComponent;
        switch ( role )
        {
            case AccessibleRole::Button:
            case AccessibleRole::CheckBox:
            case AccessibleRole::ComboBox:
            case AccessibleRole::SpinButton:
            {
                interfaces |= AtSpiInterface::kAction;
                break;
            }
            case AccessibleRole::Slider:
            case AccessibleRole::ProgressBar:
            {
                interfaces |= AtSpiInterface::kValue;
                break;
            }
            default:
            {
                break;
            }
        }
        return interfaces;
    }
} // namespace sw
