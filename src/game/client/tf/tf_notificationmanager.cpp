#include "cbase.h"
#include "tf_notificationmanager.h"
#include "tf_mainmenu.h"
#include "filesystem.h"
#include "script_parser.h"
#include "tf_gamerules.h"
#include "tf_hud_notification_panel.h"

const char *g_aRequestURLs[REQUEST_COUNT] =
{
	"http://services.0x13.io/tf2c/version/?latest=1",
	"http://services.0x13.io/tf2c/motd/"
};

static CTFNotificationManager g_TFNotificationManager;
CTFNotificationManager *GetNotificationManager()
{
	return &g_TFNotificationManager;
}

CON_COMMAND_F(tf2c_checkmessages, "Check for the messages", FCVAR_DEVELOPMENTONLY)
{
	GetNotificationManager()->AddRequest(REQUEST_VERSION);
	GetNotificationManager()->AddRequest(REQUEST_MESSAGE);
}

ConVar tf2c_checkfrequency("tf2c_checkfrequency", "900", FCVAR_DEVELOPMENTONLY, "Messages check frequency (seconds)");

bool RequestHandleLessFunc(const HTTPRequestHandle &lhs, const HTTPRequestHandle &rhs)
{
	return lhs < rhs;
}

//-----------------------------------------------------------------------------
// Purpose: constructor
//-----------------------------------------------------------------------------
CTFNotificationManager::CTFNotificationManager() : CAutoGameSystemPerFrame("CTFNotificationManager")
{
	if (!filesystem)
		return;

	m_bInited = false;
	Init();
}

CTFNotificationManager::~CTFNotificationManager()
{
}

//-----------------------------------------------------------------------------
// Purpose: Initializer
//-----------------------------------------------------------------------------
bool CTFNotificationManager::Init()
{
	if (!m_bInited)
	{
		pNotifications.RemoveAll();

		m_SteamHTTP = steamapicontext->SteamHTTP();
		m_Requests.SetLessFunc(RequestHandleLessFunc);
		fLastCheck = tf2c_checkfrequency.GetFloat() * -1; 
		iCurrentRequest = REQUEST_IDLE;
		bCompleted = false;
		bOutdated = false;
		m_bInited = true;
	}
	return true;
}

void CTFNotificationManager::Update(float frametime)
{
	if (gpGlobals->curtime - fLastCheck > tf2c_checkfrequency.GetFloat())
	{
		fLastCheck = gpGlobals->curtime;
		AddRequest(REQUEST_VERSION);
		AddRequest(REQUEST_MESSAGE);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Event handler
//-----------------------------------------------------------------------------
void CTFNotificationManager::FireGameEvent(IGameEvent *event)
{
}

void CTFNotificationManager::AddRequest(RequestType type)
{
	if (!m_SteamHTTP)
		return;

	m_httpRequest = m_SteamHTTP->CreateHTTPRequest(k_EHTTPMethodGET, g_aRequestURLs[type]);
	m_SteamHTTP->SetHTTPRequestNetworkActivityTimeout(m_httpRequest, 5);

	int iReqIndex = m_Requests.Find(m_httpRequest);
	if (iReqIndex == m_Requests.InvalidIndex())
	{
		m_Requests.Insert(m_httpRequest, type);
	}
	SteamAPICall_t hSteamAPICall;
	m_SteamHTTP->SendHTTPRequest(m_httpRequest, &hSteamAPICall);
	if (type == REQUEST_VERSION)
	{
		m_CallResultVersion.Set(hSteamAPICall, this, (&CTFNotificationManager::OnHTTPRequestCompleted));
	}
	else if(type == REQUEST_MESSAGE)
	{
		m_CallResultMessage.Set(hSteamAPICall, this, (&CTFNotificationManager::OnHTTPRequestCompleted));
	}
	bCompleted = false;
}

void CTFNotificationManager::OnHTTPRequestCompleted(HTTPRequestCompleted_t *CallResult, bool iofailure)
{
	DevMsg("CTFNotificationManager: HTTP Request %i completed: %i\n", CallResult->m_hRequest, CallResult->m_eStatusCode);

	int iReqIndex = m_Requests.Find(CallResult->m_hRequest);
	if (iReqIndex == m_Requests.InvalidIndex())
		return;
	RequestType iRequestType = m_Requests[iReqIndex];

	if (CallResult->m_eStatusCode == 200)
	{
		uint32 iBodysize;
		m_SteamHTTP->GetHTTPResponseBodySize(CallResult->m_hRequest, &iBodysize);
		uint8 iBodybuffer[128];
		m_SteamHTTP->GetHTTPResponseBodyData(CallResult->m_hRequest, iBodybuffer, iBodysize);
		char result[128];
		Q_strncpy(result, (char*)iBodybuffer, iBodysize + 1);

		switch (iRequestType)
		{
		case REQUEST_IDLE:
			break;
		case REQUEST_VERSION:
			OnVersionCheckCompleted(result);
			break;
		case REQUEST_MESSAGE:
			OnMessageCheckCompleted(result);
			break;
		}
	}

	m_Requests.Remove(CallResult->m_hRequest);
	m_SteamHTTP->ReleaseHTTPRequest(CallResult->m_hRequest);
	bCompleted = true;
}

void CTFNotificationManager::OnVersionCheckCompleted(const char* pMessage)
{
	if (pMessage[0] == '\0')
		return;

	if (Q_strcmp(GetVersionString(), pMessage) < 0)
	{
		char resultString[128];
		bOutdated = true;
		Q_snprintf(resultString, sizeof(resultString), "Your game is out of date.\nThe newest version of TF2C is %s.\nDownload the update at\nwww.tf2classic.com", pMessage);
		MessageNotification Notification("Update!", resultString);
		SendNotification(Notification);
	}
	else
	{
		bOutdated = false;
	}
}

void CTFNotificationManager::OnMessageCheckCompleted(const char* pMessage)
{		
	if (pMessage[0] == '\0')
		return;

	if (m_pzLastMessage[0] != '\0' && !Q_strcmp(pMessage, m_pzLastMessage))
		return;

	char pzResultString[128];
	char pzMessageString[128];

	char * pch;
	int id = 0;
	pch = strchr((char*)pMessage, '\n');
	if (pch != NULL)
	{
		id = pch - pMessage + 1;
	}
	Q_snprintf(pzResultString, id, "%s", pMessage);
	Q_snprintf(pzMessageString, sizeof(pzMessageString), pMessage + id);
	Q_snprintf(m_pzLastMessage, sizeof(m_pzLastMessage), pMessage);

	MessageNotification Notification(pzResultString, pzMessageString);
	SendNotification(Notification);
}

void CTFNotificationManager::SendNotification(MessageNotification pMessage)
{
	pNotifications.AddToTail(pMessage);
	MAINMENU_ROOT->OnNotificationUpdate();

	C_TFPlayer *pLocalPlayer = C_TFPlayer::GetLocalTFPlayer();
	if (pLocalPlayer)
	{
		CHudNotificationPanel *pNotifyPanel = GET_HUDELEMENT(CHudNotificationPanel);
		if (pNotifyPanel)
		{
			pNotifyPanel->SetupNotifyCustom(pMessage.sMessage, "ico_notify_flag_moving", C_TFPlayer::GetLocalTFPlayer()->GetTeamNumber());
		}
	}
}

void CTFNotificationManager::RemoveNotification(int iIndex)
{
	pNotifications.Remove(iIndex);
	MAINMENU_ROOT->OnNotificationUpdate();
};

int CTFNotificationManager::GetUnreadNotificationsCount()
{
	int iCount = 0;
	for (int i = 0; i < pNotifications.Count(); i++)
	{
		if (pNotifications[i].bUnread)
			iCount++;
	}
	return iCount;
};

char* CTFNotificationManager::GetVersionString()
{
	char verString[30];
	if (g_pFullFileSystem->FileExists("version.txt"))
	{
		FileHandle_t fh = filesystem->Open("version.txt", "r", "MOD");
		int file_len = filesystem->Size(fh);
		char* GameInfo = new char[file_len + 1];

		filesystem->Read((void*)GameInfo, file_len, fh);
		GameInfo[file_len] = 0; // null terminator

		filesystem->Close(fh);

		Q_snprintf(verString, sizeof(verString), GameInfo + 8);

		delete[] GameInfo;
	}

	char *szResult = (char*)malloc(sizeof(verString));
	Q_strncpy(szResult, verString, sizeof(verString));
	return szResult;
}