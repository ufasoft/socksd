/*######   Copyright (c) 2013-2020 Ufasoft  http://ufasoft.com  mailto:support@ufasoft.com,  Sergey Pavlov  mailto:dev@ufasoft.com ####
#                                                                                                                                     #
# 		See LICENSE for licensing information                                                                                         #
#####################################################################################################################################*/

#pragma once

#include <el/inet/proxy.h>

namespace Ext {
	namespace Inet {

class CProxyRelay : public NonInterlockedObject {
public:
	Stream *m_pStm;
	unique_ptr<MemoryStream> m_qs;

	virtual ~CProxyRelay() {}
	virtual CProxyQuery GetQuery(char beg) { return CProxyQuery(); }
	virtual void SendReply(const InternetEndPoint &ep, const error_code &ec = error_code()) {
	}

	void AfterConnect(Stream& ostm) {
		if (m_qs) {
			Span s = m_qs->AsSpan();
			if (s.size() != 0)
				ostm.Write(s);
		}
	}

	static CProxyRelay *CreateSocks4Relay();
	static CProxyRelay *CreateSocks5Relay();
	static CProxyRelay *CreateTorSocks5Relay();
	static CProxyRelay *CreateHttpRelay();
};

}} // Ext::Inet::
