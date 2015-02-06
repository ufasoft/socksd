#include <el/ext.h>

#include "proxy.h"
#include "proxyrelay.h"


namespace Ext {
	namespace Inet {

class CSocks4Relay : public CProxyRelay {
	String ReadSocks4String() {
		String s;
		while (true) {
			char ch;
			m_pStm->ReadBuffer(&ch, 1);
			if (!ch)
				return s;
			s += String(ch);
		}
	}
public:
	CProxyQuery GetQuery(char beg) override {
		CProxyQuery pq;
		byte buf[7];
		m_pStm->ReadBuffer(buf,7);
		uint16_t port = ntohs(*(uint16_t*)(buf+1));
		uint32_t host = *(uint32_t*)(buf+3);
		String userID = ReadSocks4String();
		pq.Ep = host<256 ? IPEndPoint(ReadSocks4String(),port) : IPEndPoint(host, port);
		switch (*buf) {
		case 1: pq.Typ = QueryType::Connect; break;
		case 2: pq.Typ = QueryType::Bind;    break;
		default: Throw(E_SOCKS_IncorrectProtocol);
		}
		return pq; 
	}

	void SendReply(const IPEndPoint& hp, const error_code& ec) override {
		byte ar[8] = {0};
		if (ec)
			ar[1] = 91;
		else {
			ar[1] = 90;
			*(uint16_t*)(ar+2) = htons(hp.Port);
			*(uint32_t*)(ar+4) = htonl(hp.Address.GetIP());
		}
		m_pStm->WriteBuffer(ar,sizeof ar);
	}
};

// RFC 1928
class CSocks5Relay : public CProxyRelay {
public:
	void ReadEndPoint(CSocks5Header& header, Stream& stm) {
		BinaryReader rd(stm);
		switch (header.AddrType) {
		case 1:
			header.EndPoint.Address = IPAddress(rd.ReadUInt32());
			break;
		case 4:
			{
				byte har[16];
				rd.Read(har, sizeof(har));
				header.EndPoint.Address = IPAddress(ConstBuf(har, 16));
			}
			break;
		case 2:
			break;
		case 3:
			{
				byte len = rd.ReadByte();
				byte *hp = (byte*)alloca(len);
				rd.Read(hp, len);
				header.EndPoint.Address = IPAddress::Parse(String((char*)hp, len));
			}
			break;
		default:
			Throw(E_SOCKS_IncorrectProtocol);
		}
		header.EndPoint.Port = ntohs(rd.ReadUInt16());
	}

	CProxyQuery GetQuery(char beg) override {
		Stream& stm = *m_pStm;
		BinaryReader rd(stm);
		byte nMethods = rd.ReadByte();
		byte *pm = (byte*)alloca(nMethods);
		rd.Read(pm, nMethods);
		if (nMethods) {
			for (int i=0; i<nMethods; i++) {
				if (!pm[i]) {
					byte ar[] = {5,0};
					stm.WriteBuffer(ar, 2);
					goto out;
				}
			}
			Throw(E_PROXY_MethodNotSupported);
		}
out:
		byte ar[4];
		rd.Read(ar, 4);
		if (ar[0] != 5)
			Throw(E_SOCKS_InvalidVersion);
		CSocks5Header header;
		header.Cmd = ar[1];
		header.AddrType = ar[3];
		ReadEndPoint(header, stm);
		return OnCommand(header, stm);
	}

	void SendReply(const IPEndPoint& hp, const error_code& ec) override {
		byte ar[264] = { 5, 0, 0, 1 };
		int len = 10;
		if (ec) {
			if (ec == errc::permission_denied)
				ar[1] = 2;
			else if (ec == errc::network_unreachable)
				ar[1] = 3;
			else if (ec == errc::host_unreachable)
				ar[1] = 4;
			else if (ec == errc::connection_refused)
				ar[1] = 5;
			else if (ec == errc::timed_out)
				ar[1] = 6;
			else if (ec == errc::protocol_error)
				ar[1] = 7;
			else if (ec == errc::address_family_not_supported)
				ar[1] = 8;
			else
				ar[1] = 1;
		} else {
			IPAddress ip = hp.Address;
			byte *p = &ar[4];
			switch ((int)ip.get_AddressFamily()) {
			case AF_INET:
				ar[3] = 1;
				*(uint32_t*)p = htonl(ip.GetIP());
				p += 4;
				break;
			case AF_INET6:
				ar[3] = 4;
				memcpy(p, ip.GetAddressBytes().constData(), 16);
				p += 16;
				break;
			case IPAddress::AF_DOMAIN_NAME:
				{
					ar[3] = 3;
					const char *hostname = ip.m_domainname;
					size_t slen = strlen(hostname);
					if (slen > 255)
						Throw(E_FAIL);
					*p++ = (byte)slen;
					memcpy(exchange(p, p+slen), hostname, slen);
				}
				break;
			default:
				Throw(E_NOTIMPL);
			}
			*(uint16_t*)p = htons(hp.Port);
			len = p + 2 - ar;
		}
		m_pStm->WriteBuffer(ar, len);
	}
protected:
	virtual CProxyQuery OnCommand(CSocks5Header& header, Stream& stm) {
		CProxyQuery pq;
		pq.Ep = header.EndPoint;
		switch (header.Cmd) {
		case 1: pq.Typ = QueryType::Connect; break;
		case 2: pq.Typ = QueryType::Bind; break;
		case 3: pq.Typ = QueryType::Udp; break;
		default: Throw(E_SOCKS_IncorrectProtocol);
		}
		TRC(3, "SOCKS5 req " << (int)header.Cmd  << " for " << pq.Ep );
		return pq; 
	}
};

class TorSocks5Relay : public CSocks5Relay {
	typedef CSocks5Relay base;
protected:
	CProxyQuery OnCommand(CSocks5Header& header, Stream& stm);
};

static regex s_reRequest("^(\\w+)\\s+(?:http://)?([-.\\w]+)(?::(\\d+))?(.*)", regex_constants::icase);

class CHttpRelay : public CProxyRelay {
	bool m_bConnect;
public:
	CProxyQuery GetQuery(char beg) override {
		Stream& stm = *m_pStm;
		CProxyQuery pq;
		pq.Typ = QueryType::Connect;
		String line(beg);
		ReadOneLineFromStream(stm,line);
		line += "\r\n";

		cmatch m;
		if (!regex_search(line, m, s_reRequest))
			Throw(E_PROXY_InvalidHttpRequest);
		String method = m[1];
		method.MakeUpper();
		if (m_bConnect = (method == "CONNECT")) {
			ReadHttpHeader(stm);
			pq.Ep = IPEndPoint(m[2], (uint16_t)atoi(String(m[3])));
		} else {
			uint16_t port = 80;
			if (String(m[3]) != "")
				port = (uint16_t)atoi(String(m[3]));
			pq.Ep = IPEndPoint(m[2], port);
			String rest = m[4];
			method += " ";
			m_qs.WriteBuffer((const char*)method, method.length()); //!!!
			m_qs.WriteBuffer((const char*)rest, rest.length());
		}
		/*!!!
		String oline(line);
		int i = line.FindOneOf(" \t");
		if (i == -1)
		Throw(E_SC_InvalidHttpRequest);
		String method = line.Left(i);
		for (; line[i] && My_IsCharSpaceW(line[i]); i++)
		;
		line = line.Mid(i);
		method.MakeUpper();
		if (m_bConnect = (method == "CONNECT"))
		{
		ReadHttpHeader(stm);
		i = line.FindOneOf(" \t");
		pq.m_hp = IPEndPoint(line.Left(i));
		}
		else
		{   
		const char *p = oline,
		*q = strstr(p,"http://"),
		*b = q+7,
		*e = q ? strstr(b,"/") : 0;
		if (!q || !e)
		Throw(E_SC_InvalidHttpRequest);
		String host = String(b,e-b);
		if (strchr(host,':'))
		pq.m_hp = IPEndPoint(host);
		else
		pq.m_hp = IPEndPoint(host,80);
		m_qs.WriteBuffer(p,q-p);
		m_qs.WriteBuffer(e,strlen(p)-(e-p));
		}*/
		return pq; 
	}

	void SendReply(const IPEndPoint& hp, const error_code& ec) override {
		if (ec || m_bConnect) {
			ostringstream os;
			os << "HTTP/1.0 " << (ec ? 400 : 200) << "\r\n\r\n";
			String s = os.str();
			m_pStm->WriteBuffer((const char*)s, s.length());
		}
	}
};

CProxyRelay *CProxyRelay::CreateSocks4Relay() { 
	return new CSocks4Relay;
}

CProxyRelay *CProxyRelay::CreateSocks5Relay() {
	return new CSocks5Relay;
}

CProxyRelay *CProxyRelay::CreateTorSocks5Relay() {
	return new TorSocks5Relay;
}

CProxyRelay *CProxyRelay::CreateHttpRelay() {
	return new CHttpRelay;
}

CProxyQuery TorSocks5Relay::OnCommand(CSocks5Header& header, Stream& stm) {
	CProxyQuery pq;
	pq.Ep = header.EndPoint;

	switch (header.Cmd) {
	case 0xF0:
		pq.Typ = QueryType::Resolve;
		break;
	case 0xF1:
		pq.Typ = QueryType::RevResolve;
		break;
	default:
		return base::OnCommand(header, stm);
	}
	return pq;
}

}} // Ext::Inet::


