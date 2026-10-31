#include "pch.h"

#include "Engine/UI/Accessibility/Windows/UiaAccessibilityBridge.h"

#if defined( SW_PLATFORM_WINDOWS ) && defined( SW_WITH_CLIENT_CODE )
    #include "Core/Memory/Memory.h"

    #include "Engine/UI/Accessibility/UIAccessibilityActions.h"
    #include "Engine/UI/Accessibility/UIAccessibilityService.h"
    #include "Engine/UI/Accessibility/UIAccessibilityTree.h"
    #include "Engine/UI/UISystem.h"
    #include "Engine/Window/IWindow.h"

    #include <objbase.h> // WIN32_LEAN_AND_MEAN 이면 `interface` 매크로가 없다 — UIAutomation.h 가 쓴다
    #include <oleauto.h>
    #include <UIAutomation.h>

namespace sw
{
    namespace
    {
        struct UiaAccessibilityBridgeInternal
        {
            /** @brief 한 번에 바꾸는 크기가 정해지지 않은 범위의 큰 단계(범위의 10%)입니다. */
            static constexpr float64 kLargeChangeFraction = 0.1;

            /** @brief UIA 요소가 사라졌음을 알리는 오류 값입니다(위젯이 지워졌거나 브리지가 내려갔다). */
            static HRESULT makeElementNotAvailable() { return static_cast<HRESULT>( UIA_E_ELEMENTNOTAVAILABLE ); }
            static HRESULT makeNotSupported() { return static_cast<HRESULT>( UIA_E_NOTSUPPORTED ); }

            /** @brief UTF-8 글을 BSTR 로 바꿉니다(호출한 쪽이 `SysFreeString`). 빈 글은 빈 BSTR 입니다. */
            static BSTR makeBstr( string_view text )
            {
                if ( text.empty() )
                    return SysAllocString( L"" );
                const int32 wideCount = MultiByteToWideChar( CP_UTF8, 0, text.data(), static_cast<int32>( text.size() ), nullptr, 0 );
                if ( wideCount <= 0 )
                    return SysAllocString( L"" );
                BSTR result = SysAllocStringLen( nullptr, static_cast<UINT>( wideCount ) );
                if ( result != nullptr )
                    (void)MultiByteToWideChar( CP_UTF8, 0, text.data(), static_cast<int32>( text.size() ), result, wideCount );
                return result;
            }

            /**
             * @brief 브리지와 제공자가 함께 쥐는 상태입니다. 제공자는 클라이언트가 브리지보다 오래 쥘 수 있어 브리지를 직접 가리키지 않고 이것을 가리킨다.
             * @details 브리지가 내려가면 `_pUI` 를 비워 제공자가 "요소 없음"으로 답하게 한다. 접근성 트리 스냅샷은 UI 가 갱신될 때마다 낡은 것으로 표시된다.
             */
            struct Core
            {
                UISystem*               _pUI{ nullptr };
                IWindow*                _pWindow{ nullptr };
                UIAccessibilitySnapshot _snapshot{};
                bool                    _bSnapshotValid{ false };
                LONG                    _refCount{ 1 };

                void addRef() { InterlockedIncrement( &_refCount ); }
                void release()
                {
                    if ( InterlockedDecrement( &_refCount ) == 0 )
                        sw_delete( this );
                }
                HWND getHwnd() const { return _pWindow != nullptr ? static_cast<HWND>( _pWindow->tryGetWin32Handle() ) : nullptr; }
                /** @brief 지금 접근성 트리입니다(UI 가 갱신된 뒤 처음 물으면 새로 뽑는다). UI 시스템이 내려갔으면 nullptr 입니다. */
                const UIAccessibilitySnapshot* findSnapshot()
                {
                    if ( _pUI == nullptr )
                        return nullptr;
                    if ( _bSnapshotValid == false )
                    {
                        UIAccessibilityService::populateSnapshot( *_pUI, _snapshot );
                        _bSnapshotValid = true;
                    }
                    return &_snapshot;
                }
            };

            /** @brief 노드 @p index 의 자식 중 첫째(@p bFirst)나 마지막입니다. 없으면 -1. 루트 아래는 @p index 가 -1 입니다. */
            static int32 findChild( const UIAccessibilitySnapshot& snapshot, int32 parent, bool bFirst )
            {
                int32 found = -1;
                for ( size_t index = 0; index < snapshot._listNode.size(); ++index )
                {
                    if ( snapshot._listNode[index]._parent != parent )
                        continue;
                    found = static_cast<int32>( index );
                    if ( bFirst )
                        break;
                }
                return found;
            }

            /** @brief 같은 부모의 이웃 노드입니다(@p step 이 1 이면 다음, −1 이면 앞). 없으면 -1. */
            static int32 findSibling( const UIAccessibilitySnapshot& snapshot, int32 node, int32 step )
            {
                const int32 parent = snapshot._listNode[static_cast<size_t>( node )]._parent;
                for ( int32 index = node + step; 0 <= index && index < static_cast<int32>( snapshot._listNode.size() ); index += step )
                {
                    if ( snapshot._listNode[static_cast<size_t>( index )]._parent == parent )
                        return index;
                }
                return -1;
            }

            class Element;

            /** @brief 요소를 만들어 AddRef 된 채 돌려줍니다. */
            static Element* createElement( Core& core, uint32 screen, WidgetID widget, bool bRoot );

            /**
             * @class Element
             * @brief UIA 제공자 하나입니다(루트 하나와 위젯마다). 필요한 인터페이스를 한 객체가 다 구현하고 `GetPatternProvider` 가 역할에 맞는 것만 내준다.
             */
            class Element final : public IRawElementProviderSimple,
                                  public IRawElementProviderFragment,
                                  public IRawElementProviderFragmentRoot,
                                  public IInvokeProvider,
                                  public IToggleProvider,
                                  public IValueProvider,
                                  public IRangeValueProvider
            {
            public:
                Element( Core& core, uint32 screen, WidgetID widget, bool bRoot )
                    : _pCore{ &core }
                    , _screen{ screen }
                    , _widget{ widget }
                    , _refCount{ 1 }
                    , _bRoot{ bRoot }
                {
                    _pCore->addRef();
                }

                ~Element() { _pCore->release(); }

                Element( const Element& )            = delete;
                Element& operator=( const Element& ) = delete;

                // --- IUnknown ----------------------------------------------------------------
                ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>( InterlockedIncrement( &_refCount ) ); }
                ULONG STDMETHODCALLTYPE Release() override
                {
                    const LONG count = InterlockedDecrement( &_refCount );
                    if ( count == 0 )
                        sw_delete( this );
                    return static_cast<ULONG>( count );
                }
                HRESULT STDMETHODCALLTYPE QueryInterface( REFIID riid, void** ppOutObject ) override
                {
                    if ( ppOutObject == nullptr )
                        return E_INVALIDARG;
                    *ppOutObject = nullptr;
                    if ( riid == __uuidof( IUnknown ) || riid == __uuidof( IRawElementProviderSimple ) )
                        *ppOutObject = static_cast<IRawElementProviderSimple*>( this );
                    else if ( riid == __uuidof( IRawElementProviderFragment ) )
                        *ppOutObject = static_cast<IRawElementProviderFragment*>( this );
                    else if ( riid == __uuidof( IRawElementProviderFragmentRoot ) && _bRoot )
                        *ppOutObject = static_cast<IRawElementProviderFragmentRoot*>( this );
                    else if ( riid == __uuidof( IInvokeProvider ) )
                        *ppOutObject = static_cast<IInvokeProvider*>( this );
                    else if ( riid == __uuidof( IToggleProvider ) )
                        *ppOutObject = static_cast<IToggleProvider*>( this );
                    else if ( riid == __uuidof( IValueProvider ) )
                        *ppOutObject = static_cast<IValueProvider*>( this );
                    else if ( riid == __uuidof( IRangeValueProvider ) )
                        *ppOutObject = static_cast<IRangeValueProvider*>( this );
                    else
                        return E_NOINTERFACE;
                    AddRef();
                    return S_OK;
                }

                // --- IRawElementProviderSimple -------------------------------------------------
                HRESULT STDMETHODCALLTYPE get_ProviderOptions( ProviderOptions* pRetVal ) override
                {
                    // 메인 스레드로 옮겨 불린다(UI 상태는 게임 스레드만 만진다).
                    *pRetVal = static_cast<ProviderOptions>( ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading );
                    return S_OK;
                }

                HRESULT STDMETHODCALLTYPE GetPatternProvider( PATTERNID patternID, IUnknown** ppOutProvider ) override
                {
                    *ppOutProvider = nullptr;
                    if ( _bRoot )
                        return S_OK;
                    AccessibleInfo info{};
                    if ( copyInfo( info ) == false )
                        return makeElementNotAvailable();
                    bool bSupported = false;
                    switch ( patternID )
                    {
                        case UIA_InvokePatternId:
                        {
                            bSupported = info._role == AccessibleRole::Button;
                            break;
                        }
                        case UIA_TogglePatternId:
                        {
                            bSupported = info._role == AccessibleRole::CheckBox;
                            break;
                        }
                        case UIA_ValuePatternId:
                        {
                            bSupported = info._role == AccessibleRole::TextInput || info._role == AccessibleRole::ComboBox || info._role == AccessibleRole::SpinButton;
                            break;
                        }
                        case UIA_RangeValuePatternId:
                        {
                            bSupported = info._bHasRange;
                            break;
                        }
                        default:
                        {
                            break;
                        }
                    }
                    if ( bSupported == false )
                        return S_OK;
                    *ppOutProvider = static_cast<IRawElementProviderSimple*>( this ); // QueryInterface 가 요청한 패턴 인터페이스로 바꿔 준다
                    AddRef();
                    return S_OK;
                }

                HRESULT STDMETHODCALLTYPE GetPropertyValue( PROPERTYID propertyID, VARIANT* pRetVal ) override
                {
                    VariantInit( pRetVal );
                    if ( _bRoot )
                    {
                        // 창 자체의 속성(이름, 클래스, 창 핸들)은 호스트 제공자가 답한다. 프레임워크 이름만 우리가 말한다.
                        if ( propertyID == UIA_FrameworkIdPropertyId )
                            setBstr( *pRetVal, "SW" );
                        return S_OK;
                    }
                    AccessibleInfo info{};
                    if ( copyInfo( info ) == false )
                        return makeElementNotAvailable();
                    switch ( propertyID )
                    {
                        case UIA_ControlTypePropertyId:
                        {
                            setInt( *pRetVal, UiaAccessibility::getControlType( info._role ) );
                            break;
                        }
                        case UIA_NamePropertyId:
                        {
                            setBstr( *pRetVal, info._name );
                            break;
                        }
                        case UIA_IsEnabledPropertyId:
                        {
                            setBool( *pRetVal, ( info._states & AccessibleState::kDisabled ) == 0 );
                            break;
                        }
                        case UIA_HasKeyboardFocusPropertyId:
                        {
                            setBool( *pRetVal, ( info._states & AccessibleState::kFocused ) != 0 );
                            break;
                        }
                        case UIA_IsKeyboardFocusablePropertyId:
                        {
                            setBool( *pRetVal, ( info._states & AccessibleState::kFocusable ) != 0 );
                            break;
                        }
                        case UIA_IsControlElementPropertyId:
                        case UIA_IsContentElementPropertyId:
                        {
                            setBool( *pRetVal, true );
                            break;
                        }
                        case UIA_FrameworkIdPropertyId:
                        {
                            setBstr( *pRetVal, "SW" );
                            break;
                        }
                        case UIA_ProviderDescriptionPropertyId:
                        {
                            setBstr( *pRetVal, "SW Engine runtime UI" );
                            break;
                        }
                        default:
                        {
                            break; // 모르는 속성은 비워 둔다(UIA 가 기본값을 쓴다)
                        }
                    }
                    return S_OK;
                }

                HRESULT STDMETHODCALLTYPE get_HostRawElementProvider( IRawElementProviderSimple** ppOutHost ) override
                {
                    *ppOutHost = nullptr;
                    if ( _bRoot == false )
                        return S_OK;
                    const HWND hwnd = _pCore->getHwnd();
                    return hwnd != nullptr ? UiaHostProviderFromHwnd( hwnd, ppOutHost ) : S_OK;
                }

                // --- IRawElementProviderFragment --------------------------------------------------
                HRESULT STDMETHODCALLTYPE Navigate( NavigateDirection direction, IRawElementProviderFragment** ppOutFragment ) override
                {
                    *ppOutFragment                           = nullptr;
                    const UIAccessibilitySnapshot* pSnapshot = _pCore->findSnapshot();
                    if ( pSnapshot == nullptr )
                        return makeElementNotAvailable();
                    int32 target = -1;
                    if ( _bRoot )
                    {
                        if ( direction == NavigateDirection_FirstChild )
                            target = findChild( *pSnapshot, -1, true );
                        else if ( direction == NavigateDirection_LastChild )
                            target = findChild( *pSnapshot, -1, false );
                        else
                            return S_OK; // 루트의 부모와 이웃은 호스트(창)가 답한다
                    }
                    else
                    {
                        const int32 self = pSnapshot->findNode( _screen, _widget );
                        if ( self < 0 )
                            return makeElementNotAvailable();
                        switch ( direction )
                        {
                            case NavigateDirection_Parent:
                            {
                                const int32 parent = pSnapshot->_listNode[static_cast<size_t>( self )]._parent;
                                if ( parent < 0 )
                                {
                                    *ppOutFragment = createElement( *_pCore, 0, kInvalidWidgetID, true );
                                    return S_OK;
                                }
                                target = parent;
                                break;
                            }
                            case NavigateDirection_NextSibling:
                            {
                                target = findSibling( *pSnapshot, self, 1 );
                                break;
                            }
                            case NavigateDirection_PreviousSibling:
                            {
                                target = findSibling( *pSnapshot, self, -1 );
                                break;
                            }
                            case NavigateDirection_FirstChild:
                            {
                                target = findChild( *pSnapshot, self, true );
                                break;
                            }
                            case NavigateDirection_LastChild:
                            {
                                target = findChild( *pSnapshot, self, false );
                                break;
                            }
                        }
                    }
                    if ( target < 0 )
                        return S_OK;
                    const AccessibleNode& node = pSnapshot->_listNode[static_cast<size_t>( target )];
                    *ppOutFragment             = createElement( *_pCore, node._screen, node._widget, false );
                    return S_OK;
                }

                HRESULT STDMETHODCALLTYPE GetRuntimeId( SAFEARRAY** ppOutRuntimeID ) override
                {
                    *ppOutRuntimeID = nullptr;
                    if ( _bRoot )
                        return S_OK; // 루트의 번호는 호스트(창)가 갖는다
                    const int32 arrID[3] = { UiaAppendRuntimeId, static_cast<int32>( _screen ), static_cast<int32>( _widget ) };
                    SAFEARRAY*  pArray   = SafeArrayCreateVector( VT_I4, 0, 3 );
                    if ( pArray == nullptr )
                        return E_OUTOFMEMORY;
                    for ( LONG index = 0; index < 3; ++index )
                    {
                        (void)SafeArrayPutElement( pArray, &index, const_cast<int32*>( &arrID[index] ) );
                    }
                    *ppOutRuntimeID = pArray;
                    return S_OK;
                }

                HRESULT STDMETHODCALLTYPE get_BoundingRectangle( UiaRect* pRetVal ) override
                {
                    *pRetVal        = UiaRect{};
                    const HWND hwnd = _pCore->getHwnd();
                    if ( _bRoot )
                    {
                        RECT client{};
                        if ( hwnd == nullptr || GetClientRect( hwnd, &client ) == FALSE )
                            return S_OK;
                        POINT origin{ 0, 0 };
                        (void)ClientToScreen( hwnd, &origin );
                        *pRetVal = UiaRect{ static_cast<float64>( origin.x ), static_cast<float64>( origin.y ), static_cast<float64>( client.right ), static_cast<float64>( client.bottom ) };
                        return S_OK;
                    }
                    const UIAccessibilitySnapshot* pSnapshot = _pCore->findSnapshot();
                    const int32                    node      = pSnapshot != nullptr ? pSnapshot->findNode( _screen, _widget ) : -1;
                    if ( node < 0 || hwnd == nullptr )
                        return makeElementNotAvailable();
                    const float4&     bounds   = pSnapshot->_listNode[static_cast<size_t>( node )]._bounds;
                    const UIViewport& viewport = _pCore->_pUI->getViewport();
                    const float64     scale    = viewport._uiScale > 0.0f ? static_cast<float64>( viewport._uiScale ) : 1.0;
                    POINT             topLeft{ static_cast<LONG>( static_cast<float64>( bounds._x ) * scale ) + viewport._pointerOrigin._x,
                                   static_cast<LONG>( static_cast<float64>( bounds._y ) * scale ) + viewport._pointerOrigin._y };
                    (void)ClientToScreen( hwnd, &topLeft );
                    *pRetVal = UiaRect{ static_cast<float64>( topLeft.x ), static_cast<float64>( topLeft.y ), static_cast<float64>( bounds._z ) * scale, static_cast<float64>( bounds._w ) * scale };
                    return S_OK;
                }

                HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots( SAFEARRAY** ppOutRoot ) override
                {
                    *ppOutRoot = nullptr;
                    return S_OK;
                }

                HRESULT STDMETHODCALLTYPE SetFocus() override
                {
                    if ( _bRoot || _pCore->_pUI == nullptr )
                        return S_OK;
                    return UIAccessibilityActions::focus( *_pCore->_pUI, _screen, _widget ) ? S_OK : makeElementNotAvailable();
                }

                HRESULT STDMETHODCALLTYPE get_FragmentRoot( IRawElementProviderFragmentRoot** ppOutRoot ) override
                {
                    *ppOutRoot = createElement( *_pCore, 0, kInvalidWidgetID, true );
                    return S_OK;
                }

                // --- IRawElementProviderFragmentRoot ----------------------------------------------
                HRESULT STDMETHODCALLTYPE ElementProviderFromPoint( float64 x, float64 y, IRawElementProviderFragment** ppOutFragment ) override
                {
                    *ppOutFragment                           = nullptr;
                    const UIAccessibilitySnapshot* pSnapshot = _pCore->findSnapshot();
                    const HWND                     hwnd      = _pCore->getHwnd();
                    if ( pSnapshot == nullptr || hwnd == nullptr )
                        return S_OK;
                    POINT clientPoint{ static_cast<LONG>( x ), static_cast<LONG>( y ) };
                    (void)ScreenToClient( hwnd, &clientPoint );
                    const UIViewport& viewport = _pCore->_pUI->getViewport();
                    const float64     scale    = viewport._uiScale > 0.0f ? static_cast<float64>( viewport._uiScale ) : 1.0;
                    const float64     uiX      = static_cast<float64>( clientPoint.x - viewport._pointerOrigin._x ) / scale;
                    const float64     uiY      = static_cast<float64>( clientPoint.y - viewport._pointerOrigin._y ) / scale;
                    // 트리 순서에서 부모가 자식보다 앞이라, 점을 담는 마지막 노드가 가장 깊은 요소다.
                    for ( size_t index = pSnapshot->_listNode.size(); index > 0; --index )
                    {
                        const AccessibleNode& node     = pSnapshot->_listNode[index - 1];
                        const float64         left     = static_cast<float64>( node._bounds._x );
                        const float64         top      = static_cast<float64>( node._bounds._y );
                        const bool            bInsideX = left <= uiX && uiX <= left + static_cast<float64>( node._bounds._z );
                        const bool            bInsideY = top <= uiY && uiY <= top + static_cast<float64>( node._bounds._w );
                        if ( bInsideX && bInsideY )
                        {
                            *ppOutFragment = createElement( *_pCore, node._screen, node._widget, false );
                            break;
                        }
                    }
                    return S_OK;
                }

                HRESULT STDMETHODCALLTYPE GetFocus( IRawElementProviderFragment** ppOutFragment ) override
                {
                    *ppOutFragment                           = nullptr;
                    const UIAccessibilitySnapshot* pSnapshot = _pCore->findSnapshot();
                    if ( pSnapshot == nullptr || pSnapshot->_focusedNode < 0 )
                        return S_OK;
                    const AccessibleNode& node = pSnapshot->_listNode[static_cast<size_t>( pSnapshot->_focusedNode )];
                    *ppOutFragment             = createElement( *_pCore, node._screen, node._widget, false );
                    return S_OK;
                }

                // --- IInvokeProvider · IToggleProvider ------------------------------------------------
                HRESULT STDMETHODCALLTYPE Invoke() override { return runAction(); }
                HRESULT STDMETHODCALLTYPE Toggle() override { return runAction(); }
                HRESULT STDMETHODCALLTYPE get_ToggleState( ToggleState* pRetVal ) override
                {
                    AccessibleInfo info{};
                    if ( copyInfo( info ) == false )
                        return makeElementNotAvailable();
                    *pRetVal = ( info._states & AccessibleState::kChecked ) != 0 ? ToggleState_On : ToggleState_Off;
                    return S_OK;
                }

                // --- IValueProvider(읽기 전용) --------------------------------------------------------
                HRESULT STDMETHODCALLTYPE SetValue( LPCWSTR /*val*/ ) override { return makeNotSupported(); }
                HRESULT STDMETHODCALLTYPE get_Value( BSTR* pRetVal ) override
                {
                    *pRetVal = nullptr;
                    AccessibleInfo info{};
                    if ( copyInfo( info ) == false )
                        return makeElementNotAvailable();
                    *pRetVal = makeBstr( info._value );
                    return S_OK;
                }
                HRESULT STDMETHODCALLTYPE get_IsReadOnly( BOOL* pRetVal ) override
                {
                    // IValueProvider 와 IRangeValueProvider 가 같은 이름이라 한 구현이 둘 다 맡는다. 값 패턴은 늘 읽기 전용, 범위 패턴은 진행 막대만 읽기 전용이다.
                    AccessibleInfo info{};
                    if ( copyInfo( info ) == false )
                        return makeElementNotAvailable();
                    *pRetVal = ( info._bHasRange == false || ( info._states & AccessibleState::kReadOnly ) != 0 ) ? TRUE : FALSE;
                    return S_OK;
                }

                // --- IRangeValueProvider ------------------------------------------------------------
                HRESULT STDMETHODCALLTYPE SetValue( float64 val ) override
                {
                    if ( _pCore->_pUI == nullptr )
                        return makeElementNotAvailable();
                    return UIAccessibilityActions::setRangeValue( *_pCore->_pUI, _screen, _widget, static_cast<float32>( val ) ) ? S_OK : makeElementNotAvailable();
                }
                HRESULT STDMETHODCALLTYPE get_Value( float64* pRetVal ) override { return readRange( pRetVal, &AccessibleInfo::_rangeValue ); }
                HRESULT STDMETHODCALLTYPE get_Maximum( float64* pRetVal ) override { return readRange( pRetVal, &AccessibleInfo::_rangeMax ); }
                HRESULT STDMETHODCALLTYPE get_Minimum( float64* pRetVal ) override { return readRange( pRetVal, &AccessibleInfo::_rangeMin ); }
                HRESULT STDMETHODCALLTYPE get_SmallChange( float64* pRetVal ) override { return readRange( pRetVal, &AccessibleInfo::_rangeStep ); }
                HRESULT STDMETHODCALLTYPE get_LargeChange( float64* pRetVal ) override
                {
                    AccessibleInfo info{};
                    if ( copyInfo( info ) == false )
                        return makeElementNotAvailable();
                    *pRetVal = static_cast<float64>( info._rangeMax - info._rangeMin ) * kLargeChangeFraction;
                    return S_OK;
                }

            private:
                /** @brief 이 요소가 가리키는 위젯의 지금 접근성 정보입니다. 위젯이 없으면 false 입니다. */
                [[nodiscard]] bool copyInfo( AccessibleInfo& outInfo )
                {
                    const UIAccessibilitySnapshot* pSnapshot = _pCore->findSnapshot();
                    const int32                    node      = pSnapshot != nullptr ? pSnapshot->findNode( _screen, _widget ) : -1;
                    if ( node < 0 )
                        return false;
                    outInfo = pSnapshot->_listNode[static_cast<size_t>( node )]._info;
                    return true;
                }

                HRESULT readRange( float64* pRetVal, float32 AccessibleInfo::* pMember )
                {
                    AccessibleInfo info{};
                    if ( copyInfo( info ) == false )
                        return makeElementNotAvailable();
                    *pRetVal = static_cast<float64>( info.*pMember );
                    return S_OK;
                }

                HRESULT runAction()
                {
                    if ( _pCore->_pUI == nullptr )
                        return makeElementNotAvailable();
                    return UIAccessibilityActions::invoke( *_pCore->_pUI, _screen, _widget ) ? S_OK : static_cast<HRESULT>( UIA_E_ELEMENTNOTENABLED );
                }

                static void setInt( VARIANT& outValue, int32 value )
                {
                    outValue.vt   = VT_I4;
                    outValue.lVal = value;
                }
                static void setBool( VARIANT& outValue, bool bValue )
                {
                    outValue.vt      = VT_BOOL;
                    outValue.boolVal = bValue ? VARIANT_TRUE : VARIANT_FALSE;
                }
                static void setBstr( VARIANT& outValue, string_view text )
                {
                    outValue.vt      = VT_BSTR;
                    outValue.bstrVal = makeBstr( text );
                }

                Core*    _pCore;
                uint32   _screen;
                WidgetID _widget;
                LONG     _refCount;
                bool     _bRoot;
            };

            /** @brief `IUIAccessibilityBridge` 구현입니다. 창의 `WM_GETOBJECT` 훅과 알림 보내기를 맡는다. */
            class Bridge final : public IUIAccessibilityBridge
            {
            public:
                Bridge( UISystem& ui, IWindow& window )
                    : _pCore{ make_unique<Core>().release() } // 참조 수로 사는 객체다 — 마지막 `release()` 가 `sw_delete` 로 푼다
                    , _getObjectSubscription{}
                {
                    _pCore->_pUI           = &ui;
                    _pCore->_pWindow       = &window;
                    _getObjectSubscription = window.bindAccessibilityQueryHandler( SW_DELEGATE_METHOD( WindowAccessibilityQueryDelegate, &Bridge::handleGetObject, this ) );
                }

                ~Bridge() override
                {
                    _getObjectSubscription.reset(); // 이 다리가 건 칸만 놓는다(창이 먼저 사라졌으면 무효라 아무 일도 없다)
                    const HWND hwnd = _pCore->getHwnd();
                    if ( hwnd != nullptr )
                        (void)UiaReturnRawElementProvider( hwnd, 0, 0, nullptr ); // 창에 건 제공자를 푼다
                    (void)UiaDisconnectAllProviders();
                    _pCore->_pUI     = nullptr; // 클라이언트가 쥔 제공자는 이제 "요소 없음"으로 답한다
                    _pCore->_pWindow = nullptr;
                    _pCore->release();
                }

                Bridge( const Bridge& )            = delete;
                Bridge& operator=( const Bridge& ) = delete;

                bool isClientActive() const override { return UiaClientsAreListening() != FALSE; }

                void onUIUpdated() override { _pCore->_bSnapshotValid = false; }

                void notifyFocusChanged( uint32 screen, WidgetID widget ) override
                {
                    if ( widget == kInvalidWidgetID )
                        return;
                    Element* pElement = createElement( *_pCore, screen, widget, false );
                    (void)UiaRaiseAutomationEvent( static_cast<IRawElementProviderSimple*>( pElement ), UIA_AutomationFocusChangedEventId );
                    pElement->Release();
                }

                void announce( string_view text, UIAnnouncePriority priority ) override
                {
                    Element* pRoot    = createElement( *_pCore, 0, kInvalidWidgetID, true );
                    BSTR     bstrText = makeBstr( text );
                    BSTR     bstrID   = SysAllocString( L"SWAnnouncement" );
                    // 급한 알림은 읽던 것을 끊고, 그 밖은 줄을 세운다(NotificationProcessing).
                    const NotificationProcessing processing = priority == UIAnnouncePriority::Assertive ? NotificationProcessing_ImportantMostRecent : NotificationProcessing_All;
                    (void)UiaRaiseNotificationEvent( static_cast<IRawElementProviderSimple*>( pRoot ), NotificationKind_Other, processing, bstrText, bstrID );
                    SysFreeString( bstrText );
                    SysFreeString( bstrID );
                    pRoot->Release();
                }

            private:
                /** @brief 창의 `WM_GETOBJECT` 입니다. UIA 루트 객체 요청이면 루트 제공자를 돌려줍니다. */
                bool handleGetObject( uintptr_t wParam, intptr_t lParam, intptr_t& outResult )
                {
                    if ( static_cast<LONG>( lParam ) != UiaRootObjectId )
                        return false;
                    const HWND hwnd = _pCore->getHwnd();
                    if ( hwnd == nullptr )
                        return false;
                    Element* pRoot = createElement( *_pCore, 0, kInvalidWidgetID, true );
                    outResult      = static_cast<intptr_t>( UiaReturnRawElementProvider( hwnd, wParam, static_cast<LPARAM>( lParam ), static_cast<IRawElementProviderSimple*>( pRoot ) ) );
                    pRoot->Release();
                    return true;
                }

                Core*        _pCore;
                Subscription _getObjectSubscription; ///< 창의 접근성 요청 칸(`WM_GETOBJECT`) 구독 — 다리가 사라지면 놓는다
            };
        };

        UiaAccessibilityBridgeInternal::Element* UiaAccessibilityBridgeInternal::createElement( Core& core, uint32 screen, WidgetID widget, bool bRoot )
        {
            return make_unique<Element>( core, screen, widget, bRoot ).release(); // COM 참조 수로 사는 객체다 — 마지막 `Release()` 가 푼다
        }
    } // namespace
} // namespace sw

namespace sw
{
    int32 UiaAccessibility::getControlType( AccessibleRole role )
    {
        switch ( role )
        {
            case AccessibleRole::None:
                return 0;
            case AccessibleRole::Group:
                return UIA_GroupControlTypeId;
            case AccessibleRole::Text:
                return UIA_TextControlTypeId;
            case AccessibleRole::Image:
                return UIA_ImageControlTypeId;
            case AccessibleRole::Button:
                return UIA_ButtonControlTypeId;
            case AccessibleRole::CheckBox:
                return UIA_CheckBoxControlTypeId;
            case AccessibleRole::Slider:
                return UIA_SliderControlTypeId;
            case AccessibleRole::ProgressBar:
                return UIA_ProgressBarControlTypeId;
            case AccessibleRole::ComboBox:
                return UIA_ComboBoxControlTypeId;
            case AccessibleRole::SpinButton:
                return UIA_SpinnerControlTypeId;
            case AccessibleRole::TextInput:
                return UIA_EditControlTypeId;
            case AccessibleRole::List:
                return UIA_ListControlTypeId;
        }
        return 0;
    }

    unique_ptr<IUIAccessibilityBridge> UiaAccessibility::createBridge( UISystem& ui, IWindow& window )
    {
        return make_unique<UiaAccessibilityBridgeInternal::Bridge>( ui, window );
    }

    SW_REGISTER_UI_ACCESSIBILITY_BRIDGE( "UIAutomation", &UiaAccessibility::createBridge ); // Windows 클라이언트의 브리지(`UIAccessibilityBridgeFactory` 가 등록표에서 고른다)
} // namespace sw
#else
namespace sw
{
    // 이 플랫폼(리눅스 · 서버)에는 UI Automation 이 없다. 리눅스의 AT-SPI 는 D-Bus 위의 별도 구현이라 이 파일의 일이 아니다.
} // namespace sw
#endif
