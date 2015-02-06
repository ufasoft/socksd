#include <el/ext.h>
using namespace std;

#include <el/inet/proxyrelay.h>
using namespace Ext::Inet;

CUsingSockets g_usingSockets;

class CSocksThread : public SocketThread, public CSocketLooper {
	typedef SocketThread base;
public:
	bool m_bFinal;
	IPEndPoint m_epSrc;
	Socket m_sockS, m_sockD;

	CSocksThread(thread_group *ownRef)
		: SocketThread(ownRef)
	{
	}

	void Stop() override {
		base::Stop();
		m_sockS.Close();//!!!
		m_sockD.Close();
	}
protected:
	ptr<CProxyRelay> m_relay;

	void Execute() override {
		try {
			NetworkStream stm(m_sockS);
			byte ver;
			stm.ReadBuffer(&ver, 1);
			switch (ver) {
			case 4: m_relay = CProxyRelay::CreateSocks4Relay(); break;
			case 5: m_relay = CProxyRelay::CreateTorSocks5Relay(); break;
			default: m_relay = CProxyRelay::CreateHttpRelay();
			}
			m_relay->m_pStm = &stm;
			CProxyQuery target = m_relay->GetQuery(ver);
			
			IPEndPoint epResult;
			try {	

				switch (target.Typ) {
				case QueryType::Connect:
					TRC(3, "Connect request to " << target);

					m_sockD.Create(target.Ep.AddressFamily, SocketType::Stream, ProtocolType::Tcp);
					m_sockD.Connect(target.Ep);
					epResult = m_sockD.RemoteEndPoint;
					break;
				default:
					Throw(E_NOTIMPL);
				}
			} catch (const system_error& ex) {
				m_relay->SendReply(IPEndPoint(), ex.code());
				return;
			}
			m_relay->SendReply(epResult);
			NoSignal = true;
			Loop(m_sockS, m_sockD);
		} catch (RCExc) {
		}
	}
};

class ListenerThread : public SocketThread {
	typedef SocketThread base;
public:
	Socket m_sockListen;
	SocketKeeper m_socketKeeper;
	IPEndPoint m_ep;

	ListenerThread(thread_group& tg, const IPEndPoint& ep)
		: base(&tg)
		, m_ep(ep)
		, m_sockListen(ep.Address.AddressFamily, SocketType::Stream, ProtocolType::Tcp)
		, m_socketKeeper(_self, m_sockListen, -1)
	{
	}
protected:
	void BeforeStart() override {
		m_sockListen.ReuseAddress = true;
		m_sockListen.Bind(m_ep);
		m_sockListen.Listen();
	}

	void Execute() override {
		Name = "CListenThread";
		try {
			while (!m_bStop) {
				IPEndPoint epFrom;
				Socket sock;

				DBG_LOCAL_IGNORE_CONDITION(errc::interrupted);
				if (m_sockListen.Accept(sock, epFrom)) {
					TRC(2, "Accepted connect from " << epFrom);

					if (m_bStop)
						break;

					ptr<CSocksThread> t = new CSocksThread(&GetThreadRef());
					t->m_sockS = move(sock);
					t->m_epSrc = epFrom;
					t->Start();
				}
			}
		} catch (RCExc) {

		}
	}
};

class CSocksApp : public CConApp {
	typedef CConApp base;
public:
	uint16_t Port;
	CBool ListenGlobalIP;
	thread_group m_tg;
	unordered_map<IPAddress, ptr<ListenerThread>> m_ipToListener;
	AutoResetEvent m_evStop;
	volatile bool m_bStopListen;

	CSocksApp()
		:	m_bStopListen(false)
 	{
		Port = 1080;
	}

	void StartListen(const IPAddress& ip) {
		TRC(1, "Listening on " << ip);

		m_ipToListener.insert(make_pair(ip, new ListenerThread(m_tg, IPEndPoint(ip, Port)))).first->second->Start();
	}

	void Execute() override	{
		StartListen(IPAddress::Loopback);
		while (!m_bStopListen) {
			vector<IPAddress> ips = IPAddrInfo().GetIPAddresses();
			for (auto& ip : ips) {
				if (!m_ipToListener.count(ip) && (ListenGlobalIP || !ip.IsGlobal()))
					StartListen(ip);
			}			
			m_evStop.lock(5000);
		}
		m_tg.interrupt_all();
		m_tg.join_all();
		m_tg.m_bSync = false;
	}

	bool OnSignal(int sig) override {
		m_bStopListen = true;
		m_evStop.Set();
		CConApp::OnSignal(sig);
		return true;
	}

} theApp;


EXT_DEFINE_MAIN(theApp)




