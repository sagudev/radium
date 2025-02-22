/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2020 - Raw Material Software Limited

   JUCE is an open source library subject to commercial or open-source
   licensing.

   By using JUCE, you agree to the terms of both the JUCE 6 End-User License
   Agreement and JUCE Privacy Policy (both effective as of the 16th June 2020).

   End User License Agreement: www.juce.com/juce-6-licence
   Privacy Policy: www.juce.com/juce-privacy-policy

   Or: You may also use this code under the terms of the GPL v3 (see
   www.gnu.org/licenses).

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

namespace juce
{

JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wlanguage-extension-token")

int AccessibilityNativeHandle::idCounter = 0;

//==============================================================================
static String getAutomationId (const AccessibilityHandler& handler)
{
    auto result = handler.getTitle();
    auto* parentComponent = handler.getComponent().getParentComponent();

    while (parentComponent != nullptr)
    {
        if (auto* parentHandler = parentComponent->getAccessibilityHandler())
        {
            auto parentTitle = parentHandler->getTitle();
            result << "." << (parentTitle.isNotEmpty() ? parentTitle : "<empty>");
        }

        parentComponent = parentComponent->getParentComponent();
    }

    return result;
}

static auto roleToControlTypeId (AccessibilityRole roleType)
{
    switch (roleType)
    {
        case AccessibilityRole::popupMenu:
        case AccessibilityRole::dialogWindow:
        case AccessibilityRole::splashScreen:
        case AccessibilityRole::window:        return UIA_WindowControlTypeId;

        case AccessibilityRole::label:
        case AccessibilityRole::staticText:    return UIA_TextControlTypeId;

        case AccessibilityRole::column:
        case AccessibilityRole::row:           return UIA_HeaderItemControlTypeId;

        case AccessibilityRole::button:        return UIA_ButtonControlTypeId;
        case AccessibilityRole::toggleButton:  return UIA_CheckBoxControlTypeId;
        case AccessibilityRole::radioButton:   return UIA_RadioButtonControlTypeId;
        case AccessibilityRole::comboBox:      return UIA_ComboBoxControlTypeId;
        case AccessibilityRole::image:         return UIA_ImageControlTypeId;
        case AccessibilityRole::slider:        return UIA_SliderControlTypeId;
        case AccessibilityRole::editableText:  return UIA_EditControlTypeId;
        case AccessibilityRole::menuItem:      return UIA_MenuItemControlTypeId;
        case AccessibilityRole::menuBar:       return UIA_MenuBarControlTypeId;
        case AccessibilityRole::table:         return UIA_TableControlTypeId;
        case AccessibilityRole::tableHeader:   return UIA_HeaderControlTypeId;
        case AccessibilityRole::cell:          return UIA_DataItemControlTypeId;
        case AccessibilityRole::hyperlink:     return UIA_HyperlinkControlTypeId;
        case AccessibilityRole::list:          return UIA_ListControlTypeId;
        case AccessibilityRole::listItem:      return UIA_ListItemControlTypeId;
        case AccessibilityRole::tree:          return UIA_TreeControlTypeId;
        case AccessibilityRole::treeItem:      return UIA_TreeItemControlTypeId;
        case AccessibilityRole::progressBar:   return UIA_ProgressBarControlTypeId;
        case AccessibilityRole::group:         return UIA_GroupControlTypeId;
        case AccessibilityRole::scrollBar:     return UIA_ScrollBarControlTypeId;
        case AccessibilityRole::tooltip:       return UIA_ToolTipControlTypeId;

        case AccessibilityRole::ignored:
        case AccessibilityRole::unspecified:   break;
    };

    return UIA_CustomControlTypeId;
}

//==============================================================================
AccessibilityNativeHandle::AccessibilityNativeHandle (AccessibilityHandler& handler)
    : ComBaseClassHelper (0),
      accessibilityHandler (handler)
{
}

//==============================================================================
JUCE_COMRESULT AccessibilityNativeHandle::QueryInterface (REFIID refId, void** result)
{
    *result = nullptr;

    if (! isElementValid())
        return (HRESULT) UIA_E_ELEMENTNOTAVAILABLE;

    if ((refId == __uuidof2 (IRawElementProviderFragmentRoot) && ! isFragmentRoot()))
        return E_NOINTERFACE;

    return ComBaseClassHelper::QueryInterface (refId, result);
}

//==============================================================================
JUCE_COMRESULT AccessibilityNativeHandle::get_HostRawElementProvider (IRawElementProviderSimple** pRetVal)
{
    return withCheckedComArgs (pRetVal, *this, [&]
    {
        if (isFragmentRoot())
            if (auto* wrapper = WindowsUIAWrapper::getInstanceWithoutCreating())
                return wrapper->hostProviderFromHwnd ((HWND) accessibilityHandler.getComponent().getWindowHandle(), pRetVal);

        return S_OK;
    });
}

JUCE_COMRESULT AccessibilityNativeHandle::get_ProviderOptions (ProviderOptions* options)
{
    if (options == nullptr)
        return E_INVALIDARG;

    *options = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
}

JUCE_COMRESULT AccessibilityNativeHandle::GetPatternProvider (PATTERNID pId, IUnknown** pRetVal)
{
    return withCheckedComArgs (pRetVal, *this, [&]
    {
        *pRetVal = [&]() -> IUnknown*
        {
            const auto role = accessibilityHandler.getRole();
            const auto fragmentRoot = isFragmentRoot();

            switch (pId)
            {
                case UIA_WindowPatternId:
                {
                    if (fragmentRoot)
                        return new UIAWindowProvider (this);

                    break;
                }
                case UIA_TransformPatternId:
                {
                    if (fragmentRoot)
                        return new UIATransformProvider (this);

                    break;
                }
                case UIA_TextPatternId:
                case UIA_TextPattern2Id:
                {
                    if (accessibilityHandler.getTextInterface() != nullptr)
                        return new UIATextProvider (this);

                    break;
                }
                case UIA_ValuePatternId:
                {
                    if (accessibilityHandler.getValueInterface() != nullptr)
                        return new UIAValueProvider (this);

                    break;
                }
                case UIA_RangeValuePatternId:
                {
                    if (accessibilityHandler.getValueInterface() != nullptr
                        && accessibilityHandler.getValueInterface()->getRange().isValid())
                    {
                        return new UIARangeValueProvider (this);
                    }

                    break;
                }
                case UIA_TogglePatternId:
                {
                    if (accessibilityHandler.getActions().contains (AccessibilityActionType::toggle)
                        && accessibilityHandler.getCurrentState().isCheckable())
                    {
                        return new UIAToggleProvider (this);
                    }

                    break;
                }
                case UIA_SelectionPatternId:
                {
                    if (role == AccessibilityRole::list
                        || role == AccessibilityRole::popupMenu
                        || role == AccessibilityRole::tree)
                    {
                        return new UIASelectionProvider (this);
                    }

                    break;
                }
                case UIA_SelectionItemPatternId:
                {
                    auto state = accessibilityHandler.getCurrentState();

                    if (state.isSelectable() || state.isMultiSelectable()
                        || role == AccessibilityRole::radioButton)
                    {
                        return new UIASelectionItemProvider (this);
                    }

                    break;
                }
                case UIA_GridPatternId:
                {
                    if (accessibilityHandler.getTableInterface() != nullptr)
                        return new UIAGridProvider (this);

                    break;
                }
                case UIA_GridItemPatternId:
                {
                    if (accessibilityHandler.getCellInterface() != nullptr)
                        return new UIAGridItemProvider (this);

                    break;
                }
                case UIA_InvokePatternId:
                {
                    if (accessibilityHandler.getActions().contains (AccessibilityActionType::press))
                        return new UIAInvokeProvider (this);

                    break;
                }
                case UIA_ExpandCollapsePatternId:
                {
                    if (accessibilityHandler.getActions().contains (AccessibilityActionType::showMenu)
                        && accessibilityHandler.getCurrentState().isExpandable())
                        return new UIAExpandCollapseProvider (this);

                    break;
                }
            }

            return nullptr;
        }();

        return S_OK;
    });
}

JUCE_COMRESULT AccessibilityNativeHandle::GetPropertyValue (PROPERTYID propertyId, VARIANT* pRetVal)
{
    return withCheckedComArgs (pRetVal, *this, [&]
    {
        VariantHelpers::clear (pRetVal);

        const auto role    = accessibilityHandler.getRole();
        const auto state   = accessibilityHandler.getCurrentState();
        const auto ignored = accessibilityHandler.isIgnored();

        switch (propertyId)
        {
            case UIA_AutomationIdPropertyId:
                VariantHelpers::setString (getAutomationId (accessibilityHandler), pRetVal);
                break;
            case UIA_ControlTypePropertyId:
                VariantHelpers::setInt (roleToControlTypeId (role), pRetVal);
                break;
            case UIA_FrameworkIdPropertyId:
                VariantHelpers::setString ("JUCE", pRetVal);
                break;
            case UIA_FullDescriptionPropertyId:
                VariantHelpers::setString (accessibilityHandler.getDescription(), pRetVal);
                break;
            case UIA_HelpTextPropertyId:
                VariantHelpers::setString (accessibilityHandler.getHelp(), pRetVal);
                break;
            case UIA_IsContentElementPropertyId:
                VariantHelpers::setBool (! ignored && accessibilityHandler.isVisibleWithinParent(),
                                         pRetVal);
                break;
            case UIA_IsControlElementPropertyId:
                VariantHelpers::setBool (true, pRetVal);
                break;
            case UIA_IsDialogPropertyId:
                VariantHelpers::setBool (role == AccessibilityRole::dialogWindow, pRetVal);
                break;
            case UIA_IsEnabledPropertyId:
                VariantHelpers::setBool (accessibilityHandler.getComponent().isEnabled(), pRetVal);
                break;
            case UIA_IsKeyboardFocusablePropertyId:
                VariantHelpers::setBool (state.isFocusable(), pRetVal);
                break;
            case UIA_HasKeyboardFocusPropertyId:
                VariantHelpers::setBool (accessibilityHandler.hasFocus (true), pRetVal);
                break;
            case UIA_IsOffscreenPropertyId:
                VariantHelpers::setBool (! accessibilityHandler.isVisibleWithinParent(), pRetVal);
                break;
            case UIA_IsPasswordPropertyId:
                if (auto* textInterface = accessibilityHandler.getTextInterface())
                    VariantHelpers::setBool (textInterface->isDisplayingProtectedText(), pRetVal);

                break;
            case UIA_IsPeripheralPropertyId:
                VariantHelpers::setBool (role == AccessibilityRole::tooltip
                                         || role == AccessibilityRole::popupMenu
                                         || role == AccessibilityRole::splashScreen,
                                         pRetVal);
                break;
            case UIA_NamePropertyId:
                if (! ignored)
                     VariantHelpers::setString (getElementName(), pRetVal);

                break;
            case UIA_ProcessIdPropertyId:
                VariantHelpers::setInt ((int) GetCurrentProcessId(), pRetVal);
                break;
            case UIA_NativeWindowHandlePropertyId:
                if (isFragmentRoot())
                    VariantHelpers::setInt ((int) (pointer_sized_int) accessibilityHandler.getComponent().getWindowHandle(), pRetVal);

                break;
        }

        return S_OK;
    });
}

//==============================================================================
JUCE_COMRESULT AccessibilityNativeHandle::Navigate (NavigateDirection direction, IRawElementProviderFragment** pRetVal)
{
    return withCheckedComArgs (pRetVal, *this, [&]
    {
        auto* handler = [&]() -> AccessibilityHandler*
        {
            if (direction == NavigateDirection_Parent)
                return accessibilityHandler.getParent();

            if (direction == NavigateDirection_FirstChild
                || direction == NavigateDirection_LastChild)
            {
                auto children = accessibilityHandler.getChildren();

                return children.empty() ? nullptr
                                        : (direction == NavigateDirection_FirstChild ? children.front()
                                                                                     : children.back());
            }

            if (direction == NavigateDirection_NextSibling
                || direction == NavigateDirection_PreviousSibling)
            {
                if (auto* parent = accessibilityHandler.getParent())
                {
                    const auto siblings = parent->getChildren();
                    const auto iter = std::find (siblings.cbegin(), siblings.cend(), &accessibilityHandler);

                    if (iter == siblings.end())
                        return nullptr;

                    if (direction == NavigateDirection_NextSibling && iter != std::prev (siblings.cend()))
                        return *std::next (iter);

                    if (direction == NavigateDirection_PreviousSibling && iter != siblings.cbegin())
                        return *std::prev (iter);
                }
            }

            return nullptr;
        }();

        if (handler != nullptr)
            if (auto* provider = handler->getNativeImplementation())
                if (provider->isElementValid())
                    provider->QueryInterface (IID_PPV_ARGS (pRetVal));

        return S_OK;
    });
}

JUCE_COMRESULT AccessibilityNativeHandle::GetRuntimeId (SAFEARRAY** pRetVal)
{
    return withCheckedComArgs (pRetVal, *this, [&]
    {
        if (! isFragmentRoot())
        {
            *pRetVal = SafeArrayCreateVector (VT_I4, 0, 2);

            if (*pRetVal == nullptr)
                return E_OUTOFMEMORY;

            for (LONG i = 0; i < 2; ++i)
            {
                auto hr = SafeArrayPutElement (*pRetVal, &i, &rtid[(size_t) i]);

                if (FAILED (hr))
                    return E_FAIL;
            }
        }

        return S_OK;
    });
}

JUCE_COMRESULT AccessibilityNativeHandle::get_BoundingRectangle (UiaRect* pRetVal)
{
    return withCheckedComArgs (pRetVal, *this, [&]
    {
        auto bounds = Desktop::getInstance().getDisplays()
                        .logicalToPhysical (accessibilityHandler.getComponent().getScreenBounds());

        pRetVal->left   = bounds.getX();
        pRetVal->top    = bounds.getY();
        pRetVal->width  = bounds.getWidth();
        pRetVal->height = bounds.getHeight();

        return S_OK;
    });
}

JUCE_COMRESULT AccessibilityNativeHandle::GetEmbeddedFragmentRoots (SAFEARRAY** pRetVal)
{
    return withCheckedComArgs (pRetVal, *this, []
    {
        return S_OK;
    });
}

JUCE_COMRESULT AccessibilityNativeHandle::SetFocus()
{
    if (! isElementValid())
        return (HRESULT) UIA_E_ELEMENTNOTAVAILABLE;

    const WeakReference<Component> safeComponent (&accessibilityHandler.getComponent());

    accessibilityHandler.getActions().invoke (AccessibilityActionType::focus);

    if (safeComponent != nullptr)
        accessibilityHandler.grabFocus();

    return S_OK;
}

JUCE_COMRESULT AccessibilityNativeHandle::get_FragmentRoot (IRawElementProviderFragmentRoot** pRetVal)
{
    return withCheckedComArgs (pRetVal, *this, [&]() -> HRESULT
    {
        auto* handler = [&]() -> AccessibilityHandler*
        {
            if (isFragmentRoot())
                return &accessibilityHandler;

            if (auto* peer = accessibilityHandler.getComponent().getPeer())
                return peer->getComponent().getAccessibilityHandler();

            return nullptr;
        }();

        if (handler != nullptr)
        {
            handler->getNativeImplementation()->QueryInterface (IID_PPV_ARGS (pRetVal));
            return S_OK;
        }

        return (HRESULT) UIA_E_ELEMENTNOTAVAILABLE;
    });
}

//==============================================================================
JUCE_COMRESULT AccessibilityNativeHandle::ElementProviderFromPoint (double x, double y, IRawElementProviderFragment** pRetVal)
{
    return withCheckedComArgs (pRetVal, *this, [&]
    {
        auto* handler = [&]
        {
            auto logicalScreenPoint = Desktop::getInstance().getDisplays()
                                        .physicalToLogical (Point<int> (roundToInt (x),
                                                                        roundToInt (y)));

            if (auto* child = accessibilityHandler.getChildAt (logicalScreenPoint))
                return child;

            return &accessibilityHandler;
        }();

        handler->getNativeImplementation()->QueryInterface (IID_PPV_ARGS (pRetVal));

        return S_OK;
    });
}

JUCE_COMRESULT AccessibilityNativeHandle::GetFocus (IRawElementProviderFragment** pRetVal)
{
    return withCheckedComArgs (pRetVal, *this, [&]
    {
        const auto getFocusHandler = [this]() -> AccessibilityHandler*
        {
            if (auto* modal = Component::getCurrentlyModalComponent())
            {
                const auto& component = accessibilityHandler.getComponent();

                if (! component.isParentOf (modal)
                     && component.isCurrentlyBlockedByAnotherModalComponent())
                {
                    if (auto* modalHandler = modal->getAccessibilityHandler())
                    {
                        if (auto* focusChild = modalHandler->getChildFocus())
                            return focusChild;

                        return modalHandler;
                    }
                }
            }

            if (auto* focusChild = accessibilityHandler.getChildFocus())
                return focusChild;

            return nullptr;
        };

        if (auto* focusHandler = getFocusHandler())
            focusHandler->getNativeImplementation()->QueryInterface (IID_PPV_ARGS (pRetVal));

        return S_OK;
    });
}

//==============================================================================
String AccessibilityNativeHandle::getElementName() const
{
    if (accessibilityHandler.getRole() == AccessibilityRole::tooltip)
        return accessibilityHandler.getDescription();

    auto name = accessibilityHandler.getTitle();

    if (name.isEmpty() && isFragmentRoot())
        return getAccessibleApplicationOrPluginName();

    return name;
}

JUCE_END_IGNORE_WARNINGS_GCC_LIKE

} // namespace juce
