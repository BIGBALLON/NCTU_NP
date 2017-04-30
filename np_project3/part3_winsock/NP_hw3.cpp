#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <math.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <errno.h>
#include <fcntl.h>
#include <list>
using namespace std;

#define DEBUG
#define SERVER_PORT 7799
#define MAX_LEN 32768
#define STATUS_CONNECTING 0
#define STATUS_READING 1
#define STATUS_COMMAND 2
#define STATUS_DONE 3
#define WM_SOCKET_NOTIFY (WM_USER + 1)
#define MAX_CLIENT 5
#define clr( a, b ) memset( a, b, sizeof(a) )

#include "resource.h"


//=================================================================
// Global
char h[6][MAX_LEN]; //server ip
char p[6][MAX_LEN]; //server port
char f[6][MAX_LEN]; //filename
ifstream filefd[6]; //file fd
string transbuf[6]; //trans string
int cstatus[6];     //client's status
int vis[6];         //is client can visit 
int done[6];        //client logout
int cfd[6];         //client fd
int cnt;            //client count (MAX = 5)
list<SOCKET> Socks;
SOCKET cgisock;
//=================================================================

BOOL CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);
int EditPrintf(HWND, TCHAR *, ...);

void printHtml(SOCKET sockfd, const char* msg);
void cgiHttp(SOCKET sockfd, HWND hwnd, HWND hwndEdit);
void comHost(int i, HWND hwnd, HWND hwndEdit);
void readFile(int i, HWND hwnd, HWND hwndEdit);
void printInfo();
void paserStr(char* str);
void myPrint(char* str, int id, int bd);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
	LPSTR lpCmdLine, int nCmdShow) {

	return DialogBox(hInstance, MAKEINTRESOURCE(ID_MAIN), NULL, MainDlgProc);
}

BOOL CALLBACK MainDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	WSADATA wsaData;

	static HWND hwndEdit;
	static SOCKET msock, ssock;
	static struct sockaddr_in sa;

	int err;
	bool host_reading;

	switch (Message)
	{
	case WM_INITDIALOG:
		hwndEdit = GetDlgItem(hwnd, IDC_RESULT);
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_LISTEN:

			WSAStartup(MAKEWORD(2, 0), &wsaData);

			//create master socket
			msock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

			if (msock == INVALID_SOCKET) {
				EditPrintf(hwndEdit, TEXT("=== Error: create socket error ===\r\n"));
				WSACleanup();
				return TRUE;
			}

			err = WSAAsyncSelect(msock, hwnd, WM_SOCKET_NOTIFY, FD_ACCEPT | FD_CLOSE | FD_READ);

			if (err == SOCKET_ERROR) {
				EditPrintf(hwndEdit, TEXT("=== Error: select error ===\r\n"));
				closesocket(msock);
				WSACleanup();
				return TRUE;
			}

			//fill the address info about server
			sa.sin_family = AF_INET;
			sa.sin_port = htons(SERVER_PORT);
			sa.sin_addr.s_addr = INADDR_ANY;

			//bind socket
			err = bind(msock, (LPSOCKADDR)&sa, sizeof(struct sockaddr));

			if (err == SOCKET_ERROR) {
				EditPrintf(hwndEdit, TEXT("=== Error: binding error ===\r\n"));
				WSACleanup();
				return FALSE;
			}

			err = listen(msock, 2);

			if (err == SOCKET_ERROR) {
				EditPrintf(hwndEdit, TEXT("=== Error: listen error ===\r\n"));
				WSACleanup();
				return FALSE;
			}
			else {
				EditPrintf(hwndEdit, TEXT("=== Server START(port:%d) ===\r\n"), SERVER_PORT);
			}

			break;
		case ID_EXIT:
			EndDialog(hwnd, 0);
			break;
		};
		break;

	case WM_CLOSE:
		EndDialog(hwnd, 0);
		break;

	case WM_SOCKET_NOTIFY:
		switch (WSAGETSELECTEVENT(lParam))
		{
		case FD_ACCEPT:
			ssock = accept(msock, NULL, NULL);
			Socks.push_back(ssock);
			err = WSAAsyncSelect(ssock, hwnd, WM_SOCKET_NOTIFY, FD_READ);
			EditPrintf(hwndEdit, TEXT("=== Accept new client (sockfd: %d) ===\r\n"), ssock);
			break;
		case FD_READ:
			host_reading = false;
			for (int i = 0; i < MAX_CLIENT; i++) {
				if (vis[i] && cfd[i] == wParam) {
					host_reading = true;
					comHost(i, hwnd, hwndEdit);
					break;
				}
			}
			if (!host_reading)
				cgiHttp(wParam, hwnd, hwndEdit);
			break;
		case FD_WRITE:
			for (int i = 0; i < MAX_CLIENT; i++) {

				if (cfd[i] == wParam && (!done[i])) {
					readFile(i, hwnd, hwndEdit);
					break;
				}
			}
			break;
		case FD_CLOSE:
			break;
		};
		break;

	default:
		return FALSE;


	};

	return TRUE;
}

int EditPrintf(HWND hwndEdit, TCHAR * szFormat, ...)
{
	TCHAR   szBuffer[1024];
	va_list pArgList;

	va_start(pArgList, szFormat);
	wvsprintf(szBuffer, szFormat, pArgList);
	va_end(pArgList);

	SendMessage(hwndEdit, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
	SendMessage(hwndEdit, EM_REPLACESEL, FALSE, (LPARAM)szBuffer);
	SendMessage(hwndEdit, EM_SCROLLCARET, 0, 0);
	return SendMessage(hwndEdit, EM_GETLINECOUNT, 0, 0);
}

void cgiHttp(SOCKET sockfd, HWND hwnd, HWND hwndEdit)
{
	WSAAsyncSelect(sockfd, hwnd, WM_SOCKET_NOTIFY, 0);
	
	string string_buffer;
	char buf[MAX_LEN] = { 0 };
	int n = recv(sockfd, buf, MAX_LEN, 0);
	string_buffer.assign(buf);

	int pos = string_buffer.find("Connection:");
	if (pos == -1)
		return;

	EditPrintf(hwndEdit, TEXT("---------- connected (%d) ----------\r\n"), sockfd);

	char *request = new char[pos + 1];
	clr(request, 0);
	strncpy(request, string_buffer.c_str(), pos);

	char* method = strtok(request, " ");
	char* script = strtok(NULL, " ");
	script = script + 1;

	if (strcmp(method, "GET")) {
		EditPrintf(hwndEdit, TEXT("not GET type \r\n"), sockfd);
		return;
	}

	char* file_name = strtok(script, " ?");

	char* query = strtok(NULL, " ");
	EditPrintf(hwndEdit, TEXT("file_name = %s\r\n"), file_name);
	EditPrintf(hwndEdit, TEXT("query = %s\r\n"), query);

	printHtml(sockfd, "HTTP 200 OK\n");

	if (strstr(file_name, ".cgi") != NULL) {

		cgisock = sockfd;
		char* tmp = strtok(query, "&");
		char* ptr;
		char* temp_ip;
		char* temp_port;
		char* temp_file;

		for (int i = 0; i < MAX_CLIENT; i++) {

			ptr = strchr(tmp, '=');
			if (*(ptr + 1) != '\0') {

				temp_ip = ptr + 1;
				tmp = strtok(NULL, "&");
				ptr = strchr(tmp, '=');
				temp_port = ptr + 1;
				tmp = strtok(NULL, "&");
				ptr = strchr(tmp, '=');
				temp_file = ptr + 1;
				strcpy(h[i], temp_ip);
				strcpy(p[i], temp_port);
				strcpy(f[i], temp_file);
				vis[i] = 1;
				done[i] = 0;
				transbuf[i] = "";
			}
			else {

				vis[i] = 0;
				done[i] = 0;
				cstatus[i] = STATUS_CONNECTING;
				tmp = strtok(NULL, "&");
				tmp = strtok(NULL, "&");
			}
			if (i != 4)
				tmp = strtok(NULL, "&");
		}

		printHtml(cgisock, "Content-type: text/html\n\n");
		printHtml(cgisock, "<html>");
		printHtml(cgisock, "<head>");
		printHtml(cgisock, "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=big5\" />");
		printHtml(cgisock, "<title>Network Programming Homework 3</title>");
		printHtml(cgisock, "</head>");
		printHtml(cgisock, "<style type='text/css'> xmp {display: inline;} </style>");
		printHtml(cgisock, "<body bgcolor=#336699>");
		printHtml(cgisock, "<font face=\"Courier New\" size=2 color=#FFFF99>");
		#ifndef DEBUG
		printInfo();
		#endif // !DEBUG
		printHtml(cgisock, "<table width=\"800\" border=\"1\">");
		printHtml(cgisock, "<tr>");

		for (int i = 0; i < MAX_CLIENT; i++) {
			clr(buf, 0);
			sprintf(buf, "<td><xmp>%s</xmp></td>", h[i]);
			printHtml(cgisock, buf);
		}

		printHtml(cgisock, "</tr>");

		printHtml(cgisock, "<tr>");
		printHtml(cgisock, "<td valign=\"top\" id=\"m0\"></td><td valign=\"top\" id=\"m1\"></td><td valign=\"top\" id=\"m2\"><td valign=\"top\" id=\"m3\"><td valign=\"top\" id=\"m4\"></td></tr>");
		printHtml(cgisock, "</table>");

		cnt = 0;
		for (int i = 0; i < MAX_CLIENT; i++){
			if (!vis[i]) {
				clr(buf, 0);
				sprintf(buf, "<script>document.all['m%d'].innerHTML += \"<br>\";</script>", i);
				printHtml(cgisock, buf);
			}
			else {

				struct sockaddr_in client;
				struct hostent *he;
				unsigned int clilen = sizeof(client);

				cfd[i] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

				if (cfd[i] == INVALID_SOCKET)
					EditPrintf(hwndEdit, TEXT("CGI hosts: socket() Error"));
				memset(&client, 0, sizeof(clilen));
				client.sin_family = AF_INET;

				if ((he = gethostbyname(h[i])) != NULL) {
					client.sin_addr = *((struct in_addr *)he->h_addr);
				}else{
					client.sin_addr.s_addr = inet_addr(h[i]);
				}

				client.sin_port = htons((u_short)((atoi)(p[i])));
				connect(cfd[i], (struct sockaddr *)&client, clilen);

				filefd[i].open(f[i], ios::in);
				WSAAsyncSelect(cfd[i], hwnd, WM_SOCKET_NOTIFY, FD_READ);
				cnt++;
				EditPrintf(hwndEdit, TEXT("host[%d] (%d) connected,wait READ\r\n"), i, cfd[i]);
			}
		}
	}
	else {

		printHtml(sockfd, "Content-Type: text/html\n\n\n");
		ifstream script_stream;
		script_stream.open(file_name, ios::in);
		string_buffer.assign("");

		while (getline(script_stream, string_buffer))
		{
			string_buffer = string_buffer + "\n";
			printHtml(sockfd, string_buffer.c_str());
			string_buffer.assign("");
		}
		closesocket(sockfd);
	}
	EditPrintf(hwndEdit, TEXT("---------- done  (%d) ----------\n"), sockfd);
}

void printHtml(SOCKET sockfd, const char* msg)
{
	char* write_buffer = new char[strlen(msg)];
	clr(write_buffer, 0);
	strcpy(write_buffer, msg);

	int write_status;
	int write_idx = 0;
	int total_write = strlen(msg);

	while (write_idx != total_write) {

		write_status = send(sockfd, write_buffer + write_idx, total_write - write_idx, 0);
		if (write_status != -1)
			write_idx += write_status;
	}
}

void comHost(int i, HWND hwnd, HWND hwndEdit)
{
	int n = 0;
	while (true)
	{
		if (transbuf[i].size() != 0) {
			unsigned r, end;
			while ((r = transbuf[i].find('\r')) != -1)
				transbuf[i].erase(r, 1);
			bool exec_cmd = false;
			while ((transbuf[i].size() != 0) && ((end = transbuf[i].find('\n')) != -1)) {
				if (transbuf[i][0] == '%')
					exec_cmd = true;
				string line = transbuf[i].substr(0, end);
				unsigned p = -2;
				while ((p = line.find('\"', p + 2)) != -1)
					line.insert(p, 1, '\\');
				char buf[MAX_LEN] = { 0 };
				sprintf(buf, "<script>document.all['m%d'].innerHTML += \"<xmp>%s</xmp><br>\";</script>\n", i, line.c_str());
				printHtml(cgisock, buf);
				transbuf[i].erase(0, end + 1);
			}
			if (exec_cmd) {
				cstatus[i] = STATUS_COMMAND;
				WSAAsyncSelect(cfd[i], hwnd, WM_SOCKET_NOTIFY, FD_WRITE);
				break;
			}
			if ((transbuf[i].size() != 0) && (transbuf[i][0] == '%')) {

				string sub = transbuf[i].substr(0, 2);
				transbuf[i].erase(0, 2);
				char buf[MAX_LEN] = { 0 };
				sprintf(buf, "<script>document.all['m%d'].innerHTML += \"<xmp>%s</xmp>\";</script>\n", i, sub.c_str());
				printHtml(cgisock, buf);
				cstatus[i] = STATUS_COMMAND;
				WSAAsyncSelect(cfd[i], hwnd, WM_SOCKET_NOTIFY, FD_WRITE);
				break;
			}
		}
		char buf[MAX_LEN] = { 0 };
		n = recv(cfd[i], buf, MAX_LEN, 0);
		if (n > 0)
			transbuf[i].append(buf);
		else if (n == -1 && WSAGetLastError() == WSAEWOULDBLOCK)
			break;
		else if (n == -1 && WSAGetLastError() != WSAEWOULDBLOCK) {
			closesocket(cfd[i]);
			WSACleanup();
			exit(1);
		}
		else if (n == 0) {

			vis[i] = 0;
			done[i] = 1;
			cnt--;
			closesocket(cfd[i]);
			EditPrintf(hwndEdit, TEXT("host[%d] closed\r\n"), i);
			if (cnt == 0) {

				EditPrintf(hwndEdit, TEXT("DONE\r\n"), i);
				printHtml(cgisock, "</font>");
				printHtml(cgisock, "</body>");
				printHtml(cgisock, "</html>");
				closesocket(cgisock);
			}
			break;
		}
	}
}

void myPrint(char* str, int id, int bd) {

	printf("<script>document.all[\'m%d\'].innerHTML += \"", id - 1);
	if (bd) printf("<b>");
	for (int i = 0; i < strlen(str); ++i) {
		if (str[i] == '"') {
			printf("&quot;");
		}
		else if (str[i] == '&') {
			printf("&amp;");
		}
		else if (str[i] == '<') {
			printf("&lt;");
		}
		else if (str[i] == '>') {
			printf("&gt;");
		}
		else if (str[i] == '\n') {
			printf("<br>");
		}
		else if (str[i] == ' ') {
			printf("&nbsp;");
		}
		else if (str[i] == '\r') {
			continue;
		}
		else {
			printf("%c", str[i]);
		}
	}

	if (bd) printf("</b>");
	printf("\";</script>\n");
}

void readFile(int i, HWND hwnd, HWND hwndEdit)
{
	// Read from batch
	string line;
	getline(filefd[i], line);
	if (line.size() == 0) {

		EditPrintf(hwndEdit, TEXT("readFileError\r\n"), i);
		return;
	}
	if (line[line.size() - 1] == '\r')
		line = line.substr(0, line.size() - 1);	// remove /r;

	char buf[MAX_LEN] = { 0 };
	sprintf(buf, "<script>document.all['m%d'].innerHTML += \"<b><xmp>%s</xmp></b><br>\";</script>\n", i, line.c_str());
	printHtml(cgisock, buf);
	line = line + '\n';
	printHtml(cfd[i], line.c_str());

	if (line == "exit\n") {

		done[i] = 1;
		filefd[i].close();

		while (true) {

			char exitMsg[MAX_LEN] = { 0 };

			int n = recv(cfd[i], exitMsg, MAX_LEN, 0);

			if ((n == -1) && (WSAGetLastError() == WSAEWOULDBLOCK)) {
				//EditPrintf(hwndEdit, TEXT("block\r\n"), i);
				continue;
			}
			else if (n > 0)
			{
				unsigned r;
				transbuf[i].assign(exitMsg);
				while ((r = transbuf[i].find('\r')) != -1)
					transbuf[i].erase(r, 1);
				while (transbuf[i].size() != 0) {

					r = transbuf[i].find('\n');
					char exitScript[MAX_LEN] = { 0 };
					if (r != -1) {

						string line = transbuf[i].substr(0, r);// no \n
						sprintf(exitScript, "<script>document.all['m%d'].innerHTML += \"<xmp>%s</xmp><br>\";</script>\n", i, line.c_str());
						printHtml(cgisock, exitScript);
						transbuf[i].erase(0, r + 1);
					}
					else {

						sprintf(exitScript, "<script>document.all['m%d'].innerHTML += \"<xmp>%s</xmp><br>\";</script>\n", i, transbuf[i].c_str());
						printHtml(cgisock, exitScript);
						transbuf[i].assign("");
					}
				}
			}
			else{
				vis[i] = 0;
				done[i] = 1;
				cnt--;
				closesocket(cfd[i]);
				EditPrintf(hwndEdit, TEXT("host[%d] closed\n"), i);
				if (cnt == 0) {
					EditPrintf(hwndEdit, TEXT("DONE\r\n"), i);
					printHtml(cgisock, "</font>\n");
					printHtml(cgisock, "</body>\n");
					printHtml(cgisock, "</html>\n");
					closesocket(cgisock);
				}
				break;
			}
		}
	}

	WSAAsyncSelect(cfd[i], hwnd, WM_SOCKET_NOTIFY, FD_READ);
}


void paserStr(char* str) {
	clr(h, 0); clr(p, 0); clr(f, 0); clr(vis, 0);

	for (int i = 0; i < (int)strlen(str); ++i) {
		if (str[i] == 'h') {
			int index = str[i + 1] - '0';
			if (index >= 0 && index <= 5) {
				i += 3;
				int pos = 0;
				while (str[i + pos] && str[i + pos] != '&') {
					h[index][pos] = str[i + pos];
					pos++;
					vis[index] = 1;
				}
			}
		}
		else if (str[i] == 'p') {
			int index = str[i + 1] - '0';
			if (index >= 0 && index <= 5) {
				i += 3;
				int pos = 0;
				while (str[i + pos] && str[i + pos] != '&') {
					p[index][pos] = str[i + pos];
					pos++;
				}
			}
		}
		else if (str[i] == 'f') {
			int index = str[i + 1] - '0';
			if (index >= 0 && index <= 5) {
				i += 3;
				int pos = 0;
				while (str[i + pos] && str[i + pos] != '&') {
					f[index][pos] = str[i + pos];
					pos++;
				}
			}
		}
	}
}

void printInfo() {
	for (int i = 0; i < MAX_CLIENT; i++) {
		char buf[MAX_LEN];
		sprintf(buf, "<xmp>Host%d:    ip=%s    port=%s    batch=%s</xmp><br>", i + 1, h[i], p[i], f[i]);
		printHtml(cgisock, buf);
	}
}