/**
 * @file ServerListModel.h
 * @brief 서버 고르기 화면이 읽는 서버 목록 상태입니다. `ServerDirectoryClient::requestServerList` 의 응답을 받아 목록과 상태(`Loading` · `Loaded` · `Failed`)를 보관합니다.
 * @details 화면(`ServerPickerScreen`)은 요청을 직접 보내지 않고 이 객체를 읽는다 — 응답 델리게이트가 화면 수명에 묶이지 않게 한다. 이 객체는 클라이언트와 함께(또는 먼저 요청이 끝난 뒤) 소멸해야 한다
 *          (`ServerDirectoryClient` 의 다른 사용처와 같은 규칙). 목록이 바뀔 때마다 `getRevision` 이 올라가 화면이 다시 그릴지 판단한다.
 *          목록을 받지 못하면(`Failed`) 이전 목록을 남기지 않고 비운다 — 낡은 목록을 새것처럼 보이지 않게 한다.
 */
#pragma once
#include "Core/Container/string.h"
#include "Core/Container/vector.h"
#include "Core/Foundation/Types.h"

#include "GameFramework/GameFrameworkExports.h"
#include "GameFramework/Kits/Feature/Online/ServerDirectory/Runtime/ServerDirectoryClient.h"

namespace sw
{
    /** @brief 목록을 받는 단계입니다. */
    enum class ServerListState : uint8
    {
        Idle = 0, ///< 아직 묻지 않았다
        Loading,
        Loaded,
        Failed ///< 오류 코드는 `getErrorCode`
    };

    /**
     * @class ServerListModel
     * @brief 서버 목록 한 벌과 그 상태입니다.
     */
    class SW_KIT_API ServerListModel
    {
    public:
        ServerListModel();
        ServerListModel( const ServerListModel& )            = delete; // 응답 델리게이트가 this 를 가리킨다
        ServerListModel& operator=( const ServerListModel& ) = delete;

        /** @brief 클라이언트를 빌려 씁니다(`ServerDirectoryClient` 가 서비스 클라이언트에 등록·초기화된 것). */
        void initialize( ServerDirectoryClient* pClient ) { _pClient = pClient; }
        /**
         * @brief @p kind 종류의 서버 목록을 묻습니다. 이미 묻는 중이면 보내지 않고 false 입니다(응답이 겹쳐 낡은 것이 새것을 덮지 않게 한다). 클라이언트가 없거나 종류가 상한을 넘으면 `Failed` 로 끝나고 false 입니다.
         */
        [[nodiscard]] bool refresh( string_view kind );

        ServerListState                getState() const { return _state; }
        uint16                         getErrorCode() const { return _errorCode; }
        uint64                         getRevision() const { return _revision; }
        const vector<ServerListEntry>& getEntries() const { return _listEntry; }

    private:
        void onServerList( uint16 errorCode, const vector<ServerListEntry>& listEntry );

        vector<ServerListEntry> _listEntry;
        ServerDirectoryClient*  _pClient;
        uint64                  _revision;
        uint16                  _errorCode;
        ServerListState         _state;
    };
} // namespace sw
