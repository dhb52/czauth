// Win32Project1.cpp : Defines the entry point for the application.
//
#include "stdafx.h"

#include <ShellAPI.h> 
#include <WinInet.h>
#include <WinReg.h>
#include <winsock2.h>
#include <iphlpapi.h>
#include <icmpapi.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

#include <stdio.h>
#include <string>

#include "dgauth.h"

#define QUIET_MODE 1

#define STATIC_STRLEN(tstr)         (sizeof(tstr)/sizeof(tstr[0]))

#define WM_IAWENTRAY				WM_USER + 5

#define IDT_AUTH_HEARTBEAT_TIMER	WM_USER + 6
#define AUTH_HEARTBEAT_TIMEELAPSE   40 * 1000         // 40 sec

#define IDT_AUTH_NETCHK_TIMER		WM_USER + 7
#define AUTH_NETCHK_TIMEELAPSE		1 * 1000          // 1 sec

#define CLIENT_AGENT				_T("Mozilla/5.0 (Windows NT 5.1; rv:21.0) Gecko/20100101 Firefox/21.0")
#define AUTH_HOST					_T("auth.cz.gmcc.net")
#define AUTH_HOST_IP				"10.250.1.147"
#define HTTP_LOCATION_MAXLEN        256
#define LOGIN_SUCCESS_LOCATION      _T("https://auth.cz.gmcc.net/dana/home/starter0.cgi?check=yes")
#define AUTH_REFRESH_URL			_T("/dana/home/infranet.cgi")

const char szPostDataTemplate[] =   "username=%s&password=%s&realm=%%e8%%ae%%a4%%e8%%af%%81%%e5%%9f%%9f-Portal";
#define NAMEPASS_MAXLEN             30
#define POSTDATA_MAXLEN             (sizeof(szPostDataTemplate) + NAMEPASS_MAXLEN * 2)
#define DGAUTH_REGDIR               _T("Software\\Anonymous\\dgauth")

static NOTIFYICONDATA nid = {0};
static UINT WM_TASKBARCREATED;
static BOOL bLogin = FALSE;

typedef enum 
{
	DGAUTH_SUCCESS,
	INTERNET_OPEN_FAILED,
	INTERNET_CONNECT_FAILED,
	HTTP_OPENREQUEST_FAILED,
	HTTP_SENDREQUEST_FAILED,
	DGAUTH_LOGIN_FAILED,
	DGAUTH_UNKNOWN_FAILED
} DGAUTH_STATUS;

enum
{
	s_init,
	s_netready,
	s_netdown,
	s_authok,
	s_autherr
} app_state = s_init;

/**
*  class InternetHandleWrapper
*  Wrapper for HINTERNET, offer InternetCloseHandle automation by destructor,
*  Using convetion: never assign a HINTERNET handle to more than one wrappers.
*/
class InternetHandleWrapper
{
private:
	inline InternetHandleWrapper(const InternetHandleWrapper&);
	inline InternetHandleWrapper& operator= (const InternetHandleWrapper&);

public:
	inline InternetHandleWrapper(HINTERNET handle = NULL) : _handle(handle) {}
	inline InternetHandleWrapper& operator= (HINTERNET handle) { _handle = handle; }
	inline ~InternetHandleWrapper() { InternetCloseHandle(_handle); }
	inline operator HINTERNET () { return _handle; }

private:
	HINTERNET _handle;
};

static  INT_PTR CALLBACK	LoginDlgProc(HWND, UINT, WPARAM, LPARAM);
static  VOID		        InitNotifyIconData(HINSTANCE hInstance, HWND hwnd, HICON hIcon);
static  VOID		        UpdateNotifyIconData(BOOL online);
static  VOID		        PopupSystrayMenu(HWND hDlg);

static  BOOL                OnLoginBtn(HWND hDlg);
static  DGAUTH_STATUS		AuthLogin(LPCTSTR szName, LPCTSTR szPass);
static  BOOL				AuthRefresh();
static	VOID				RegWriteAuthInfo(LPCTSTR username, LPCTSTR password);
static  BOOL				RegReadAuthInfo(HWND hDlg);
static  VOID				OnDestroy();
#ifdef UNICODE
static  std::string         wcs_cstr(wchar_t const* szWcs);
#endif // UNICODE
static  bool				PingHostByIp(const char* szIp);
static  std::string			url_encode(char const *s);



int APIENTRY _tWinMain(HINSTANCE hInstance,
	HINSTANCE hPrevInstance,
	LPTSTR    lpCmdLine,
	int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	WM_TASKBARCREATED = RegisterWindowMessage(_T("TaskbarCreated"));

	INT_PTR nResult = DialogBox(
		hInstance,
		MAKEINTRESOURCE(IDD_LOGIN),
		NULL, 
		LoginDlgProc
		);

	return nResult;
}


INT_PTR CALLBACK LoginDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	HINSTANCE hInstance = GetModuleHandle(NULL);

	switch (message)
	{
	case WM_INITDIALOG:
		SendMessage(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP)));
		InitNotifyIconData(hInstance, hDlg, LoadIcon(hInstance, MAKEINTRESOURCE(IDI_OFFLINE)));
		Shell_NotifyIcon(NIM_ADD, &nid);
		RegReadAuthInfo(hDlg);
		SetTimer(hDlg, IDT_AUTH_NETCHK_TIMER, AUTH_NETCHK_TIMEELAPSE, NULL);
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) 
		{
		case IDOK:
		case IDCANCEL:
			//EndDialog(hDlg, (INT_PTR)FALSE);
			ShowWindow(hDlg, SW_HIDE);
			break;

		case IDC_BTN_LOGIN:
			OnLoginBtn(hDlg);
			break;

		case ID_NID_EXIT:
			PostQuitMessage(0);
			break;

			//case ID_NID_SHOWWINDOW:
			//	ShowWindow(hDlg, SW_NORMAL);
			//	break;
		}
		break;

	case WM_IAWENTRAY:  
		if(wParam == IDI_OFFLINE || wParam == IDI_ONLINE)
		{  
			if (lParam == WM_LBUTTONDOWN)
			{
				ShowWindow(hDlg, SW_NORMAL);
			}
			if (lParam == WM_RBUTTONDOWN) 
			{
				PopupSystrayMenu(hDlg);
			}
		}  
		break;

	case WM_TIMER:
		if (wParam == IDT_AUTH_HEARTBEAT_TIMER && AuthRefresh() == FALSE)
		{
			KillTimer(hDlg, IDT_AUTH_HEARTBEAT_TIMER);
			UpdateNotifyIconData(FALSE);
			//ShowWindow(hDlg, SW_NORMAL);
			OnLoginBtn(hDlg);
		}
		if (wParam == IDT_AUTH_NETCHK_TIMER && OnLoginBtn(hDlg) == TRUE)
		{
			KillTimer(hDlg, IDT_AUTH_NETCHK_TIMER);
			UpdateNotifyIconData(TRUE);
		}
		break;

	case WM_DESTROY:
		OnDestroy();
		return (INT_PTR)TRUE;
	}

	if (message == WM_TASKBARCREATED)
	{
		Shell_NotifyIcon(NIM_ADD, &nid);
	}

	return (INT_PTR)FALSE;
}


VOID InitNotifyIconData(HINSTANCE hInstance, HWND hwnd, HICON hIcon) 
{
	nid.cbSize				= sizeof(NOTIFYICONDATA);  
	nid.hWnd				= hwnd;  
	nid.uID					= IDI_OFFLINE;  
	nid.uFlags				= NIF_ICON | NIF_MESSAGE | NIF_TIP;  
	nid.uCallbackMessage	= WM_IAWENTRAY;  
	nid.hIcon				= hIcon;  
	_tcscpy_s(nid.szTip, TEXT("Offline"));
}

VOID UpdateNotifyIconData(BOOL online)
{
	HINSTANCE hInstance = GetModuleHandle(NULL);

	if (online == FALSE) 
	{
		nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_OFFLINE));
		_tcscpy_s(nid.szTip, TEXT("Offline"));
	}
	else
	{
		nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ONLINE));
		_tcscpy_s(nid.szTip, TEXT("Online"));
	}

	Shell_NotifyIcon(NIM_MODIFY, &nid);
}

BOOL OnLoginBtn(HWND hDlg)
{
	TCHAR szName[NAMEPASS_MAXLEN] = {0};
	TCHAR szPass[NAMEPASS_MAXLEN] = {0};
	GetDlgItemText(hDlg, IDC_LOGIN_NAME, szName, STATIC_STRLEN(szName));
	GetDlgItemText(hDlg, IDC_LOGIN_PASS, szPass, STATIC_STRLEN(szPass));

	if (_tcscmp(szName, _T("")) == 0 || _tcscmp(szPass, _T("")) == 0)
	{
		KillTimer(hDlg, IDT_AUTH_NETCHK_TIMER);
		return FALSE;
	}

	if (PingHostByIp(AUTH_HOST_IP) == FALSE)
	{
#ifndef QUIET_MODE 
		MessageBox(hDlg, _T("Ping auth.cz.gmcc.net"), _T("Ping Failed!"), MB_ICONERROR);
#endif //QUIET_MODE
		SetTimer(hDlg, IDT_AUTH_NETCHK_TIMER, AUTH_NETCHK_TIMEELAPSE, NULL);

		return FALSE;
	}

	DGAUTH_STATUS status = AuthLogin(szName, szPass);
	switch (status) 
	{
	case DGAUTH_SUCCESS:
		AuthRefresh();  
		SetTimer(hDlg, IDT_AUTH_HEARTBEAT_TIMER, AUTH_HEARTBEAT_TIMEELAPSE, NULL);
		UpdateNotifyIconData(TRUE);
		ShowWindow(hDlg, SW_HIDE);
		RegWriteAuthInfo(szName, szPass);

		return TRUE;

#ifndef QUIET_MODE 
	case INTERNET_OPEN_FAILED:
		MessageBox(hDlg, _T("INTERNET_OPEN_FAILED failed."), _T("Connection Failed!"), MB_ICONERROR);
		break;

	case INTERNET_CONNECT_FAILED:
		MessageBox(hDlg, _T("INTERNET_CONNECT_FAILED failed."), _T("Connection Failed!"), MB_ICONERROR);
		break;

	case HTTP_OPENREQUEST_FAILED:
		MessageBox(hDlg, _T("HTTP_OPENREQUEST_FAILED failed."), _T("Connection Failed!"), MB_ICONERROR);
		break;     

	case HTTP_SENDREQUEST_FAILED:
		MessageBox(hDlg, _T("Connection to auth.dg.gmcc.net failed."), _T("Connection Failed!"), MB_ICONERROR);
		break;
	case DGAUTH_UNKNOWN_FAILED:
		MessageBox(hDlg, _T("Unknown failure."), _T("Panic!"), MB_ICONERROR);
		break;
#endif // QUIET_MODE

	case DGAUTH_LOGIN_FAILED:
		KillTimer(hDlg, IDT_AUTH_NETCHK_TIMER);
		MessageBox(hDlg, _T("Wrong username/password or max connection reached."), _T("Login Failed!"), MB_ICONINFORMATION);
		break;
	}

	return FALSE;
}

DGAUTH_STATUS AuthLogin(LPCTSTR szName, LPCTSTR szPass)
{
	InternetHandleWrapper hSession = InternetOpen(CLIENT_AGENT, 
		INTERNET_OPEN_TYPE_DIRECT, 
		NULL , 
		NULL, 
		0);
	if (hSession == NULL) return INTERNET_OPEN_FAILED;

	InternetHandleWrapper hConnect = InternetConnect(hSession, 
		AUTH_HOST,
		INTERNET_DEFAULT_HTTPS_PORT, 
		NULL,
		NULL, 
		INTERNET_SERVICE_HTTP, 
		0,
		0);
	if (hConnect == NULL) return INTERNET_CONNECT_FAILED;

	InternetHandleWrapper hRequest = HttpOpenRequest(hConnect, 
		_T("POST"), 
		_T("/dana-na/auth/url_1/login.cgi") , //CZ //_T("/dana-na/auth/url_default/login.cgi"), //DG
		_T("HTTP/1.1"),
		NULL, 
		NULL, 
		INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_AUTO_REDIRECT | SECURITY_IGNORE_ERROR_MASK | INTERNET_FLAG_SECURE | SECURITY_FLAG_IGNORE_UNKNOWN_CA, 
		0);

	if (hRequest == NULL) return HTTP_OPENREQUEST_FAILED;

	LPCTSTR headers = _T("Content-Type: application/x-www-form-urlencoded");
	HttpAddRequestHeaders(
		hRequest, 
		headers, 
		_tcslen(headers),
		HTTP_ADDREQ_FLAG_REPLACE | HTTP_ADDREQ_FLAG_ADD
		);

	char post_data[POSTDATA_MAXLEN] = {0};
#ifdef UNICODE
	std::string name = wcs_cstr(szName);
	std::string pass = wcs_cstr(szPass);
	std::string passUrl = url_encode(pass.c_str());
	sprintf_s(post_data, szPostDataTemplate, name.c_str(), passUrl.c_str());
#else
	std::string passUrl = url_encode(szPass);
	sprintf_s(post_data, szPostDataTemplate, szName, passUrl.c_str());
#endif // UNICODE
	BOOL bSuccess = HttpSendRequest(hRequest, 
		NULL,					// Headers
		0,						// Headers length
		post_data,				// Options
		strlen(post_data));		// Options legnth

	if (bSuccess == FALSE) return HTTP_SENDREQUEST_FAILED;

	// check redirect URL in response 
	TCHAR buffer[HTTP_LOCATION_MAXLEN] = {0};
	DWORD bufflen = sizeof(buffer) - 1;
	HttpQueryInfo(hRequest,
		HTTP_QUERY_LOCATION,
		buffer, 
		&bufflen,
		0);

	if (_tcscmp(LOGIN_SUCCESS_LOCATION, buffer)  == 0)
	{
		return DGAUTH_SUCCESS;
	}

	if (bufflen > sizeof(buffer) - 1 || _tcsstr(buffer, _T("failed")) != NULL)
	{
		return DGAUTH_LOGIN_FAILED;
	}

	return DGAUTH_UNKNOWN_FAILED;
}

VOID PopupSystrayMenu(HWND hDlg)
{
	HINSTANCE hInstance = GetModuleHandle(NULL);

	POINT pt = {0};
	GetCursorPos(&pt);
	HMENU hMenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDR_SYSTRAYMENU));
	HMENU hSubMenu = GetSubMenu(hMenu, 0); 
	SetForegroundWindow(hDlg);
	TrackPopupMenu(hSubMenu,
		TPM_RIGHTBUTTON,
		pt.x,
		pt.y,
		0,
		hDlg,
		NULL);
	PostMessage(hDlg, WM_NULL, 0, 0);

	DestroyMenu(hMenu);
}

BOOL AuthRefresh()
{
	InternetHandleWrapper hSession = InternetOpen(CLIENT_AGENT, 
		INTERNET_OPEN_TYPE_DIRECT, 
		NULL , 
		NULL, 
		0);

	if (hSession == NULL) return FALSE;

	InternetHandleWrapper hConnect = InternetConnect(hSession, 
		AUTH_HOST,
		INTERNET_DEFAULT_HTTPS_PORT, 
		NULL,
		NULL, 
		INTERNET_SERVICE_HTTP, 
		0,
		0);

	if (hConnect == NULL) return FALSE; 

	InternetHandleWrapper hRequest = HttpOpenRequest(hConnect, 
		_T("GET"), 
		AUTH_REFRESH_URL,
		_T("HTTP/1.1"),
		NULL, 
		NULL, 
		INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_AUTO_REDIRECT | SECURITY_IGNORE_ERROR_MASK | INTERNET_FLAG_SECURE, 
		0);

	if (hRequest == NULL) return FALSE;

	BOOL bSuccess = HttpSendRequest(hRequest, 
		NULL,					// Headers
		0,						// Headers length
		NULL,  		            // Options
		0);             		// Options legnth

	if (bSuccess == FALSE) return FALSE;

	TCHAR buffer[1024] = {0};
	DWORD bufflen = sizeof(buffer) - 1;
	BOOL ret = HttpQueryInfo(hRequest,
		HTTP_QUERY_LOCATION,
		buffer, 
		&bufflen,
		0);
	// if any redirect happens, it means refresh failed
	// e.g. location: https://auth.dg.gmcc.net/dana-na/auth/welcome.cgi?p=forced-off
	if (ret == TRUE && bufflen > 0)
	{
		return FALSE;
	}

	return TRUE;
}



BOOL RegReadAuthInfo(HWND hDlg)
{
	HKEY hKey;
	if(RegOpenKeyEx(HKEY_CURRENT_USER, DGAUTH_REGDIR, 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) 
	{
		TCHAR username[MAX_PATH] = {0};
		TCHAR password[MAX_PATH] = {0};

		DWORD len = sizeof(username);
		LSTATUS status = RegQueryValueEx(hKey, _T("username"), 0, NULL, (LPBYTE)username, &len);
		if (status == ERROR_SUCCESS) 
		{
			SetDlgItemText(hDlg, IDC_LOGIN_NAME, username);
		}

		len = sizeof(password);
		status = RegQueryValueEx(hKey, _T("password"), 0, NULL, (LPBYTE)password, &len);
		if (status == ERROR_SUCCESS)
		{
			SetDlgItemText(hDlg, IDC_LOGIN_PASS, password);
		}

		RegCloseKey(hKey);

		return TRUE;
	}

	return FALSE;
}

VOID RegWriteAuthInfo(LPCTSTR username, LPCTSTR password) 
{
	HKEY hKey;
	LSTATUS status = RegCreateKey(HKEY_CURRENT_USER, DGAUTH_REGDIR, &hKey);
	if (status != ERROR_SUCCESS) return;
	status = RegSetValueEx(hKey, _T("username"), 0, REG_SZ, (CONST BYTE*)username, _tcslen(username) * sizeof(TCHAR));
	if (status != ERROR_SUCCESS) return;
	status = RegSetValueEx(hKey, _T("password"), 0, REG_SZ, (CONST BYTE*)password, _tcslen(password) * sizeof(TCHAR));
	if (status != ERROR_SUCCESS) return;

	RegCloseKey(hKey);
}

static VOID OnDestroy()
{
	Shell_NotifyIcon(NIM_DELETE, &nid);
}

#ifdef UNICODE
std::string wcs_cstr(wchar_t const* szWcs)
{
	std::string cstr(wcslen(szWcs), '0');
	for (std::size_t n = 0; szWcs[n] != L'\0'; ++n)
	{
		cstr[n] = static_cast<char>(szWcs[n]);
	}

	return cstr;
}
#endif // UNICODE


bool PingHostByIp(const char* szIp)
{
	HANDLE hIcmpFile;
	unsigned long ipaddr = INADDR_NONE;
	DWORD dwRetVal = 0;
	char SendData[32] = "Data Buffer";
	BYTE ReplyBuffer[sizeof(ICMP_ECHO_REPLY) + sizeof(SendData)] = {0};
	DWORD ReplySize = sizeof(ReplyBuffer);

	ipaddr = inet_addr(szIp);
	hIcmpFile = IcmpCreateFile();
	if (hIcmpFile == INVALID_HANDLE_VALUE) {
		return FALSE;
	}    

	dwRetVal = IcmpSendEcho(hIcmpFile, ipaddr, SendData, sizeof(SendData), 
		NULL, ReplyBuffer, ReplySize, 1000);
	if (dwRetVal == 0) return FALSE;

	PICMP_ECHO_REPLY pEchoReply = (PICMP_ECHO_REPLY)ReplyBuffer;
	struct in_addr ReplyAddr;
	ReplyAddr.S_un.S_addr = pEchoReply->Address;
	if (pEchoReply->Status == 0) return TRUE;

	return FALSE;
}

///http://blog.csdn.net/langeldep/article/details/6264058 
std::string url_encode(char const *s)  
{  
	register unsigned char c;  
	unsigned char *to, *start;  
	unsigned char const *from, *end;  
	int len;

	static unsigned char hexchars[] = "0123456789ABCDEF"; 

	len = strlen(s);
	from = (unsigned char *)s;  
	end  = (unsigned char *)s + len;  
	start = to = (unsigned char *) calloc(1, 3*len+1);  

	while (from < end)   
	{  
		c = *from++;  

		if (c == ' ')   
		{  
			*to++ = '+';  
		}   
		else if ((c < '0' && c != '-' && c != '.') ||  
			(c < 'A' && c > '9') ||  
			(c > 'Z' && c < 'a' && c != '_') ||  
			(c > 'z'))   
		{  
			to[0] = '%';  
			to[1] = hexchars[c >> 4];  
			to[2] = hexchars[c & 15];  
			to += 3;  
		}  
		else   
		{  
			*to++ = c;  
		}  
	}  
	*to = 0;  

	std::string newstr = (char*)start;  
	free(start);
	return newstr;
} 
