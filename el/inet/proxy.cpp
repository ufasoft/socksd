#include <el/ext.h>

#if UCFG_WIN32
#	include <wtypes.h>
#endif

#include "proxy.h"

namespace Ext {
namespace Inet {

bool g_bNoSendBuffers = true;

// #include <ws2tcpip.h>//!!!
// #include <wspiapi.h> // to keep compatiblity with pre-XP windows

ostream& operator<<(ostream& os, const CProxyQuery& pq) {
	switch (pq.Typ) {
	case QueryType::Connect:		os << "QueryType::Connect"; break;
	case QueryType::Bind:			os << "QueryType::Bind"; break;
	case QueryType::Udp:			os << "QueryType::Udp"; break;
	case QueryType::Resolve:		os << "QueryType::Resolve"; break;
	case QueryType::RevResolve:		os << "QueryType::RevResolve"; break;
	}
	return os << " " << pq.Ep;
}

bool ResolveLocally() {
#if UCFG_COMPLEX_WINAPP
	return RegistryKey(AfxGetApp()->KeyCU, "Options").TryQueryValue("ResolveLocally", 1);
#else
	return true;
#endif
}

void CProxyBase::ConnectWithResolving(Stream& stm, const IPEndPoint& hp) {
	CProxyQuery q = { QueryType::Connect, hp };
	if (ResolveLocally())
		q.Ep.Address = IPAddress(htonl(hp.Address.GetIP()));
	Connect(stm, q);
}

DWORD CProxySocket::GetTimeout() {
#if UCFG_COMPLEX_WINAPP
	return (DWORD)RegistryKey(AfxGetApp()->KeyCU,"Options").TryQueryValue("Timeout", DEFAULT_RECEIVE_TIMEOUT)*1000;
#else
	return DEFAULT_RECEIVE_TIMEOUT*1000;
#endif
}

DWORD CProxySocket::GetType() {
#if UCFG_COMPLEX_WINAPP
	RegistryKey key(AfxGetApp()->KeyCU, "Options");
	return DWORD(key.TryQueryValue("ProxyType", DWORD(0)));
#else
	return 5;
#endif
}

void CProxySocket::ConnectToProxy(Stream& stm) {
	DWORD proxyType = GetType();
#if UCFG_COMPLEX_WINAPP
	RegistryKey key(AfxGetApp()->KeyCU,"Options");	
	WORD port = (WORD)DWORD(key.TryQueryValue("Port", DWORD(1080)));
#else
	WORD port = 1080;
#endif
	ptr<CProxyBase> pProxy;
	switch (proxyType) {
	case 4: pProxy = new CSocks4Proxy; break;
	case 5: pProxy = new CSocks5Proxy; break;
	}
	if (pProxy.get()) {
#if UCFG_COMPLEX_WINAPP
		pProxy->m_user = key.TryQueryValue("User","");
		pProxy->m_password = key.TryQueryValue("Password","");
#endif
		pProxy->Authenticate(stm);
	}
}

void CProxySocket::AssociateUDP() {
#if UCFG_COMPLEX_WINAPP
	RegistryKey key(AfxGetApp()->KeyCU,"Options");
	String server = key.TryQueryValue("Server","");
	WORD port = (WORD)DWORD(key.TryQueryValue("Port",DWORD(1080)));
#else
	String server = "localhost";
	WORD port = 1080;
#endif
	DWORD timeout = GetTimeout();
	m_sock.Create();
	try {
		m_sock.Connect(server, port);
	} catch (RCExc) {
		cerr << "I can not connect to inital proxy, please verify settings in Tools|Options|Connection dialog!";
		//!!!    Sleep(SLEEP_TIME);
		throw;
	}
	m_sock.ReceiveTimeout = (int)timeout;
	NetworkStream stm(m_sock);
	ConnectToProxy(stm);
	byte ar[10] = {5, 3, 0, 1, 0, 0, 0, 0, 0, 0};
	stm.WriteBuffer(ar,10);
	m_ep = ReadSocks5Reply(stm);
}

bool CProxySocket::ConnectHelper(const IPEndPoint& hp) {
	if (m_bLingerZero)
		LingerState = LingerOption(true, 0);	//!!!
#if UCFG_COMPLEX_WINAPP
	RegistryKey key(AfxGetApp()->KeyCU, "Options");
#endif
	DWORD proxyType = GetType();
	DWORD timeout = GetTimeout();
	if (proxyType == 0) {
		//!!!    Create();
		if (g_bNoSendBuffers) {
			if (!(GetOsVersion() & OSVER_FLAG_NT)) {		//!!!
				int nSndBuf = 0;
				SetSocketOption(SOL_SOCKET, SO_SNDBUF, nSndBuf);
			}
		}    

		CEvent ev;
		EventSelect(SafeHandle::HandleAccess(ev), FD_CONNECT);
		Socket::ConnectHelper(hp);
		if (WaitForSingleObject(SafeHandle::HandleAccess(ev), timeout) == WAIT_TIMEOUT)
			Throw(E_PROXY_ConnectTimeOut);
		WSANETWORKEVENTS events;
		SocketCheck(WSAEnumNetworkEvents(Socket::HandleAccess(_self), 0, &events));
		if (DWORD code = events.iErrorCode[FD_CONNECT_BIT])
			Throw(HRESULT_FROM_WIN32(code));
		EventSelect(0,0);
		DWORD dw = 0;
		Ioctl(IOC_IN|FIONBIO, &dw, 4);
	} else {
#if UCFG_COMPLEX_WINAPP
		String server = key.TryQueryValue("Server","");
		WORD port = (WORD)DWORD(key.TryQueryValue("Port", DWORD(1080)));
		String user = key.TryQueryValue("User","");
		String password = key.TryQueryValue("Password", "");
#else
		String server = "localhost";
		WORD port = 1080;
		String user = "";
		String password = "";
#endif
		try {
			IPEndPoint ep(server, port);
			TRC(2,"proxy = " << ep);
			Socket::ConnectHelper(ep);
			//!!! sock.Connect(server, port);
		} catch (RCExc DBG_PARAM(ex)) {
			TRC(0, ex.what());
			cerr << "I can not connect to inital proxy, please verify settings in Tools|Options|Connection dialog!";
			//!!!    Sleep(SLEEP_TIME);
			throw;
		}
		ReceiveTimeout = (int)timeout;
		NetworkStream stm(_self);
		ConnectToProxy(stm);
		if (proxyType == 5) //!!!? why without authenticate
			m_remoteHostPort = CSocks5Proxy().TcpBy(stm, hp, 1);
		else {
			ptr<CProxyBase> pProxy;
			switch (proxyType) {
			case 1: pProxy = new CHttpProxy; break;
			case 4: pProxy = new CSocks4Proxy; break;
			default:
				Throw(E_FAIL);
			}
			pProxy->m_user = user;
			pProxy->m_password = password;
			CProxyQuery q = { QueryType::Connect, hp };
			pProxy->Connect(stm, q);
		}
	}
	timeout = 0;
	ReceiveTimeout = (int)timeout;
	return true;
}

IPEndPoint CProxySocket::GetRemoteHostPort() {
	return m_remoteHostPort.Address.GetIP()==0 ? RemoteEndPoint : m_remoteHostPort;
}

void CProxySocket::SendTo(const ConstBuf& cbuf, const IPEndPoint& ep) {
	if (!m_sock.Valid())
		Socket::SendTo(cbuf, ep);
	else {
		int size = sizeof(UDP_REPLY)+cbuf.Size;
		UDP_REPLY *udp = (UDP_REPLY*)alloca(size);
		udp->m_rsv = 0;
		udp->m_frag = 0;
		udp->m_atype = 1;
		udp->m_host = *(const uint32_t*)ep.Address.GetAddressBytes().constData();
		udp->m_port = htons(ep.Port);
		memcpy(udp+1, cbuf.P, cbuf.Size);
		Socket::SendTo(ConstBuf(udp, size), ep);
	}
}

Blob CProxySocket::ReceiveUDP(IPEndPoint& hp) {
	byte buf[UDP_BUF_SIZE];
	IPEndPoint ep;
	int r = ReceiveFrom(buf, sizeof buf, ep);
	if (!m_sock.Valid()) {
		if (r == SOCKET_ERROR)
			ThrowWSALastError();
		hp = ep;
		return Blob(buf,r);
	} else {
		if (r == SOCKET_ERROR)
			ThrowWSALastError();
		if (ep == m_ep) {
			UDP_REPLY *udp = (UDP_REPLY*)buf;
			byte *p = (byte*)(udp+1);
			switch (udp->m_atype)
			{
			case 1:
				hp = IPEndPoint(udp->m_host, ntohs(udp->m_port));
				break;
			case 3:
				hp = IPEndPoint(String((const char*)(buf+5),buf[4]), ntohs(*(WORD*)(buf+5+buf[4])));
				p += buf[4]-3;
				break;
			case 4:
				hp = IPEndPoint(IPAddress(ConstBuf(buf+4, 16)), ntohs(*(WORD*)(buf+20)));
				p += 12;
				break;
			default:
				Throw(E_SOCKS_AddressTypeNotSupported);
			}
			return Blob(p, buf+r-p);
		} else {
			hp = ep;
			return Blob(buf, r);
		}
	}
}


IPEndPoint CSocks4Proxy::Connect(Stream& stm, const CProxyQuery& q) {
	TRC(2, "Connect by SOCKS4 to\t" << q.Ep);

	if (q.Typ != QueryType::Connect)
		Throw(E_NOTIMPL);
	IPEndPoint hp = q.Ep;
	MemoryStream qs;
	BinaryWriter wr(qs);
	wr << (byte)4 << (byte)1 << htons(hp.Port);
	const char *szPassword = m_password;
	if (AddressFamily::InterNetwork == hp.AddressFamily) {
		wr << htonl(hp.Address.GetIP());
		wr.Write(szPassword, strlen(szPassword)+1);
	} else {
		wr << (uint32_t)0x01000000;
		wr.Write(szPassword,strlen(szPassword)+1);
		wr.Write((const char*)hp.Address.m_domainname, hp.Address.m_domainname.length()+1);
	}
	Blob blob = qs.Blob;
	stm.WriteBuffer(blob.constData(), blob.Size);
	byte buf[8];
	stm.ReadBuffer(buf, 8);//!!!
	switch (buf[1]) {
	case 90:
		break;
	case 91:
		Throw(E_SOCKS_RejectedOrFailed);
	case 92:
		Throw(E_SOCKS_RejectedBecauseIDENTD);
	case 93:
		Throw(E_SOCKS_DifferentUserIDs);
	default:
		Throw(E_SOCKS_IncorrectProtocol);
	}
	m_stage = STAGE_AUTHENTICATED;
	return IPEndPoint(IPAddress(*(uint32_t*)&buf[4]), ntohs(*(uint16_t*)&buf[2]));
}

void CSocks5Proxy::Authenticate(Stream& stm) {
	byte ar[4] = { 5, 2, 0, 2 };
	stm.WriteBuffer(ar, 4);
	stm.ReadBuffer(ar, 2);
	switch (ar[1]) {
	case 0:
		break;
	case 2:
		{
			const char *szUser = m_user,
				*szPassword = m_password;
			byte len = byte(strlen(szUser)+strlen(szPassword)+3); //!!!
			byte *p = (byte*)alloca(len);
			p[0] = 1;
			p[1] = (byte)strlen(szUser);
			memcpy(p+2, szUser, p[1]);
			int off = 2+p[1];
			p[off] = (byte)strlen(szPassword);
			memcpy(p+off+1,szPassword, p[off]);
			stm.WriteBuffer(p, len);
			stm.ReadBuffer(ar, 2);
			if (ar[1])
				Throw(E_SOCKS_BadUserOrPassword);
		}
		break;
	default:
		Throw(E_SOCKS_AuthNotSupported);
	}
	m_stage = STAGE_AUTHENTICATED;
}

IPEndPoint CSocks5Proxy::TcpBy(Stream& stm, const IPEndPoint& hp, byte cmd) {
	TRC(2, "Connect by SOCKS5\t" << hp);

	byte ar[262] = { 5, cmd, 0, 0 };
	int portOffset;
	switch ((int)hp.AddressFamily) {
	case AF_INET:
		ar[3] = 1;
		*(DWORD*)(ar+4) = htonl(hp.Address.GetIP());
		portOffset = 8;
		break;
	case AF_INET6:
		ar[3] = 4;
		memcpy(ar+4, hp.Address.GetAddressBytes().constData(), 16);
		portOffset = 20;
		break;
	case IPAddress::AF_DOMAIN_NAME:
		{
			ar[3] = 3;
			const char *domain = hp.Address.m_domainname;
			size_t len = strlen(domain);
			if (len > 255)
				Throw(E_PROXY_LongDomainName);
			ar[4] = (byte)len;
			memcpy(ar+5, domain, len);
			portOffset = byte(5+len);
		}
		break;
	default:
		Throw(E_FAIL);
	}
	*(uint16_t*)(ar+portOffset) = htons(hp.Port);
	stm.WriteBuffer(ar, portOffset+2);
	return ReadSocks5Reply(stm);
}

IPEndPoint CSocks5Proxy::Connect(Stream& stm, const CProxyQuery& q) {
	Authenticate(stm);

	byte cmd;
	switch (q.Typ) {
	case QueryType::Connect:	cmd = 1; break;
	case QueryType::Resolve:	cmd = 0xF0; break;
	case QueryType::RevResolve:	cmd = 0xF1; break;
	default:
		Throw(E_NOTIMPL);
	}
	return TcpBy(stm, q.Ep, cmd);
}

pair<String,String> CHttpProxy::HttpAuthHeader(String user, String password) {
	String s = user+":"+password;
	return make_pair(String("Proxy-Authorization"), "Basic " + Convert::ToBase64String(ConstBuf((const byte*)(const char*)s, s.length())));
}

IPEndPoint CHttpProxy::Connect(Stream& stm, const CProxyQuery& q) {
	TRC(2, "Connect by HTTP proxy to\t" << q.Ep);

	if (q.Typ != QueryType::Connect)
		Throw(E_NOTIMPL);
	IPEndPoint hp = q.Ep;

	m_stage = STAGE_AUTHENTICATED;
	ostringstream os;
	os << "CONNECT " << hp << " HTTP/1.0\r\n";//!!! 1.0

	if (m_user != "") {
		WebHeaderCollection headers;
		pair<String,String> pp = HttpAuthHeader(m_user, m_password);
		headers[pp.first].push_back(pp.second);
		os << headers;
	}
	os << "\r\n";
	String s = os.str();
	TRC(2, s);
	stm.WriteBuffer((const char*)s, s.length());
	vector<String> header = ReadHttpHeader(stm);
	TRC(2,"");
	for (int i=0; i<header.size(); i++) //!!!
		TRC(2,header[i]);
	if (header.size() < 1)
		Throw(E_PROXY_VeryLongLine);//!!!
	String line = header[0];
	//!!!ReadDoubleLineFromStream(stm,line);
	istringstream is(line.c_str());
	string ver;
	int code;
	is >> ver >> code;
	if (code != 200)
		Throw(E_HTTP_Base+code);
	return IPEndPoint();
}

IPEndPoint ReadSocks5Reply(Stream& stm) {
	BinaryReader rd(stm);
	BinaryWriter wr(stm);
	SSocks5ReplyHeader reply;
	rd.ReadStruct(reply);
	if (reply.m_ver != 5)
		Throw(E_SOCKS_InvalidVersion);
	if (reply.m_rep)
		Throw(E_SOCKS_Base+reply.m_rep);
	switch (reply.m_atyp) {
	case 1:
		{
			DWORD host;
			WORD port;
			rd >> host >> port;
			return IPEndPoint(host, ntohs(port));
		}
	case 3:
		{
			byte len = rd.ReadByte();
			void *p = alloca(len);
			rd.Read(p, len);
			uint16_t port = rd.ReadUInt16();
			return IPEndPoint(String((const char*)p, len), ntohs(port));
		}
	case 4:
		byte ar[16];
		WORD port;
		rd.Read(ar, sizeof ar);
		rd >> port;
		return IPEndPoint(IPAddress(ConstBuf(ar, 16)), ntohs(port));
	default:
		Throw(E_SOCKS_AddressTypeNotSupported);
	}
}


vector<DWORD> GetLocalIPs() {
	Socket sock;
	sock.Open(SOCK_DGRAM,IPPROTO_UDP,AF_INET);
	INTERFACE_INFO la[10];
	int r = sock.Ioctl(SIO_GET_INTERFACE_LIST,0,0,&la,sizeof la);
	vector<DWORD> ar;
	for (int i=0; i<r/sizeof(INTERFACE_INFO); i++)
		ar.push_back(ntohl(((SOCKADDR_IN*)&la[i].iiAddress)->sin_addr.s_addr));
	return ar;
}


DWORD GetBestLocalHost(const vector<DWORD>& ar) {
	int i;
	for (i=0; i<ar.size(); i++) {
		DWORD host = ar[i];
		if (IPAddress(htonl(host)).IsGlobal())
			return host;
	}
	for (i=0; i<ar.size(); i++) {
		DWORD host = ar[i];
		if (host != INADDR_LOOPBACK)
			return host;
	}
	for (i=0; i<ar.size(); i++)
		return ar[i];
	Throw(E_PROXY_NoLocalHosts);
}

#ifndef NO_INTERNET

void CProxyInternetSession::Create() {
	String agent = AfxGetAppName();
	RegistryKey key(AfxGetApp()->KeyCU,"Options");
	DWORD proxyType = key.TryQueryValue("ProxyType",DWORD(0));
	String server = key.TryQueryValue("Server","");
	WORD port = (WORD)DWORD(key.TryQueryValue("Port",DWORD(1080)));
	String user = key.TryQueryValue("User","");
	String password = key.TryQueryValue("Password","");
	if (proxyType==1 && user != "") {
		pair<String,String> pp = CHttpProxy::HttpAuthHeader(user, password);
//!!!?		Headers[pp.first].push_back(pp.second);
	}

	switch (proxyType) {
	case 0:
		Init(agent, 0, INTERNET_OPEN_TYPE_DIRECT);
		break;
	case 1:
		Init(agent, 0, INTERNET_OPEN_TYPE_PROXY, "http=http://"+server+":"+Convert::ToString(port));
		break;
	case 4:
	case 5://!!!
		Init(agent, 0, INTERNET_OPEN_TYPE_PROXY,"socks="+server+":"+Convert::ToString(port));
		break;
	default:
		Throw(E_FAIL);
	}
}

void CSiteNotifier::Create() {
	(new CSiteNotifierThreader(_self))->Start();
}

void CSiteNotifier::AddURL(RCString url) {
	EXT_LOCK (m_cs) {
		m_ar.push(url);
	}
	m_pThreader->QueueAPC();
}

void CSiteNotifierThreader::Execute() {
	while (!m_bStop) {
		String url;
		EXT_LOCK (m_siteNotifier.m_cs) {
			Dequeue(m_siteNotifier.m_ar, url);
		}
		if (!url.empty()) {
			try {
//!!!R				CInetIStream is;
//!!!R				is.open(m_sess,url);
			} catch (RCExc){
			}  
		}
		Sleep(NOTIFY_TRY_PERIOD);
	}
}

#endif // NO_INTERNET

}} // Ext::Inet::
