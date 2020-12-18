/*######   Copyright (c) 2014-2019 Ufasoft  http://ufasoft.com  mailto:support@ufasoft.com,  Sergey Pavlov  mailto:dev@ufasoft.com ####
#                                                                                                                                     #
# 		See LICENSE for licensing information                                                                                         #
#####################################################################################################################################*/

#pragma once

#include <el/libext/ext-net.h>
#include <el/inet/http.h>


//!!!#include "ProxyMsg.h"

namespace Ext {
namespace Inet {

using namespace Ext;

const int NOTIFY_TRY_PERIOD = 600000;

const size_t UDP_BUF_SIZE = 65536;


extern bool g_bNoSendBuffers;

const int DEFAULT_RECEIVE_TIMEOUT = 4;

ENUM_CLASS(QueryType) {
	Connect
	, Bind
	, Udp
	, Resolve
	, RevResolve,
} END_ENUM_CLASS(QueryType);

struct CProxyQuery {
	QueryType Typ;
	ptr<EndPoint> Ep;
};

inline ostream& operator<<(ostream& os, const CProxyQuery& pq) {
	switch (pq.Typ) {
	case QueryType::Connect:		os << "QueryType::Connect"; break;
	case QueryType::Bind:			os << "QueryType::Bind"; break;
	case QueryType::Udp:			os << "QueryType::Udp"; break;
	case QueryType::Resolve:		os << "QueryType::Resolve"; break;
	case QueryType::RevResolve:		os << "QueryType::RevResolve"; break;
	}
	return os << " " << *pq.Ep;
}

#pragma pack(push,1)

struct CSocks5Header {
	uint8_t Cmd;
	uint8_t AddrType;
	ptr<InternetEndPoint> EndPoint;
};

struct SSocks5ReplyHeader {
	uint8_t m_ver,
		m_rep,
		m_rsv,
		m_atyp;
};

struct UDP_REPLY {
	uint16_t m_rsv;
	uint8_t m_frag,
		m_atype;
	uint32_t m_host;
	uint16_t m_port;
};

#pragma pack(pop)

class CProxySocket : public Socket {
	typedef Socket base;
private:
	IPEndPoint m_remoteHostPort;
protected:
	IPEndPoint m_ep;
public:
	Socket m_sock;
	bool m_bLingerZero;

	CProxySocket()
		: m_bLingerZero(false)
	{}

	CProxySocket(AddressFamily af, SocketType sockType, ProtocolType protoType)
		: base(af, sockType, protoType)
		, m_bLingerZero(false)
	{}

	void AssociateUDP();
	void SendTo(RCSpan cbuf, const IPEndPoint& ep) override;
	Blob ReceiveUDP(IPEndPoint& hp);
	static DWORD GetTimeout();
	static DWORD GetType();
	IPEndPoint GetRemoteHostPort();

	static void ConnectToProxy(Stream& stm);
	bool ConnectHelper(const EndPoint& ep) override;
};


class CProxyBase : public NonInterlockedObject {
public:
	enum EStage {
		STAGE_CONNECTED,
		STAGE_AUTHENTICATED //!!!
	};
	EStage m_stage;
	String m_user, m_password;

	CProxyBase()
		: m_stage(STAGE_CONNECTED)
	{}

	virtual ~CProxyBase() {};
	virtual void Authenticate(Stream& stm) {}
	virtual IPEndPoint Connect(Stream& stm, const CProxyQuery& q) = 0;
	void ConnectWithResolving(Stream& stm, const EndPoint& ep);
};

class CSocks4Proxy : public CProxyBase {
public:
	IPEndPoint Connect(Stream& stm, const CProxyQuery& q) override;
};

class CSocks5Proxy : public CProxyBase {
public:
	virtual IPEndPoint TcpBy(Stream& stm, const EndPoint& ep, uint8_t cmd);
	void Authenticate(Stream& stm) override;
	IPEndPoint Connect(Stream& stm, const CProxyQuery& q) override;
};

class CHttpProxy : public CProxyBase {
public:
	static pair<String, String> HttpAuthHeader(String user, String password);
	IPEndPoint Connect(Stream& stm, const CProxyQuery& q) override;
};

IPEndPoint ReadSocks5Reply(Stream& stm);


#ifndef NO_INTERNET

class CProxyInternetSession : public CInternetSession {
public:
	CProxyInternetSession()
		: CInternetSession(false)
	{}

	void Create();
};

vector<DWORD> GetLocalIPs();
DWORD GetBestLocalHost(const vector<DWORD>& ar);

class CSiteNotifierThreader;

class CSiteNotifier {
public:
	thread_group m_tr;
	CCriticalSection m_cs;
	observer_ptr<CSiteNotifierThreader> m_pThreader;
	queue<String> m_ar;

	void Create();
	void AddURL(RCString url);
};

class CSiteNotifierThreader : public Thread {
public:
	CSiteNotifier& m_siteNotifier;
	CProxyInternetSession m_sess;

	CSiteNotifierThreader(CSiteNotifier& siteNotifier)
		: Thread(&siteNotifier.m_tr)
		, m_siteNotifier(siteNotifier)
	{
	}
protected:
	void Stop() {
		Thread::Stop();
		m_sess.Close();
	}

	void Execute();
};

bool ResolveLocally();

#endif // NO_INTERNET

}} // Ext::Inet::
