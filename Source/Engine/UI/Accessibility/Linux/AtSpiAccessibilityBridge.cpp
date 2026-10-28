#include "pch.h"

#include "Engine/UI/Accessibility/Linux/AtSpiAccessibilityBridge.h"

#if defined( SW_PLATFORM_LINUX ) && defined( SW_WITH_CLIENT_CODE ) && defined( SW_WITH_ATSPI )

    #include "Core/Container/string.h"
    #include "Core/Container/vector.h"
    #include "Core/Log/Logger.h"
    #include "Core/Memory/Memory.h"

    #include "Engine/UI/Accessibility/AtSpiMapping.h"
    #include "Engine/UI/Accessibility/UIAccessibilityActions.h"
    #include "Engine/UI/Accessibility/UIAccessibilityTree.h"
    #include "Engine/UI/UISystem.h"
    #include "Engine/Window/IWindow.h"

    #include <dbus/dbus.h>
    #include <X11/Xlib.h>

    #include "Core/Common/X11MacroUndef.h"

namespace sw
{
    SW_LOG_CALLER( "AtSpiAccessibility" );

    namespace
    {
        /**
         * @brief AT-SPI 브리지의 D-Bus 쪽 구현입니다. 이 파일의 모양은 at-spi2-core 의 D-Bus 계약(`org.a11y.atspi.*`)을 따른다 — 객체 경로 · 서명을 바꾸면 Orca 가 읽지 못한다.
         * @details 확인 상태: 이 파일은 Windows 개발 기계에서 지어 보지 못했다(libdbus 가 없다). 리눅스 CI 에서 `-DSW_ENABLE_ATSPI=ON` 으로 짓고 Orca + at-spi2 세션(`Candidates-ui.md` AtSpiBridge)에서
         *          트리 · 포커스 · 누르기를 확인해야 한다. 특히 이벤트 서명(`siiva{sv}`)은 at-spi2-core 판마다 달랐던 부분이라 실기에서 확인한다.
         */
        namespace AtSpiInternal
        {
            constexpr const utf8* kPathPrefix  = "/org/a11y/atspi/accessible";
            constexpr const utf8* kRootPath    = "/org/a11y/atspi/accessible/root";
            constexpr const utf8* kNullPath    = "/org/a11y/atspi/null";
            constexpr const utf8* kIfaceAcc    = "org.a11y.atspi.Accessible";
            constexpr const utf8* kIfaceComp   = "org.a11y.atspi.Component";
            constexpr const utf8* kIfaceAction = "org.a11y.atspi.Action";
            constexpr const utf8* kIfaceValue  = "org.a11y.atspi.Value";
            constexpr const utf8* kIfaceApp    = "org.a11y.atspi.Application";
            constexpr const utf8* kIfaceProps  = "org.freedesktop.DBus.Properties";
            constexpr const utf8* kIfaceEvent  = "org.a11y.atspi.Event.Object";
            constexpr int32       kTimeoutMs   = 1000;
            constexpr uint32      kRoleApp     = 75; // ATSPI_ROLE_APPLICATION
            constexpr const utf8* kToolkitName = "SWEngine";

            /** @brief 객체 경로 → 대상. 루트는 `bRoot`, 그 밖은 접근성 트리 노드입니다. */
            struct Target
            {
                bool  _bRoot{ false };
                int32 _node{ -1 };
            };

            string makeNodePath( const AccessibleNode& node )
            {
                return string( kPathPrefix ) + "/n" + to_string( node._screen ) + "_" + to_string( node._widget );
            }

            /** @brief `/org/a11y/atspi/accessible/n<화면>_<위젯>` 에서 두 번호를 읽습니다. 모양이 다르면 false. */
            [[nodiscard]] bool parseNodePath( const utf8* pPath, uint32& outScreen, uint32& outWidget )
            {
                const string prefix = string( kPathPrefix ) + "/n";
                const string path( pPath );
                if ( path.size() <= prefix.size() || path.compare( 0, prefix.size(), prefix ) != 0 )
                    return false;
                uint64 arrNumber[2]{ 0, 0 };
                size_t part   = 0;
                bool   bDigit = false;
                for ( size_t index = prefix.size(); index < path.size(); ++index )
                {
                    const utf8 ch         = path[index];
                    const bool bIsDigit   = ch >= '0' && ch <= '9';
                    const bool bSeparator = ch == '_' && part == 0 && bDigit;
                    if ( bIsDigit )
                    {
                        arrNumber[part] = arrNumber[part] * 10 + static_cast<uint64>( ch - '0' );
                        bDigit          = true;
                    }
                    else if ( bSeparator )
                    {
                        part   = 1;
                        bDigit = false;
                    }
                    else
                    {
                        return false;
                    }
                }
                if ( part != 1 || bDigit == false )
                    return false;
                outScreen = static_cast<uint32>( arrNumber[0] );
                outWidget = static_cast<uint32>( arrNumber[1] );
                return true;
            }

            void appendString( DBusMessageIter* pIter, const utf8* pText )
            {
                dbus_message_iter_append_basic( pIter, DBUS_TYPE_STRING, &pText );
            }

            void appendRef( DBusMessageIter* pIter, const string& busName, const utf8* pPath )
            {
                DBusMessageIter sub;
                dbus_message_iter_open_container( pIter, DBUS_TYPE_STRUCT, nullptr, &sub );
                appendString( &sub, busName.c_str() );
                dbus_message_iter_append_basic( &sub, DBUS_TYPE_OBJECT_PATH, &pPath );
                dbus_message_iter_close_container( pIter, &sub );
            }

            void appendInt( DBusMessageIter* pIter, int32 value )
            {
                dbus_int32_t wire = value;
                dbus_message_iter_append_basic( pIter, DBUS_TYPE_INT32, &wire );
            }

            void appendUint( DBusMessageIter* pIter, uint32 value )
            {
                dbus_uint32_t wire = value;
                dbus_message_iter_append_basic( pIter, DBUS_TYPE_UINT32, &wire );
            }

            void appendBool( DBusMessageIter* pIter, bool bValue )
            {
                dbus_bool_t wire = bValue ? TRUE : FALSE;
                dbus_message_iter_append_basic( pIter, DBUS_TYPE_BOOLEAN, &wire );
            }

            void appendDouble( DBusMessageIter* pIter, float64 value )
            {
                dbus_message_iter_append_basic( pIter, DBUS_TYPE_DOUBLE, &value );
            }

            /** @brief 정수 하나를 든 변이(variant)를 덧붙입니다. */
            void appendVariantInt( DBusMessageIter* pIter, int32 value )
            {
                DBusMessageIter variant;
                dbus_message_iter_open_container( pIter, DBUS_TYPE_VARIANT, "i", &variant );
                appendInt( &variant, value );
                dbus_message_iter_close_container( pIter, &variant );
            }
        } // namespace AtSpiInternal

        class Bridge final : public IUIAccessibilityBridge
        {
        public:
            Bridge( UISystem& ui, IWindow& window )
                : _ui{ ui }
                , _window{ window }
            {
            }

            ~Bridge() override
            {
                if ( _pBus != nullptr )
                {
                    dbus_connection_unregister_object_path( _pBus, AtSpiInternal::kPathPrefix );
                    dbus_connection_close( _pBus );
                    dbus_connection_unref( _pBus );
                }
                if ( _pSession != nullptr )
                {
                    dbus_connection_close( _pSession );
                    dbus_connection_unref( _pSession );
                }
            }

            Bridge( const Bridge& )            = delete;
            Bridge& operator=( const Bridge& ) = delete;

            /** @brief 세션 버스 → 접근성 버스 주소 → 접근성 버스 연결 → 객체 등록 → 레지스트리 등록. 어느 단계든 못 하면 이유를 남기고 false 입니다. */
            bool connect()
            {
                using namespace AtSpiInternal;
                DBusError error;
                dbus_error_init( &error );
                _pSession = dbus_bus_get_private( DBUS_BUS_SESSION, &error );
                if ( _pSession == nullptr )
                    return fail( "no D-Bus session bus", error );
                dbus_connection_set_exit_on_disconnect( _pSession, FALSE );

                DBusMessage* pCall = dbus_message_new_method_call( "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress" );
                DBusMessage* pRep  = dbus_connection_send_with_reply_and_block( _pSession, pCall, kTimeoutMs, &error );
                dbus_message_unref( pCall );
                if ( pRep == nullptr )
                    return fail( "the accessibility bus address is not available (org.a11y.Bus)", error );
                const utf8*  pAddress = nullptr;
                const bool   bAddress = dbus_message_get_args( pRep, &error, DBUS_TYPE_STRING, &pAddress, DBUS_TYPE_INVALID ) != FALSE;
                const string address  = bAddress ? string( pAddress ) : string{};
                dbus_message_unref( pRep );
                if ( bAddress == false )
                    return fail( "org.a11y.Bus.GetAddress returned no address", error );

                _pBus = dbus_connection_open_private( address.c_str(), &error );
                if ( _pBus == nullptr )
                    return fail( "cannot open the accessibility bus", error );
                dbus_connection_set_exit_on_disconnect( _pBus, FALSE );
                if ( dbus_bus_register( _pBus, &error ) == FALSE )
                    return fail( "cannot register on the accessibility bus", error );
                _busName = dbus_bus_get_unique_name( _pBus );

                static const DBusObjectPathVTable vtable{ nullptr, &Bridge::onMessage, nullptr, nullptr, nullptr, nullptr };
                if ( dbus_connection_register_fallback( _pBus, kPathPrefix, &vtable, this ) == FALSE )
                {
                    SW_LOG_WARNING( "AT-SPI: cannot register the accessible object paths" );
                    return false;
                }

                // 레지스트리에 앱 루트를 알린다 — 이 호출이 되어야 스크린 리더가 이 앱을 안다.
                pCall = dbus_message_new_method_call( "org.a11y.atspi.Registry", kRootPath, "org.a11y.atspi.Socket", "Embed" );
                DBusMessageIter iter;
                dbus_message_iter_init_append( pCall, &iter );
                appendRef( &iter, _busName, kRootPath );
                pRep = dbus_connection_send_with_reply_and_block( _pBus, pCall, kTimeoutMs, &error );
                dbus_message_unref( pCall );
                if ( pRep == nullptr )
                    return fail( "the AT-SPI registry did not accept the application (org.a11y.atspi.Registry.Socket.Embed)", error );
                dbus_message_unref( pRep );

                // 스크린 리더가 붙었는지는 세션 버스의 `org.a11y.Status.ScreenReaderEnabled` 가 말한다 — 지금 값을 읽고 바뀔 때의 신호를 받는다.
                _bReaderOn = readScreenReaderEnabled();
                dbus_bus_add_match( _pSession, "type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',path='/org/a11y/bus'", &error );
                if ( dbus_error_is_set( &error ) )
                    dbus_error_free( &error );
                return true;
            }

            bool isClientActive() const override { return _bReaderOn; }

            void onUIUpdated() override
            {
                _bSnapshotValid = false;
                pump();
            }

            void notifyFocusChanged( uint32 screen, WidgetID widget ) override
            {
                if ( _pBus == nullptr )
                    return;
                const AccessibleNode*          pNode     = nullptr;
                const UIAccessibilitySnapshot* pSnapshot = findSnapshot();
                if ( pSnapshot != nullptr && widget != kInvalidWidgetID )
                {
                    const int32 index = pSnapshot->findNode( screen, widget );
                    if ( index >= 0 )
                        pNode = &pSnapshot->_listNode[static_cast<size_t>( index )];
                }
                const string path = pNode != nullptr ? AtSpiInternal::makeNodePath( *pNode ) : string( AtSpiInternal::kRootPath );
                emitObjectEvent( path.c_str(), "StateChanged", "focused", 1 );
            }

            void announce( string_view text, UIAnnouncePriority priority ) override
            {
                if ( _pBus == nullptr )
                    return;
                using namespace AtSpiInternal;
                const string    textCopy( text );
                DBusMessage*    pSignal = dbus_message_new_signal( kRootPath, kIfaceEvent, "Announcement" );
                DBusMessageIter iter;
                dbus_message_iter_init_append( pSignal, &iter );
                appendString( &iter, "" );
                appendInt( &iter, priority == UIAnnouncePriority::Assertive ? 2 : 1 ); // 정보: 판마다 값이 다르다 — 실기 확인 대상(클래스 설명)
                appendInt( &iter, 0 );
                DBusMessageIter variant;
                dbus_message_iter_open_container( &iter, DBUS_TYPE_VARIANT, "s", &variant );
                appendString( &variant, textCopy.c_str() );
                dbus_message_iter_close_container( &iter, &variant );
                DBusMessageIter props;
                dbus_message_iter_open_container( &iter, DBUS_TYPE_ARRAY, "{sv}", &props );
                dbus_message_iter_close_container( &iter, &props );
                dbus_connection_send( _pBus, pSignal, nullptr );
                dbus_message_unref( pSignal );
                dbus_connection_flush( _pBus );
            }

        private:
            bool fail( const utf8* pWhat, DBusError& inoutError )
            {
                SW_LOG_INFO( "AT-SPI bridge is off: %# (%#)", pWhat, dbus_error_is_set( &inoutError ) ? inoutError.message : "no detail" );
                if ( dbus_error_is_set( &inoutError ) )
                    dbus_error_free( &inoutError );
                return false;
            }

            /** @brief 지금 접근성 트리입니다(UI 가 갱신된 뒤 처음 물으면 새로 뽑는다). */
            const UIAccessibilitySnapshot* findSnapshot()
            {
                if ( _bSnapshotValid == false )
                {
                    _ui.populateAccessibilitySnapshot( _snapshot );
                    _bSnapshotValid = true;
                }
                return &_snapshot;
            }

            [[nodiscard]] bool readScreenReaderEnabled()
            {
                DBusError error;
                dbus_error_init( &error );
                DBusMessage* pCall  = dbus_message_new_method_call( "org.a11y.Bus", "/org/a11y/bus", AtSpiInternal::kIfaceProps, "Get" );
                const utf8*  pIface = "org.a11y.Status";
                const utf8*  pName  = "ScreenReaderEnabled";
                dbus_message_append_args( pCall, DBUS_TYPE_STRING, &pIface, DBUS_TYPE_STRING, &pName, DBUS_TYPE_INVALID );
                DBusMessage* pRep = dbus_connection_send_with_reply_and_block( _pSession, pCall, AtSpiInternal::kTimeoutMs, &error );
                dbus_message_unref( pCall );
                if ( pRep == nullptr )
                {
                    dbus_error_free( &error );
                    return false;
                }
                bool            bEnabled = false;
                DBusMessageIter iter;
                if ( dbus_message_iter_init( pRep, &iter ) && dbus_message_iter_get_arg_type( &iter ) == DBUS_TYPE_VARIANT )
                {
                    DBusMessageIter variant;
                    dbus_message_iter_recurse( &iter, &variant );
                    if ( dbus_message_iter_get_arg_type( &variant ) == DBUS_TYPE_BOOLEAN )
                    {
                        dbus_bool_t value = FALSE;
                        dbus_message_iter_get_basic( &variant, &value );
                        bEnabled = value != FALSE;
                    }
                }
                dbus_message_unref( pRep );
                return bEnabled;
            }

            /** @brief 세션 버스의 `PropertiesChanged` 에서 `ScreenReaderEnabled` 가 바뀌었으면 반영합니다. */
            void onSessionSignal( DBusMessage* pMessage )
            {
                if ( dbus_message_is_signal( pMessage, AtSpiInternal::kIfaceProps, "PropertiesChanged" ) == FALSE )
                    return;
                DBusMessageIter iter;
                if ( dbus_message_iter_init( pMessage, &iter ) == FALSE || dbus_message_iter_get_arg_type( &iter ) != DBUS_TYPE_STRING )
                    return;
                const utf8* pIface = nullptr;
                dbus_message_iter_get_basic( &iter, &pIface );
                const bool bStatusIface = pIface != nullptr && string( pIface ) == "org.a11y.Status";
                if ( bStatusIface == false || dbus_message_iter_next( &iter ) == FALSE )
                    return;
                DBusMessageIter dict;
                dbus_message_iter_recurse( &iter, &dict );
                while ( dbus_message_iter_get_arg_type( &dict ) == DBUS_TYPE_DICT_ENTRY )
                {
                    DBusMessageIter entry;
                    dbus_message_iter_recurse( &dict, &entry );
                    const utf8* pKey = nullptr;
                    dbus_message_iter_get_basic( &entry, &pKey );
                    const bool bReaderKey = pKey != nullptr && string( pKey ) == "ScreenReaderEnabled";
                    if ( bReaderKey && dbus_message_iter_next( &entry ) != FALSE )
                    {
                        DBusMessageIter variant;
                        dbus_message_iter_recurse( &entry, &variant );
                        if ( dbus_message_iter_get_arg_type( &variant ) == DBUS_TYPE_BOOLEAN )
                        {
                            dbus_bool_t value = FALSE;
                            dbus_message_iter_get_basic( &variant, &value );
                            _bReaderOn = value != FALSE;
                        }
                    }
                    dbus_message_iter_next( &dict );
                }
            }

            /** @brief 쌓인 D-Bus 메시지를 한 번 처리합니다(막지 않는다). 접근성 버스의 요청은 `onMessage` 로, 세션 버스의 신호는 `onSessionSignal` 로 간다. */
            void pump()
            {
                if ( _pBus != nullptr )
                {
                    dbus_connection_read_write( _pBus, 0 );
                    while ( dbus_connection_dispatch( _pBus ) == DBUS_DISPATCH_DATA_REMAINS )
                    {
                    }
                }
                if ( _pSession != nullptr )
                {
                    dbus_connection_read_write( _pSession, 0 );
                    while ( DBusMessage* pMessage = dbus_connection_pop_message( _pSession ) )
                    {
                        onSessionSignal( pMessage );
                        dbus_message_unref( pMessage );
                    }
                }
            }

            void emitObjectEvent( const utf8* pPath, const utf8* pMember, const utf8* pDetail, int32 detail1 )
            {
                using namespace AtSpiInternal;
                DBusMessage*    pSignal = dbus_message_new_signal( pPath, kIfaceEvent, pMember );
                DBusMessageIter iter;
                dbus_message_iter_init_append( pSignal, &iter );
                appendString( &iter, pDetail );
                appendInt( &iter, detail1 );
                appendInt( &iter, 0 );
                appendVariantInt( &iter, 0 );
                DBusMessageIter props;
                dbus_message_iter_open_container( &iter, DBUS_TYPE_ARRAY, "{sv}", &props );
                dbus_message_iter_close_container( &iter, &props );
                dbus_connection_send( _pBus, pSignal, nullptr );
                dbus_message_unref( pSignal );
                dbus_connection_flush( _pBus );
            }

            // ---- 요청 처리 ----------------------------------------------------------------------------------------------------------------

            static DBusHandlerResult onMessage( [[maybe_unused]] DBusConnection* pConnection, DBusMessage* pMessage, void* pUser )
            {
                if ( dbus_message_get_type( pMessage ) != DBUS_MESSAGE_TYPE_METHOD_CALL )
                    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
                return static_cast<Bridge*>( pUser )->handle( pMessage );
            }

            DBusHandlerResult replyError( DBusMessage* pMessage, const utf8* pName, const utf8* pText )
            {
                DBusMessage* pError = dbus_message_new_error( pMessage, pName, pText );
                dbus_connection_send( _pBus, pError, nullptr );
                dbus_message_unref( pError );
                return DBUS_HANDLER_RESULT_HANDLED;
            }

            /** @brief 객체 경로가 가리키는 대상입니다. 루트나 지금 있는 노드가 아니면 false(사라진 위젯). */
            [[nodiscard]] bool resolve( const utf8* pPath, AtSpiInternal::Target& outTarget )
            {
                if ( string( pPath ) == AtSpiInternal::kRootPath )
                {
                    outTarget._bRoot = true;
                    return true;
                }
                uint32 screen = 0;
                uint32 widget = 0;
                if ( AtSpiInternal::parseNodePath( pPath, screen, widget ) == false )
                    return false;
                const UIAccessibilitySnapshot* pSnapshot = findSnapshot();
                const int32                    index     = pSnapshot->findNode( screen, widget );
                if ( index < 0 )
                    return false;
                outTarget._bRoot = false;
                outTarget._node  = index;
                return true;
            }

            vector<int32> listChildren( const AtSpiInternal::Target& target )
            {
                vector<int32>                  list;
                const UIAccessibilitySnapshot* pSnapshot = findSnapshot();
                const int32                    parent    = target._bRoot ? -1 : target._node;
                for ( size_t index = 0; index < pSnapshot->_listNode.size(); ++index )
                {
                    if ( pSnapshot->_listNode[index]._parent == parent )
                        list.push_back( static_cast<int32>( index ) );
                }
                return list;
            }

            void appendNodeRef( DBusMessageIter* pIter, int32 node )
            {
                const string path = AtSpiInternal::makeNodePath( findSnapshot()->_listNode[static_cast<size_t>( node )] );
                AtSpiInternal::appendRef( pIter, _busName, path.c_str() );
            }

            void appendParentRef( DBusMessageIter* pIter, const AtSpiInternal::Target& target )
            {
                if ( target._bRoot )
                {
                    AtSpiInternal::appendRef( pIter, string{}, AtSpiInternal::kNullPath );
                    return;
                }
                const int32 parent = findSnapshot()->_listNode[static_cast<size_t>( target._node )]._parent;
                if ( parent < 0 )
                    AtSpiInternal::appendRef( pIter, _busName, AtSpiInternal::kRootPath );
                else
                    appendNodeRef( pIter, parent );
            }

            const AccessibleNode& nodeOf( const AtSpiInternal::Target& target ) { return findSnapshot()->_listNode[static_cast<size_t>( target._node )]; }

            /** @brief 속성 @p pName 의 값을 변이로 덧붙입니다. 모르는 속성이면 false. */
            bool appendProperty( DBusMessageIter* pIter, const utf8* pIface, const utf8* pName, const AtSpiInternal::Target& target )
            {
                using namespace AtSpiInternal;
                const string    iface( pIface );
                const string    name( pName );
                DBusMessageIter variant;
                if ( iface == kIfaceAcc )
                {
                    const bool bTextProperty = name == "Name" || name == "Description" || name == "Locale";
                    if ( bTextProperty )
                    {
                        const string text = ( name == "Name" ) ? ( target._bRoot ? string( kToolkitName ) : nodeOf( target )._info._name ) : string{};
                        dbus_message_iter_open_container( pIter, DBUS_TYPE_VARIANT, "s", &variant );
                        appendString( &variant, text.c_str() );
                    }
                    else if ( name == "Parent" )
                    {
                        dbus_message_iter_open_container( pIter, DBUS_TYPE_VARIANT, "(so)", &variant );
                        appendParentRef( &variant, target );
                    }
                    else if ( name == "ChildCount" )
                    {
                        dbus_message_iter_open_container( pIter, DBUS_TYPE_VARIANT, "i", &variant );
                        appendInt( &variant, static_cast<int32>( ( target._bRoot || nodeOf( target )._info._bLeaf == false ) ? listChildren( target ).size() : 0 ) );
                    }
                    else
                        return false;
                }
                else if ( iface == kIfaceAction && name == "NActions" )
                {
                    dbus_message_iter_open_container( pIter, DBUS_TYPE_VARIANT, "i", &variant );
                    appendInt( &variant, 1 );
                }
                else if ( iface == kIfaceValue && target._bRoot == false )
                {
                    const AccessibleInfo& info  = nodeOf( target )._info;
                    float64               value = 0.0;
                    if ( name == "MinimumValue" )
                        value = info._rangeMin;
                    else if ( name == "MaximumValue" )
                        value = info._rangeMax;
                    else if ( name == "CurrentValue" )
                        value = info._rangeValue;
                    else if ( name == "MinimumIncrement" )
                        value = info._rangeStep;
                    else
                        return false;
                    dbus_message_iter_open_container( pIter, DBUS_TYPE_VARIANT, "d", &variant );
                    appendDouble( &variant, value );
                }
                else if ( iface == kIfaceApp && ( target._bRoot && name == "ToolkitName" ) )
                {
                    dbus_message_iter_open_container( pIter, DBUS_TYPE_VARIANT, "s", &variant );
                    appendString( &variant, kToolkitName );
                }
                else
                    return false;
                dbus_message_iter_close_container( pIter, &variant );
                return true;
            }

            DBusHandlerResult handleProperties( DBusMessage* pMessage, const AtSpiInternal::Target& target )
            {
                using namespace AtSpiInternal;
                const utf8*  pMember = dbus_message_get_member( pMessage );
                const string member( pMember != nullptr ? pMember : "" );
                if ( member == "Get" )
                {
                    const utf8* pIface = nullptr;
                    const utf8* pName  = nullptr;
                    if ( dbus_message_get_args( pMessage, nullptr, DBUS_TYPE_STRING, &pIface, DBUS_TYPE_STRING, &pName, DBUS_TYPE_INVALID ) == FALSE )
                        return replyError( pMessage, DBUS_ERROR_INVALID_ARGS, "Get(interface, name)" );
                    DBusMessage*    pRep = dbus_message_new_method_return( pMessage );
                    DBusMessageIter iter;
                    dbus_message_iter_init_append( pRep, &iter );
                    const bool bFound = appendProperty( &iter, pIface, pName, target );
                    if ( bFound )
                        dbus_connection_send( _pBus, pRep, nullptr );
                    dbus_message_unref( pRep );
                    return bFound ? DBUS_HANDLER_RESULT_HANDLED : replyError( pMessage, DBUS_ERROR_UNKNOWN_PROPERTY, "no such property" );
                }
                if ( member == "Set" )
                {
                    const utf8*     pIface = nullptr;
                    const utf8*     pName  = nullptr;
                    DBusMessageIter iter;
                    if ( dbus_message_iter_init( pMessage, &iter ) == FALSE )
                        return replyError( pMessage, DBUS_ERROR_INVALID_ARGS, "Set(interface, name, value)" );
                    dbus_message_iter_get_basic( &iter, &pIface );
                    dbus_message_iter_next( &iter );
                    dbus_message_iter_get_basic( &iter, &pName );
                    dbus_message_iter_next( &iter );
                    const bool bNamed       = pIface != nullptr && pName != nullptr;
                    const bool bValueSetter = bNamed && string( pIface ) == kIfaceValue && string( pName ) == "CurrentValue";
                    const bool bVariant     = dbus_message_iter_get_arg_type( &iter ) == DBUS_TYPE_VARIANT;
                    const bool bSettable    = target._bRoot == false && bValueSetter && bVariant;
                    if ( bSettable == false )
                        return replyError( pMessage, DBUS_ERROR_PROPERTY_READ_ONLY, "only Value.CurrentValue can be set" );
                    DBusMessageIter variant;
                    dbus_message_iter_recurse( &iter, &variant );
                    float64 value = 0.0;
                    if ( dbus_message_iter_get_arg_type( &variant ) != DBUS_TYPE_DOUBLE )
                        return replyError( pMessage, DBUS_ERROR_INVALID_ARGS, "a D-Bus double is expected" );
                    dbus_message_iter_get_basic( &variant, &value );
                    const AccessibleNode& node = nodeOf( target );
                    if ( UIAccessibilityActions::setRangeValue( _ui, node._screen, node._widget, static_cast<float32>( value ) ) == false )
                        return replyError( pMessage, DBUS_ERROR_FAILED, "the widget did not take the value" );
                    return sendEmptyReturn( pMessage );
                }
                return replyError( pMessage, DBUS_ERROR_UNKNOWN_METHOD, "only Properties.Get and Properties.Set are implemented" );
            }

            DBusHandlerResult sendEmptyReturn( DBusMessage* pMessage )
            {
                DBusMessage* pRep = dbus_message_new_method_return( pMessage );
                dbus_connection_send( _pBus, pRep, nullptr );
                dbus_message_unref( pRep );
                return DBUS_HANDLER_RESULT_HANDLED;
            }

            DBusHandlerResult handleAccessible( DBusMessage* pMessage, const string& member, const AtSpiInternal::Target& target )
            {
                using namespace AtSpiInternal;
                DBusMessage*    pRep = dbus_message_new_method_return( pMessage );
                DBusMessageIter iter;
                dbus_message_iter_init_append( pRep, &iter );
                const AccessibleRole role = target._bRoot ? AccessibleRole::Group : nodeOf( target )._info._role;
                bool                 bOk  = true;
                if ( member == "GetRole" )
                    appendUint( &iter, target._bRoot ? kRoleApp : AtSpiMapping::getRole( role ) );
                else if ( member == "GetRoleName" || member == "GetLocalizedRoleName" )
                    appendString( &iter, target._bRoot ? "application" : AtSpiMapping::getRoleName( role ) );
                else if ( member == "GetState" )
                {
                    const AtSpiStateSet set = AtSpiMapping::makeStateSet( role, target._bRoot ? 0u : nodeOf( target )._info._states );
                    DBusMessageIter     array;
                    dbus_message_iter_open_container( &iter, DBUS_TYPE_ARRAY, "u", &array );
                    appendUint( &array, set._word[0] );
                    appendUint( &array, set._word[1] );
                    dbus_message_iter_close_container( &iter, &array );
                }
                else if ( member == "GetChildren" || member == "GetChildAtIndex" )
                {
                    const vector<int32> children = ( target._bRoot || nodeOf( target )._info._bLeaf == false ) ? listChildren( target ) : vector<int32>{};
                    if ( member == "GetChildren" )
                    {
                        DBusMessageIter array;
                        dbus_message_iter_open_container( &iter, DBUS_TYPE_ARRAY, "(so)", &array );
                        for ( const int32 child : children )
                        {
                            appendNodeRef( &array, child );
                        }
                        dbus_message_iter_close_container( &iter, &array );
                    }
                    else
                    {
                        dbus_int32_t index = -1;
                        dbus_message_get_args( pMessage, nullptr, DBUS_TYPE_INT32, &index, DBUS_TYPE_INVALID );
                        if ( 0 <= index && index < static_cast<dbus_int32_t>( children.size() ) )
                            appendNodeRef( &iter, children[static_cast<size_t>( index )] );
                        else
                            bOk = false;
                    }
                }
                else if ( member == "GetIndexInParent" )
                {
                    int32 position = -1;
                    if ( target._bRoot == false )
                    {
                        const int32                    parent    = nodeOf( target )._parent;
                        int32                          count     = 0;
                        const UIAccessibilitySnapshot* pSnapshot = findSnapshot();
                        for ( int32 index = 0; index < target._node; ++index )
                        {
                            count += pSnapshot->_listNode[static_cast<size_t>( index )]._parent == parent ? 1 : 0;
                        }
                        position = count;
                    }
                    appendInt( &iter, position );
                }
                else if ( member == "GetAttributes" )
                {
                    DBusMessageIter array;
                    dbus_message_iter_open_container( &iter, DBUS_TYPE_ARRAY, "{ss}", &array );
                    dbus_message_iter_close_container( &iter, &array );
                }
                else if ( member == "GetApplication" )
                    appendRef( &iter, _busName, kRootPath );
                else if ( member == "GetInterfaces" )
                {
                    DBusMessageIter array;
                    dbus_message_iter_open_container( &iter, DBUS_TYPE_ARRAY, "s", &array );
                    appendString( &array, kIfaceAcc );
                    if ( target._bRoot )
                        appendString( &array, kIfaceApp );
                    const uint32 bits = target._bRoot ? 0u : AtSpiMapping::getInterfaces( role );
                    if ( ( bits & AtSpiInterface::kComponent ) != 0 )
                        appendString( &array, kIfaceComp );
                    if ( ( bits & AtSpiInterface::kAction ) != 0 )
                        appendString( &array, kIfaceAction );
                    if ( ( bits & AtSpiInterface::kValue ) != 0 )
                        appendString( &array, kIfaceValue );
                    dbus_message_iter_close_container( &iter, &array );
                }
                else
                    bOk = false;
                if ( bOk )
                    dbus_connection_send( _pBus, pRep, nullptr );
                dbus_message_unref( pRep );
                return bOk ? DBUS_HANDLER_RESULT_HANDLED : replyError( pMessage, DBUS_ERROR_UNKNOWN_METHOD, "unsupported Accessible method or index" );
            }

            /** @brief 창 클라이언트 원점의 화면 좌표입니다. X11 창만 알 수 있고(`XTranslateCoordinates`) 나머지는 0, 0 을 둡니다. */
            void readWindowOrigin( int32& outX, int32& outY ) const
            {
                X11SurfaceHandles handles;
                if ( _window.tryGetX11Surface( handles ) == false || handles._pDisplay == nullptr )
                    return;
                Display* pDisplay = static_cast<Display*>( handles._pDisplay );
                int      x        = 0;
                int      y        = 0;
                ::Window child    = 0;
                if ( XTranslateCoordinates( pDisplay, static_cast<::Window>( handles._window ), DefaultRootWindow( pDisplay ), 0, 0, &x, &y, &child ) == 0 )
                    return;
                outX = static_cast<int32>( x );
                outY = static_cast<int32>( y );
            }

            DBusHandlerResult handleComponent( DBusMessage* pMessage, const string& member, const AtSpiInternal::Target& target )
            {
                using namespace AtSpiInternal;
                if ( target._bRoot )
                    return replyError( pMessage, DBUS_ERROR_UNKNOWN_METHOD, "the application root has no extents" );
                const AccessibleNode& node = nodeOf( target );
                if ( member == "GrabFocus" )
                {
                    DBusMessage*    pRep = dbus_message_new_method_return( pMessage );
                    DBusMessageIter iter;
                    dbus_message_iter_init_append( pRep, &iter );
                    appendBool( &iter, UIAccessibilityActions::focus( _ui, node._screen, node._widget ) );
                    dbus_connection_send( _pBus, pRep, nullptr );
                    dbus_message_unref( pRep );
                    return DBUS_HANDLER_RESULT_HANDLED;
                }
                // 사각형은 게임 창 클라이언트 기준 UI 단위다. 요청의 좌표계가 화면(ATSPI_COORD_TYPE_SCREEN = 0)이면 창 클라이언트의 화면 위치를 더한다(확대기 · 강조 상자가 맞는다).
                // 창 좌표(1) · 부모 좌표(2, 최상위 노드는 창과 같다)는 그대로 둔다. X11 이 아니면(Wayland 는 클라이언트에 전역 좌표를 주지 않는다) 원점 0 이다.
                dbus_uint32_t coordType = 0;
                dbus_message_get_args( pMessage, nullptr, DBUS_TYPE_UINT32, &coordType, DBUS_TYPE_INVALID );
                int32 originX = 0;
                int32 originY = 0;
                if ( coordType == 0 && member != "GetSize" )
                    readWindowOrigin( originX, originY );
                const int32     x    = static_cast<int32>( node._bounds._x ) + originX;
                const int32     y    = static_cast<int32>( node._bounds._y ) + originY;
                const int32     w    = static_cast<int32>( node._bounds._z );
                const int32     h    = static_cast<int32>( node._bounds._w );
                DBusMessage*    pRep = dbus_message_new_method_return( pMessage );
                DBusMessageIter iter;
                dbus_message_iter_init_append( pRep, &iter );
                bool bOk = true;
                if ( member == "GetExtents" )
                {
                    DBusMessageIter sub;
                    dbus_message_iter_open_container( &iter, DBUS_TYPE_STRUCT, nullptr, &sub );
                    appendInt( &sub, x );
                    appendInt( &sub, y );
                    appendInt( &sub, w );
                    appendInt( &sub, h );
                    dbus_message_iter_close_container( &iter, &sub );
                }
                else if ( member == "GetPosition" || member == "GetSize" )
                {
                    DBusMessageIter sub;
                    dbus_message_iter_open_container( &iter, DBUS_TYPE_STRUCT, nullptr, &sub );
                    appendInt( &sub, member == "GetPosition" ? x : w );
                    appendInt( &sub, member == "GetPosition" ? y : h );
                    dbus_message_iter_close_container( &iter, &sub );
                }
                else
                    bOk = false;
                if ( bOk )
                    dbus_connection_send( _pBus, pRep, nullptr );
                dbus_message_unref( pRep );
                return bOk ? DBUS_HANDLER_RESULT_HANDLED : replyError( pMessage, DBUS_ERROR_UNKNOWN_METHOD, "unsupported Component method" );
            }

            DBusHandlerResult handleAction( DBusMessage* pMessage, const string& member, const AtSpiInternal::Target& target )
            {
                using namespace AtSpiInternal;
                if ( target._bRoot )
                    return replyError( pMessage, DBUS_ERROR_UNKNOWN_METHOD, "the application root has no actions" );
                const AccessibleNode& node  = nodeOf( target );
                dbus_int32_t          index = -1;
                dbus_message_get_args( pMessage, nullptr, DBUS_TYPE_INT32, &index, DBUS_TYPE_INVALID );
                DBusMessage*    pRep = dbus_message_new_method_return( pMessage );
                DBusMessageIter iter;
                dbus_message_iter_init_append( pRep, &iter );
                bool bOk = true;
                if ( member == "DoAction" )
                    appendBool( &iter, index == 0 && UIAccessibilityActions::invoke( _ui, node._screen, node._widget ) );
                else if ( member == "GetName" )
                    appendString( &iter, index == 0 ? "click" : "" );
                else if ( member == "GetDescription" || member == "GetKeyBinding" )
                    appendString( &iter, "" );
                else
                    bOk = false;
                if ( bOk )
                    dbus_connection_send( _pBus, pRep, nullptr );
                dbus_message_unref( pRep );
                return bOk ? DBUS_HANDLER_RESULT_HANDLED : replyError( pMessage, DBUS_ERROR_UNKNOWN_METHOD, "unsupported Action method" );
            }

            DBusHandlerResult handle( DBusMessage* pMessage )
            {
                using namespace AtSpiInternal;
                if ( _bReaderOn == false )
                    return replyError( pMessage, DBUS_ERROR_NO_SERVER, "no screen reader is attached" ); // 붙은 스크린 리더가 없으면 트리를 뽑지 않는다
                const utf8* pPath      = dbus_message_get_path( pMessage );
                const utf8* pIface     = dbus_message_get_interface( pMessage );
                const utf8* pMember    = dbus_message_get_member( pMessage );
                const bool  bNamedCall = pPath != nullptr && pIface != nullptr && pMember != nullptr;
                if ( bNamedCall == false )
                    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
                Target target;
                if ( resolve( pPath, target ) == false )
                    return replyError( pMessage, DBUS_ERROR_UNKNOWN_OBJECT, "the widget is gone" );
                const string iface( pIface );
                const string member( pMember );
                if ( iface == kIfaceProps )
                    return handleProperties( pMessage, target );
                if ( iface == kIfaceAcc )
                    return handleAccessible( pMessage, member, target );
                if ( iface == kIfaceComp )
                    return handleComponent( pMessage, member, target );
                if ( iface == kIfaceAction )
                    return handleAction( pMessage, member, target );
                if ( iface == "org.freedesktop.DBus.Introspectable" && member == "Introspect" )
                {
                    DBusMessage*    pRep = dbus_message_new_method_return( pMessage );
                    DBusMessageIter iter;
                    dbus_message_iter_init_append( pRep, &iter );
                    appendString( &iter, "<node/>" );
                    dbus_connection_send( _pBus, pRep, nullptr );
                    dbus_message_unref( pRep );
                    return DBUS_HANDLER_RESULT_HANDLED;
                }
                return replyError( pMessage, DBUS_ERROR_UNKNOWN_METHOD, "unsupported interface" );
            }

            UISystem&               _ui;
            IWindow&                _window;
            DBusConnection*         _pSession{ nullptr }; ///< 세션 버스(접근성 버스 주소와 스크린 리더 켜짐 신호를 받는다)
            DBusConnection*         _pBus{ nullptr };     ///< 접근성 버스(객체를 내보내는 연결)
            string                  _busName{};           ///< 접근성 버스에서의 이 앱 고유 이름
            bool                    _bReaderOn{ false };
            UIAccessibilitySnapshot _snapshot{};
            bool                    _bSnapshotValid{ false };
        };
    } // namespace

    unique_ptr<IUIAccessibilityBridge> AtSpiAccessibility::createBridge( UISystem& ui, IWindow& window )
    {
        unique_ptr<Bridge> pBridge = make_unique<Bridge>( ui, window );
        if ( pBridge->connect() == false )
            return nullptr;
        return pBridge;
    }
} // namespace sw

#endif
