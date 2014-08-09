//*****************************************************************************
// Copyright (C) 2014 Texas Instruments Incorporated
//
// All rights reserved. Property of Texas Instruments Incorporated.
// Restricted rights to use, duplicate or disclose this code are
// granted through contract.
// The program may not be used without the written permission of
// Texas Instruments Incorporated or against the terms and conditions
// stipulated in the agreement under which this program has been supplied,
// and under no circumstances can it be used with non-TI connectivity device.
//
//*****************************************************************************

#include "xmpp.h"


#define XMPP_RESOURCE "Work"

#define SO_SECMETHOD    25  // security metohd
#define SO_SECURE_MASK  26  // security mask 0 and mask 1
#define SL_SECURITY_ANY                     (100)


#define PRESENCE_MESSAGE "<presence><priority>4</priority><status>Online</status><c xmlns='http://jabber.org/protocol/caps' node='http://jajc.jrudevels.org/caps' ver='0.0.8.125 (04.01.2012)'/></presence>"
#define JABBER_XMLNS_INFO "version='1.0' xmlns:stream='http://etherx.jabber.org/streams' xmlns='jabber:client'>"


extern void ConvertToBase64(char *pcOutStr, const char *pccInStr, int iLen);

typedef enum
{
  XMPP_INACTIVE = 0,
  XMPP_INIT,
  FIRST_STREAM_SENT,
  FIRST_STREAM_RECV,
  STARTTLS_RESPONSE_RECV,
  AUTH_QUERY_SET,
  AUTH_RESULT_RECV,
  BIND_FEATURE_REQUEST,
  BIND_FEATURE_RESPONSE,
  BIND_CONFIG_SET,
  BIND_CONFIG_RECV,
  XMPP_SESSION_SET,
  XMPP_SESSION_RECV,
  PRESENCE_SET,
  CONNECTION_ESTABLISHED,
  ROSTER_REQUEST,
  ROSTER_RESPONSE
}_SlXMPPStatus_e;



#define BUF_SIZE 1024
unsigned int g_XMPPStatus = XMPP_INIT;



char g_RecvBuf[2*BUF_SIZE];
char g_SendBuf[BUF_SIZE/2];
char g_MyBaseKey[128];
char MyJid[128];
char RemoteJid[128];
char g_RosterJid[128];
SlNetAppXmppOpt_t g_XmppOpt;
SlNetAppXmppUserName_t g_XmppUserName;
SlNetAppXmppPassword_t g_XmppPassword;
SlNetAppXmppDomain_t g_XmppDomain;
SlNetAppXmppResource_t g_XmppResource;




unsigned int g_JabberMsgSentFromPeer = 0;
short           g_SockID;
unsigned int g_XmppSetStatus = 0;
int		    g_FirstClientConnect = 0;


void _SlXmppConnectionSM(void);
int _SlValidateServerInfo(void);
int _SlValidateQueryResult(void);
int _SlValidateBindFeature(void);
int _SlBindingConfigure(void);
int _SlValidateRoster(void);
int _SlXMPPSessionConfig(void);


long sl_NetAppXmppSet(unsigned char AppId ,unsigned char Option ,unsigned char OptionLen, unsigned char *pOptionValue)
{
    SlNetAppXmppOpt_t* pXmppOpt = 0;
    SlNetAppXmppUserName_t* pXmppUserName;
    SlNetAppXmppPassword_t* pXmppPassword;
    SlNetAppXmppDomain_t* pXmppDomain;
    SlNetAppXmppResource_t* pXmppResource;

    if (AppId != SL_NET_APP_XMPP_ID)
    {
        return -1;
    }

    switch (Option)
    {
        case NETAPP_XMPP_ADVANCED_OPT:
            pXmppOpt = (SlNetAppXmppOpt_t*)pOptionValue;

            g_XmppOpt.Port = pXmppOpt->Port;
            g_XmppOpt.Family = pXmppOpt->Family;
            g_XmppOpt.SecurityMethod = pXmppOpt->SecurityMethod;
            g_XmppOpt.SecurityCypher = pXmppOpt->SecurityCypher;
            g_XmppOpt.Ip = pXmppOpt->Ip;

            g_XmppSetStatus+=1;
            break;

        case NETAPP_XMPP_USER_NAME:
            pXmppUserName = (SlNetAppXmppUserName_t*)pOptionValue;

            memcpy(g_XmppUserName.UserName, pXmppUserName->UserName, OptionLen);
            g_XmppUserName.Length = OptionLen;

            g_XmppSetStatus+=2;
            break;

        case NETAPP_XMPP_PASSWORD:
            pXmppPassword = (SlNetAppXmppPassword_t*)pOptionValue;

            memcpy(g_XmppPassword.Password, pXmppPassword->Password, OptionLen);
            g_XmppPassword.Length = OptionLen;

            g_XmppSetStatus+=4;
            break;

        case NETAPP_XMPP_DOMAIN:
            pXmppDomain = (SlNetAppXmppDomain_t*)pOptionValue;

            memcpy(g_XmppDomain.DomainName, pXmppDomain->DomainName, OptionLen);
            g_XmppDomain.Length = OptionLen;

            g_XmppSetStatus+=8;
            break;

        case NETAPP_XMPP_RESOURCE:
            pXmppResource = (SlNetAppXmppResource_t*)pOptionValue;
            memcpy(g_XmppResource.Resource, pXmppResource->Resource, OptionLen);
            g_XmppResource.Length = OptionLen;
            g_XmppSetStatus+=16;

        default:
            return -1;
    }

    return 0;
}

int sl_NetAppXmppConnect()
{
    SlSockAddrIn_t  Addr;
    int             AddrSize;
    int             Status;
    char             method;
    long             cipher;

    if (g_XmppSetStatus != 0x1F)
    {
        return -1;
    }

    method = g_XmppOpt.SecurityMethod;
    cipher = g_XmppOpt.SecurityCypher;
 
    Addr.sin_family = g_XmppOpt.Family;
    Addr.sin_port = sl_Htons(g_XmppOpt.Port);
    Addr.sin_addr.s_addr = sl_Htonl(g_XmppOpt.Ip);

    AddrSize = sizeof(SlSockAddrIn_t);

    /* opens a secure socket */
    g_SockID = sl_Socket(SL_AF_INET,SL_SOCK_STREAM, SL_SECURITY_ANY);

    if( g_SockID < 0 )
    {
        return -1;
    }

    /*configure the socket security method */
    Status = sl_SetSockOpt(g_SockID, SL_SOL_SOCKET, SO_SECMETHOD, &method, sizeof(method));
    if( Status < 0 )
    {
        return -1;

    }

     /*configure the socket cipher */
    Status = sl_SetSockOpt(g_SockID, SL_SOL_SOCKET, SO_SECURE_MASK, &cipher, sizeof(cipher));
    if( Status < 0 )
    {
        return -1;

    }

    /* connect to the peer device */
    Status = sl_Connect(g_SockID, ( SlSockAddr_t *)&Addr, AddrSize);
    if( (Status < 0 ) && (SL_ESECSNOVERIFY != Status))
    {
        return -1;

    }

    _SlXmppConnectionSM();

    return g_SockID;
}


int _SlValidateServerInfo(void)
{
    char *pServerStr;
    
    if(!strstr(g_RecvBuf, "stream:stream"))
    {        
        return -1;
    }

    pServerStr = strstr(g_RecvBuf, "from=");

    if(strncmp(pServerStr+6, (char*)g_XmppDomain.DomainName, g_XmppDomain.Length))
    {
        return -1;
    }

    return 0;
}


int _SlValidateQueryResult(void)
{
    if(strstr(g_RecvBuf, "<success"))
        return 0;
        else
                return -1;
}


int _SlValidateBindFeature(void)
{
    if(!strstr(g_RecvBuf, "xmpp-bind"))
    {
        return -1;
    }

    return 0;
}


int _SlBindingConfigure(void)
{
    char *pJid;
    char *pMyJid = MyJid;
    int i = 0;
    
    if(!strstr(g_RecvBuf, "result") || !strstr(g_RecvBuf, "bind"))
    {
        return -1;
    }

    pJid = strstr(g_RecvBuf, "<jid>");
    if(!pJid)
        return -1;

    
    pJid += 5;
    
    while(*pJid != '<')
    {
        pMyJid[i++] = *pJid;
        pJid ++;
    }

    pMyJid[i] = '\0';

    return 0;
}


int _SlValidateRoster(void)
{
    char *pJid;
    char *pMyJid = g_RosterJid;
    int i = 0;
    
    if(!strstr(g_RecvBuf, "jabber:iq:roster") || !strstr(g_RecvBuf, "roster_1"))
    {
        return -1;
    }

            
    pJid = strstr(g_RecvBuf, "item jid=");
    while (pJid != NULL)
    {
        pJid += 10;
    
        while(*pJid != ' ')
        {
            pMyJid[i++] = *pJid;
            pJid ++;
        }

        pMyJid[i-1] = '\0';

        i = 0;

        pJid = strstr(pJid, "item jid=");
    }

    return 0;
}


int _SlXMPPSessionConfig(void)
{
    if(!strstr(g_RecvBuf, "result"))
    {
        return -1;
    }

    return 0;

}


INT8 sl_NetAppXmppSend(UINT8* pRemoteJid, UINT16 Jidlen, UINT8* pMessage, UINT16 Msglen )
{
    char *ccPtr;
    int Status = 0;

    memset(g_SendBuf, 0, sizeof(g_SendBuf));

    ccPtr = g_SendBuf;

    memcpy(ccPtr, "<message to='", 13);
    ccPtr += 13;

    memcpy(ccPtr, pRemoteJid, Jidlen);
    ccPtr += Jidlen;

    memcpy(ccPtr, "' type='chat' from='", 20);
    ccPtr += 20;
    memcpy(ccPtr, MyJid, strlen(MyJid));
    ccPtr += strlen(MyJid);
    memcpy(ccPtr, "'><body>", 8);
    ccPtr += 8;
    memcpy(ccPtr, pMessage, Msglen);
    ccPtr += Msglen;

    memcpy(ccPtr, "</body><active xmlns='http://jabber.org/protocol/chatstates'/></message>", 72);
    ccPtr += 72;
    *ccPtr= '\0';

    Status = sl_Send(g_SockID, g_SendBuf, strlen(g_SendBuf), 0);

    return Status;
}


INT16 sl_NetAppXmppRecv(UINT8* pRemoteJid, UINT16 Jidlen, UINT8* pMessage, UINT16 Msglen )
{
    char *pBasePtr;
    char *pBodyPtr;
    char *pFromPtr;
    char *pMsgPtr;
    short    nonBlocking = 1;
    int Status = 0;

    setsockopt(g_SockID, SOL_SOCKET, SO_NONBLOCKING, &nonBlocking, sizeof(nonBlocking));

    g_FirstClientConnect = 0;
    memset(g_RosterJid, 0, sizeof(g_RosterJid));


    Status = sl_Recv(g_SockID, g_RecvBuf, BUF_SIZE*2, 0);

    if (Status > 0)
    {
        g_RecvBuf[strlen(g_RecvBuf)] = '\0';

        pBasePtr = strstr(g_RecvBuf, "<body>");

        if(!pBasePtr)
        {
            return -1;
        }

        pBasePtr += 6;
        pBodyPtr = pBasePtr;
        while(*pBodyPtr != '<')
        {
            pBodyPtr ++;
        }

        // null the body string
        *pBodyPtr = '\0';

        pBodyPtr = strstr(g_RecvBuf, "from=");


        pBodyPtr += 6;
        pFromPtr = pBodyPtr;
        while(*pBodyPtr != '"')
        {
            pBodyPtr ++;
        }

        // null the body string
        *pBodyPtr = '\0';

        if (strlen(pFromPtr)+1 < Jidlen)
        {
            memcpy(pRemoteJid, pFromPtr, strlen(pFromPtr)+1);
            memcpy(RemoteJid, pFromPtr, strlen(pFromPtr)+1);

            memcpy(g_RosterJid, RemoteJid, strlen(RemoteJid)+1);

            g_JabberMsgSentFromPeer = 1;
        }
        else
        {
            return -2;
        }

        *pBodyPtr = '"';

        pBodyPtr = strstr(g_RecvBuf, "<body>");
        pBodyPtr += 6;
        pMsgPtr = pBodyPtr;
        pBodyPtr = strstr(g_RecvBuf, "</body>");
        *pBodyPtr = '\0';
        if (strlen(pMsgPtr)+1 < Msglen)
        {
            memcpy(pMessage, pMsgPtr, strlen(pMsgPtr)+1);
        }
        else
        {
            return -3;
        }
    }

    return Status;
}


void FlushSendRecvBuffer(void)
{
    memset(g_RecvBuf, '\0', sizeof(g_RecvBuf));
    g_RecvBuf[0] = '\0';

    memset(g_SendBuf, '\0', sizeof(g_SendBuf));
    g_SendBuf[0] = '\0';
}


void GenerateBase64Key(void)
{    
    char OneByteArray[1] = {0x00};
    

    char InputStr[128];

    char *pIn = (char *)InputStr;

    //Generate Base64 Key. The orginal key is "username@domain" + '\0' + "username" + '\0' + "password"

    memcpy(pIn, g_XmppUserName.UserName, g_XmppUserName.Length);

    pIn += g_XmppUserName.Length;

    memcpy(pIn, "@", 1);

    pIn ++;

    memcpy(pIn, g_XmppDomain.DomainName, g_XmppDomain.Length);

    pIn += g_XmppDomain.Length;

    memcpy(pIn, OneByteArray, 1);        

    pIn ++;        

    memcpy(pIn,  g_XmppUserName.UserName, g_XmppUserName.Length);

    pIn +=  g_XmppUserName.Length;

    memcpy(pIn, OneByteArray, 1);        

    pIn ++;            

    memcpy(pIn, g_XmppPassword.Password, g_XmppPassword.Length);
    
    ConvertToBase64(g_MyBaseKey, (void *)InputStr, g_XmppUserName.Length*2 + 3 + g_XmppDomain.Length +  g_XmppPassword.Length);

}

void _SlXmppConnectionSM(void)
{    
    int NeedToSend = 0;
    int RecvRetVal = 0;
    int NeedToReceive = 0;
    char *ccPtr;
    int len =0;

    /* 3 steps for XMPP connection establishment
    1) initialization
    2) authentication
    3) binding
    */
    
    memset(g_MyBaseKey, '\0', sizeof(g_MyBaseKey));
    memset(MyJid, '\0', sizeof(MyJid));
    memset(RemoteJid, '\0', sizeof(RemoteJid));
    memset(g_RosterJid, '\0', sizeof(g_RosterJid));

    if(g_XMPPStatus == XMPP_INACTIVE)
    {
        return;
    }

    /* encode the username, password and domain in Base64 */
    GenerateBase64Key();

    while(g_XMPPStatus != CONNECTION_ESTABLISHED)
    {    
        FlushSendRecvBuffer();
                
        ccPtr = (char*)g_SendBuf;
        
        if(NeedToReceive == 1)
            RecvRetVal = sl_Recv(g_SockID, g_RecvBuf, BUF_SIZE*2, 0);

            
        if(NeedToReceive == 1 && strlen(g_RecvBuf) <= 0)
            continue;


        switch(g_XMPPStatus)
        {
            case XMPP_INIT:
                memcpy(ccPtr, "<stream:stream to='", 19);
                ccPtr += 19;
                memcpy(ccPtr, g_XmppDomain.DomainName, g_XmppDomain.Length);
                ccPtr += g_XmppDomain.Length;
                memcpy(ccPtr, "' ", 2);
                ccPtr += 2;
                memcpy(ccPtr, JABBER_XMLNS_INFO, strlen(JABBER_XMLNS_INFO));
                ccPtr += strlen(JABBER_XMLNS_INFO);
                *ccPtr= '\0';

                NeedToSend = 1;
                NeedToReceive = 1;

                g_XMPPStatus = FIRST_STREAM_SENT;
                break;
                
            case FIRST_STREAM_SENT:
                if(RecvRetVal > 0 && _SlValidateServerInfo() == 0)
                {
                    NeedToReceive = 0;

                    g_XMPPStatus = FIRST_STREAM_RECV;
                }
                else
                    NeedToReceive = 1;
                break;
                
            case FIRST_STREAM_RECV:                        
                g_XMPPStatus = STARTTLS_RESPONSE_RECV;

                break;

            case STARTTLS_RESPONSE_RECV:
                //__delay_cycles(48000000);
                memcpy(ccPtr, "<auth xmlns='urn:ietf:params:xml:ns:xmpp-sasl' mechanism='PLAIN'>", 65);
                ccPtr += 65;
                memcpy(ccPtr, g_MyBaseKey, strlen(g_MyBaseKey));
                ccPtr += strlen(g_MyBaseKey);
                memcpy(ccPtr, "</auth>", 7);
                ccPtr += 7;
                *ccPtr= '\0';
                
                NeedToSend = 1;
                NeedToReceive = 1;

                g_XMPPStatus = AUTH_QUERY_SET;
                break;

            case AUTH_QUERY_SET:
                if((RecvRetVal > 0) && (_SlValidateQueryResult() == 0))
                {
                    NeedToReceive = 0;

                    g_XMPPStatus = AUTH_RESULT_RECV;
                }
                else
                    NeedToReceive = 1;
                break;
                
            case AUTH_RESULT_RECV:    
                memcpy(ccPtr, "<stream:stream to='", 19);
                ccPtr += 19;
                memcpy(ccPtr, g_XmppDomain.DomainName, g_XmppDomain.Length);
                ccPtr += g_XmppDomain.Length;
                memcpy(ccPtr, "' ", 2);
                ccPtr += 2;
                memcpy(ccPtr, JABBER_XMLNS_INFO, strlen(JABBER_XMLNS_INFO));
                ccPtr += strlen(JABBER_XMLNS_INFO);
                *ccPtr= '\0';

                NeedToSend = 1;
                NeedToReceive = 1;

                g_XMPPStatus = BIND_FEATURE_RESPONSE;
                break;

            case BIND_FEATURE_REQUEST:
                NeedToSend = 0;
                NeedToReceive = 0;

                g_XMPPStatus = BIND_FEATURE_RESPONSE;
                break;
                
            case BIND_FEATURE_RESPONSE:
                if(RecvRetVal > 0 && _SlValidateBindFeature() == 0)
                {
                    NeedToReceive = 0;
                    
                    g_XMPPStatus = BIND_CONFIG_SET;
                }
                else
                    NeedToReceive = 1;
                
                break;
                
            case BIND_CONFIG_SET:
                memcpy(ccPtr, "<iq id='JAJSBind' type='set'><bind xmlns='urn:ietf:params:xml:ns:xmpp-bind'><resource>", 86);
                ccPtr += 86;
                memcpy(ccPtr, g_XmppResource.Resource, g_XmppResource.Length);
                ccPtr += g_XmppResource.Length;
                memcpy(ccPtr, "</resource></bind></iq>", 23);
                ccPtr += 23;
                *ccPtr= '\0';

                NeedToSend = 1;
                NeedToReceive = 1;
                
                g_XMPPStatus = BIND_CONFIG_RECV;
                break;

            case BIND_CONFIG_RECV:
                if(RecvRetVal > 0 && _SlBindingConfigure() == 0)
                {
                    NeedToReceive = 0;

                    g_XMPPStatus = XMPP_SESSION_SET;
                }
                else
                {
                    NeedToSend = 0;
                    NeedToReceive = 0;
                    g_XMPPStatus = XMPP_INIT;
                }
                break;

            case XMPP_SESSION_SET:
                memcpy(ccPtr, "<iq type='set' id='2'><session xmlns='urn:ietf:params:xml:ns:xmpp-session'/></iq>", 81);
                ccPtr += 81;
                *ccPtr= '\0';

                NeedToSend = 1;
                NeedToReceive = 1;
                
                g_XMPPStatus = XMPP_SESSION_RECV;
                break;

            case XMPP_SESSION_RECV:
                if(RecvRetVal > 0 && _SlXMPPSessionConfig() == 0)
                {
                    NeedToReceive = 0;
                    g_XMPPStatus = PRESENCE_SET;
                }
                else
                    NeedToReceive = 1;
                
                break;

            case PRESENCE_SET:
                
                NeedToSend = 1;
                NeedToReceive = 0;

                memcpy(ccPtr, PRESENCE_MESSAGE, strlen(PRESENCE_MESSAGE));
                ccPtr += strlen(PRESENCE_MESSAGE);
                *ccPtr= '\0';
                
                g_XMPPStatus = ROSTER_REQUEST;
                break;

            case ROSTER_REQUEST:

                memcpy(ccPtr, "<iq type='get' id='roster_1' from='", 35);
                ccPtr += 35;
                memcpy(ccPtr, MyJid, strlen(MyJid));
                ccPtr += strlen(MyJid);
                memcpy(ccPtr, "'> <query xmlns='jabber:iq:roster'/></iq>", 42);
                ccPtr += 50;
                *ccPtr= '\0';

                NeedToSend = 1;
                NeedToReceive = 0;

                g_XMPPStatus = ROSTER_RESPONSE;
                
                break;

            case ROSTER_RESPONSE:
                NeedToSend = 1;
                NeedToReceive = 1;
                
                if (_SlValidateRoster() == 0)
                {
                    g_XMPPStatus = CONNECTION_ESTABLISHED;
                    NeedToReceive = 0;
                    NeedToSend = 0;
                }
                    
                break;
        }

        if(NeedToSend ==  1)
        {
            NeedToSend = 0;
            len = strlen(g_SendBuf);

            sl_Send(g_SockID, g_SendBuf, len, 0);
        }
            
    }
}
