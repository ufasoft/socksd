#pragma once

#include <el/libext/ext-net.h>
#include <el/libext/ext-http.h>


//!!!#include "ProxyMsg.h"

namespace Ext {
	namespace Inet {

const int NOTIFY_TRY_PERIOD = 600000;

const size_t UDP_BUF_SIZE = 65536;


extern bool g_bNoSendBuffers;

const int DEFAULT_RECEIVE_TIMEOUT = 4;

ENUM_CLASS(QueryType) {
	Connect,
	Bind,
	Udp,
	Resolve,
	RevResolve,
} END_ENUM_CLASS(QueryType);

struct CProxyQuery {
	QueryType Typ;
	IPEndPoint Ep;
};

inline ostream& operator<<(ostream& os, const CProxyQuery& pq) {
	switch (pq.Typ) {
	case QueryType::Connect:		os << "QueryType::Connect"; break;
	case QueryType::Bind:			os << "QueryType::Bind"; break;
	case QueryType::Udp:			os << "QueryType::Udp"; break;
	case QueryType::Resolve:		os << "QueryType::Resolve"; break;
	case QueryType::RevResolve:		os << "QueryType::RevResolve"; break;
	}
	return os << " " << pq.Ep;
}

#pragma pack(push,1)

struct CSocks5Header {
	byte Cmd;
	byte AddrType;
	IPEndPoint EndPoint;
};

struct SSocks5ReplyHeader {
	byte m_ver,
		m_rep,
		m_rsv,
		m_atyp;
};

struct UDP_REPLY {
	uint16_t m_rsv;
	byte m_frag,
		m_atype;
	uint32_t m_host;
	uint16_t m_port;
};

#pragma pack(pop)

class CProxySocket : public Socket {
	typedef Socket base;
public:
	Socket m_sock;
	bool m_bLingerZero;

	CProxySocket()
		: m_bLingerZero(false)
	{}

	CProxySocket(AddressFamily af, SocketType sockType, ProtocolType protoType)
		:	base(af, sockType, protoType)
		,	m_bLingerZero(false)
	{}

	void AssociateUDP();
	void SendTo(const ConstBuf& cbuf, const IPEndPoint& ep) override;
	Blob ReceiveUDP(IPEndPoint& hp);
	static DWORD GetTimeout();
	static DWORD GetType();
	IPEndPoint GetRemoteHostPort();
protected:
	IPEndPoint m_ep;

	static void ConnectToProxy(Stream& stm);
	bool ConnectHelper(const IPEndPoint& hp) override;
private:
	IPEndPoint m_remoteHostPort;
};


class CProxyBase : public Object {
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
	void ConnectWithResolving(Stream& stm, const IPEndPoint& hp);
};

class CSocks4Proxy : public CProxyBase {
public:
	IPEndPoint Connect(Stream& stm, const CProxyQuery& q) override;
};

class CSocks5Proxy : public CProxyBase {
public:
	virtual IPEndPoint TcpBy(Stream& stm, const IPEndPoint& hp, byte cmd);
	void Authenticate(Stream& stm) override;
	IPEndPoint Connect(Stream& stm, const CProxyQuery& q) override;
};

class CHttpProxy : public CProxyBase {
public:
	static pair<String, String> HttpAuthHeader(String user, String password);
	IPEndPoint Connect(Stream& stm, const CProxyQuery& q) override;
};

IPEndPoint ReadSocks5Reply(Stream& stm);

void LogMessage(RCString s);

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
protected:
	void Stop() {
		Thread::Stop();
		m_sess.Close();
	}

	void Execute();
public:
	CSiteNotifier& m_siteNotifier;
	CProxyInternetSession m_sess;

	CSiteNotifierThreader(CSiteNotifier& siteNotifier)
		: Thread(&siteNotifier.m_tr)
		, m_siteNotifier(siteNotifier)
	{
	}
};

bool ResolveLocally();

#endif // NO_INTERNET

}} // Ext::Inet::
