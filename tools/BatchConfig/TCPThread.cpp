//---------------------------------------------------------------------------

#include <vcl.h>
#pragma hdrstop

#include "TCPThread.h"
#include "MainFrm.h"
#include "net_struct.h"
#pragma package(smart_init)
//---------------------------------------------------------------------------

//   Important: Methods and properties of objects in VCL can only be
//   used in a method called using Synchronize, for example:
//
//      Synchronize(UpdateCaption);
//
//   where UpdateCaption could look like:
//
//      void __fastcall TTCPServer::UpdateCaption()
//      {
//        Form1->Caption = "Updated in a thread";
//      }
//---------------------------------------------------------------------------

__fastcall TTCPServer::TTCPServer(bool CreateSuspended)
: TThread(CreateSuspended)
{
	devNum = 0;
}
//---------------------------------------------------------------------------
void __fastcall TTCPServer::ResetSetNo()
{
	this->devNum = 0;
}
void __fastcall TTCPServer::UpdateEdit()
{
	Form1->SetEditText(errStr);
}
typedef struct _fd_node
{
	SOCKET fd;
	struct _fd_node *next;
}fd_node;
void __fastcall TTCPServer::Execute()
{
	//---- Place thread code here ----
	SOCKET sock;
	SOCKADDR_IN clientAddr;
	SOCKADDR_IN tcpAddr;
	fd_set readfds;
	int addrLen;
	int result;
	unsigned int maxfd;
	fd_node *pNodeHead = NULL;
	fd_node *pNode, *pTmpNode;

	tcpSock = socket(AF_INET,SOCK_STREAM,0);
	if (tcpSock == INVALID_SOCKET)
	{
		errStr = "Socket Open failed";
		Synchronize(UpdateEdit);
		return;
	}

	memset(&tcpAddr, 0, sizeof(tcpAddr));
	tcpAddr.sin_family = AF_INET;
	tcpAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	tcpAddr.sin_port = htons(TCP_SERVER_PORT);
	bind(tcpSock, (SOCKADDR *)&tcpAddr, sizeof(tcpAddr));
	listen(tcpSock, 1024);

	pNode = new fd_node;
	pNode->fd = tcpSock;
	pNode->next = NULL;
	pNodeHead = pNode;

	while (!this->Terminated)
	{
		maxfd = 0;
		FD_ZERO(&readfds);
		pNode = pNodeHead;
		while (pNode != NULL)
		{
			FD_SET(pNode->fd, &readfds);
			if (pNode->fd > maxfd)
				maxfd = pNode->fd;
			pNode = pNode->next;
		}
		if (select(maxfd + 1, &readfds, NULL, NULL, NULL) > 0)
		{
			pNode = pNodeHead;
			while (pNode != NULL)
			{
				if (FD_ISSET(pNode->fd, &readfds))
				{
					// 接收客户端的连接
					if (pNode->fd == tcpSock)
					{
						//FD_CLR(tcpSock, &readfds);
						addrLen = sizeof(clientAddr);
						sock = accept(tcpSock, (SOCKADDR *)&clientAddr, &addrLen);
						if (sock == SOCKET_ERROR)
						{
							errStr = "TCP连接失败";
							Synchronize(UpdateEdit);
							continue;
						}
						//ShowMessage("accept success");
						this->devNum++;
						pTmpNode = new fd_node;
						pTmpNode->fd = sock;
						pTmpNode->next = pNodeHead;
						pNodeHead = pTmpNode;
					}
					else
					{
						//接收客户端的命令
						// ShowMessage("FD_ISSET client fd");
						int ret = DisposeTcpCmd(pNode->fd);
						if (ret < 0)
						{
							AnsiString str = "TCP传输失败";
							Form1->lblInfo->Caption = str;
							Form1->SetEditText(str);
							closesocket(pNode->fd);
							pNode->fd = -1;
						}
						else if (ret == 0)	 // 客户端已经断开连接
						{
							closesocket(pNode->fd);
							pNode->fd = -1;	// 标识,方便删除
						}
					}
				}
				pNode = pNode->next;
			}
			// 删除已经出错或者断开连接的sock 
			// fd_node *pTmpNode;

			pNode = pNodeHead;
			pNodeHead = NULL;
			while (pNode != NULL)
			{
				if (pNode->fd == -1)
				{
					pTmpNode = pNode;
					pNode = pNode->next;
					delete pTmpNode;
				}
				else
				{
					if (pNodeHead == NULL)
					{
						pNodeHead = pNode;
						pTmpNode = pNode->next;
						pNode->next = NULL;
						pNode = pTmpNode;
					}
					else
					{
						pTmpNode = pNode->next;
						pNode->next = pNodeHead;
						pNodeHead = pNode;
						pNode = pTmpNode;
					}
				}
			}
		}
	}
}
int __fastcall TTCPServer::RecvTcpData(SOCKET sockfd, char *rcvBuf, int bufSize, int rcvSize)
{
	int nleft = 0;
	int nread = 0;
	char *ptr = NULL;

	ptr  = rcvBuf;
	nleft= rcvSize;

	if (rcvBuf == NULL || bufSize <= 0)
		return -1;

	if (bufSize < rcvSize)
		nleft = bufSize;

	while (nleft > 0)
	{
		nread = recv(sockfd, ptr, nleft, 0);
		if (nread == SOCKET_ERROR)
		{
			return -1;
		}
		else if (nread == 0)
		{
			return 0;
		}
		nleft -= nread;
		ptr   += nread;
	}

	return  rcvSize;
}
int __fastcall TTCPServer::SendTcpData(SOCKET sockfd, char *sendBuf, int sendSize)
{
	int nleft;
	int nwritten;
	const char *ptr;

	if (sendBuf == NULL || sendSize <= 0)
		return -1;

	ptr = sendBuf;
	nleft = sendSize;
	while (nleft > 0)
	{
		if ( (nwritten = send(sockfd, ptr, nleft, 0)) <= 0)
		{
			return -1;	   /* error */
		}

		nleft -= nwritten;
		ptr += nwritten;
	}
	return(sendSize - nleft);
}
int __fastcall TTCPServer::DisposeTcpCmd(SOCKET sockfd)
{
	char buffer[256];
	int recvLen;
	cmd_info *pCmdInfo;

	recvLen = RecvTcpData(sockfd, buffer, sizeof(buffer), sizeof(buffer));
	if (recvLen == sizeof(buffer))
	{
		pCmdInfo = (cmd_info*)buffer;
		if (pCmdInfo->magic == 0x12341234)
		{
			switch (pCmdInfo->cmd)
			{
			case CMD_REQUEST_MAC_AND_HWID:
				{
					memset(buffer, 0, sizeof(buffer));
					pCmdInfo->magic = 0x12341234;
					pCmdInfo->cmd = CMD_RESPONSE;
					if(!Form1->RequestMacFromList(pCmdInfo->data))
						memset(pCmdInfo->data, 0, 6);
					if (!Form1->RequestHwidFromList(pCmdInfo->data + 6, pCmdInfo->data))
						memset(pCmdInfo->data + 6, 0, 48);	// 48为HWID大小
					this->SendTcpData(sockfd, buffer, sizeof(buffer));
					break;
				}
			case CMD_UPDATE_FILNISH:
				{
					Form1->DeviceWriteMacResponse(pCmdInfo->data);
					break;
				}
			default:
				break;
			}
		}
	}
	return recvLen;
}
//---------------------------------------------------------------------------

