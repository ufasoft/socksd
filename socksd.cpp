/*######   Copyright (c) 2013-2018 Ufasoft  http://ufasoft.com  mailto:support@ufasoft.com,  Sergey Pavlov  mailto:dev@ufasoft.com ####
#                                                                                                                                     #
# 		See LICENSE for licensing information                                                                                         #
#####################################################################################################################################*/

#include <el/ext.h>
using namespace std;

#include <el/inet/proxyrelay.h>
using namespace Ext::Inet;

CUsingSockets g_usingSockets;


class CSocksThread : public SocketThread, public CSocketLooper {
	typedef SocketThread base;
public:
	Socket m_sock, m_sockD;

	void Stop() override {
		base::Stop();
		m_sock.Close();//!!!
		m_sockD.Close();
	}
protected:
	ptr<CProxyRelay> m_relay;

	void Execute() override {
		try {
			NetworkStream stm(m_sock);
			uint8_t ver;
			stm.ReadBuffer(&ver, 1);
			switch (ver) {
			case 4: m_relay = CProxyRelay::CreateSocks4Relay(); break;
			case 5: m_relay = CProxyRelay::CreateTorSocks5Relay(); break;
			default: m_relay = CProxyRelay::CreateHttpRelay();
			}
			m_relay->m_pStm = &stm;

			DBG_LOCAL_IGNORE_CONDITION(errc::connection_aborted);

			CProxyQuery target = m_relay->GetQuery(ver);

			IPEndPoint epResult;
			try {
				DBG_LOCAL_IGNORE_CONDITION(errc::timed_out);

				switch (target.Typ) {
				case QueryType::Connect:
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
			Loop(m_sock, m_sockD);
		} catch (RCExc) {
		}
	}
};

class CSocksApp : public CConApp {
	typedef CConApp base;
public:
	uint16_t Port;
	IPAddress Ip;
	CBool ListenGlobalIP;
	thread_group m_tg;
	unordered_set<IPAddress> m_ips;
	AutoResetEvent m_evStop;
	volatile bool m_bStopListen;

	CSocksApp()
		:	m_bStopListen(false)
 	{
		Port = 1080;
	}

	void StartListen(const IPAddress& ip) {
		ptr<ListenerThread<CSocksThread>> p = new ListenerThread<CSocksThread>(m_tg, IPEndPoint(ip, Port));
		p->m_sockListen.ReuseAddress = true;
		m_ips.insert(ip);
		p->Start();
	}

 	void PrintUsage() {
		cout << "Usage: " << System.get_ExeFilePath().stem() << " {-l ip -p port}" << "\n";
		cout << "  -p port       Listening port, by default 1080\n"
			 << "  -l ip		Listening IP, by default non-global\n"
			<< endl;
	}

	void Execute() override	{
#if UCFG_USE_POSIX
		signal(SIGHUP, SIG_IGN);
#endif

		for (int arg; (arg = getopt(Argc, Argv, "hl:p:")) != EOF;) {
			switch (arg) {
			case 'h':
				PrintUsage();
				return;
			case 'l':
				Ip = IPAddress::Parse(optarg);
				break;
			case 'p':
				Port = uint16_t(atoi(optarg));
				break;
			}
		}

		if (!Ip.IsEmpty())
			StartListen(Ip);
		StartListen(IPAddress::Loopback);
		while (!m_bStopListen) {
			if (Ip.IsEmpty()) {
				vector<IPAddress> ips = IPAddrInfo().GetIPAddresses();
				for (auto& ip : ips) {
					if (!m_ips.count(ip) && (ListenGlobalIP || !ip.IsGlobal()))
						StartListen(ip);
				}
			}
			m_evStop.lock(60000);
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
